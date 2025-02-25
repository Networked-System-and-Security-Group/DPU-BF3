#include <bits/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_erasure_coding.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>
#include <utils.h>

#include "common.h"

DOCA_LOG_REGISTER(EC_RECOVER);

int total_pos, total_time[10];

#define SLEEP_IN_NANOS (10 * 1000) /* sample the task every 10 microseconds */
/* assert function - if fails print error, clean up(state - ec_sample_objects)
 * and exit  */
#define SAMPLE_ASSERT(condition, result, state, error...)                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            DOCA_LOG_ERR(error);                                               \
            ec_cleanup(state);                                                 \
            return result;                                                     \
        }                                                                      \
    } while (0)
/* callback assert function - if fails print error, update the callback_result
 * parameter and exit  */
#define CB_ASSERT(condition, result, cb_result, error...)                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            DOCA_LOG_ERR(error);                                               \
            *(cb_result) = (result);                                           \
            goto free_task;                                                    \
        }                                                                      \
    } while (0)
/* assert function - same as before just for doca error  */
#define ASSERT_DOCA_ERR(result, state, error)                                  \
    SAMPLE_ASSERT((result) == DOCA_SUCCESS, (result), state, (error ": %s"),   \
                  doca_error_get_descr(result))

#define NUM_EC_TASKS (8192)    /* EC tasks number */
#define USER_MAX_PATH_NAME 255 /* Max file name length */
#define MAX_PATH_NAME                                                          \
    (USER_MAX_PATH_NAME + 1) /* Max file name string length                    \
                              */
#define MAX_DATA_SIZE                                                          \
    (MAX_PATH_NAME +                                                           \
     100) /* Max data file length - path + max int string size */
#define RECOVERED_FILE_NAME                                                    \
    "_recovered" /* Recovered file extension (if file name not given) */
#define DATA_INFO_FILE_NAME                                                    \
    "data_info" /* Data information file name - i.e. size & name of original   \
                   file */
#define DATA_BLOCK_FILE_NAME                                                   \
    "data_block_" /* Data blocks file name (attached index at the end) */
#define RDNC_BLOCK_FILE_NAME                                                   \
    "rdnc_block_" /* Redundancy blocks file name (attached index at the end)   \
                   */

struct ec_sample_objects {
    struct doca_buf
        *src_doca_buf; /* Source doca buffer as input for the task */
    struct doca_buf
        *dst_doca_buf;  /* Destination doca buffer as input for the task */
    struct doca_ec *ec; /* DOCA Erasure coding context */
    char
        *src_buffer; /* Source memory region to be used as input for the task */
    char *dst_buffer; /* Destination memory region to be used as output for task
                         results */
    char *file_data;  /* Block data pointer from reading block file */
    char *block_file_data; /* Block data pointer from reading block file */
    uint32_t *
        missing_indices; /* Data indices to that are missing and need recover */
    FILE *out_file;      /* Recovered file pointer to write to */
    FILE *block_file;    /* Block file pointer to write to */
    struct doca_ec_matrix *encoding_matrix; /* Encoding matrix that will be use
                                               to create the redundancy */
    struct doca_ec_matrix *decoding_matrix; /* Decoding matrix that will be use
                                               to recover the data */
    struct program_core_objects core_state; /* DOCA core objects - please refer
                                               to struct program_core_objects */
    bool run_pe_progress; /* Controls whether progress loop should run */
};

/*
 * Clean all the sample resources
 *
 * @state [in]: ec_sample_objects struct
 * @ec [in]: ec context
 */
