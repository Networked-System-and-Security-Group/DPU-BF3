#include "com_dev.h"
#include <dpaintrin.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <libflexio-libc/string.h>
#include <stddef.h>
/* Shared header file for packet processor sample */
#include "../../common/flexio/transfer.h"
#include "../check_header_com.h"
#include "libflexio-dev/flexio_dev.h"

/* Mask for CQ index */
#define CQ_IDX_MASK ((1 << LOG_CQ_DEPTH) - 1)
/* Mask for RQ index */
#define RQ_IDX_MASK ((1 << LOG_RQ_DEPTH) - 1)
/* Mask for SQ index */
#define SQ_IDX_MASK ((1 << (LOG_SQ_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
/* Mask for data index */
#define DATA_IDX_MASK ((1 << (LOG_SQ_DEPTH)) - 1)

/* RPC call handlers */
flexio_dev_rpc_handler_t check_header_init;
flexio_dev_rpc_handler_t check_header_stop;
flexio_dev_rpc_handler_t check_header_query;

/* Event handler */
flexio_dev_event_handler_t check_header_event_handler;

struct ether_addr {
    uint8_t addr_bytes[6];
};

struct ether_hdr {
    struct ether_addr dst_addr;
    struct ether_addr src_addr;
    uint16_t ether_type;
} __attribute__((__packed__));

struct ipv4_hdr {
    uint8_t version_ihl;
    uint8_t type_of_service;
    uint16_t total_length;
    uint16_t packet_id;
    uint16_t fragment_offset;
    uint8_t time_to_live;
    uint8_t next_proto_id;
    uint16_t hdr_checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} __attribute__((__packed__));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t dgram_len;
    uint16_t dgram_cksum;
} __attribute__((__packed__));

struct udp_packet {
    struct ether_hdr eth_hdr;
    struct ipv4_hdr ip_hdr;
    struct udp_hdr udp_hdr;
    // no data element entry
} __attribute__((__packed__));

/* The structure of the sample DPA application contains global data that the
 * application uses */
static struct app_context {
    /* Packet count - processed packets */
    uint64_t packets_count;
    /* lkey - local memory key */
    uint32_t lkey;

    cq_ctx_t rq_cq_ctx; /* RQ CQ */
    rq_ctx_t rq_ctx;    /* RQ */
    sq_ctx_t sq_ctx;    /* SQ */
    cq_ctx_t sq_cq_ctx; /* SQ CQ */
    dt_ctx_t dt_ctx;    /* SQ Data ring */

    uint8_t thread_id;         /* Thread id */
    uint8_t is_initialized;    /* Wether thread is initialized */
    uint8_t should_stop;       /* Wether thread should stop */
    uint8_t first_handle;      /* Wether is the first time to run the event
                                  handler(per thread)*/
    uint64_t reschedule_times; /* Times of reschedule */
} __attribute__((__aligned__(64))) app_ctxs[MAX_NB_THREADS];

static const size_t CLASSIFIC_NUMBER = 1024 * 100;
static size_t classific_counter[10][102400];

// static unsigned int RSHash(const char *str, unsigned int length) {
//     unsigned int b = 378551;
//     unsigned int a = 63689;
//     unsigned int hash = 0;
//     unsigned int i = 0;

//     for (i = 0; i < length; ++str, ++i) {
//         hash = hash * a + (*str);
//         a = a * b;
//     }

//     return hash;
// }

static unsigned int JSHash(const char *str, unsigned int length) {
    unsigned int hash = 1315423911;
    unsigned int i = 0;

    for (i = 0; i < length; ++str, ++i) {
        hash ^= ((hash << 5) + (*str) + (hash >> 2));
    }

    return hash;
}
static inline void classific_hash(char *data, uint64_t thread_index) {
    size_t *arr = classific_counter[thread_index];
    uint32_t total_tuple_number = 0;
    struct udp_packet *pkt = (struct udp_packet *)data;

    total_tuple_number += pkt->ip_hdr.dst_addr;
    total_tuple_number += pkt->ip_hdr.src_addr;
    total_tuple_number += pkt->udp_hdr.dst_port;
    total_tuple_number += pkt->udp_hdr.src_port;

    uint64_t hash_value = JSHash((const char *)&total_tuple_number, 4);
    arr[hash_value % CLASSIFIC_NUMBER]++;
}

