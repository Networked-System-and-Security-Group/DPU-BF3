#include "eh_context.hpp"
#include "libflexio/flexio.h"
#include "queue/cq_context.hpp"
#include "queue/rq_context.hpp"
#include "queue/sq_context.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>

eh_context::eh_context(flexio_process *process, uint64_t thread_id)
    : process(process), eh(nullptr), thread_id(thread_id),

      app_data_daddr(0) {
    sqcq_ctx = new cq_context(process);
    sq_ctx = new sq_context(process);
    rqcq_ctx = new cq_context(process);
    rq_ctx = new rq_context(process);
}

eh_context::~eh_context() {
    if (thread_id == 0 && app_data_daddr &&
        flexio_buf_dev_free(process, app_data_daddr) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to dealloc app data memory on Flex IO heap\n");
    }

    delete sq_ctx;
    delete sqcq_ctx;
    delete rq_ctx;
    delete rqcq_ctx;

    /* Destroy event handler if created */
    if (eh && flexio_event_handler_destroy(eh)) {
        printf("Failed to destroy event handler\n");
    }
}

flexio_status eh_context::create_event_handler(flexio_func_t *stub_func) {
    flexio_event_handler_attr eh_attr = {0};
    eh_attr.host_stub_func = stub_func;
    eh_attr.affinity.type = FLEXIO_AFFINITY_STRICT;
    eh_attr.affinity.id = thread_id;
    return flexio_event_handler_create(process, &eh_attr, &eh);
}

flexio_status eh_context::create_queues() {
    if (sqcq_ctx->create_cq(FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ, eh) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create SQCQ\n");
        return FLEXIO_STATUS_FAILED;
    }
    if (sq_ctx->create_sq(flexio_cq_get_cq_num(sqcq_ctx->flexio_cq_ptr)) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create SQ\n");
        return FLEXIO_STATUS_FAILED;
    }
    if (rqcq_ctx->create_cq(FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD, eh) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create RQCQ\n");
        return FLEXIO_STATUS_FAILED;
    }

    if (rq_ctx->create_rq(flexio_cq_get_cq_num(rqcq_ctx->flexio_cq_ptr)) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create RQ\n");
        return FLEXIO_STATUS_FAILED;
    }

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status eh_context::copy_app_data_to_dpa() {
    /* Size of application information struct. */
    uint64_t struct_bsize = sizeof(struct host2dev_base_data);
    /* Temporary application information struct to copy. */
    struct host2dev_base_data *h2d_data;
    /* Function return value. */
    flexio_status status = FLEXIO_STATUS_SUCCESS;

    /* Allocate memory for temporary struct to copy. */
    h2d_data = static_cast<host2dev_base_data *>(calloc(1, struct_bsize));
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