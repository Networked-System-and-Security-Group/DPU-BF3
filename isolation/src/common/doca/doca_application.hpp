#ifndef __DOCA_APPLICATION_H__
#define __DOCA_APPLICATION_H__

#include <doca_argp.h>
#include <doca_error.h>
#include <doca_log.h>

class doca_application {
  public:
    const char *name;

    doca_application(const char *name);
    virtual ~doca_application();

    doca_error_t create_logger();
    doca_error_t parse_args(int argc, char **argv, void *config);

  protected:
    virtual doca_error_t register_params() = 0;
    doca_error_t register_param(const char *short_name, const char *long_name,
                                const char *description,
                                doca_argp_param_cb_t callback,
                                doca_argp_type type);
};

#endif