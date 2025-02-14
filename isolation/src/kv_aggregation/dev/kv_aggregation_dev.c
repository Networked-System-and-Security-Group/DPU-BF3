#include "com_dev.h"
#include <dpaintrin.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <libflexio-libc/string.h>
#include <stddef.h>
/* Shared header file for packet processor sample */
#include "../kv_aggregation_com.h"
#include "libflexio-dev/flexio_dev.h"

/* Mask for CQ index */
#define CQ_IDX_MASK ((1 << LOG_CQ_DEPTH) - 1)
/* Mask for RQ index */
#define RQ_IDX_MASK ((1 << LOG_RQ_DEPTH) - 1)
/* Mask for SQ index */
#define SQ_IDX_MASK ((1 << (LOG_SQ_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
/* Mask for data index */
#define DATA_IDX_MASK ((1 << (LOG_SQ_DEPTH)) - 1)

/* RPC call handlers */
flexio_dev_rpc_handler_t kv_aggregation_init;
flexio_dev_rpc_handler_t kv_aggregation_stop;
flexio_dev_rpc_handler_t kv_aggregation_query;

/* Event handler */
flexio_dev_event_handler_t kv_aggregation_event_handler;

/* The structure of the sample DPA application contains global data that the
 * application uses */
static struct app_context {
    /* Packet count - processed packets */
    uint64_t packets_count;
    /* lkey - local memory key */
    uint32_t lkey;

    cq_ctx_t rq_cq_ctx; /* RQ CQ */
    rq_ctx_t rq_ctx;    /* RQ */
    sq_ctx_t sq_ctx;    /* SQ */
    cq_ctx_t sq_cq_ctx; /* SQ CQ */
    dt_ctx_t dt_ctx;    /* SQ Data ring */

    uint8_t thread_id;         /* Thread id */
    uint8_t is_initialized;    /* Wether thread is initialized */
    uint8_t should_stop;       /* Wether thread should stop */
    uint8_t first_handle;      /* Wether is the first time to run the event
                                  handler(per thread)*/
    uint64_t reschedule_times; /* Times of reschedule */
} __attribute__((__aligned__(64))) app_ctxs[MAX_NB_THREADS];

uint64_t *kv_elements; /* KV buffer ptr */

/* Initialize the app_ctx structure from the host data.
 *  data_from_host - pointer host2dev_kv_aggregation_data from host.
 */
__dpa_rpc__ uint64_t kv_aggregation_init(uint64_t data_ptr) {
    struct host2dev_kv_aggregation_data *data_from_host =
        (struct host2dev_kv_aggregation_data *)data_ptr;
    uint8_t thread_id = data_from_host->thread_id;
    struct app_context *app_ctx = &app_ctxs[thread_id];
    app_ctx->thread_id = thread_id;
    app_ctx->should_stop = 0;
    app_ctx->first_handle = 1;
    if (thread_id == 0) {
        /* Every thread shares the same KV buffer */
        kv_elements = (uint64_t *)data_from_host->kv_buf_ptr;
    }

    app_ctx->packets_count = 0;
    app_ctx->reschedule_times = 0;
    app_ctx->lkey = data_from_host->sq_transf.wqd_mkey_id;

    /* Set context for RQ's CQ */
    com_cq_ctx_init(&app_ctx->rq_cq_ctx, data_from_host->rq_cq_transf.cq_num,
                    data_from_host->rq_cq_transf.log_cq_depth,
                    data_from_host->rq_cq_transf.cq_ring_daddr,
                    data_from_host->rq_cq_transf.cq_dbr_daddr);

    /* Set context for RQ */
    com_rq_ctx_init(&app_ctx->rq_ctx, data_from_host->rq_transf.wq_num,
                    data_from_host->rq_transf.wq_ring_daddr,
                    data_from_host->rq_transf.wq_dbr_daddr);

    /* Set context for SQ */
    com_sq_ctx_init(&app_ctx->sq_ctx, data_from_host->sq_transf.wq_num,
                    data_from_host->sq_transf.wq_ring_daddr);

    /* Set context for SQ's CQ */
    com_cq_ctx_init(&app_ctx->sq_cq_ctx, data_from_host->sq_cq_transf.cq_num,
                    data_from_host->sq_cq_transf.log_cq_depth,
                    data_from_host->sq_cq_transf.cq_ring_daddr,
                    data_from_host->sq_cq_transf.cq_dbr_daddr);

    /* Set context for data */
    com_dt_ctx_init(&app_ctx->dt_ctx, data_from_host->sq_transf.wqd_daddr);
    app_ctx->is_initialized = 1;
    flexio_dev_print("Thread %d initialized\n", thread_id);
    return 0;
}

