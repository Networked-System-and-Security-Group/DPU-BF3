#ifndef __ETH_APPLICATION__
#define __ETH_APPLICATION__

#include "doca_application.hpp"
#include <doca_dev.h>

struct eth_application_config {
    char ib_dev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE]; /* DOCA IB device name */
};

class eth_application : public doca_application {
  public:
    eth_application(const char *name);
    virtual ~eth_application();

    virtual doca_error_t register_params() override;
};

#endif