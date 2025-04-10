#ifndef __FT_CONTEXT_H__
#define __FT_CONTEXT_H__

#include "../transfer.h"
#include "queue/cq_context.hpp"
#include "queue/rq_context.hpp"
#include "queue/sq_context.hpp"
#include <cstdint>
#include <infiniband/verbs.h>
#include <libflexio/flexio.h>

class eh_context {
  public:
    /* Flex IO process is used to load a program to the DPA. */
    flexio_process *process;
    /* Flex IO event handler is used to execute code over the DPA. */
    flexio_event_handler *eh;

    /* DPA thread id */
    uint8_t thread_id;

    /* SQCQ context */
    cq_context *sqcq_ctx;
    /* SQ context */
    sq_context *sq_ctx;
    /* RQCQ context */
    cq_context *rqcq_ctx;
    /* RQ context */
    rq_context *rq_ctx;

    /* DPA heap memory address of application information struct.
     * Invoked event handler will get this as argument and parse it to the
     * application information struct.
     */
    flexio_uintptr_t app_data_daddr;

    /* Class functions */
    eh_context(flexio_process *process, uint64_t thread_id);
    virtual ~eh_context();

    flexio_status create_event_handler(flexio_func_t *stub_func);
    /* Create SQ, RQ, CQs */
    flexio_status create_queues();
    virtual flexio_status copy_app_data_to_dpa();
};

#endif