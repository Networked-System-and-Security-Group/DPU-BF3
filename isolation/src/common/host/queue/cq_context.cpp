#include "queue/cq_context.hpp"
#include "../com_host.hpp"
#include "libflexio/flexio.h"

cq_context::cq_context(flexio_process *process) : process(process) {}

cq_context::~cq_context() {
    if (flexio_cq_ptr && flexio_cq_destroy(flexio_cq_ptr)) {
        printf("Failed to destroy CQ\n");
    }

    if (cq_transf.cq_ring_daddr &&
        flexio_buf_dev_free(process, cq_transf.cq_ring_daddr)) {
        printf("Failed to free cq_transf.cq_ring_daddr\n");
    }

    if (cq_transf.cq_dbr_daddr &&
        flexio_buf_dev_free(process, cq_transf.cq_dbr_daddr)) {
        printf("Failed to free cq_transf.cq_dbr_daddr\n");
    }
}

flexio_status cq_context::create_cq(uint8_t cq_element_type,
                                    flexio_event_handler *eh) {
    /* Attributes for the CQ. */
    struct flexio_cq_attr cq_attr = {0};

    /* UAR ID for CQ from Flex IO process UAR. */
    uint32_t uar_id = flexio_uar_get_id(flexio_process_get_uar(process));
    /* CQ number. */
    uint32_t cq_num;

    /* Allocate CQ memory (ring and DBR) on DPA heap memory. */
    if (alloc_cq_mem() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to alloc memory for SQ's CQ.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Set CQ depth (log) attribute. */
    cq_attr.log_cq_depth = LOG_Q_DEPTH;
    /* Set CQ element type attribute
     * FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD for RQCQ
     * FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ for SQCQ
     */
    cq_attr.element_type = cq_element_type;
    /* Set CQ thread to the application event handler's thread(only for RQCQ) */
    if (eh != nullptr)
        cq_attr.thread = flexio_event_handler_get_thread(eh);
    /* Set CQ UAR ID attribute to the Flex IO process UAR ID.
     * This will allow updating/arming the CQ from the DPA side.
     */
    cq_attr.uar_id = uar_id;
    /* Set CQ DBR memory. DBR memory is on the DPA side in order to allow direct
     * access from DPA.
     */
    cq_attr.cq_dbr_daddr = cq_transf.cq_dbr_daddr;
    /* Set CQ ring memory. Ring memory is on the DPA side in order to allow
     * reading CQEs from DPA during packet forwarding.
     */
    cq_attr.cq_ring_qmem.daddr = cq_transf.cq_ring_daddr;
    /* Create CQ for SQ. */
    if (flexio_cq_create(process, NULL, &cq_attr, &flexio_cq_ptr)) {
        printf("Failed to create Flex IO CQ\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Fetch CQ number to communicate to DPA side. */
    cq_num = flexio_cq_get_cq_num(flexio_cq_ptr);
    /* Set CQ number in communication struct. */
    cq_transf.cq_num = cq_num;
    /* Set CQ depth in communication struct. */
    cq_transf.log_cq_depth = LOG_Q_DEPTH;

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status cq_context::alloc_cq_mem() {
    /* Pointer to the CQ ring source memory on the host (to copy). */
    struct mlx5_cqe64 *cq_ring_src;
    /* Temp pointer to an iterator for CQE initialization. */
    struct mlx5_cqe64 *cqe;

    /* DBR source memory on the host (to copy). */
    __be32 dbr[2] = {0, 0};
    /* Function return value. */
    flexio_status status = FLEXIO_STATUS_SUCCESS;
    /* Iterator for CQE initialization. */
    uint32_t i;

    /* Allocate and initialize CQ DBR memory on the DPA heap memory. */
    if (flexio_copy_from_host(process, dbr, sizeof(dbr),
                              &cq_transf.cq_dbr_daddr)) {
        printf("Failed to allocate CQ DBR memory on DPA heap.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Allocate memory for the CQ ring on the host. */
    cq_ring_src = static_cast<mlx5_cqe64 *>(calloc(Q_DEPTH, CQE_BSIZE));
    if (!cq_ring_src) {
        printf("Failed to allocate memory for cq_ring_src.\n");
        return FLEXIO_STATUS_FAILED;
    }

    /* Init CQEs and set ownership bit. */
    for (i = 0, cqe = cq_ring_src; i < Q_DEPTH; i++)
        mlx5dv_set_cqe_owner(cqe++, 1);

    /* Allocate and copy the initialized CQ ring from host to DPA heap memory.
     */
    if (flexio_copy_from_host(process, cq_ring_src, CQ_BSIZE,
                              &cq_transf.cq_ring_daddr)) {
        printf("Failed to allocate CQ ring memory on DPA heap.\n");
        status = FLEXIO_STATUS_FAILED;
    }

    /* Free CQ ring source memory from host once copied to DPA. */
    free(cq_ring_src);

    return status;
}