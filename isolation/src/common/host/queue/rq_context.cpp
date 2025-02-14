#include "queue/rq_context.hpp"
#include "../com_host.hpp"
#include "libflexio/flexio.h"

rq_context::rq_context(flexio_process *process) : process(process) {}

rq_context::~rq_context() {
    /* Clean up rq pointer if created */
    if (flexio_rq_ptr && flexio_rq_destroy(flexio_rq_ptr)) {
        printf("Failed to destroy RQ\n");
    }

    /* Clean up memory key for rqd if created */
    if (rqd_mkey && flexio_device_mkey_destroy(rqd_mkey)) {
        printf("Failed to destroy mkey RQD\n");
    }

    /* Clean up app data daddr if created */
    if (rq_transf.wq_dbr_daddr &&
        flexio_buf_dev_free(process, rq_transf.wq_dbr_daddr)) {
        printf("Failed to free rq_transf.wq_dbr_daddr\n");
    }

    /* Clean up wq_ring_daddr for rq_transf if created */
    if (rq_transf.wq_ring_daddr &&
        flexio_buf_dev_free(process, rq_transf.wq_ring_daddr)) {
        printf("Failed to free rq_transf.wq_ring_daddr\n");
    }

    if (rq_transf.wqd_daddr &&
        flexio_buf_dev_free(process, rq_transf.wqd_daddr)) {
        printf("Failed to free rq_transf.wqd_daddr\n");
    }
}

