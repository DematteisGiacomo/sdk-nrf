/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(nrf9251_smp_single_slot, LOG_LEVEL_INF);

/* Bump when testing a firmware update over MCUboot serial recovery. */
#define APP_FW_VERSION 1

/* Version N blinks at N Hz so the rate change confirms an update. */
#define BLINK_HALF_PERIOD_MS (500 / APP_FW_VERSION)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	int err;

	LOG_INF("nRF9251 SMP single-slot sample, firmware version %d", APP_FW_VERSION);

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to configure LED: %d", err);
		return err;
	}

	while (true) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_HALF_PERIOD_MS);
	}

	return 0;
}
