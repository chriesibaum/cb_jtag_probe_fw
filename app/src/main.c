#include <stdio.h>

#include <zephyr/kernel.h>

#include "error.h"
#include "info.h"
#include "jtag_engine.h"
#include "usb_bulk_jtag.h"
#include "usb_jtag_transport.h"

// ---- logging includes/defines ----------------------------------------------
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);


int32_t main(void)
{
    int ret;

    ret = jtag_engine_init();
    ERR_CHECK(ret < 0, ret, "JTAG engine init failed (err %d)", ret);

    ret = usb_bulk_jtag_init();
    ERR_CHECK(ret < 0, ret, "USB bulk JTAG init failed (err %d)", ret);

    k_sleep(K_SECONDS(1));

    info_log();

    while (true) {
        k_sleep(K_SECONDS(1));
    }
}