static inline void udp_shift_updating(char *data) {
    (void)data;
    struct udp_packet *pkt = (struct udp_packet *)data;

    pkt->ip_hdr.dst_addr = pkt->ip_hdr.dst_addr + 1;
    pkt->ip_hdr.src_addr = pkt->ip_hdr.src_addr + 1;

    pkt->udp_hdr.dst_port = pkt->udp_hdr.dst_port + 1;
    pkt->udp_hdr.src_port = pkt->udp_hdr.src_port + 1;
}

static inline size_t check_ipheader(char *data) {
    struct udp_packet *pkt = (struct udp_packet *)data;

    if (pkt->ip_hdr.version_ihl != 0x45) {
        pkt->ip_hdr.version_ihl = 0x45;
    }
    if (pkt->ip_hdr.type_of_service != 0) {
        pkt->ip_hdr.type_of_service = 0;
    }
    if (pkt->ip_hdr.total_length == cpu_to_be16(0)) {
        pkt->ip_hdr.total_length = cpu_to_be16(1024);
    }
    if (pkt->ip_hdr.time_to_live != 64) {
        pkt->ip_hdr.time_to_live = 64;
    }
    if (pkt->ip_hdr.next_proto_id != 17) {
        pkt->ip_hdr.next_proto_id = 17;
    }
    if (pkt->ip_hdr.packet_id == 0) {
        return 0;
    }

    if (pkt->ip_hdr.src_addr == 0) {
        return 0;
    }
    if (pkt->ip_hdr.dst_addr == 0) {
        return 0;
    }

    if (pkt->udp_hdr.src_port == 0) {
        return 0;
    }
    if (pkt->udp_hdr.dst_port == 0) {
        return 0;
    }
    if (pkt->udp_hdr.dgram_len == 0) {
        return 0;
    }
    if (pkt->udp_hdr.dgram_len == 0) {
        return 0;
    }

    return 1;
}

/* Initialize the app_ctx structure from the host data.
 *  data_from_host - pointer host2dev_base_data from host.
 */
__dpa_rpc__ uint64_t check_header_init(uint64_t data_ptr) {
    struct host2dev_base_data *data_from_host =
        (struct host2dev_base_data *)data_ptr;
    uint8_t thread_id = data_from_host->thread_id;
    struct app_context *app_ctx = &app_ctxs[thread_id];
    app_ctx->thread_id = thread_id;
    app_ctx->should_stop = 0;
    app_ctx->first_handle = 1;

    app_ctx->packets_count = 0;
    app_ctx->reschedule_times = 0;
    app_ctx->lkey = data_from_host->sq_transf.wqd_mkey_id;

    /* Set context for RQ's CQ */
    com_cq_ctx_init(&app_ctx->rq_cq_ctx, data_from_host->rq_cq_transf.cq_num,
                    data_from_host->rq_cq_transf.log_cq_depth,
                    data_from_host->rq_cq_transf.cq_ring_daddr,
                    data_from_host->rq_cq_transf.cq_dbr_daddr);

    /* Set context for RQ */
    com_rq_ctx_init(&app_ctx->rq_ctx, data_from_host->rq_transf.wq_num,
                    data_from_host->rq_transf.wq_ring_daddr,
                    data_from_host->rq_transf.wq_dbr_daddr);

    /* Set context for SQ */
    com_sq_ctx_init(&app_ctx->sq_ctx, data_from_host->sq_transf.wq_num,
                    data_from_host->sq_transf.wq_ring_daddr);

    /* Set context for SQ's CQ */
    com_cq_ctx_init(&app_ctx->sq_cq_ctx, data_from_host->sq_cq_transf.cq_num,
                    data_from_host->sq_cq_transf.log_cq_depth,
                    data_from_host->sq_cq_transf.cq_ring_daddr,
                    data_from_host->sq_cq_transf.cq_dbr_daddr);

    /* Set context for data */
    com_dt_ctx_init(&app_ctx->dt_ctx, data_from_host->sq_transf.wqd_daddr);
    app_ctx->is_initialized = 1;
    flexio_dev_print("Thread %d initialized\n", thread_id);
    return 0;
}