static void ec_cleanup(struct ec_sample_objects *state) {
    doca_error_t result = DOCA_SUCCESS;

    if (state->src_doca_buf != NULL) {
        result = doca_buf_dec_refcount(state->src_doca_buf, NULL);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count: %s",
                         doca_error_get_descr(result));
    }
    if (state->dst_doca_buf != NULL) {
        result = doca_buf_dec_refcount(state->dst_doca_buf, NULL);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count: %s",
                         doca_error_get_descr(result));
    }

    if (state->missing_indices != NULL)
        free(state->missing_indices);
    if (state->block_file_data != NULL)
        free(state->block_file_data);
    if (state->file_data != NULL)
        free(state->file_data);
    if (state->src_buffer != NULL)
        free(state->src_buffer);
    if (state->dst_buffer != NULL)
        free(state->dst_buffer);
    if (state->out_file != NULL)
        fclose(state->out_file);
    if (state->block_file != NULL)
        fclose(state->block_file);
    if (state->encoding_matrix != NULL) {
        result = doca_ec_matrix_destroy(state->encoding_matrix);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to destroy ec encoding matrix: %s",
                         doca_error_get_descr(result));
    }
    if (state->decoding_matrix != NULL) {
        result = doca_ec_matrix_destroy(state->decoding_matrix);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to destroy ec decoding matrix: %s",
                         doca_error_get_descr(result));
    }

    if (state->core_state.ctx != NULL) {
        result = doca_ctx_stop(state->core_state.ctx);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Unable to stop context: %s",
                         doca_error_get_descr(result));
        state->core_state.ctx = NULL;
    }
    if (state->ec != NULL) {
        result = doca_ec_destroy(state->ec);
        if (result != DOCA_SUCCESS)
            DOCA_LOG_ERR("Failed to destroy ec: %s",
                         doca_error_get_descr(result));
    }

    result = destroy_core_objects(&state->core_state);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to destroy DOCA core objects: %s",
                     doca_error_get_descr(result));
    }
}

/**
 * Callback triggered whenever Erasure Coding context state changes
 *
 * @user_data [in]: User data associated with the Erasure Coding context. Will
 * hold struct ec_sample_objects *
 * @ctx [in]: The Erasure Coding context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when
 * the callback is called)
 */
static void ec_state_changed_callback(const union doca_data user_data,
                                      struct doca_ctx *ctx,
                                      enum doca_ctx_states prev_state,
                                      enum doca_ctx_states next_state) {
    (void)ctx;
    (void)prev_state;

    struct ec_sample_objects *state = (struct ec_sample_objects *)user_data.ptr;

    switch (next_state) {
    case DOCA_CTX_STATE_IDLE:
        // DOCA_LOG_INFO("Erasure Coding context has been stopped");
        /* We can stop progressing the PE */
        state->run_pe_progress = false;
        break;
    case DOCA_CTX_STATE_STARTING:
        /**
         * The context is in starting state, this is unexpected for Erasure
         * Coding.
         */
        DOCA_LOG_ERR("Erasure Coding context entered into starting state. "
                     "Unexpected transition");
        break;
    case DOCA_CTX_STATE_RUNNING:
        // DOCA_LOG_INFO("Erasure Coding context is running");
        break;
    case DOCA_CTX_STATE_STOPPING:
        /**
         * doca_ctx_stop() has been called.
         * In this sample, this happens either due to a failure encountered, in
         * which case doca_pe_progress() will cause any inflight task to be
         * flushed, or due to the successful compilation of the sample flow. In
         * both cases, in this sample, doca_pe_progress() will eventually
         * transition the context to idle state.
         */
        // DOCA_LOG_INFO("Erasure Coding context entered into stopping state.
        // Any "
        //               "inflight tasks will be flushed");
        break;
    default:
        break;
    }
}

/**
 * Init ec core objects.
 *
 * @state [in]: The DOCA EC sample state
 * @pci_addr [in]: The PCI address of a doca device
 * @is_support_func [in]: Function that pci device should support
 * @max_bufs [in]: The buffer count to create
 * @src_size [in]: The source data size (to create the buffer)
 * @dst_size [in]: The destination data size (to create the buffer)
 * @max_block_size [out]: The maximum block size supported for ec operations
 * @return: DOCA_SUCCESS if the core init successfully and DOCA_ERROR otherwise.
 */
