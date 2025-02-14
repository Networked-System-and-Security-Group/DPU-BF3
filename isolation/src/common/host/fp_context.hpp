#ifndef __FP_CONTEXT_H__
#define __FP_CONTEXT_H__

#include "eh_context.hpp"
#include "flow_steerer.hpp"
#include <libflexio/flexio.h>
#include <vector>

class fp_context {
  public:
    /* Flex IO process is used to load a program to the DPA. */
    flexio_process *process;
    /* Flex IO message stream is used to get messages from the DPA. */
    flexio_msg_stream *stream;
    /* IBV context opened for the device name provided by the user. */
    ibv_context *ibv_ctx;
    /* Flex IO thread context array */
    std::vector<eh_context *> *eh_ctxs;
    /* Flow steering context */
    flow_steerer *steerer;

    /* Class functions */
    fp_context();
    ~fp_context();

    flexio_status open_ibv_ctx(char *device);
    flexio_status init_flow_steer();
    flexio_status create_process(flexio_app *app);
    flexio_status create_msg_stream();
};

#endif