#ifndef __CQ_CONTEXT_H__
#define __CQ_CONTEXT_H__

/* CQE size is 64B */
#define CQE_BSIZE 64
#define CQ_BSIZE (Q_DEPTH * CQE_BSIZE)

#include "../transfer.h"
#include "libflexio/flexio.h"

class cq_context {
  public:
    /* Flex IO process is used to load a program to the DPA. */
    flexio_process *process;
    /* Flex IO CQ. */
    flexio_cq *flexio_cq_ptr;
    /* CQ transfer information. */
    app_transfer_cq cq_transf;

    /* Class functions */
    cq_context(flexio_process *process);
    ~cq_context();

    flexio_status create_cq(uint8_t cq_element_type, flexio_event_handler *eh);

  private:
    flexio_status alloc_cq_mem();
};

#endif