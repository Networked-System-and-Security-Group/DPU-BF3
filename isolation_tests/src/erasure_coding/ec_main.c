#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <doca_argp.h>
#include <doca_dev.h>
#include <doca_erasure_coding.h>
#include <doca_error.h>
#include <doca_log.h>

#include <utils.h>

DOCA_LOG_REGISTER(EC_RECOVER::MAIN);

#define USER_MAX_PATH_NAME 255 /* max file name length */
#define MAX_PATH_NAME                                                          \
    (USER_MAX_PATH_NAME + 1)  /* max file name string length                   \
                               */
#define MAX_BLOCKS (128 + 32) /* ec blocks up to 128 in, 32 out */

/* Configuration struct */
struct ec_cfg {
    char output_path[MAX_PATH_NAME]; /* output might be a file or a folder -
                                        depends on the input and do_both */
    char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE]; /* device PCI address */
    uint32_t data_block_count;                    /* data block count */
    uint32_t rdnc_block_count;                    /* redundancy block count */
    int total_nb_tasks;
};

/* Sample's Logic */
doca_error_t ec_recover(const char *pci_addr, const char *output_path,
                        uint32_t data_block_count, uint32_t rdnc_block_count,
                        int total_nb_tasks);

/*
 * ARGP Callback - Handle user output path parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t output_path_callback(void *param, void *config) {
    struct ec_cfg *ec_cfg = (struct ec_cfg *)config;
    char *file = (char *)param;
    int len;

    len = strnlen(file, MAX_PATH_NAME);
    if (len >= MAX_PATH_NAME) {
        DOCA_LOG_ERR("Invalid output path name length, max %d",
                     USER_MAX_PATH_NAME);
        return DOCA_ERROR_INVALID_VALUE;
    }
    if (access(ec_cfg->output_path, F_OK) == -1) {
        DOCA_LOG_ERR("Output file/folder not found %s", ec_cfg->output_path);
        return DOCA_ERROR_NOT_FOUND;
    }
    strcpy(ec_cfg->output_path, file);
    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle data block count parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t data_block_count_callback(void *param, void *config) {
    struct ec_cfg *ec_cfg = (struct ec_cfg *)config;

    ec_cfg->data_block_count = *(uint32_t *)param;
    if (ec_cfg->data_block_count <= 0) {
        DOCA_LOG_ERR("Data block size should be bigger than 0");
        return DOCA_ERROR_INVALID_VALUE;
    }
    return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle redundancy block count parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdnc_block_count_callback(void *param, void *config) {
    struct ec_cfg *ec_cfg = (struct ec_cfg *)config;

    ec_cfg->rdnc_block_count = *(uint32_t *)param;
    if (ec_cfg->rdnc_block_count <= 0) {
        DOCA_LOG_ERR("Redundancy block size should be bigger than 0");
        return DOCA_ERROR_INVALID_VALUE;
    }
    return DOCA_SUCCESS;
}

static doca_error_t nb_tasks_callback(void *param, void *config) {
    struct ec_cfg *ec_cfg = (struct ec_cfg *)config;

    ec_cfg->total_nb_tasks = *(uint32_t *)param;
    if (ec_cfg->total_nb_tasks <= 0) {
        DOCA_LOG_ERR("Redundancy block size should be bigger than 0");
        return DOCA_ERROR_INVALID_VALUE;
    }
    return DOCA_SUCCESS;
}

/*
 * Register the command line parameters for the sample.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t register_ec_params(void) {
    doca_error_t result;
    struct doca_argp_param *output_path_param, *data_block_count_param,
        *rdnc_block_count_param, *total_nb_tasks_param;

    result = doca_argp_param_create(&output_path_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(output_path_param, "o");
    doca_argp_param_set_long_name(output_path_param, "output");
    doca_argp_param_set_description(output_path_param,
                                    "Output file/folder to ec - default: /tmp");
    doca_argp_param_set_callback(output_path_param, output_path_callback);
    doca_argp_param_set_type(output_path_param, DOCA_ARGP_TYPE_STRING);
    result = doca_argp_register_param(output_path_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_argp_param_create(&data_block_count_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(data_block_count_param, "t");
    doca_argp_param_set_long_name(data_block_count_param, "data");
    doca_argp_param_set_description(data_block_count_param,
                                    "Data block count - default: 2");
    doca_argp_param_set_callback(data_block_count_param,
                                 data_block_count_callback);
    doca_argp_param_set_type(data_block_count_param, DOCA_ARGP_TYPE_INT);
    result = doca_argp_register_param(data_block_count_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_argp_param_create(&rdnc_block_count_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(rdnc_block_count_param, "r");
    doca_argp_param_set_long_name(rdnc_block_count_param, "rdnc");
    doca_argp_param_set_description(rdnc_block_count_param,
                                    "Redundancy block count - default: 2");
    doca_argp_param_set_callback(rdnc_block_count_param,
                                 rdnc_block_count_callback);
    doca_argp_param_set_type(rdnc_block_count_param, DOCA_ARGP_TYPE_INT);
    result = doca_argp_register_param(rdnc_block_count_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_argp_param_create(&total_nb_tasks_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ARGP param: %s",
                     doca_error_get_descr(result));
        return result;
    }
    doca_argp_param_set_short_name(total_nb_tasks_param, "n");
    doca_argp_param_set_long_name(total_nb_tasks_param, "num");
    doca_argp_param_set_description(total_nb_tasks_param,
                                    "Total nb tasks - default: 10");
    doca_argp_param_set_callback(total_nb_tasks_param, nb_tasks_callback);
    doca_argp_param_set_type(total_nb_tasks_param, DOCA_ARGP_TYPE_INT);
    result = doca_argp_register_param(total_nb_tasks_param);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register program param: %s",
                     doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv) {
    doca_error_t result;
    int exit_status = EXIT_FAILURE;
    struct ec_cfg ec_cfg;
    struct doca_log_backend *sdk_log;
    int len;

    /* Register a logger backend */
    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS)
        goto sample_exit;

    /* Register a logger backend for internal SDK errors and warnings */
    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS)
        goto sample_exit;
    result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
    if (result != DOCA_SUCCESS)
        goto sample_exit;

    // DOCA_LOG_INFO("Starting the sample");

    len = strnlen(argv[0], USER_MAX_PATH_NAME);
    if (len >= MAX_PATH_NAME) {
        DOCA_LOG_ERR("Self path is too long, max %d", USER_MAX_PATH_NAME);
        goto sample_exit;
    }
    strcpy(ec_cfg.pci_address, "03:00.0");
    strcpy(ec_cfg.output_path, "/tmp");
    ec_cfg.data_block_count = 2; /* data block count */
    ec_cfg.rdnc_block_count = 2; /* redundancy block count */
    ec_cfg.total_nb_tasks = 10;

    result = doca_argp_init("doca_erasure_coding_recover", &ec_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init ARGP resources: %s",
                     doca_error_get_descr(result));
        goto sample_exit;
    }

    result = register_ec_params();
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register ARGP params: %s",
                     doca_error_get_descr(result));
        goto argp_cleanup;
    }

    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to parse sample input: %s",
                     doca_error_get_descr(result));
        goto argp_cleanup;
    }

    result = ec_recover(ec_cfg.pci_address, ec_cfg.output_path,
                        ec_cfg.data_block_count, ec_cfg.rdnc_block_count,
                        ec_cfg.total_nb_tasks);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("ec_recover() encountered an error: %s",
                     doca_error_get_descr(result));
        goto argp_cleanup;
    }

    exit_status = EXIT_SUCCESS;

argp_cleanup:
    doca_argp_destroy();
sample_exit:
    if (exit_status == EXIT_SUCCESS)
        DOCA_LOG_INFO("Sample finished successfully");
    else
        DOCA_LOG_INFO("Sample finished with errors");
    return exit_status;
}
