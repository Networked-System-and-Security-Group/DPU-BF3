#include <signal.h>
#include <stdint.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <utils.h>

#include "eth_l2_fwd_core.h"

DOCA_LOG_REGISTER(ETH_L2_FWD);

extern uint32_t max_forwardings;

/*
 * Signal handler
 *
 * @signum [in]: The signal received to handle
 */
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
        eth_l2_fwd_force_stop();
    }
}

/*
 * ARGP Callback - Handle IB devices names parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t mlxdevs_names_callback(void *param, void *config) {
    struct eth_l2_fwd_cfg *app_cfg = (struct eth_l2_fwd_cfg *)config;
    char *mlxdevs_name = (char *)param;
    int len;

    len = strnlen(mlxdevs_name, DOCA_DEVINFO_IBDEV_NAME_SIZE);
    if (len == DOCA_DEVINFO_IBDEV_NAME_SIZE) {
        DOCA_LOG_ERR("IB device name exceeding the maximum size of %d",
                     DOCA_DEVINFO_IBDEV_NAME_SIZE - 1);
        return DOCA_ERROR_INVALID_VALUE;
    }
    strlcpy(app_cfg->mlxdev_name1, mlxdevs_name, len + 1);

    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle max forwardings parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t max_forwardings_callback(void *param, void *config) {
    (void)config;
    int *max_fwds = (int *)param;

    if (*max_fwds < 0) {
        DOCA_LOG_ERR("Max forwardings parameter must be non-negative");
        return DOCA_ERROR_INVALID_VALUE;
    }

    max_forwardings = *max_fwds;

    return DOCA_SUCCESS;
}

/*
 * Registers all flags used by the application for DOCA argument parser, so that
 * when parsing it can be parsed accordingly
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t register_eth_l2_fwd_params(void) {
    doca_error_t result;
    struct doca_argp_param *mlxdevs_names, *max_fwds;

    /* Create and register IB devices names param */
    result = doca_argp_param_create(&mlxdevs_names);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(mlxdevs_names, "d");
    doca_argp_param_set_long_name(mlxdevs_names, "devs-names");
    doca_argp_param_set_arguments(mlxdevs_names, "<name1,name2>");
    doca_argp_param_set_description(
        mlxdevs_names,
        "Set two IB devices names separated by a comma, without spaces.");
    doca_argp_param_set_callback(mlxdevs_names, mlxdevs_names_callback);
    doca_argp_param_set_type(mlxdevs_names, DOCA_ARGP_TYPE_STRING);
    doca_argp_param_set_mandatory(mlxdevs_names);
    result = doca_argp_register_param(mlxdevs_names);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    /* Create and register max forwardings param */
    result = doca_argp_param_create(&max_fwds);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(max_fwds, "f");
    doca_argp_param_set_long_name(max_fwds, "max-forwardings");
    doca_argp_param_set_arguments(max_fwds, "<num>");
    doca_argp_param_set_description(
        max_fwds, "Set max forwarded packet batches limit after which the "
                  "application run will end, default is 0, meaning no limit.");
    doca_argp_param_set_callback(max_fwds, max_forwardings_callback);
    doca_argp_param_set_type(max_fwds, DOCA_ARGP_TYPE_INT);
    result = doca_argp_register_param(max_fwds);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

/*
 * Ethernet L2 Forwarding application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv) {
    struct eth_l2_fwd_resources app_resources = {0};
    struct eth_l2_fwd_cfg app_cfg = {
        .pkts_recv_rate = ETH_L2_FWD_PKTS_RECV_RATE_DEFAULT,
        .max_pkt_size = ETH_L2_FWD_MAX_PKT_SIZE_DEFAULT,
        .pkt_max_process_time = ETH_L2_FWD_PKT_MAX_PROCESS_TIME_DEFAULT,
        .num_task_batches = ETH_L2_FWD_NUM_TASK_BATCHES_DEFAULT,
        .one_sided_fwd = 0};
    struct doca_log_backend *sdk_log;
    doca_error_t result;
    int exit_status = EXIT_SUCCESS;

    /* Register a logger backend */
    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS)
        return EXIT_FAILURE;

    /* Register a logger backend for internal SDK errors and warnings */
    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS)
        return EXIT_FAILURE;

    result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
    if (result != DOCA_SUCCESS)
        return EXIT_FAILURE;

    /* Parse cmdline/json arguments */
    result = doca_argp_init("doca_eth_l2_fwd", &app_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init ARGP resources: %s",
                     doca_error_get_descr(result));
        return EXIT_FAILURE;
    }

    /* Register application parameters */
    result = register_eth_l2_fwd_params();
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register application parameters: %s",
                     doca_error_get_descr(result));
        exit_status = EXIT_FAILURE;
        goto destroy_argp;
    }

    /* Start Arg Parser */
    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to parse application input: %s",
                     doca_error_get_descr(result));
        exit_status = EXIT_FAILURE;
        goto destroy_argp;
    }

    /* Setting the max burst size separately after all the set parameters became
     * permanent */
    app_cfg.max_burst_size =
        app_cfg.num_task_batches * ETH_L2_FWD_NUM_TASKS_PER_BATCH;

    /* Signal handlers for graceful termination */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Execute Ethernet L2 Forwarding Application logic */
    result = eth_l2_fwd_execute(&app_cfg, &app_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to execute Ethernet L2 Forwarding Application: %s",
                     doca_error_get_descr(result));
        exit_status = EXIT_FAILURE;
    }

    /* Ethernet L2 Forwarding Application resources cleanup */
    result = eth_l2_fwd_cleanup(&app_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to clean up Ethernet L2 Forwarding Application "
                     "resources: %s",
                     doca_error_get_descr(result));
        exit_status = EXIT_FAILURE;
    }

destroy_argp:
    /* Arg Parser cleanup */
    doca_argp_destroy();

    return exit_status;
}