static doca_error_t ec_core_init(struct ec_sample_objects *state,
                                 const char *pci_addr,
                                 tasks_check is_support_func, uint32_t max_bufs,
                                 uint32_t src_size, uint32_t dst_size,
                                 uint64_t *max_block_size) {
    doca_error_t result;
    union doca_data ctx_user_data;

    result = open_doca_device_with_pci(pci_addr, is_support_func,
                                       &state->core_state.dev);
    ASSERT_DOCA_ERR(result, state, "Unable to open the pci device");

    result = create_core_objects(&state->core_state, max_bufs);
    ASSERT_DOCA_ERR(result, state, "Failed to init core");

    result = doca_ec_create(state->core_state.dev, &state->ec);
    ASSERT_DOCA_ERR(result, state, "Unable to create ec engine");

    result = doca_ec_cap_get_max_block_size(
        doca_dev_as_devinfo(state->core_state.dev), max_block_size);
    ASSERT_DOCA_ERR(result, state,
                    "Unable to query maximum block size supported");

    state->core_state.ctx = doca_ec_as_ctx(state->ec);
    SAMPLE_ASSERT(state->core_state.ctx != NULL, DOCA_ERROR_UNEXPECTED, state,
                  "Unable to retrieve ctx");

    result = doca_pe_connect_ctx(state->core_state.pe, state->core_state.ctx);
    ASSERT_DOCA_ERR(result, state,
                    "Unable to connect context to progress engine");

    result = doca_mmap_set_memrange(state->core_state.dst_mmap,
                                    state->dst_buffer, dst_size);
    ASSERT_DOCA_ERR(result, state, "Failed to set mmap mem range dst");

    result = doca_mmap_start(state->core_state.dst_mmap);
    ASSERT_DOCA_ERR(result, state, "Failed to start mmap dst");

    result = doca_mmap_set_memrange(state->core_state.src_mmap,
                                    state->src_buffer, src_size);
    ASSERT_DOCA_ERR(result, state, "Failed to set mmap mem range src");

    result = doca_mmap_start(state->core_state.src_mmap);
    ASSERT_DOCA_ERR(result, state, "Failed to start mmap src");

    /* Construct DOCA buffer for each address range */
    result = doca_buf_inventory_buf_get_by_addr(
        state->core_state.buf_inv, state->core_state.src_mmap,
        state->src_buffer, src_size, &state->src_doca_buf);
    ASSERT_DOCA_ERR(result, state,
                    "Unable to acquire DOCA buffer representing source buffer");

    /* Construct DOCA buffer for each address range */
    result = doca_buf_inventory_buf_get_by_addr(
        state->core_state.buf_inv, state->core_state.dst_mmap,
        state->dst_buffer, dst_size, &state->dst_doca_buf);
    ASSERT_DOCA_ERR(
        result, state,
        "Unable to acquire DOCA buffer representing destination buffer");

    /* Setting data length in doca buffer */
    result =
        doca_buf_set_data(state->src_doca_buf, state->src_buffer, src_size);
    ASSERT_DOCA_ERR(result, state, "Unable to set DOCA buffer data");

    /* Include state in user data of context to be used in callbacks */
    ctx_user_data.ptr = state;
    result = doca_ctx_set_user_data(state->core_state.ctx, ctx_user_data);
    ASSERT_DOCA_ERR(result, state, "Unable to set user data to context");

    /* Set state change callback to be called whenever the context state changes
     */
    result = doca_ctx_set_state_changed_cb(state->core_state.ctx,
                                           ec_state_changed_callback);
    ASSERT_DOCA_ERR(result, state, "Unable to set state change callback");

    return DOCA_SUCCESS;
}

/*
 * EC tasks mutual error callback
 *
 * @task [in]: the failed doca task
 * @task_status [out]: the status of the task
 * @cb_result [out]: the result of the callback
 */
static void ec_task_error(struct doca_task *task, doca_error_t *task_status,
                          doca_error_t *cb_result) {
    *task_status = DOCA_ERROR_UNEXPECTED;

    DOCA_LOG_ERR("EC Task finished unsuccessfully");

    /* Free task */
    doca_task_free(task);

    *cb_result = DOCA_SUCCESS;

    /* Stop context once task is completed */
    (void)doca_ctx_stop(doca_task_get_ctx(task));
}

/*
 * All the necessary variables for EC recover task callback functions defined in
 * this sample
 */
struct recover_task_data {
    const char *dir_path;      /* The path to the tasks output file */
    char *output_file_path;    /* The path of the recovered file */
    int64_t file_size;         /* The size of the input file */
    int32_t block_size;        /* The block size used for EC */
    uint32_t data_block_count; /* The number of data blocks created */
    size_t n_missing; /* The number of missing data blocks that are to be
                       * recovered on success
                       */
    struct doca_buf
        *recovered_data_blocks; /* The buffer to which the blocks of recovered
                                 * data will be written on success
                                 */
    doca_error_t *task_status;  /* The status of the task (output parameter) */
    doca_error_t *cb_result; /* The result of the callback (output parameter) */
    struct timespec bt;
};

