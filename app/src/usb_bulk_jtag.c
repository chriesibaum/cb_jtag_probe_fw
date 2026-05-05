#include "usb_bulk_jtag.h"

#include <errno.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>

#include "jtag_proto.h"
#include "usb_jtag_transport.h"

LOG_MODULE_REGISTER(usb_bulk_jtag, CONFIG_LOG_DEFAULT_LEVEL);

#define JTAG_BULK_EP_OUT_ADDR 0x01
#define JTAG_BULK_EP_IN_ADDR  0x81

#define JTAG_BULK_MAX_BITS        4096u
#define JTAG_BULK_MAX_REQ_BYTES   (JTAG_PROTO_HEADER_SIZE + (2u * (JTAG_BULK_MAX_BITS / 8u)))
#define JTAG_BULK_MAX_RESP_BYTES  (JTAG_PROTO_HEADER_SIZE + (JTAG_BULK_MAX_BITS / 8u))
#define JTAG_BULK_OUT_XFER_BYTES  USBD_MAX_BULK_MPS

enum {
	JTAG_BULK_ENABLED = 0,
	JTAG_BULK_OUT_ARMED,
	JTAG_BULK_IN_FLIGHT,
};

struct jtag_bulk_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_hs_out_ep;
	struct usb_ep_descriptor if0_hs_in_ep;
	struct usb_desc_header nil_desc;
};

struct jtag_bulk_data {
	struct jtag_bulk_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	const struct usb_desc_header **const hs_desc;
	atomic_t state;
	size_t rx_len;
	uint8_t rx_buf[JTAG_BULK_MAX_REQ_BYTES];
};

static uint8_t tx_buf[JTAG_BULK_MAX_RESP_BYTES];

static int jtag_bulk_expected_req_len(const uint8_t *buf, size_t len, size_t *expected)
{
	uint8_t cmd;
	uint32_t n_bits;
	size_t n_bytes;

	if ((buf == NULL) || (expected == NULL)) {
		return -EINVAL;
	}

	if (len < JTAG_PROTO_HEADER_SIZE) {
		return -EAGAIN;
	}

	cmd = buf[0];
	if ((cmd == JTAG_CMD_NSRST_HIGH) ||
	    (cmd == JTAG_CMD_NSRST_LOW) ||
	    (cmd == JTAG_CMD_GET_FW_VERSION)) {
		*expected = JTAG_PROTO_HEADER_SIZE;
		return 0;
	}

	if (cmd != JTAG_CMD_SCAN) {
		return -EINVAL;
	}

	/* Header layout: cmd(1), flags(1), reserved(2), n_bits(4 LE). */
	n_bits = (uint32_t)buf[4] |
		 ((uint32_t)buf[5] << 8) |
		 ((uint32_t)buf[6] << 16) |
		 ((uint32_t)buf[7] << 24);

	if (n_bits == 0u) {
		return -EINVAL;
	}

	n_bytes = jtag_bytes_for_bits(n_bits);
	*expected = JTAG_PROTO_HEADER_SIZE + (2u * n_bytes);

	if (*expected > JTAG_BULK_MAX_REQ_BYTES) {
		return -EMSGSIZE;
	}

	return 0;
}

static uint8_t jtag_bulk_get_out_ep(struct usbd_class_data *const c_data)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);

	if (USBD_SUPPORTS_HIGH_SPEED && usbd_bus_speed(ctx) == USBD_SPEED_HS) {
		return data->desc->if0_hs_out_ep.bEndpointAddress;
	}

	return data->desc->if0_out_ep.bEndpointAddress;
}

static uint8_t jtag_bulk_get_in_ep(struct usbd_class_data *const c_data)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);

	if (USBD_SUPPORTS_HIGH_SPEED && usbd_bus_speed(ctx) == USBD_SPEED_HS) {
		return data->desc->if0_hs_in_ep.bEndpointAddress;
	}

	return data->desc->if0_in_ep.bEndpointAddress;
}

