#include "fp_context.hpp"
#include "com_host.hpp"
#include "eh_context.hpp"
#include "flow_steerer.hpp"
#include "libflexio/flexio.h"
#include <cstddef>
#include <cstdio>
#include <infiniband/verbs.h>
#include <vector>

fp_context::fp_context()
    : process(nullptr), stream(nullptr), ibv_ctx(nullptr), eh_ctxs(nullptr),
      steerer(nullptr) {
    eh_ctxs = new std::vector<eh_context *>();
}

fp_context::~fp_context() {
    delete steerer;

    if (eh_ctxs != nullptr) {
        for (eh_context *eh_ctx : *eh_ctxs) {
            delete eh_ctx;
        }
        delete eh_ctxs;
    }

    if (stream != nullptr &&
        flexio_msg_stream_destroy(stream) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to destroy Flex IO msg stream\n");
    }

    if (process != nullptr &&
        flexio_process_destroy(process) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to destroy Flex IO process\n");
    }

    if (ibv_ctx != nullptr && ibv_close_device(ibv_ctx)) {
        printf("Failed to close ibv device\n");
    }
}

flexio_status fp_context::open_ibv_ctx(char *device) {
    /* Queried IBV device list. */
    ibv_device **dev_list;
    /* IBV device iterator. */
    int dev_i;

    /* Query IBV devices list. */
    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        printf("Failed to get IB devices list\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Loop over found IBV devices. */
    for (dev_i = 0; dev_list[dev_i]; dev_i++) {
        /* Look for a device with the user provided name. */
        if (!strcmp(ibv_get_device_name(dev_list[dev_i]), device))
            break;
    }

    /* Check a device was found. */
    if (!dev_list[dev_i]) {
        printf("No IBV device found for device name '%s'\n", device);
        ibv_free_device_list(dev_list);
        return FLEXIO_STATUS_FAILED;
    }

    /* Open IBV device context for the requested device. */
    ibv_ctx = ibv_open_device(dev_list[dev_i]);
    if (ibv_ctx == nullptr) {
        printf("Couldn't open an IBV context for device '%s'\n", device);
        ibv_free_device_list(dev_list);
        return FLEXIO_STATUS_FAILED;
    }
    return FLEXIO_STATUS_SUCCESS;
}

flexio_status fp_context::init_flow_steer() {
    steerer = new flow_steerer(ibv_ctx);
    return steerer->create_shared_objs();
}

flexio_status fp_context::create_process(flexio_app *app) {
    return flexio_process_create(ibv_ctx, app, NULL, &process);
}

flexio_status fp_context::create_msg_stream() {
    /* Message stream attributes. */
    flexio_msg_stream_attr_t stream_fattr = {0};
    stream_fattr.data_bsize = MSG_HOST_BUFF_BSIZE;
    stream_fattr.sync_mode = FLEXIO_LOG_DEV_SYNC_MODE_SYNC;
    stream_fattr.level = FLEXIO_MSG_DEV_INFO;
    return flexio_msg_stream_create(process, &stream_fattr, stdout, NULL,
                                    &stream);
}