/*
 * EC recover task error callback
 *
 * @recover_task [in]: the failed recover task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void ec_recover_error_callback(struct doca_ec_task_recover *recover_task,
                                      union doca_data task_user_data,
                                      union doca_data ctx_user_data) {
    struct recover_task_data *task_data = task_user_data.ptr;
    (void)ctx_user_data;

    ec_task_error(doca_ec_task_recover_as_task(recover_task),
                  task_data->task_status, task_data->cb_result);
}

int nb_finished_tasks, total_nb_tasks;
struct timespec begin_time, end_time;

/*
 * EC recover task completed callback
 *
 * @recover_task [in]: the completed recover task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
ec_recover_completed_callback(struct doca_ec_task_recover *recover_task,
                              union doca_data task_user_data,
                              union doca_data ctx_user_data) {
    (void)recover_task;
    struct recover_task_data *task_data = task_user_data.ptr;
    struct ec_sample_objects *state = ctx_user_data.ptr;

    struct timespec et;
    clock_gettime(CLOCK_REALTIME, &et);
    DOCA_LOG_INFO("Cur task use %.0fns",
                  1e9 * (et.tv_sec - task_data->bt.tv_sec) +
                      (et.tv_nsec - task_data->bt.tv_nsec));

    nb_finished_tasks++;

    *task_data->task_status = DOCA_SUCCESS;

    // DOCA_LOG_INFO("File was decoded successfully, task nb is %d",
    //               nb_finished_tasks - 1);

    *task_data->cb_result = DOCA_SUCCESS;

    /* Free task */
    doca_task_free(doca_ec_task_recover_as_task(recover_task));

    if (nb_finished_tasks == total_nb_tasks) {
        clock_gettime(CLOCK_REALTIME, &end_time);
        total_time[total_pos++] = 1e9 * (end_time.tv_sec - begin_time.tv_sec) +
                                  (end_time.tv_nsec - begin_time.tv_nsec);
        (void)doca_ctx_stop(state->core_state.ctx);
    }
}

