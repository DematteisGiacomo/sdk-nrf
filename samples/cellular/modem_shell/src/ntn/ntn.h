/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MOSH_NTN_H
#define MOSH_NTN_H

/* Enable modem location updates from external GNSS. */
int ntn_location_update_enable(uint32_t interval);

/* Disable modem location updates from external GNSS. */
int ntn_location_update_disable(void);

/* Set location to modem and store it for satellite elevation and azimuth computation. */
int mosh_ntn_location_set(double latitude, double longitude, float altitude, uint32_t validity);

/* Invalidate location in modem and the stored location. */
int mosh_ntn_location_invalidate(void);

#endif /* MOSH_NTN_H */
