/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <net/nrf_cloud_coap.h>
#include <zephyr/shell/shell.h>
#if defined(CONFIG_DATE_TIME)
#include <date_time.h>
#endif
#include "mosh_print.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_NRF_CLOUD_COAP));

BUILD_ASSERT(!IS_ENABLED(CONFIG_MOSH_CLOUD_LWM2M));

extern struct k_work_q mosh_common_work_q;

static bool connected;
static bool paused;

static void coap_connect_work_fn(struct k_work *item)
{
	ARG_UNUSED(item);

	int err;

	if (paused) {
		mosh_error("Connection to nRF Cloud is paused, use 'cloud resume'");
		return;
	}

	if (connected) {
		mosh_error("Already connected to nRF Cloud");
		return;
	}

	err = nrf_cloud_coap_connect(NULL);
	if (err) {
		mosh_error("Connecting to nRF Cloud failed, error: %d", err);
		return;
	}

	connected = true;
}

K_WORK_DEFINE(coap_connect_work, coap_connect_work_fn);

static void cmd_cloud_coap_connect(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	static bool initialized;

	if (!initialized) {
		err = nrf_cloud_coap_init();
		if (err) {
			mosh_error("Initializing nRF Cloud CoAP library failed, error: %d", err);
			return;
		}

		initialized = true;
	}

	k_work_submit_to_queue(&mosh_common_work_q, &coap_connect_work);
}

static void cmd_cloud_coap_disconnect(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;

	if (!connected) {
		mosh_error("Not connected to nRF Cloud");
		return;
	}

	err = nrf_cloud_coap_disconnect();
	if (err) {
		mosh_error("Disconnecting from nRF Cloud failed, error: %d", err);
	}

	connected = false;
	paused = false;
}

static int cmd_cloud_coap_pause(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;

	if (!connected) {
		mosh_error("Not connected to nRF Cloud");
		return -ENOTCONN;
	}

	if (paused) {
		mosh_error("Connection to nRF Cloud is already paused");
		return -EALREADY;
	}

	err = nrf_cloud_coap_pause();
	if (!err) {
		paused = true;
		mosh_print("Connection to nRF Cloud paused, DTLS CID session saved");
		return 0;
	}

	if (err == -EACCES) {
		mosh_error("Pausing failed: DTLS CID not active or connection not authenticated");
		return err;
	}

	/* Any other error means the socket is closed and the connection is lost. */
	connected = false;
	mosh_error("Pausing connection to nRF Cloud failed, error: %d", err);
	mosh_warn("Connection to nRF Cloud lost, use 'cloud connect'");

	return err;
}

static int cmd_cloud_coap_resume(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;

	if (!paused) {
		mosh_error("Connection to nRF Cloud is not paused");
		return -EACCES;
	}

	err = nrf_cloud_coap_resume();
	paused = false;
	if (err) {
		connected = false;
		mosh_error("Resuming connection to nRF Cloud failed, error: %d", err);
		mosh_warn("Use 'cloud connect' to reconnect with a full DTLS handshake");
		return err;
	}

	mosh_print("Connection to nRF Cloud resumed using DTLS CID");

	return 0;
}

static int cloud_location_args_parse(size_t argc, char **argv, struct nrf_cloud_gnss_pvt *pvt)
{
	char *end;

	pvt->lat = strtod(argv[1], &end);
	if (*end != '\0' || pvt->lat < -90.0 || pvt->lat > 90.0) {
		mosh_error("Invalid latitude: %s", argv[1]);
		return -EINVAL;
	}

	pvt->lon = strtod(argv[2], &end);
	if (*end != '\0' || pvt->lon < -180.0 || pvt->lon > 180.0) {
		mosh_error("Invalid longitude: %s", argv[2]);
		return -EINVAL;
	}

	if (argc > 3) {
		pvt->accuracy = strtof(argv[3], &end);
		if (*end != '\0' || pvt->accuracy < 0.0f) {
			mosh_error("Invalid accuracy: %s", argv[3]);
			return -EINVAL;
		}
	}

	return 0;
}

static int cmd_cloud_coap_location(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	int err;
	struct nrf_cloud_gnss_data gnss = {
		.type = NRF_CLOUD_GNSS_TYPE_PVT,
		.ts_ms = NRF_CLOUD_NO_TIMESTAMP,
	};

	err = cloud_location_args_parse(argc, argv, &gnss.pvt);
	if (err) {
		return err;
	}

	if (paused) {
		mosh_error("Connection to nRF Cloud is paused, use 'cloud resume'");
		return -EACCES;
	}

#if defined(CONFIG_DATE_TIME)
	if (date_time_now(&gnss.ts_ms)) {
		gnss.ts_ms = NRF_CLOUD_NO_TIMESTAMP;
	}
#endif

	err = nrf_cloud_coap_location_send(&gnss, true);
	if (err == -EACCES) {
		mosh_error("Not connected to nRF Cloud");
		return err;
	} else if (err == -ENETDOWN) {
		connected = false;
		mosh_error("Socket to nRF Cloud closed by the modem, use 'cloud connect'");
		return err;
	} else if (err) {
		mosh_error("Sending location to nRF Cloud failed, error: %d", err);
		return err;
	}

	mosh_print("Location sent to nRF Cloud: lat %.06f, lon %.06f, accuracy %.01f m",
		   gnss.pvt.lat, gnss.pvt.lon, (double)gnss.pvt.accuracy);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_cloud,
	SHELL_CMD_ARG(connect, NULL, "Establish CoAP connection to nRF Cloud.",
		      cmd_cloud_coap_connect, 1, 0),
	SHELL_CMD_ARG(disconnect, NULL, "Disconnect from nRF Cloud.",
		      cmd_cloud_coap_disconnect, 1, 0),
	SHELL_CMD_ARG(pause, NULL,
		      "Pause CoAP connection to nRF Cloud and save the DTLS CID session.\n"
		      "Flight mode (link funmode -4) can be used while paused. Power off\n"
		      "(link funmode -0) closes the socket and the session is lost.",
		      cmd_cloud_coap_pause, 1, 0),
	SHELL_CMD_ARG(resume, NULL,
		      "Resume paused CoAP connection to nRF Cloud using the saved DTLS CID "
		      "session, without a new handshake.",
		      cmd_cloud_coap_resume, 1, 0),
	SHELL_CMD_ARG(location, NULL,
		      "<latitude> <longitude> [<accuracy>]\n"
		      "Send location to nRF Cloud as a GNSS (PVT) message. Latitude and longitude "
		      "in degrees, accuracy in meters (default: 0).",
		      cmd_cloud_coap_location, 3, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(cloud, &sub_cloud, "CoAP connection to nRF Cloud", mosh_print_help_shell);
