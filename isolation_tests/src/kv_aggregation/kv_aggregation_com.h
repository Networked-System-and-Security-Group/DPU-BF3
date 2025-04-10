/* Shared header file with defines and structures that used in
 * both host and device sides app.
 */

#ifndef __KV_AGGREGATION_COM_H__
#define __KV_AGGREGATION_COM_H__

#define NB_ENTRIES 65536

#ifdef ON_DEVICE
#include <libflexio-dev/flexio_dev.h>
#else
#include "libflexio/flexio.h"
#include <cstdint>
#endif

#include "../common/flexio/transfer.h"

/* Collateral structure for transfer host data to device */
struct host2dev_kv_aggregation_data {
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
    /* KV buffer ptr, only used by thread 0 */
    flexio_uintptr_t kv_buf_ptr;
} __attribute__((__packed__, aligned(8)));

/* KV pair struct */
struct kv_pair {
    uint64_t key;
    uint64_t value;
};

#endif
