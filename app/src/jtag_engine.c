#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <string.h>

#include "jtag_engine.h"
#include "jtag_proto.h"
#include "cb_error.h"

// ---- logging includes/defines ----------------------------------------------
#include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(JTAG_ENGINE, CONFIG_LOG_DEFAULT_LEVEL);
LOG_MODULE_REGISTER(JTAG_ENGINE, LOG_LEVEL_DBG);

#define JTAG_TCK_NODE DT_ALIAS(jtag_tck)
#define JTAG_TMS_NODE DT_ALIAS(jtag_tms)
#define JTAG_TDI_NODE DT_ALIAS(jtag_tdi)
#define JTAG_TDO_NODE DT_ALIAS(jtag_tdo)
#define JTAG_NSRST_NODE DT_NODELABEL(jtag_nsrst)

#if DT_HAS_ALIAS(jtag_tck) && DT_HAS_ALIAS(jtag_tms) && \
	DT_HAS_ALIAS(jtag_tdi) && DT_HAS_ALIAS(jtag_tdo)

    static const struct gpio_dt_spec jtag_tck = GPIO_DT_SPEC_GET(JTAG_TCK_NODE, gpios);
    static const struct gpio_dt_spec jtag_tms = GPIO_DT_SPEC_GET(JTAG_TMS_NODE, gpios);
    static const struct gpio_dt_spec jtag_tdi = GPIO_DT_SPEC_GET(JTAG_TDI_NODE, gpios);
    static const struct gpio_dt_spec jtag_tdo = GPIO_DT_SPEC_GET(JTAG_TDO_NODE, gpios);
#else
#error "JTAG GPIO aliases (jtag-tck/tms/tdi/tdo) must be defined in devicetree"
#endif

static bool jtag_ready;
static bool nsrst_ready;
static const size_t jtag_log_hexdump_max_bytes = 64u;

#if DT_NODE_EXISTS(JTAG_NSRST_NODE)
static const struct gpio_dt_spec jtag_nsrst = GPIO_DT_SPEC_GET(JTAG_NSRST_NODE, gpios);
#endif

static inline uint8_t get_bit(const uint8_t *buf, uint32_t bit_index)
{
	return (buf[bit_index / 8u] >> (bit_index % 8u)) & 0x1u;
}

static inline void set_bit(uint8_t *buf, uint32_t bit_index, uint8_t value)
{
	uint8_t mask = (uint8_t)(1u << (bit_index % 8u));

	if (value != 0u) {
		buf[bit_index / 8u] |= mask;
	} else {
		buf[bit_index / 8u] &= (uint8_t)(~mask);
	}
}

int jtag_engine_init(void)
{
	int rc;

	if (!device_is_ready(jtag_tck.port) || !device_is_ready(jtag_tms.port) ||
	    !device_is_ready(jtag_tdi.port) || !device_is_ready(jtag_tdo.port)) {
		LOG_ERR("JTAG GPIO port not ready");
		return -1;
	}

	rc = gpio_pin_configure_dt(&jtag_tck, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("Failed to configure TCK rc=%d", rc);
		return rc;
	}

	rc = gpio_pin_configure_dt(&jtag_tms, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("Failed to configure TMS rc=%d", rc);
		return rc;
	}

	rc = gpio_pin_configure_dt(&jtag_tdi, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("Failed to configure TDI rc=%d", rc);
		return rc;
	}

	rc = gpio_pin_configure_dt(&jtag_tdo, GPIO_INPUT);
	if (rc != 0) {
		LOG_ERR("Failed to configure TDO rc=%d", rc);
		return rc;
	}

#if DT_NODE_EXISTS(JTAG_NSRST_NODE)
	if (!device_is_ready(jtag_nsrst.port)) {
		LOG_WRN("nSRST GPIO port not ready, reset control disabled");
		nsrst_ready = false;
	} else {
		rc = gpio_pin_configure_dt(&jtag_nsrst, GPIO_OUTPUT_ACTIVE);
		if (rc != 0) {
			LOG_WRN("Failed to configure nSRST rc=%d, reset control disabled", rc);
			nsrst_ready = false;
		} else {
			nsrst_ready = true;
		}
	}
#else
	nsrst_ready = false;
#endif

	jtag_ready = true;
	return 0;
}

