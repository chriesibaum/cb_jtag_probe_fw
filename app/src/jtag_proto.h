#ifndef JTAG_PROTO_H_
#define JTAG_PROTO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JTAG_CMD_SCAN 0x01u
#define JTAG_CMD_NSRST_HIGH 0x02u
#define JTAG_CMD_NSRST_LOW 0x03u
#define JTAG_CMD_GET_FW_VERSION 0x04u

#define JTAG_FW_VERSION_PAYLOAD_LEN 32u

#define JTAG_STATUS_OK 0x00u
#define JTAG_STATUS_BAD_CMD 0x01u
#define JTAG_STATUS_BAD_LEN 0x02u
#define JTAG_STATUS_INTERNAL_ERR 0x03u

#define JTAG_PROTO_HEADER_SIZE 8u

struct jtag_scan_request {
	uint8_t flags;
	uint32_t n_bits;
	size_t n_bytes;
	const uint8_t *tdi;
	const uint8_t *tms;
};

struct jtag_scan_response {
	uint8_t status;
	uint8_t flags;
	uint32_t n_bits;
	size_t n_bytes;
	const uint8_t *tdo;
};

size_t jtag_bytes_for_bits(uint32_t n_bits);

int jtag_proto_decode_scan_request(const uint8_t *buf, size_t len,
					 struct jtag_scan_request *req);

int jtag_proto_encode_scan_response(const struct jtag_scan_response *rsp,
					 uint8_t *buf,
					 size_t buf_len,
					 size_t *encoded_len);

#ifdef __cplusplus
}
#endif

#endif /* JTAG_PROTO_H_ */
