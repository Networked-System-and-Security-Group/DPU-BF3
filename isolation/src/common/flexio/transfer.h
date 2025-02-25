#ifndef __TRANSFER_H__
#define __TRANSFER_H__

#ifdef ON_DEVICE
#include <libflexio-dev/flexio_dev.h>
#else
#include <libflexio/flexio.h>
#include <stdint.h>
#endif

/* Depth of CQ is (1 << LOG_CQ_DEPTH) */
#define LOG_CQ_DEPTH 7
/* Depth of RQ is (1 << LOG_RQ_DEPTH) */
#define LOG_RQ_DEPTH 7
/* Depth of SQ is (1 << LOG_SQ_DEPTH) */
#define LOG_SQ_DEPTH 7

/* Size of WQD is (1 << LOG_WQD_CHUNK_BSIZE) */
#define LOG_WQD_CHUNK_BSIZE 11

/* Structure for transfer CQ data */
struct app_transfer_cq {
    /* CQ number */
    uint32_t cq_num;
    /* Depth of CQ in the logarithm */
    uint32_t log_cq_depth;
    /* CQ ring DPA address */
    flexio_uintptr_t cq_ring_daddr;
    /* CQ DBR DPA address */
    flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));

/* Structure for transfer WQ data */
struct app_transfer_wq {
    /* WQ number */
    uint32_t wq_num;
    /* WQ MKEY Id */
    uint32_t wqd_mkey_id;
    /* WQ ring DPA address */
    flexio_uintptr_t wq_ring_daddr;
    /* WQ ring DBR address */
    flexio_uintptr_t wq_dbr_daddr;
    /* WQ data address */
    flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));

enum query_type { PACKETS_COUNT = 0, RESCHEDULE_TIMES = 1 };

struct host2dev_base_data {
    /* RQ's CQ transfer information. */
    struct app_transfer_cq rq_cq_transf;
    /* RQ transfer information. */
    struct app_transfer_wq rq_transf;
    /* SQ's CQ transfer information. */
    struct app_transfer_cq sq_cq_transf;
    /* SQ transfer information. */
    struct app_transfer_wq sq_transf;
    /* event handler's thread id */
    uint8_t thread_id;
} __attribute__((__packed__, aligned(8)));

#endif
