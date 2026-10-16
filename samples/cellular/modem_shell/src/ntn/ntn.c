/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <math.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/sys/util.h>

#include <nrf_modem_at.h>
#include <modem/nrf_modem_lib.h>
#include <modem/at_monitor.h>
#include <modem/at_parser.h>
#include <modem/ntn.h>

#include "mosh_print.h"
#include "ntn.h"

#if defined(CONFIG_GNSS)
#define GNSS_DEV DEVICE_DT_GET(DT_ALIAS(gnss))
#endif

#define NANODEG_PER_DEG	1000000000.0
#define MM_PER_M	1000.0

/* SIB31 notification parameter indexes. */
#define AT_SIB_NUMBER_INDEX		1
#define AT_SIB31_CELL_ID_INDEX		2
#define AT_SIB31_EPHEMERIS_TYPE_INDEX	3
#define AT_SIB31_POS_X_INDEX		4

#define SIB31_EPHEMERIS_TYPE_STATE_VECTOR 1
/* Position step of the SIB31 state vector, see 3GPP TS 36.331 EphemerisStateVectors. */
#define SIB31_POSITION_STEP_M		1.3

/* WGS-84 ellipsoid */
#define WGS84_A		6378137.0
#define WGS84_F		(1.0 / 298.257223563)
#define WGS84_E2	(WGS84_F * (2.0 - WGS84_F))

#define PI		3.14159265358979323846
#define DEG_TO_RAD(deg)	((deg) * PI / 180.0)
#define RAD_TO_DEG(rad)	((rad) * 180.0 / PI)

struct ue_location {
	bool valid;
	double latitude;
	double longitude;
	double altitude;
};

static struct ue_location ue_location;
static struct k_spinlock ue_location_lock;

#if defined(CONFIG_GNSS)
static bool update_enabled;
static uint16_t update_interval;
static uint64_t update_timestamp;

static uint32_t update_interval_calculate(uint32_t accuracy)
{
	uint32_t interval;

	/* Calculate update interval required to maintain accuracy at 120 km/h speed. */
	interval = (accuracy / 33.333);
	/* Make sure update interval is not shorter than 5 seconds. */
	interval = MAX(interval, 5);

	return interval;
}
#endif

static void ntn_evt_handler(const struct ntn_evt *evt)
{
	switch (evt->type) {
	case NTN_EVT_LOCATION_REQUEST:
		mosh_print("NTN location request event: requested: %s, accuracy: %d m",
			   evt->location_request.requested ? "true" : "false",
			   evt->location_request.accuracy);
#if defined(CONFIG_GNSS)
		if (evt->location_request.requested) {
			ntn_location_update_enable(
				update_interval_calculate(evt->location_request.accuracy));
		} else {
			ntn_location_update_disable();
		}
#endif /* defined(CONFIG_GNSS) */
		break;

	default:
		mosh_warn("Unknown NTN event: %d", evt->type);
		break;
	}
}

#if defined(CONFIG_GNSS)
static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	uint64_t current_timestamp;

	if (update_enabled && data->info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
		current_timestamp = k_uptime_get();
		if (((current_timestamp - update_timestamp) >
		    ((update_interval - 1) * MSEC_PER_SEC)) || update_timestamp == 0) {
			update_timestamp = current_timestamp;

			mosh_ntn_location_set(
				data->nav_data.latitude / NANODEG_PER_DEG,
				data->nav_data.longitude / NANODEG_PER_DEG,
				data->nav_data.altitude / MM_PER_M,
				update_interval * 2);
		}
	}
}

GNSS_DATA_CALLBACK_DEFINE(GNSS_DEV, gnss_data_cb);
#endif /* defined(CONFIG_GNSS) */

int ntn_location_update_enable(uint32_t interval)
{
#if defined(CONFIG_GNSS)
	if (!update_enabled) {
		mosh_print("NTN location updates from external GNSS enabled");
	}

	/* Reset timestamp to force a location update immediately. */
	update_timestamp = 0;
	update_interval = interval;
	update_enabled = true;

	return 0;
#else
	mosh_error("External GNSS not available, enable CONFIG_GNSS");

	return -ENOEXEC;
#endif /* defined(CONFIG_GNSS) */
}

int ntn_location_update_disable(void)
{
#if defined(CONFIG_GNSS)
	mosh_print("NTN location updates from external GNSS disabled");

	update_enabled = false;
	update_interval = 0;

	return 0;
#else
	mosh_error("External GNSS not available, enable CONFIG_GNSS");

	return -ENOEXEC;
#endif /* defined(CONFIG_GNSS) */
}

int mosh_ntn_location_set(double latitude, double longitude, float altitude, uint32_t validity)
{
	k_spinlock_key_t key;
	int err;

	err = ntn_location_set(latitude, longitude, altitude, validity);
	if (err) {
		return err;
	}

	key = k_spin_lock(&ue_location_lock);
	ue_location.valid = true;
	ue_location.latitude = latitude;
	ue_location.longitude = longitude;
	ue_location.altitude = (double)altitude;
	k_spin_unlock(&ue_location_lock, key);

	return 0;
}

int mosh_ntn_location_invalidate(void)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&ue_location_lock);
	ue_location.valid = false;
	k_spin_unlock(&ue_location_lock, key);

	return ntn_location_invalidate();
}

