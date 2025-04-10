#include "kv_agg_eh_context.hpp"
#include "eh_context.hpp"
#include "libflexio/flexio.h"

kv_agg_eh_context::kv_agg_eh_context(flexio_process *process,
                                     uint64_t thread_id,
                                     flexio_uintptr_t kv_buf_ptr)
    : eh_context(process, thread_id), kv_buf_ptr(kv_buf_ptr) {}

kv_agg_eh_context::~kv_agg_eh_context() {
    /* Clean up app data daddr if created(only run on thread 0) */
    if (thread_id == 0 && kv_buf_ptr &&
        flexio_buf_dev_free(process, kv_buf_ptr)) {
        printf("Failed to dealloc KV buffer data memory on Flex IO heap\n");
    }
}

flexio_status kv_agg_eh_context::copy_app_data_to_dpa() {
    /* Size of application information struct. */
    uint64_t struct_bsize = sizeof(struct host2dev_kv_aggregation_data);
    /* Temporary application information struct to copy. */
    struct host2dev_kv_aggregation_data *h2d_data;
    /* Function return value. */
    flexio_status status = FLEXIO_STATUS_SUCCESS;

    /* Allocate memory for temporary struct to copy. */
    h2d_data =
        static_cast<host2dev_kv_aggregation_data *>(calloc(1, struct_bsize));
    if (!h2d_data) {
        printf("Failed to allocate memory for h2d_data\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Set SQ's CQ information. */
    h2d_data->sq_cq_transf = sqcq_ctx->cq_transf;
    /* Set SQ's information. */
    h2d_data->sq_transf = sq_ctx->sq_transf;
    /* Set RQ's CQ information. */
    h2d_data->rq_cq_transf = rqcq_ctx->cq_transf;
    /* Set RQ's information. */
    h2d_data->rq_transf = rq_ctx->rq_transf;
    /* Set event handler thread id. */
    h2d_data->thread_id = thread_id;
    /* Set KV buf ptr */
    h2d_data->kv_buf_ptr = kv_buf_ptr;

    /* Copy to DPA heap memory.
     * Allocated DPA heap memory address will be kept in app_data_daddr.
     */
    if (flexio_copy_from_host(process, h2d_data, struct_bsize,
                              &app_data_daddr)) {
        printf("Failed to copy application information to DPA.\n");
        status = FLEXIO_STATUS_FAILED;
    }

    /* Free temporary host memory. */
    free(h2d_data);
    return status;
}