/*
 * Run ec decode
 *
 * @pci_addr [in]: PCI address of a doca device
 * @user_output_file_path [in]: path to the task output file
 * @dir_path [in]: path to the tasks output file
 * @data_block_count [in]: data block count
 * @rdnc_block_count [in]: redundancy block count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t ec_decode(const char *pci_addr, const char *user_output_file_path,
                       const char *dir_path, uint32_t data_block_count,
                       uint32_t rdnc_block_count) {
    uint32_t max_bufs = 2;
    doca_error_t result;
    int ret;
    size_t i;
    uint64_t max_block_size;
    size_t block_file_size;
    uint64_t block_size = 0;
    uint32_t str_len;
    uint64_t src_size = -1;
    uint64_t src_size_cur = 0;
    uint64_t dst_size;
    struct ec_sample_objects state_object = {0};
    struct ec_sample_objects *state = &state_object;
    size_t n_missing = 0;
    char *end;
    int64_t file_size;
    char output_file_path[MAX_PATH_NAME];
    char full_path[MAX_PATH_NAME];
    struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };

    ret = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path,
                   DATA_INFO_FILE_NAME);
    SAMPLE_ASSERT(ret >= 0 && ret < (int)sizeof(full_path),
                  DOCA_ERROR_IO_FAILED, state, "Path exceeded max path len");
    result = read_file(full_path, &state->block_file_data, &block_file_size);
    ASSERT_DOCA_ERR(result, state, "Unable to open data file");
    SAMPLE_ASSERT(block_file_size > 0, DOCA_ERROR_INVALID_VALUE, state,
                  "File data info size is empty");
    SAMPLE_ASSERT(
        strnlen(state->block_file_data, block_file_size) < MAX_DATA_SIZE,
        DOCA_ERROR_INVALID_VALUE, state, "File data info may be nonfinite");
    file_size = strtol(state->block_file_data, &end, 10);
    SAMPLE_ASSERT(file_size > 0, DOCA_ERROR_INVALID_VALUE, state,
                  "File size from data info file none positive");
    SAMPLE_ASSERT(*end != '\0', DOCA_ERROR_INVALID_VALUE, state,
                  "Data info file not containing path");

    if (user_output_file_path != NULL) {
        SAMPLE_ASSERT(
            strnlen(user_output_file_path, MAX_PATH_NAME) < MAX_PATH_NAME,
            DOCA_ERROR_INVALID_VALUE, state, "Path exceeded max path len");
        strcpy(output_file_path, user_output_file_path);
    } else {
        str_len = block_file_size - (end + 1 - state->block_file_data);
        SAMPLE_ASSERT(strnlen(end + 1, str_len) <
                          USER_MAX_PATH_NAME - sizeof(RECOVERED_FILE_NAME),
                      DOCA_ERROR_INVALID_VALUE, state,
                      "File data info contain file path bigger then max size");
        ret = snprintf(output_file_path, sizeof(output_file_path), "%.*s%s",
                       str_len, end + 1, RECOVERED_FILE_NAME);
        SAMPLE_ASSERT(ret >= 0 && ret < (int)sizeof(output_file_path),
                      DOCA_ERROR_IO_FAILED, state,
                      "Path exceeded max path len");
    }

    free(state->block_file_data);
    state->block_file_data = NULL;

    state->out_file = fopen(output_file_path, "wr");
    SAMPLE_ASSERT(state->out_file != NULL, DOCA_ERROR_IO_FAILED, state,
                  "Unable to open output file: %s", output_file_path);

    state->missing_indices =
        calloc(data_block_count + rdnc_block_count, sizeof(uint32_t));
    SAMPLE_ASSERT(state->missing_indices != NULL, DOCA_ERROR_NO_MEMORY, state,
                  "Unable to allocate missing_indices");

    for (i = 0; i < data_block_count + rdnc_block_count; i++) {
        char *file_name =
            i < data_block_count ? DATA_BLOCK_FILE_NAME : RDNC_BLOCK_FILE_NAME;
        size_t index = i < data_block_count ? i : i - data_block_count;

        ret = snprintf(full_path, sizeof(full_path), "%s/%s%ld", dir_path,
                       file_name, index);
        SAMPLE_ASSERT(ret >= 0 && ret < (int)sizeof(full_path),
                      DOCA_ERROR_IO_FAILED, state,
                      "Path exceeded max path len");
        result =
            read_file(full_path, &state->block_file_data, &block_file_size);
        if (result == DOCA_SUCCESS && block_file_size > 0 && block_size == 0) {
            block_size = block_file_size;
            SAMPLE_ASSERT(block_size % 64 == 0, DOCA_ERROR_INVALID_VALUE, state,
                          "Block size is not 64 byte aligned");
            src_size = (uint64_t)block_size * data_block_count;
            ret = posix_memalign((void **)&state->src_buffer, 64, src_size);
            SAMPLE_ASSERT(state->src_buffer != NULL && ret == 0,
                          DOCA_ERROR_NO_MEMORY, state,
                          "Unable to allocate src_buffer string");
        }
        if (result == DOCA_SUCCESS) {
            SAMPLE_ASSERT((uint64_t)block_file_size == block_size,
                          DOCA_ERROR_INVALID_VALUE, state,
                          "Blocks are not same size");
            // DOCA_LOG_INFO("Copy: %s", full_path);
            memcpy(state->src_buffer + src_size_cur, state->block_file_data,
                   block_size);
            src_size_cur += block_size;
            free(state->block_file_data);
            state->block_file_data = NULL;
        } else
            state->missing_indices[n_missing++] = i;
        if (src_size_cur == src_size)
            break;
    }

    SAMPLE_ASSERT(src_size_cur == src_size, DOCA_ERROR_INVALID_VALUE, state,
                  "Not enough data for recover");
    SAMPLE_ASSERT(n_missing > 0, DOCA_ERROR_INVALID_VALUE, state,
                  "Nothing to decode, all original data block are in place");
    dst_size = block_size * n_missing;

    ret = posix_memalign((void **)&state->dst_buffer, 64, dst_size);
    SAMPLE_ASSERT(state->dst_buffer != NULL, DOCA_ERROR_NO_MEMORY, state,
                  "Unable to allocate dst_buffer string");

    result = ec_core_init(state, pci_addr,
                          (tasks_check)&doca_ec_cap_task_recover_is_supported,
                          max_bufs, src_size, dst_size, &max_block_size);
    if (result != DOCA_SUCCESS)
        return result;

    SAMPLE_ASSERT(
        block_size <= max_block_size, DOCA_ERROR_INVALID_VALUE, state,
        "Block size (%lu) exceeds the maximum size supported (%lu). Try to "
        "increase the number of blocks or use a smaller file as input",
        block_size, max_block_size);

    /* Set task configuration */
    result =
        doca_ec_task_recover_set_conf(state->ec, ec_recover_completed_callback,
                                      ec_recover_error_callback, NUM_EC_TASKS);
    ASSERT_DOCA_ERR(result, state,
                    "Unable to set configuration for recover tasks");

    /* Start the task */
    result = doca_ctx_start(state->core_state.ctx);
    ASSERT_DOCA_ERR(result, state, "Unable to start context");

    /* Create a matrix for the task */
    result = doca_ec_matrix_create(state->ec, DOCA_EC_MATRIX_TYPE_CAUCHY,
                                   data_block_count, rdnc_block_count,
                                   &state->encoding_matrix);
    ASSERT_DOCA_ERR(result, state, "Unable to create ec matrix");

    result = doca_ec_matrix_create_recover(state->ec, state->encoding_matrix,
                                           state->missing_indices, n_missing,
                                           &state->decoding_matrix);
    ASSERT_DOCA_ERR(result, state, "Unable to create recovery matrix");

    /* Include all necessary parameters for completion callback in user data of
     * task */
    doca_error_t task_status = DOCA_SUCCESS;
    doca_error_t callback_result = DOCA_SUCCESS;
    struct doca_task *doca_tasks[NUM_EC_TASKS];

    /* Construct EC recover tasks */
    for (int k = 0; k < total_nb_tasks; k++) {
        struct doca_ec_task_recover *task;
        struct recover_task_data task_data;
        union doca_data user_data;

        task_data = (struct recover_task_data){
            .dir_path = dir_path,
            .output_file_path = output_file_path,
            .file_size = file_size,
            .block_size = block_size,
            .data_block_count = data_block_count,
            .n_missing = n_missing,
            .recovered_data_blocks = state->dst_doca_buf,
            .task_status = &task_status,
            .cb_result = &callback_result};
        clock_gettime(CLOCK_REALTIME, &task_data.bt);

        user_data.ptr = &task_data;

        result = doca_ec_task_recover_allocate_init(
            state->ec, state->decoding_matrix, state->src_doca_buf,
            state->dst_doca_buf, user_data, &task);
        ASSERT_DOCA_ERR(result, state, "Unable to allocate and initiate task");

        doca_tasks[k] = doca_ec_task_recover_as_task(task);
        SAMPLE_ASSERT(doca_tasks[k] != NULL, DOCA_ERROR_UNEXPECTED, state,
                      "Unable to retrieve task as doca_task");
    }

    clock_gettime(CLOCK_REALTIME, &begin_time);
    /* Enqueue ec recover tasks */
    for (int k = 0; k < total_nb_tasks; k++) {
        result = doca_task_submit(doca_tasks[k]);
        ASSERT_DOCA_ERR(result, state, "Unable to submit task");
    }

    state->run_pe_progress = true;

    /* Wait for recover task completion and for context to return to idle */
    while (state->run_pe_progress) {
        if (doca_pe_progress(state->core_state.pe) == 0)
            nanosleep(&ts, &ts);
    }

    /* Check result of task and the callback */
    ASSERT_DOCA_ERR(task_status, state, "EC recover task failed");

    if (callback_result == DOCA_SUCCESS)
        // DOCA_LOG_INFO("Success, data was recovered");
        ;
    else
        DOCA_LOG_ERR("Sample failed: %s",
                     doca_error_get_descr(callback_result));

    /* The task was already freed and the context was stopped in the callbacks
     */

    /* Clean and destroy all relevant objects */
    ec_cleanup(state);

    return callback_result;
}

/*
 * Run ec_recover sample
 *
 * @pci_addr [in]: PCI address of a doca device
 * @output_path [in]: output might be a file or a folder - depends on the input
 * and do_both
 * @data_block_count [in]: data block count
 * @rdnc_block_count [in]: redundancy block count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t ec_recover(const char *pci_addr, const char *output_path,
                        uint32_t data_block_count, uint32_t rdnc_block_count,
                        int total_nb_tasks_param) {
    doca_error_t result = DOCA_SUCCESS;
    const char *dir_path = output_path;
    const char *output_file_path = NULL;
    total_nb_tasks = total_nb_tasks_param;

    for (int i = 0; i < 10; i++) {
        nb_finished_tasks = 0;
        result = ec_decode(pci_addr, output_file_path, dir_path,
                           data_block_count, rdnc_block_count);
    }

    for (int i = 0; i < 10; i++) {
        printf("%d%c", total_time[i], i == 9 ? '\n' : ',');
    }
    return result;
}
