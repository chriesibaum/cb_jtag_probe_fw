#ifndef INFO_H_
#define INFO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_INFO_LINES_COUNT 10u

/*
 * Returns a pointer to a static array of informational text lines.
 * The returned pointer remains valid until the next call.
 */
const char *const *get_device_information(size_t *count);

void info_log(void);

#ifdef __cplusplus
}
#endif

#endif /* INFO_H_ */