flexio_status rq_context::create_rq(uint32_t cq_num) {
    /* Attributes for the RQ. */
    struct flexio_wq_attr rq_attr = {0};

    /* Allocate RQ memory (ring and data) on DPA heap memory. */
    if (alloc_rq_mem() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to allocate memory for RQ.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Create an MKey for RX buffer */
    rqd_mkey = create_dpa_mkey();
    if (rqd_mkey == nullptr) {
        printf("Failed to create an MKey for RQ data buffer.\n");
        return FLEXIO_STATUS_FAILED;
    }
    /* Set SQ's data buffer MKey ID in communication struct. */
    rq_transf.wqd_mkey_id = flexio_mkey_get_id(rqd_mkey);
    /* Initialize RQ ring. */
    if (init_dpa_rq_ring() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to init RQ ring.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Set RQ depth (log) attribute. */
    rq_attr.log_wq_depth = LOG_Q_DEPTH;
    /* Set RQ protection domain attribute to be the same as the Flex IO process.
     */
    rq_attr.pd = flexio_process_get_pd(process);
    /* Set RQ DBR memory type to DPA heap memory. */
    rq_attr.wq_dbr_qmem.memtype = FLEXIO_MEMTYPE_DPA;
    /* Set RQ DBR memory address. */
    rq_attr.wq_dbr_qmem.daddr = rq_transf.wq_dbr_daddr;
    /* Set RQ ring memory address. */
    rq_attr.wq_ring_qmem.daddr = rq_transf.wq_ring_daddr;
    /* Create the Flex IO RQ.
     * Second argument is NULL as RQ is created on the same GVMI as the process.
     */
    if (flexio_rq_create(process, NULL, cq_num, &rq_attr, &flexio_rq_ptr) !=
        FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create Flex IO RQ.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Fetch RQ's number to communicate to DPA side. */
    rq_transf.wq_num = flexio_rq_get_wq_num(flexio_rq_ptr);
    if (init_rq_dbr() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to init RQ DBR.\n");
        return FLEXIO_STATUS_FAILED;
    }

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status rq_context::alloc_rq_mem() {
    /* DBR source memory on the host (to copy). */
    __be32 dbr[2] = {0, 0};

    /* Allocate DPA heap memory for RQ data. */
    flexio_buf_dev_alloc(process, Q_DATA_BSIZE, &rq_transf.wqd_daddr);
    if (!rq_transf.wqd_daddr)
        return FLEXIO_STATUS_FAILED;

    /* Allocate DPA heap memory for RQ ring. */
    flexio_buf_dev_alloc(process, RQ_RING_BSIZE, &rq_transf.wq_ring_daddr);
    if (!rq_transf.wq_ring_daddr)
        return FLEXIO_STATUS_FAILED;

    /* Allocate and initialize RQ DBR memory on the DPA heap memory. */
    flexio_copy_from_host(process, dbr, sizeof(dbr), &rq_transf.wq_dbr_daddr);
    if (!rq_transf.wq_dbr_daddr)
        return FLEXIO_STATUS_FAILED;

    return FLEXIO_STATUS_SUCCESS;
}

flexio_mkey *rq_context::create_dpa_mkey() {
    /* Flex IO MKey attributes. */
    struct flexio_mkey_attr mkey_attr = {0};
    /* Flex IO MKey. */
    struct flexio_mkey *mkey;

    /* Set MKey protection domain (PD) to the Flex IO process PD. */
    mkey_attr.pd = flexio_process_get_pd(process);
    /* Set MKey address. */
    mkey_attr.daddr = rq_transf.wqd_daddr;
    /* Set MKey length. */
    mkey_attr.len = Q_DATA_BSIZE;
    /* Set MKey access to memory write (from DPA). */
    mkey_attr.access = IBV_ACCESS_LOCAL_WRITE;
    /* Create Flex IO MKey. */
    if (flexio_device_mkey_create(process, &mkey_attr, &mkey)) {
        printf("Failed to create Flex IO Mkey\n");
        return NULL;
    }

    return mkey;
}

flexio_status rq_context::init_dpa_rq_ring() {
    /* RQ WQE data iterator. */
    flexio_uintptr_t wqe_data_daddr = rq_transf.wqd_daddr;
    /* RQ ring MKey. */
    uint32_t mkey_id = rq_transf.wqd_mkey_id;
    /* Temporary host memory for RQ ring. */
    struct mlx5_wqe_data_seg *rx_wqes;
    /* RQ WQE iterator. */
    struct mlx5_wqe_data_seg *dseg;
    /* Function return value. */
    flexio_status status = FLEXIO_STATUS_SUCCESS;
    /* RQ WQE index iterator. */
    uint32_t i;

    /* Allocate temporary host memory for RQ ring.*/
    rx_wqes = static_cast<mlx5_wqe_data_seg *>(calloc(1, RQ_RING_BSIZE));
    if (!rx_wqes) {
        printf("Failed to allocate memory for rx_wqes\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Initialize RQ WQEs'. */
    for (i = 0, dseg = rx_wqes; i < Q_DEPTH; i++, dseg++) {
        /* Set WQE's data segment to point to the relevant RQ data segment. */
        mlx5dv_set_data_seg(dseg, Q_DATA_ENTRY_BSIZE, mkey_id, wqe_data_daddr);
        /* Advance data pointer to next segment. */
        wqe_data_daddr += Q_DATA_ENTRY_BSIZE;
    }

    /* Copy RX WQEs from host to RQ ring DPA heap memory. */
    if (flexio_host2dev_memcpy(process, rx_wqes, RQ_RING_BSIZE,
                               rq_transf.wq_ring_daddr)) {
        status = FLEXIO_STATUS_FAILED;
    }

    /* Free temporary host memory. */
    free(rx_wqes);
    return status;
}

flexio_status rq_context::init_rq_dbr() {
    /* Temporary host memory for DBR value. */
    __be32 dbr[2];

    /* Set receiver counter to number of WQEs. */
    dbr[0] = htobe32(Q_DEPTH & 0xffff);
    /* Send counter is not used for RQ so it is nullified. */
    dbr[1] = htobe32(0);
    /* Copy DBR value to DPA heap memory.*/
    if (flexio_host2dev_memcpy(process, dbr, sizeof(dbr),
                               rq_transf.wq_dbr_daddr)) {
        return FLEXIO_STATUS_FAILED;
    }

    return FLEXIO_STATUS_SUCCESS;
}