static int jtag_bulk_submit_out(struct usbd_class_data *const c_data)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int ret;

	if (!atomic_test_bit(&data->state, JTAG_BULK_ENABLED)) {
		return -EPERM;
	}

	buf = usbd_ep_buf_alloc(c_data, jtag_bulk_get_out_ep(c_data), JTAG_BULK_OUT_XFER_BYTES);
	if (buf == NULL) {
		LOG_ERR("Failed to allocate OUT buffer (%u bytes)", JTAG_BULK_OUT_XFER_BYTES);
		return -ENOMEM;
	}

	ret = usbd_ep_enqueue(c_data, buf);
	if (ret != 0) {
		LOG_ERR("Failed to enqueue OUT transfer ret=%d", ret);
		net_buf_unref(buf);
	} else {
		atomic_set_bit(&data->state, JTAG_BULK_OUT_ARMED);
	}

	return ret;
}

static int jtag_bulk_submit_in(struct usbd_class_data *const c_data,
			      const uint8_t *data,
			      size_t len)
{
	struct jtag_bulk_data *bulk_data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int ret;

	if ((data == NULL) || (len == 0u) || (len > JTAG_BULK_MAX_RESP_BYTES)) {
		return -EINVAL;
	}

	buf = usbd_ep_buf_alloc(c_data, jtag_bulk_get_in_ep(c_data), len);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, data, len);
	ret = usbd_ep_enqueue(c_data, buf);
	if (ret != 0) {
		net_buf_unref(buf);
	} else {
		atomic_set_bit(&bulk_data->state, JTAG_BULK_IN_FLIGHT);
	}

	return ret;
}

static void jtag_bulk_send_status(struct usbd_class_data *const c_data, uint8_t status)
{
	struct jtag_scan_response rsp = {
		.status = status,
		.flags = 0u,
		.n_bits = 0u,
		.n_bytes = 0u,
		.tdo = NULL,
	};
	size_t tx_len = 0u;

	if (jtag_proto_encode_scan_response(&rsp, tx_buf, sizeof(tx_buf), &tx_len) == 0) {
		(void)jtag_bulk_submit_in(c_data, tx_buf, tx_len);
	}
}

static void jtag_bulk_reset_rx(struct jtag_bulk_data *data)
{
	data->rx_len = 0u;
}