__dpa_rpc__ uint64_t check_header_stop(__attribute__((unused)) uint64_t arg) {
    for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
        app_ctxs[thread_id].should_stop = 1;
    }
    return 0;
}

__dpa_rpc__ uint64_t check_header_query(uint64_t query_type) {
    if (query_type == PACKETS_COUNT) {
        uint64_t total_packets_count = 0;
        for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
            total_packets_count += app_ctxs[thread_id].packets_count;
        }
        return total_packets_count;
    } else {
        uint64_t total_reschedule_times = 0;
        for (uint8_t thread_id = 0; thread_id < MAX_NB_THREADS; thread_id++) {
            total_reschedule_times += app_ctxs[thread_id].reschedule_times;
        }
        return total_reschedule_times;
    }
}

/* process packet - read it, swap MAC addresses, modify it, create a send WQE
 * and send it back. */
static void process_packet(struct app_context *app_ctx) {
    /* RX packet handling variables */
    struct flexio_dev_wqe_rcv_data_seg *rwqe;
    /* RQ WQE index */
    uint32_t rq_wqe_idx;
    /* Pointer to RQ data */
    char *rq_data;

    /* Extract relevant data from the CQE */
    rq_wqe_idx = flexio_dev_cqe_get_wqe_counter(app_ctx->rq_cq_ctx.cqe);

    /* Get the RQ WQE pointed to by the CQE */
    rwqe = &app_ctx->rq_ctx.rq_ring[rq_wqe_idx & RQ_IDX_MASK];

    /* Extract data (whole packet) pointed to by the RQ WQE */
    rq_data = flexio_dev_rwqe_get_addr(rwqe);

    udp_shift_updating(rq_data);
    classific_hash(rq_data, app_ctx->thread_id % 1);
    check_ipheader(rq_data);

    /* Ring DB */
    __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
    flexio_dev_dbr_rq_inc_pi(app_ctx->rq_ctx.rq_dbr);
}

/* Entry point function that host side call for the execute.
 *  thread_arg - pointer to the host2dev_base_data structure
 *     to transfer data from the host side.
 */
__dpa_global__ void check_header_event_handler(uint64_t thread_id) {
    struct app_context *app_ctx = &app_ctxs[thread_id];
    if (app_ctx->is_initialized == 0) {
        flexio_dev_thread_reschedule();
    }

    if (app_ctx->first_handle) {
        flexio_dev_print("Thread %d event handler start\n", app_ctx->thread_id);
        app_ctx->first_handle = 0;
    }

    /* Poll CQ until the package is received.
     */
    while (flexio_dev_cqe_get_owner(app_ctx->rq_cq_ctx.cqe) !=
               app_ctx->rq_cq_ctx.cq_hw_owner_bit &&
           app_ctx->should_stop == 0) {
        /* Process the packet */
        process_packet(app_ctx);
        /* Print the message */
        app_ctx->packets_count++;
        if (app_ctx->packets_count % 100000 == 0) {
            flexio_dev_print("Thread %lu processed packet %ld\n", thread_id,
                             app_ctx->packets_count);
        }
        /* Update memory to DPA */
        __dpa_thread_fence(__DPA_MEMORY, __DPA_R, __DPA_R);
        /* Update RQ CQ */
        com_step_cq(&app_ctx->rq_cq_ctx);
    }
    /* Update the memory to the chip */
    __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
    /* Arming cq for next packet */
    flexio_dev_cq_arm(app_ctx->rq_cq_ctx.cq_idx, app_ctx->rq_cq_ctx.cq_number);

    /* Reschedule the thread */
    if (app_ctx->should_stop) {
        flexio_dev_thread_finish();
    } else {
        app_ctx->reschedule_times++;
        flexio_dev_thread_reschedule();
    }
}
