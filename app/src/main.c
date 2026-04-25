/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#if defined(CONFIG_SOC_SERIES_NRF52X)
#include <hal/nrf_power.h>
#endif

// Override the default fatal error handler so we can flush the log buffer
// before the MCU resets. The default handler in Zephyr calls sys_reboot()
// immediately, which truncates the fault dump being sent over USB CDC.
// Only meaningful when CONFIG_LOG is enabled (logging builds); for other
// builds we fall back to default Zephyr behavior.
#if IS_ENABLED(CONFIG_LOG)
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf) {
    LOG_ERR("===== FATAL ERROR (reason=%u) =====", reason);
    LOG_ERR("Thread: %s (%p)", k_thread_name_get(k_current_get()), k_current_get());
    log_panic();  // Force log subsystem into immediate/synchronous mode

    // Spin briefly to give USB CDC time to drain the buffer.
    // We cannot call k_sleep() in fatal context.
    for (volatile uint32_t i = 0; i < 0x800000; i++) { }

    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}
#endif

#if IS_ENABLED(CONFIG_TASK_WDT)
#include <zephyr/task_wdt/task_wdt.h>

// Feed the task watchdog from the system workqueue every 5 seconds.
// If the system workqueue hangs, the feed stops, and after the timeout
// the hardware watchdog resets the MCU with RESETREAS=WATCHDOG.
#define WDT_FEED_INTERVAL_MS 5000
#define WDT_TIMEOUT_MS 30000

static int wdt_channel_id = -1;

static void wdt_feed_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(wdt_feed_work, wdt_feed_handler);

static void wdt_feed_handler(struct k_work *work) {
    if (wdt_channel_id >= 0) {
        task_wdt_feed(wdt_channel_id);
    }
    k_work_schedule(&wdt_feed_work, K_MSEC(WDT_FEED_INTERVAL_MS));
}

static void wdt_init(void) {
    int err = task_wdt_init(NULL);
    if (err) {
        LOG_ERR("Task WDT init failed: %d", err);
        return;
    }
    wdt_channel_id = task_wdt_add(WDT_TIMEOUT_MS, NULL, NULL);
    if (wdt_channel_id < 0) {
        LOG_ERR("Task WDT add channel failed: %d", wdt_channel_id);
        return;
    }
    k_work_schedule(&wdt_feed_work, K_MSEC(WDT_FEED_INTERVAL_MS));
    LOG_INF("Task watchdog started: feed=%dms timeout=%dms", WDT_FEED_INTERVAL_MS, WDT_TIMEOUT_MS);
}
#endif /* CONFIG_TASK_WDT */

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

#if IS_ENABLED(CONFIG_TASK_WDT)
    wdt_init();
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
