#include "queue/sq_context.hpp"
#include "../com_host.hpp"
#include "libflexio/flexio.h"
#include <cstdint>

sq_context::sq_context(flexio_process *process) : process(process) {}

sq_context::~sq_context() {
    if (flexio_sq_ptr && flexio_sq_destroy(flexio_sq_ptr)) {
        printf("Failed to destroy SQ\n");
    }

    if (sqd_mkey && flexio_device_mkey_destroy(sqd_mkey)) {
        printf("Failed to destroy mkey SQD\n");
    }

    if (sq_transf.wq_ring_daddr &&
        flexio_buf_dev_free(process, sq_transf.wq_ring_daddr)) {
        printf("Failed to free sq_transf.wq_ring_daddr\n");
    }

    if (sq_transf.wqd_daddr &&
        flexio_buf_dev_free(process, sq_transf.wqd_daddr)) {
        printf("Failed to free sq_transf.wqd_daddr\n");
    }
}

flexio_status sq_context::create_sq(uint32_t cq_num) {
    /* Attributes for the SQ. */
    struct flexio_wq_attr sq_attr = {0};

    /* UAR ID for CQ/SQ from Flex IO process UAR. */
    uint32_t uar_id = flexio_uar_get_id(flexio_process_get_uar(process));

    /* Allocate SQ memory (ring and data) on DPA heap memory. */
    if (alloc_sq_mem() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to allocate memory for SQ\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Set SQ depth (log) attribute. */
    sq_attr.log_wq_depth = LOG_Q_DEPTH;
    /* Set SQ UAR ID attribute to the Flex IO process UAR ID.
     * This will allow writing doorbells to the SQ from the DPA side.
     */
    sq_attr.uar_id = uar_id;
    /* Set SQ ring memory. Ring memory is on the DPA side in order to allow
     * writing WQEs from DPA during packet forwarding.
     */
    sq_attr.wq_ring_qmem.daddr = sq_transf.wq_ring_daddr;

    /* Set SQ protection domain */
    sq_attr.pd = flexio_process_get_pd(process);

    /* Create SQ.
     * Second argument is NULL as SQ is created on the same GVMI as the process.
     */
    if (flexio_sq_create(process, NULL, cq_num, &sq_attr, &flexio_sq_ptr) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create Flex IO SQ\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Fetch SQ's number to communicate to DPA side. */
    sq_transf.wq_num = flexio_sq_get_wq_num(flexio_sq_ptr);

    /* Create an MKey for SQ data buffer to send. */
    sqd_mkey = create_dpa_mkey();
    if (sqd_mkey == nullptr) {
        printf("Failed to create an MKey for SQ data buffer\n");
        return FLEXIO_STATUS_FAILED;
    }
    /* Set SQ's data buffer MKey ID in communication struct. */
    sq_transf.wqd_mkey_id = flexio_mkey_get_id(sqd_mkey);

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status sq_context::alloc_sq_mem() {
    /* Allocate DPA heap memory for SQ data. */
    flexio_buf_dev_alloc(process, Q_DATA_BSIZE, &sq_transf.wqd_daddr);
    if (!sq_transf.wqd_daddr)
        return FLEXIO_STATUS_FAILED;

    /* Allocate DPA heap memory for SQ ring. */
    flexio_buf_dev_alloc(process, SQ_RING_BSIZE, &sq_transf.wq_ring_daddr);
    if (!sq_transf.wq_ring_daddr)
        return FLEXIO_STATUS_FAILED;

    return FLEXIO_STATUS_SUCCESS;
}

flexio_mkey *sq_context::create_dpa_mkey() {
    /* Flex IO MKey attributes. */
    struct flexio_mkey_attr mkey_attr = {0};
    /* Flex IO MKey. */
    struct flexio_mkey *mkey;

    /* Set MKey protection domain (PD) to the Flex IO process PD. */
    mkey_attr.pd = flexio_process_get_pd(process);
    /* Set MKey address. */
    mkey_attr.daddr = sq_transf.wqd_daddr;
    /* Set MKey length. */
    mkey_attr.len = Q_DATA_BSIZE;
    /* Set MKey access to memory write (from DPA). */
    mkey_attr.access = IBV_ACCESS_LOCAL_WRITE;
    /* Create Flex IO MKey. */
    if (flexio_device_mkey_create(process, &mkey_attr, &mkey) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create Flex IO Mkey\n");
        return nullptr;
    }

    return mkey;
}