__dpa_rpc__ uint64_t kv_aggregation_stop(__attribute__((unused)) uint64_t arg) {
    for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
        app_ctxs[thread_id].should_stop = 1;
    }
    return 0;
}

__dpa_rpc__ uint64_t kv_aggregation_query(uint64_t query_type) {
    if (query_type == PACKETS_COUNT) {
        uint64_t total_packets_count = 0;
        for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
            total_packets_count += app_ctxs[thread_id].packets_count;
        }
        return total_packets_count;
    } else {
        uint64_t total_reschedule_times = 0;
        for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
            total_reschedule_times += app_ctxs[thread_id].reschedule_times;
        }
        return total_reschedule_times;
    }
}

/* process packet - read it, swap MAC addresses, modify it, create a send WQE
 * and send it back. */
static void process_packet(struct app_context *app_ctx) {
    /* RX packet handling variables */
    struct flexio_dev_wqe_rcv_data_seg *rwqe;
    /* RQ WQE index */
    uint32_t rq_wqe_idx;
    /* Pointer to RQ data */
    char *rq_data;

    /* Extract relevant data from the CQE */
    rq_wqe_idx = flexio_dev_cqe_get_wqe_counter(app_ctx->rq_cq_ctx.cqe);

    /* Get the RQ WQE pointed to by the CQE */
    rwqe = &app_ctx->rq_ctx.rq_ring[rq_wqe_idx & RQ_IDX_MASK];

    /* Extract data (whole packet) pointed to by the RQ WQE */
    rq_data = flexio_dev_rwqe_get_addr(rwqe);

    uint64_t nb_kv_pairs = *(uint64_t *)(rq_data + 56);
    struct kv_pair *kv_pairs = (struct kv_pair *)(rq_data + 64);

    for (uint64_t i = 0; i < nb_kv_pairs; i++) {
        kv_elements[kv_pairs[i].key % NB_ENTRIES] +=
            (uint64_t)kv_pairs[i].value;
    }

    /* Ring DB */
    __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
    flexio_dev_dbr_rq_inc_pi(app_ctx->rq_ctx.rq_dbr);
}

/* Entry point function that host side call for the execute.
 *  thread_arg - pointer to the host2dev_kv_aggregation_data structure
 *     to transfer data from the host side.
 */
__dpa_global__ void kv_aggregation_event_handler(uint64_t thread_id) {
    struct app_context *app_ctx = &app_ctxs[thread_id];
    if (app_ctx->is_initialized == 0) {
        flexio_dev_thread_reschedule();
    }

    if (app_ctx->first_handle) {
        flexio_dev_print("Thread %d event handler start\n", app_ctx->thread_id);
        app_ctx->first_handle = 0;
    }

    /* Poll CQ until the package is received.
     */
    while (flexio_dev_cqe_get_owner(app_ctx->rq_cq_ctx.cqe) !=
               app_ctx->rq_cq_ctx.cq_hw_owner_bit &&
           app_ctx->should_stop == 0) {
        /* Process the packet */
        process_packet(app_ctx);
        /* Print the message */
        app_ctx->packets_count++;
        if (app_ctx->packets_count % 100000 == 0) {
            flexio_dev_print("Thread %lu processed packet %ld\n", thread_id,
                             app_ctx->packets_count);
        }
        /* Update memory to DPA */
        __dpa_thread_fence(__DPA_MEMORY, __DPA_R, __DPA_R);
        /* Update RQ CQ */
        com_step_cq(&app_ctx->rq_cq_ctx);
    }
    /* Update the memory to the chip */
    __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
    /* Arming cq for next packet */
    flexio_dev_cq_arm(app_ctx->rq_cq_ctx.cq_idx, app_ctx->rq_cq_ctx.cq_number);

    /* Reschedule the thread */
    if (app_ctx->should_stop) {
        flexio_dev_thread_finish();
    } else {
        app_ctx->reschedule_times++;
        flexio_dev_thread_reschedule();
    }
}
