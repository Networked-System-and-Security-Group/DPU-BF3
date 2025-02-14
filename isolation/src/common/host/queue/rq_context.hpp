#ifndef __RQ_CONTEXT_H__
#define __RQ_CONTEXT_H__

/* RQ WQE byte size is 64B. */
#define LOG_RQ_WQE_BSIZE 4
/* RQ WQE byte size log to value. */
#define RQ_WQE_BSIZE L2V(LOG_RQ_WQE_BSIZE)
/* RQ ring byte size is queue depth times WQE byte size. */
#define RQ_RING_BSIZE Q_DEPTH *RQ_WQE_BSIZE

#include "../transfer.h"
#include "libflexio/flexio.h"
#include <cstdint>

class rq_context {
  public:
    /* Flex IO process is used to load a program to the DPA. */
    flexio_process *process;
    /* Flex IO RQ. */
    flexio_rq *flexio_rq_ptr;
    /* RQ transfer information. */
    struct app_transfer_wq rq_transf;
    /* MKey for RQ data. */
    struct flexio_mkey *rqd_mkey;

    /* Class functions */
    rq_context(flexio_process *process);
    ~rq_context();

    flexio_status create_rq(uint32_t cq_num);

  private:
    flexio_status alloc_rq_mem();
    flexio_mkey *create_dpa_mkey();
    flexio_status init_dpa_rq_ring();
    flexio_status init_rq_dbr();
};

#endif