static int jtag_bulk_request_handler(struct usbd_class_data *const c_data,
				     struct net_buf *const buf,
				     int err)
{
	struct udc_buf_info *bi = udc_get_buf_info(buf);
	uint8_t ep = bi->ep;

	if (ep == jtag_bulk_get_out_ep(c_data)) {
		struct jtag_bulk_data *data = usbd_class_get_private(c_data);

		atomic_clear_bit(&data->state, JTAG_BULK_OUT_ARMED);

		if (err == 0) {
			size_t tx_len = 0u;
			size_t expected = 0u;
			int ret;

			if ((data->rx_len + buf->len) > sizeof(data->rx_buf)) {
				LOG_WRN("RX frame overflow (%u + %u)", (uint32_t)data->rx_len,
					(uint32_t)buf->len);
				jtag_bulk_send_status(c_data, JTAG_STATUS_BAD_LEN);
				jtag_bulk_reset_rx(data);
				net_buf_unref(buf);
				if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
					(void)jtag_bulk_submit_out(c_data);
				}
				return 0;
			}

			memcpy(&data->rx_buf[data->rx_len], buf->data, buf->len);
			data->rx_len += buf->len;

			ret = jtag_bulk_expected_req_len(data->rx_buf, data->rx_len, &expected);
			if (ret == -EAGAIN) {
				net_buf_unref(buf);
				if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
					(void)jtag_bulk_submit_out(c_data);
				}
				return 0;
			}

			if (ret != 0) {
				jtag_bulk_send_status(c_data, JTAG_STATUS_BAD_LEN);
				jtag_bulk_reset_rx(data);
				net_buf_unref(buf);
				if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
					(void)jtag_bulk_submit_out(c_data);
				}
				return 0;
			}

			if (data->rx_len < expected) {
				net_buf_unref(buf);
				if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
					(void)jtag_bulk_submit_out(c_data);
				}
				return 0;
			}

			if (data->rx_len > expected) {
				jtag_bulk_send_status(c_data, JTAG_STATUS_BAD_LEN);
				jtag_bulk_reset_rx(data);
				net_buf_unref(buf);
				if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
					(void)jtag_bulk_submit_out(c_data);
				}
				return 0;
			}

			ret = usb_jtag_transport_process_frame(data->rx_buf, data->rx_len,
							      tx_buf, sizeof(tx_buf), &tx_len);
			jtag_bulk_reset_rx(data);

			if ((ret == 0) && (tx_len > 0u)) {
				ret = jtag_bulk_submit_in(c_data, tx_buf, tx_len);
				if (ret != 0) {
					LOG_WRN("Failed to enqueue IN transfer ret=%d", ret);
				}
			} else {
				jtag_bulk_send_status(c_data, JTAG_STATUS_INTERNAL_ERR);
			}
		} else {
			jtag_bulk_send_status(c_data, JTAG_STATUS_INTERNAL_ERR);
			jtag_bulk_reset_rx(data);
		}

		net_buf_unref(buf);
		if (!atomic_test_bit(&data->state, JTAG_BULK_IN_FLIGHT)) {
			(void)jtag_bulk_submit_out(c_data);
		}
		return 0;
	}

	if (ep == jtag_bulk_get_in_ep(c_data)) {
		struct jtag_bulk_data *data = usbd_class_get_private(c_data);

		atomic_clear_bit(&data->state, JTAG_BULK_IN_FLIGHT);
		net_buf_unref(buf);

		if (atomic_test_bit(&data->state, JTAG_BULK_ENABLED) &&
		    !atomic_test_bit(&data->state, JTAG_BULK_OUT_ARMED)) {
			(void)jtag_bulk_submit_out(c_data);
		}

		if (err != 0) {
			LOG_WRN("IN transfer failed ep=0x%02x err=%d", ep, err);
		}

		return 0;
	}

	net_buf_unref(buf);
	if (err != 0) {
		LOG_WRN("Transfer failed ep=0x%02x err=%d", ep, err);
	}

	return 0;
}

static void *jtag_bulk_get_desc(struct usbd_class_data *const c_data,
				const enum usbd_speed speed)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);

	if (USBD_SUPPORTS_HIGH_SPEED && speed == USBD_SPEED_HS) {
		return data->hs_desc;
	}

	return data->fs_desc;
}

static void jtag_bulk_enable(struct usbd_class_data *const c_data)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);

	if (!atomic_test_and_set_bit(&data->state, JTAG_BULK_ENABLED)) {
		int ret = jtag_bulk_submit_out(c_data);

		if (ret != 0) {
			LOG_ERR("Failed to arm bulk OUT endpoint ret=%d", ret);
		}
	}
}

static void jtag_bulk_disable(struct usbd_class_data *const c_data)
{
	struct jtag_bulk_data *data = usbd_class_get_private(c_data);

	atomic_clear_bit(&data->state, JTAG_BULK_ENABLED);
	atomic_clear_bit(&data->state, JTAG_BULK_OUT_ARMED);
	atomic_clear_bit(&data->state, JTAG_BULK_IN_FLIGHT);
	jtag_bulk_reset_rx(data);
}

static int jtag_bulk_class_init(struct usbd_class_data *c_data)
{
	ARG_UNUSED(c_data);
	return 0;
}

static const struct usbd_class_api jtag_bulk_api = {
	.request = jtag_bulk_request_handler,
	.get_desc = jtag_bulk_get_desc,
	.enable = jtag_bulk_enable,
	.disable = jtag_bulk_disable,
	.init = jtag_bulk_class_init,
};