int jtag_engine_shift_bits(const uint8_t *tdi,
			   const uint8_t *tms,
			   uint8_t *tdo,
			   uint32_t n_bits)
{
	size_t n_bytes;
	size_t dump_bytes;
	int rc;

	if (!jtag_ready) {
		return -1;
	}

	if ((tdi == NULL) || (tms == NULL) || (tdo == NULL) || (n_bits == 0u)) {
		return -2;
	}

	n_bytes = jtag_bytes_for_bits(n_bits);
	dump_bytes = (n_bytes > jtag_log_hexdump_max_bytes) ? jtag_log_hexdump_max_bytes : n_bytes;

	LOG_DBG("Shifting %u bits through JTAG chain (%u bytes)", n_bits, (uint32_t)n_bytes);
	LOG_HEXDUMP_DBG(tdi, dump_bytes, "TDI");
	LOG_HEXDUMP_DBG(tms, dump_bytes, "TMS");
	if (dump_bytes < n_bytes) {
		LOG_DBG("Hexdump truncated to %u/%u bytes", (uint32_t)dump_bytes, (uint32_t)n_bytes);
	}

	memset(tdo, 0, n_bytes);

	for (uint32_t i = 0; i < n_bits; i++) {
		rc = gpio_pin_set_dt(&jtag_tms, get_bit(tms, i));
		if (rc != 0) {
			return rc;
		}

		rc = gpio_pin_set_dt(&jtag_tdi, get_bit(tdi, i));
		if (rc != 0) {
			return rc;
		}

		rc = gpio_pin_set_dt(&jtag_tck, 1);
		if (rc != 0) {
			return rc;
		}

		rc = gpio_pin_get_dt(&jtag_tdo);
		if (rc < 0) {
			return rc;
		}
		set_bit(tdo, i, (uint8_t)rc);

		rc = gpio_pin_set_dt(&jtag_tck, 0);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

int jtag_engine_shift_loopback(const uint8_t *tdi,
				      const uint8_t *tms,
				      uint8_t *tdo,
				      uint32_t n_bits)
{
	size_t n_bytes;
	uint8_t last_mask;

	(void)tms;

	if ((tdi == NULL) || (tdo == NULL) || (n_bits == 0u)) {
		return -1;
	}

	n_bytes = jtag_bytes_for_bits(n_bits);
	memcpy(tdo, tdi, n_bytes);

	/* Ensure padding bits above n_bits are always cleared. */
	last_mask = (uint8_t)((n_bits % 8u) == 0u ? 0xFFu : ((1u << (n_bits % 8u)) - 1u));
	tdo[n_bytes - 1u] &= last_mask;

    LOG_HEXDUMP_DBG(tdo, n_bytes, "TDO");

	return 0;
}

int jtag_engine_set_nsrst_high(void)
{
#if DT_NODE_EXISTS(JTAG_NSRST_NODE)
	if (!jtag_ready) {
		return -EIO;
	}

	if (!nsrst_ready) {
		return -ENOTSUP;
	}

	LOG_DBG("set nSRST high");
	return gpio_pin_set_dt(&jtag_nsrst, 1);
#else
	return -ENOTSUP;
#endif
}

int jtag_engine_set_nsrst_low(void)
{
#if DT_NODE_EXISTS(JTAG_NSRST_NODE)
	if (!jtag_ready) {
		return -EIO;
	}

	if (!nsrst_ready) {
		return -ENOTSUP;
	}

	LOG_DBG("set nSRST low");
	return gpio_pin_set_dt(&jtag_nsrst, 0);
#else
	return -ENOTSUP;
#endif
}


