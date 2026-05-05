#include "info.h"
#include "fw_version.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/version.h>

// ---- logging includes/defines ----------------------------------------------
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info, CONFIG_LOG_DEFAULT_LEVEL);

#ifndef CONFIG_BOARD
#define CONFIG_BOARD "unknown"
#endif

#ifndef CONFIG_SOC
#define CONFIG_SOC "unknown"
#endif

static const char *const separator_line =
    "--------------------------------------------------------------------";

static const char *cpu_type_name(void)
{
#if defined(__riscv)
    return "RISC-V";
#elif defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__)
#if defined(CONFIG_CPU_CORTEX_M85)
    return "ARM Cortex-M85";
#elif defined(CONFIG_CPU_CORTEX_M55)
    return "ARM Cortex-M55";
#elif defined(CONFIG_CPU_CORTEX_M52)
    return "ARM Cortex-M52";
#elif defined(CONFIG_CPU_CORTEX_M33)
    return "ARM Cortex-M33";
#elif defined(CONFIG_CPU_CORTEX_M23)
    return "ARM Cortex-M23";
#elif defined(CONFIG_CPU_CORTEX_M7)
    return "ARM Cortex-M7";
#elif defined(CONFIG_CPU_CORTEX_M4)
    return "ARM Cortex-M4";
#elif defined(CONFIG_CPU_CORTEX_M3)
    return "ARM Cortex-M3";
#elif defined(CONFIG_CPU_CORTEX_M1)
    return "ARM Cortex-M1";
#elif defined(CONFIG_CPU_CORTEX_M0PLUS)
    return "ARM Cortex-M0+";
#elif defined(CONFIG_CPU_CORTEX_M0)
    return "ARM Cortex-M0";
#elif defined(CONFIG_CPU_CORTEX_R52)
    return "ARM Cortex-R52";
#elif defined(CONFIG_CPU_CORTEX_R8)
    return "ARM Cortex-R8";
#elif defined(CONFIG_CPU_CORTEX_A72)
    return "ARM Cortex-A72";
#elif defined(CONFIG_CPU_CORTEX_A55)
    return "ARM Cortex-A55";
#elif defined(CONFIG_CPU_CORTEX_A53)
    return "ARM Cortex-A53";
#else
    return "ARM";
#endif
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

static void format_device_id(char *dst, size_t dst_len)
{
    uint8_t id_buf[16];
    ssize_t id_len;
    size_t pos = 0u;

    id_len = hwinfo_get_device_id(id_buf, sizeof(id_buf));
    if (id_len <= 0) {
        (void)snprintf(dst, dst_len, "unknown");
        return;
    }

    for (ssize_t i = 0; i < id_len; i++) {
        int written;

        written = snprintf(&dst[pos], dst_len - pos, "%02X", id_buf[i]);
        if ((written <= 0) || ((size_t)written >= (dst_len - pos))) {
            break;
        }
        pos += (size_t)written;
    }
}

const char *const *get_device_information(size_t *count)
{
    static char release_line[96];
    static char zephyr_line[96];
    static char build_date_line[96];
    static char board_line[96];
    static char soc_line[128];
    static char cpu_line[96];
    static char device_id_line[96];
    static char device_id[40];
    static const char *lines[DEVICE_INFO_LINES_COUNT];

    format_device_id(device_id, sizeof(device_id));

    (void)snprintf(release_line, sizeof(release_line),
               "  Release:         %s", FW_VERSION_STRING);
    (void)snprintf(zephyr_line, sizeof(zephyr_line),
               "  Zephyr release:  %s", KERNEL_VERSION_STRING);
    (void)snprintf(build_date_line, sizeof(build_date_line),
               "  Build date:      %s %s", __DATE__, __TIME__);
    (void)snprintf(board_line, sizeof(board_line),
               "  Board name:      %s", CONFIG_BOARD);
    (void)snprintf(soc_line, sizeof(soc_line),
               "  SOC:             %s running @ %u MHz",
               CONFIG_SOC,
               (uint32_t)(sys_clock_hw_cycles_per_sec() / 1000000u));
    (void)snprintf(cpu_line, sizeof(cpu_line),
               "  CPU type:        %s (%u-bit)",
               cpu_type_name(),
               (unsigned int)(sizeof(void *) * 8u));
    (void)snprintf(device_id_line, sizeof(device_id_line),
               "  Device ID:       %s", device_id);

    lines[0] = separator_line;
    lines[1] = "Application and board information:";
    lines[2] = release_line;
    lines[3] = zephyr_line;
    lines[4] = build_date_line;
    lines[5] = board_line;
    lines[6] = soc_line;
    lines[7] = cpu_line;
    lines[8] = device_id_line;
    lines[9] = separator_line;

    if (count != NULL) {
        *count = DEVICE_INFO_LINES_COUNT;
    }

    return lines;
}



void info_log(void)
{
    // info content to log channel
    size_t count;
    const char *const *lines = get_device_information(&count);
    for (size_t i = 0u; i < count; i++) {
        LOG_INF("%s", lines[i]);
    }
}