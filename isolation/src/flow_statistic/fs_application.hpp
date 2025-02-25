#ifndef __FS_APPLICATION__
#define __FS_APPLICATION__

#include "eth_application.hpp"

class fs_application : public eth_application {
  public:
    fs_application();
    ~fs_application();

    doca_error_t register_params() override;
};

#endif