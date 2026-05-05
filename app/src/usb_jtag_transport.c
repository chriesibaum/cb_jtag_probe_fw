#include "usb_jtag_transport.h"

#include <string.h>

#include "fw_version.h"
#include "jtag_engine.h"
#include "jtag_proto.h"

int usb_jtag_transport_process_frame(const uint8_t *rx_buf,
				     size_t rx_len,
				     uint8_t *tx_buf,
				     size_t tx_buf_len,
				     size_t *tx_len)
{
	struct jtag_scan_request req;
	struct jtag_scan_response rsp;
	uint16_t reserved;
	uint32_t n_bits;
	uint8_t cmd;
	int rc;

	if ((rx_buf == NULL) || (tx_buf == NULL) || (tx_len == NULL)) {
		return -1;
	}

	cmd = rx_buf[0];

	if (cmd == JTAG_CMD_SCAN) {
		rc = jtag_proto_decode_scan_request(rx_buf, rx_len, &req);
		if (rc != 0) {
			rsp.status = JTAG_STATUS_BAD_LEN;
			rsp.flags = 0u;
			rsp.n_bits = 0u;
			rsp.n_bytes = 0u;
			rsp.tdo = NULL;
			return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
		}

		rc = jtag_engine_shift_bits(req.tdi, req.tms,
					    &tx_buf[JTAG_PROTO_HEADER_SIZE], req.n_bits);
		if (rc != 0) {
			rsp.status = JTAG_STATUS_INTERNAL_ERR;
			rsp.flags = req.flags;
			rsp.n_bits = req.n_bits;
			rsp.n_bytes = 0u;
			rsp.tdo = NULL;
			return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
		}

		rsp.status = JTAG_STATUS_OK;
		rsp.flags = req.flags;
		rsp.n_bits = req.n_bits;
		rsp.n_bytes = req.n_bytes;
		rsp.tdo = &tx_buf[JTAG_PROTO_HEADER_SIZE];

		return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
	}

	if (rx_len != JTAG_PROTO_HEADER_SIZE) {
		rsp.status = JTAG_STATUS_BAD_LEN;
		rsp.flags = 0u;
		rsp.n_bits = 0u;
		rsp.n_bytes = 0u;
		rsp.tdo = NULL;
		return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
	}

	reserved = (uint16_t)rx_buf[2] | ((uint16_t)rx_buf[3] << 8);
	n_bits = (uint32_t)rx_buf[4] |
		 ((uint32_t)rx_buf[5] << 8) |
		 ((uint32_t)rx_buf[6] << 16) |
		 ((uint32_t)rx_buf[7] << 24);

	if ((reserved != 0u) || (n_bits != 0u)) {
		rsp.status = JTAG_STATUS_BAD_LEN;
		rsp.flags = 0u;
		rsp.n_bits = 0u;
		rsp.n_bytes = 0u;
		rsp.tdo = NULL;
		return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
	}

	if (cmd == JTAG_CMD_NSRST_HIGH) {
		rc = jtag_engine_set_nsrst_high();
	} else if (cmd == JTAG_CMD_NSRST_LOW) {
		rc = jtag_engine_set_nsrst_low();
	} else if (cmd == JTAG_CMD_GET_FW_VERSION) {
		uint8_t version_payload[JTAG_FW_VERSION_PAYLOAD_LEN] = {0};

		strncpy((char *)version_payload, FW_VERSION_STRING,
			JTAG_FW_VERSION_PAYLOAD_LEN - 1u);

		rsp.status = JTAG_STATUS_OK;
		rsp.flags = 0u;
		rsp.n_bits = JTAG_FW_VERSION_PAYLOAD_LEN * 8u;
		rsp.n_bytes = JTAG_FW_VERSION_PAYLOAD_LEN;
		rsp.tdo = version_payload;
		return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
	} else {
		rsp.status = JTAG_STATUS_BAD_CMD;
		rsp.flags = 0u;
		rsp.n_bits = 0u;
		rsp.n_bytes = 0u;
		rsp.tdo = NULL;
		return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
	}

	rsp.status = (rc == 0) ? JTAG_STATUS_OK : JTAG_STATUS_INTERNAL_ERR;
	rsp.flags = 0u;
	rsp.n_bits = 0u;
	rsp.n_bytes = 0u;
	rsp.tdo = NULL;

	return jtag_proto_encode_scan_response(&rsp, tx_buf, tx_buf_len, tx_len);
}
