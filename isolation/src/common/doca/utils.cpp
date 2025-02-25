#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>

#include "utils.hpp"

DOCA_LOG_REGISTER(UTILS);

doca_error_t extract_ibdev_name(char *ibdev_name, char *ibdev_name_out) {
    int len;

    if (ibdev_name == NULL || ibdev_name_out == NULL)
        return DOCA_ERROR_INVALID_VALUE;

    len = strnlen(ibdev_name, DOCA_DEVINFO_IBDEV_NAME_SIZE);
    if (len == DOCA_DEVINFO_IBDEV_NAME_SIZE) {
        DOCA_LOG_ERR("IB device name exceeding the maximum size of %d",
                     DOCA_DEVINFO_IBDEV_NAME_SIZE - 1);
        return DOCA_ERROR_INVALID_VALUE;
    }

    strncpy(ibdev_name_out, ibdev_name, len + 1);

    return DOCA_SUCCESS;
}

doca_error_t open_doca_device_with_ibdev_name(const uint8_t *value,
                                              size_t val_size,
                                              struct doca_dev **retval) {
	return DOCA_SUCCESS;
}
