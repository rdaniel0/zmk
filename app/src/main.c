/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#if defined(CONFIG_SOC_SERIES_NRF52X)
#include <hal/nrf_power.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)

#include <zmk/display.h>
#include <lvgl.h>

#endif

int main(void) {
    LOG_INF("Welcome to ZMK!\n");

#if defined(CONFIG_SOC_SERIES_NRF52X) && defined(NRF_POWER_HAS_RESETREAS) && NRF_POWER_HAS_RESETREAS
    uint32_t reason = nrf_power_resetreas_get(NRF_POWER);
    LOG_ERR("RESETREAS=0x%08x [%s%s%s%s%s]", reason,
            (reason & NRF_POWER_RESETREAS_RESETPIN_MASK) ? "PIN " : "",
            (reason & NRF_POWER_RESETREAS_DOG_MASK) ? "WATCHDOG " : "",
            (reason & NRF_POWER_RESETREAS_SREQ_MASK) ? "SOFT " : "",
            (reason & NRF_POWER_RESETREAS_LOCKUP_MASK) ? "LOCKUP " : "",
            (reason & NRF_POWER_RESETREAS_OFF_MASK) ? "OFF " : "");
    if (reason == 0) {
        LOG_ERR("RESETREAS: power-on reset (cold boot)");
    }
    nrf_power_resetreas_clear(NRF_POWER, reason);
#endif

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();
    settings_load();
#endif

#ifdef CONFIG_ZMK_DISPLAY
    zmk_display_init();

#if IS_ENABLED(CONFIG_ARCH_POSIX)
    // Workaround for an SDL display issue:
    // https://github.com/zephyrproject-rtos/zephyr/issues/71410
    while (1) {
        lv_task_handler();
        k_sleep(K_MSEC(10));
    }
#endif

#endif /* CONFIG_ZMK_DISPLAY */

    return 0;
}