static void sat_look_angles_print(const char *cell_id, const double sat_pos[3])
{
	struct ue_location loc;
	k_spinlock_key_t key;

	key = k_spin_lock(&ue_location_lock);
	loc = ue_location;
	k_spin_unlock(&ue_location_lock, key);

	mosh_print("SIB31: cell %s, satellite ECEF x: %.0f m, y: %.0f m, z: %.0f m",
		   cell_id, sat_pos[0], sat_pos[1], sat_pos[2]);

	if (!loc.valid) {
		mosh_warn("SIB31: UE location not set, cannot compute elevation and azimuth");
		return;
	}

	const double lat = DEG_TO_RAD(loc.latitude);
	const double lon = DEG_TO_RAD(loc.longitude);
	const double sin_lat = sin(lat), cos_lat = cos(lat);
	const double sin_lon = sin(lon), cos_lon = cos(lon);
	/* Prime vertical radius of curvature */
	const double n = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);

	const double ue_pos[3] = {
		(n + loc.altitude) * cos_lat * cos_lon,
		(n + loc.altitude) * cos_lat * sin_lon,
		(n * (1.0 - WGS84_E2) + loc.altitude) * sin_lat,
	};
	const double diff[3] = {
		sat_pos[0] - ue_pos[0],
		sat_pos[1] - ue_pos[1],
		sat_pos[2] - ue_pos[2],
	};
	const double distance_m = sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);

	if (distance_m < 1.0) {
		mosh_warn("SIB31: invalid satellite position");
		return;
	}

	/* Line of sight projected onto the local East-North-Up basis at the UE */
	const double d_east = -sin_lon * diff[0] + cos_lon * diff[1];
	const double d_north = -sin_lat * cos_lon * diff[0] - sin_lat * sin_lon * diff[1] +
			       cos_lat * diff[2];
	const double d_up = cos_lat * cos_lon * diff[0] + cos_lat * sin_lon * diff[1] +
			    sin_lat * diff[2];

	const double elevation = RAD_TO_DEG(asin(CLAMP(d_up / distance_m, -1.0, 1.0)));
	double azimuth = RAD_TO_DEG(atan2(d_east, d_north));

	if (azimuth < 0.0) {
		azimuth += 360.0;
	}

	mosh_print("SIB31: cell %s, elevation: %.1f deg, azimuth: %.1f deg, distance: %.1f km",
		   cell_id, elevation, azimuth, distance_m / 1000.0);
}

static void at_handler_sib(const char *notif)
{
	struct at_parser parser;
	char cell_id[9];
	size_t cell_id_len = sizeof(cell_id);
	int64_t pos[3];
	double sat_pos[3];
	int sib;
	int ephemeris_type;
	int err;

	err = at_parser_init(&parser, notif);
	if (err) {
		return;
	}

	err = at_parser_num_get(&parser, AT_SIB_NUMBER_INDEX, &sib);
	if (err || sib != 31) {
		/* Other SIBs and the end of delivery notification are not handled here. */
		return;
	}

	err = at_parser_string_get(&parser, AT_SIB31_CELL_ID_INDEX, cell_id, &cell_id_len);
	if (err) {
		mosh_warn("SIB31: failed to parse cell ID (err %d)", err);
		return;
	}

	err = at_parser_num_get(&parser, AT_SIB31_EPHEMERIS_TYPE_INDEX, &ephemeris_type);
	if (err) {
		mosh_warn("SIB31: failed to parse ephemeris type (err %d)", err);
		return;
	}

	if (ephemeris_type != SIB31_EPHEMERIS_TYPE_STATE_VECTOR) {
		mosh_warn("SIB31: ephemeris type %d not supported, only state vector", ephemeris_type);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(pos); i++) {
		err = at_parser_num_get(&parser, AT_SIB31_POS_X_INDEX + i, &pos[i]);
		if (err) {
			mosh_warn("SIB31: failed to parse satellite position (err %d)", err);
			return;
		}
		sat_pos[i] = pos[i] * SIB31_POSITION_STEP_M;
	}

	sat_look_angles_print(cell_id, sat_pos);
}

AT_MONITOR(ntn_atmon_sibconfig, "%SIBCONFIG", at_handler_sib);
AT_MONITOR(ntn_atmon_sibreq, "%SIBREQ", at_handler_sib);

NRF_MODEM_LIB_ON_INIT(mosh_ntn_init_hook, mosh_ntn_on_modem_init, NULL);

static void mosh_ntn_on_modem_init(int ret, void *ctx)
{
	int err;

	ARG_UNUSED(ctx);

	if (ret) {
		return;
	}

	/* Optional type, so that the modem can still connect if SIB31 is not received. */
	err = nrf_modem_at_printf("AT%%SIBCONFIG=31,1");
	if (err) {
		mosh_warn("Failed to configure SIB31 delivery (err %d)", err);
	}
}

NRF_MODEM_LIB_ON_CFUN(mosh_ntn_cfun_hook, mosh_ntn_on_modem_cfun, NULL);

static void mosh_ntn_on_modem_cfun(int mode, void *ctx)
{
	int err;

	ARG_UNUSED(mode);
	ARG_UNUSED(ctx);

	/* The modem does not re-deliver an unchanged SIB31 when reconnecting to the same cell,
	 * so reconfigure on every functional mode change to get a fresh delivery.
	 */
	err = nrf_modem_at_printf("AT%%SIBCONFIG=31,1");
	if (err) {
		mosh_warn("Failed to reconfigure SIB31 delivery (err %d)", err);
	}
}

int mosh_ntn_init(void)
{
	ntn_register_handler(ntn_evt_handler);

#if defined(CONFIG_GNSS) && defined(CONFIG_PM_DEVICE)
	/* Resume GNSS device to start receiving GNSS data. */
	return pm_device_action_run(GNSS_DEV, PM_DEVICE_ACTION_RESUME);
#else
	return 0;
#endif /* defined(CONFIG_GNSS) && defined(CONFIG_PM_DEVICE) */
}

SYS_INIT(mosh_ntn_init, APPLICATION, 0);
