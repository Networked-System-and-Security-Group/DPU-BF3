#include "eth_application.hpp"
#include "doca_application.hpp"
#include "doca_error.h"
#include <doca_argp.h>

#include "utils.hpp"

eth_application::eth_application(const char *name) : doca_application(name) {}

eth_application::~eth_application() {}

static doca_error_t device_address_callback(void *param, void *config) {
    struct eth_application_config *eth_rxq_cfg =
        (struct eth_application_config *)config;

    return extract_ibdev_name((char *)param, eth_rxq_cfg->ib_dev_name);
}

doca_error_t eth_application::register_params() {
    doca_error_t result = DOCA_SUCCESS;

    register_param("d", "device", "IB device name - default: mlx5_0",
                   device_address_callback, DOCA_ARGP_TYPE_STRING);

    return result;
}
