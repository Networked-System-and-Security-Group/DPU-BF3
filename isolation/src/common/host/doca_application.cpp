#include "doca_application.hpp"

#include <doca_argp.h>
#include <doca_error.h>
#include <doca_log.h>

#include <cstdio>

DOCA_LOG_REGISTER(DOCA_APP)

doca_application::doca_application(const char *name) : name(name) {}

doca_application::~doca_application() { doca_argp_destroy(); }

doca_error_t doca_application::create_logger() {
    doca_log_backend *sdk_log;
    if (doca_log_backend_create_standard() != DOCA_SUCCESS) {
        printf("Failed to create standard backend\n");
        return DOCA_ERROR_DRIVER;
    }

    if (doca_log_backend_create_with_file_sdk(stderr, &sdk_log) !=
        DOCA_SUCCESS) {
        printf("Failed to create log backend with stderr\n");
        return DOCA_ERROR_DRIVER;
    }

    if (doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING) !=
        DOCA_SUCCESS) {
        printf("Failed to set log level\n");
        return DOCA_ERROR_DRIVER;
    }

    DOCA_LOG_INFO("Successfully create logger");

    return DOCA_SUCCESS;
}

doca_error_t doca_application::parse_args(int argc, char **argv, void *config) {
    doca_error_t result = DOCA_SUCCESS;

    result = doca_argp_init(name, config);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init argp: %s", doca_error_get_descr(result));
        return result;
    }

    result = register_params();
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register params\n");
        return result;
    }

    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start argp: %s", doca_error_get_descr(result));
        return result;
    }

    return result;
}

doca_error_t doca_application::register_param(const char *short_name,
                                              const char *long_name,
                                              const char *description,
                                              doca_argp_param_cb_t callback,
                                              doca_argp_type type) {
    doca_argp_param *param;
    doca_error_t result = DOCA_SUCCESS;

    result = doca_argp_param_create(&param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    doca_argp_param_set_short_name(param, short_name);
    doca_argp_param_set_long_name(param, long_name);
    doca_argp_param_set_description(param, description);
    doca_argp_param_set_callback(param, callback);
    doca_argp_param_set_type(param, type);

    result = doca_argp_register_param(param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register param: %s",
                     doca_error_get_descr(result));
        return result; /* result will be returned anyway :) */
    }

    return result;
}