static struct jtag_bulk_desc jtag_bulk_desc_0 = {
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.if0_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = JTAG_BULK_EP_OUT_ADDR,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(64u),
		.bInterval = 0,
	},
	.if0_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = JTAG_BULK_EP_IN_ADDR,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(64u),
		.bInterval = 0,
	},
	.if0_hs_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = JTAG_BULK_EP_OUT_ADDR,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(512u),
		.bInterval = 0,
	},
	.if0_hs_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = JTAG_BULK_EP_IN_ADDR,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(512u),
		.bInterval = 0,
	},
	.nil_desc = {
		.bLength = 0,
		.bDescriptorType = 0,
	},
};

static const struct usb_desc_header *jtag_bulk_fs_desc_0[] = {
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0,
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0_in_ep,
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0_out_ep,
	(struct usb_desc_header *)&jtag_bulk_desc_0.nil_desc,
};

static const struct usb_desc_header *jtag_bulk_hs_desc_0[] = {
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0,
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0_hs_in_ep,
	(struct usb_desc_header *)&jtag_bulk_desc_0.if0_hs_out_ep,
	(struct usb_desc_header *)&jtag_bulk_desc_0.nil_desc,
};

static struct jtag_bulk_data jtag_bulk_data_0 = {
	.desc = &jtag_bulk_desc_0,
	.fs_desc = jtag_bulk_fs_desc_0,
	.hs_desc = jtag_bulk_hs_desc_0,
};

USBD_DEFINE_CLASS(jtag_bulk_0, &jtag_bulk_api, &jtag_bulk_data_0, NULL);

USBD_DEVICE_DEFINE(jtag_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   0x2FE3,
		   0x0001);
USBD_DESC_LANG_DEFINE(jtag_lang);
USBD_DESC_MANUFACTURER_DEFINE(jtag_mfr, "Chriesibaum GmbH");
USBD_DESC_PRODUCT_DEFINE(jtag_product, "CB-7777 JTAG Probe");
USBD_DESC_CONFIG_DEFINE(jtag_fs_cfg_desc, "JTAG FS");
USBD_DESC_CONFIG_DEFINE(jtag_hs_cfg_desc, "JTAG HS");
USBD_CONFIGURATION_DEFINE(jtag_fs_config, 0, 50, &jtag_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(jtag_hs_config, 0, 50, &jtag_hs_cfg_desc);

static int register_classes_for_speed(enum usbd_speed speed)
{
	int ret;

	ret = usbd_register_class(&jtag_usbd, "jtag_bulk_0", speed, 1);
	if (ret != 0) {
		return ret;
	}

#if defined(CONFIG_USBD_CDC_ACM_CLASS)
	ret = usbd_register_class(&jtag_usbd, "cdc_acm_0", speed, 1);
	if (ret != 0) {
		return ret;
	}
#endif

	return 0;
}

static void jtag_usbd_msg_cb(struct usbd_context *const usbd_ctx,
			     const struct usbd_msg *const msg)
{
	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			(void)usbd_enable(usbd_ctx);
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(usbd_ctx);
		}
	}
}

int usb_bulk_jtag_init(void)
{
	int ret;

	ret = usbd_add_descriptor(&jtag_usbd, &jtag_lang);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_add_descriptor(&jtag_usbd, &jtag_mfr);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_add_descriptor(&jtag_usbd, &jtag_product);
	if (ret != 0) {
		return ret;
	}

	if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&jtag_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&jtag_usbd, USBD_SPEED_HS, &jtag_hs_config);
		if (ret != 0) {
			return ret;
		}

		ret = register_classes_for_speed(USBD_SPEED_HS);
		if (ret != 0) {
			return ret;
		}
	}

	ret = usbd_add_configuration(&jtag_usbd, USBD_SPEED_FS, &jtag_fs_config);
	if (ret != 0) {
		return ret;
	}

	ret = register_classes_for_speed(USBD_SPEED_FS);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_msg_register_cb(&jtag_usbd, jtag_usbd_msg_cb);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_init(&jtag_usbd);
	if (ret != 0) {
		return ret;
	}

	if (!usbd_can_detect_vbus(&jtag_usbd)) {
		ret = usbd_enable(&jtag_usbd);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}
