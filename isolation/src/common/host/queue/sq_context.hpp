#ifndef __SQ_CONTEXT_H__
#define __SQ_CONTEXT_H__

#include "../transfer.h"
#include "libflexio/flexio.h"
#include <cstdint>

/* SQ WQE byte size is 64B. */
#define LOG_SQ_WQE_BSIZE 6
/* SQ WQE byte size log to value. */
#define SQ_WQE_BSIZE L2V(LOG_SQ_WQE_BSIZE)
/* SQ ring byte size is queue depth times WQE byte size. */
#define SQ_RING_BSIZE (Q_DEPTH * SQ_WQE_BSIZE)

class sq_context {
  public:
    /* Flex IO process is used to load a program to the DPA. */
    flexio_process *process;
    /* Flex IO SQ. */
    flexio_sq *flexio_sq_ptr;
    /* SQ transfer information. */
    app_transfer_wq sq_transf;
    /* Memory key (MKey) for SQ data. */
    flexio_mkey *sqd_mkey;

    /* Class functions */
    sq_context(flexio_process *process);
    ~sq_context();

    flexio_status create_sq(uint32_t cq_num);

  private:
    flexio_status alloc_sq_mem();
    flexio_mkey *create_dpa_mkey();
};

#endif