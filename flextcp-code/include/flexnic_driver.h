#ifndef FLEXNIC_DRIVER_H_
#define FLEXNIC_DRIVER_H_

#include <stddef.h>
#include <flexnic.h>

/**
 * Connect to flexnic. Returns 0 on success, < 0 on error, > 0 if flexnic is not
 * ready yet.
 */
int flexnic_driver_connect(struct flexnic_info **info, void **mem_start);

/** Connect to flexnic internal memory. */
int flexnic_driver_internal(void **int_mem_start);

#endif /* ndef FLEXNIC_DRIVER_H_ */
