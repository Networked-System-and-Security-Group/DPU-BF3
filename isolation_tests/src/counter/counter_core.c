#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <doca_buf.h>
#include <doca_common_defines.h>
#include <doca_ctx.h>
#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_eth_txq.h>
#include <doca_eth_txq_cpu_data_path.h>
#include <doca_log.h>
#include <doca_pe.h>

#include <samples/common.h>
#include <samples/doca_eth/eth_rxq_common.h>

#include "doca_error.h"
#include "doca_flow.h"
#include "eth_l2_fwd_core.h"
#include <arpa/inet.h>

#define NS_PER_SEC 1E9 /* Nano-seconds per second */
#define STATS_MAX_BUFF_SIZE                                                    \
    1024 /* Max buffer size to hold statistics string                          \
          */

DOCA_LOG_REGISTER(ETH_L2_FWD : Core);

/* Flag for forcing the application to stop and terminate gracefully */
static bool force_app_stop = false;

static uint32_t total_forwardings = 0;

/* Global variables with default values that may change according to the app's
 * input args */
uint32_t max_forwardings = 0;

flow_stat_t *flow_stat_table = NULL;

/* Ethernet L2 Forwarding application device statistics */
struct eth_l2_fwd_dev_stats {
    uint64_t rx_pkts;    /* Number of RX packets that were handled without being
                            dropped */
    uint64_t rx_dropped; /* Number of RX packets that were dropped (by SW) */
    uint64_t total_tx_pkts; /* Total number of TX packets */
};

/* Ethernet L2 Forwarding application statistics */
struct eth_l2_fwd_stats {
    struct eth_l2_fwd_dev_stats dev1_stats; /* Device 1 statistics */
    struct eth_l2_fwd_dev_stats dev2_stats; /* Device 2 statistics */
    uint64_t initial_time_ns; /* Time (in nanoseconds) at the beginning of the
                                 forwarding phase */
};

static struct eth_l2_fwd_stats stats = {
    .dev1_stats.rx_pkts = 0,
    .dev1_stats.rx_dropped = 0,
};

/*
 * Prints the forwarding statistics of the running application instance
 *
 * @note By default, this function is used only once at the end of the
 * forwarding phase, but is designed to handle multiple calls during the
 * application's run as well
 *
 */
static void eth_l2_fwd_show_stats(void) {
    struct timespec t;
    static uint64_t prev_total_rx_pkts_dev1 = 0;
    static uint64_t prev_time_ns = 0;
    size_t buff_size = STATS_MAX_BUFF_SIZE;
    char buff[buff_size];
    char *buff_cursor = buff;
    int curr_buff_offset = 0;

    if (clock_gettime(CLOCK_REALTIME, &t) != 0) {
        DOCA_LOG_ERR(
            "Failed to show statistics: Failed to get time specification "
            "with clock_gettime()");
        return;
    }

    if (prev_time_ns == 0)
        prev_time_ns = stats.initial_time_ns;

    uint64_t curr_time_ns =
        (uint64_t)t.tv_nsec + (uint64_t)t.tv_sec * NS_PER_SEC;
    uint64_t diff_ns = curr_time_ns - prev_time_ns;

    uint64_t dev1_total_rx_pkts =
        stats.dev1_stats.rx_pkts + stats.dev1_stats.rx_dropped;
    uint64_t dev1_diff_total_pkts_rx =
        dev1_total_rx_pkts - prev_total_rx_pkts_dev1;

    uint64_t dev1_rx_pps =
        ((double)(dev1_diff_total_pkts_rx) / diff_ns) * NS_PER_SEC;

    curr_buff_offset += snprintf(
        buff_cursor, buff_size,
        "\n**************************** Forward statistics for device 1 "
        "*****************************\n");
    curr_buff_offset +=
        snprintf(buff_cursor + curr_buff_offset, buff_size - curr_buff_offset,
                 "RX-packets: %-17" PRIu64 " RX-SW-dropped: %-17" PRIu64
                 " RX-total: %-17" PRIu64 "\n",
                 stats.dev1_stats.rx_pkts, stats.dev1_stats.rx_dropped,
                 dev1_total_rx_pkts);
    curr_buff_offset +=
        snprintf(buff_cursor + curr_buff_offset, buff_size - curr_buff_offset,
                 "\nThroughput (since last call)\n");
    curr_buff_offset +=
        snprintf(buff_cursor + curr_buff_offset, buff_size - curr_buff_offset,
                 "RX-pps: %-" PRIu64 "\n", dev1_rx_pps);
    curr_buff_offset += snprintf(
        buff_cursor + curr_buff_offset, buff_size - curr_buff_offset,
        "***************************************************************"
        "***************************\n");

    snprintf(
        buff_cursor + curr_buff_offset, buff_size - curr_buff_offset,
        "*******************************************************************"
        "***********************");

    DOCA_LOG_INFO("%s", buff);

    prev_total_rx_pkts_dev1 = dev1_total_rx_pkts;

    prev_time_ns = curr_time_ns;
}

/*
 * Function that checks if a device have the required TXQ/RXQ capabilities
 * to be used by open_doca_device_with_ibdev_name()
 *
 * @devinfo [in]: DOCA device info to query for capabilities
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t check_device_caps(struct doca_devinfo *devinfo) {
    doca_error_t result;

    result = doca_eth_txq_cap_is_type_supported(
        devinfo, DOCA_ETH_TXQ_TYPE_REGULAR, DOCA_ETH_TXQ_DATA_PATH_TYPE_CPU);
    if (result != DOCA_SUCCESS)
        return result;

    result = doca_eth_rxq_cap_is_type_supported(
        devinfo, DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL,
        DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU);
    if (result != DOCA_SUCCESS)
        return result;

    return DOCA_SUCCESS;
}

/**
 * Create a DOCA mmap, initialize and start it
 *
 * @dev1 [in]: DOCA device to set to mmap
 * @dev2 [in]: DOCA device to set to mmap
 * @mmap_res [in/out]: DOCA mmap resources to use for mmap creation
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t create_mmap(struct doca_dev *dev1,
                                struct mmap_resources *mmap_res) {
    doca_error_t result;

    int res = posix_memalign(&mmap_res->mmap_buffer, 64, mmap_res->mmap_size);
    if (res != 0 || mmap_res->mmap_buffer == NULL) {
        DOCA_LOG_ERR("Failed to allocate memory for mmap's buffer");
        return DOCA_ERROR_NO_MEMORY;
    }

    result = doca_mmap_create(&mmap_res->mmap);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create mmap: %s", doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_set_permissions(mmap_res->mmap,
                                       DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set memory permissions: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_set_memrange(mmap_res->mmap, mmap_res->mmap_buffer,
                                    mmap_res->mmap_size);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set memory range: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_set_max_num_devices(mmap_res->mmap, 2);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set mmap max number of devices: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_add_dev(mmap_res->mmap, dev1);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to add device 1 to mmap: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_mmap_start(mmap_res->mmap);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start mmap: %s", doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

/*
 * ETH RXQ managed receive event batch successful completion callback for device
 * 1
 *
 * @event_batch_managed_recv [in]: The managed receive event batch
 * @events_number [in]: Number of retrieved events, each representing a single
 * received packet
 * @event_batch_user_data [in]: User provided data, holding a pointer to the ETH
 * TXQ context to forward the packets with
 * @status [in]: Status of retrieved event batch
 * @pkt_array [in]: Array of doca_bufs containing the received packets
 */
static void rx_success_cb1(
    struct doca_eth_rxq_event_batch_managed_recv *event_batch_managed_recv,
    uint16_t events_number, union doca_data event_batch_user_data,
    doca_error_t status, struct doca_buf **pkt_array) {
    /* Unused parameters */
    (void)status;
    (void)event_batch_managed_recv;

    flow_stat_t *flow_stat_table = (flow_stat_t *)(event_batch_user_data.ptr);

    for (uint16_t i = 0; i < events_number; i++) {
        void *pkt_buf = NULL;
        doca_error_t result = doca_buf_get_data(pkt_array[i], &pkt_buf);
        if (result != DOCA_SUCCESS || pkt_buf == NULL) {
            DOCA_LOG_ERR("Failed to get pkt data buf: %s",
                         doca_error_get_descr(result));
        } else {
            uint32_t src_ip = ntohl(*(uint32_t *)(pkt_buf + 26));
            uint32_t dst_ip = ntohl(*(uint32_t *)(pkt_buf + 30));
            if (src_ip == 0) { /* preset magic src_ip for pkt cnt app */
                flow_stat_table[dst_ip].cnt++;
                flow_stat_table[dst_ip].reserved[10]++;
            }
        }
    }

    doca_eth_rxq_event_batch_managed_recv_pkt_array_free(pkt_array);

    stats.dev1_stats.rx_pkts += events_number;
}

/*
 * A common ETH RXQ managed receive event batch error completion callback
 *
 * @event_batch_managed_recv [in]: The managed receive event batch
 * @events_number [in]: Number of retrieved events, each representing a single
 * received packet
 * @event_batch_user_data [in]: User provided data, holding a pointer to the ETH
 * TXQ context to forward the packets with
 * @status [in]: Status of retrieved event batch
 * @pkt_array [in]: Array of doca_bufs containing the received packets
 */
static void rx_error_cb(
    struct doca_eth_rxq_event_batch_managed_recv *event_batch_managed_recv,
    uint16_t events_number, union doca_data event_batch_user_data,
    doca_error_t status, struct doca_buf **pkt_array) {
    /* Unused parameters */
    (void)events_number;
    (void)pkt_array;
    (void)event_batch_managed_recv;
    (void)(event_batch_user_data);
    (void)(status);

    force_app_stop = true;
}

/*
 * Initialize (create and start) an ETH RXQ context
 *
 * @cfg [in]: Ethernet L2 Forwarding application configuration to use for
 * context creation
 * @dev_resrc [in]: Resources of the device to create the ETH RXQ context with
 * @pe [in]: DOCA progress engine to which the ETH RXQ context will be connected
 * @rx_success_cb [in]: RXQ event batch managed receive successful completion
 * callback to set in registration
 * @data [in]: Pointer to data to save as user_data
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t init_eth_rxq_ctx(
    struct eth_l2_fwd_cfg *cfg, struct eth_l2_fwd_dev_resources *dev_resrc,
    struct doca_pe *pe,
    doca_eth_rxq_event_batch_managed_recv_handler_cb_t rx_success_cb,
    void *data) {
    union doca_data user_data;
    doca_error_t result;
    uint32_t max_burst_size, max_pkt_size;

    result = doca_eth_rxq_cap_get_max_burst_size(
        doca_dev_as_devinfo(dev_resrc->mlxdev), &max_burst_size);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get RXQ max burst size capability: %s",
                     doca_error_get_descr(result));
        return result;
    }

    if (max_burst_size < cfg->max_burst_size) {
        DOCA_LOG_ERR("Failed to create ETH RXQ context: max burst size to set "
                     "is to big");
        return DOCA_ERROR_TOO_BIG;
    }

    result = doca_eth_rxq_cap_get_max_packet_size(
        doca_dev_as_devinfo(dev_resrc->mlxdev), &max_pkt_size);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get RXQ max packet size capability: %s",
                     doca_error_get_descr(result));
        return result;
    }

    if (max_pkt_size < cfg->max_pkt_size) {
        DOCA_LOG_ERR("Failed to create ETH RXQ context: max packet size to set "
                     "is to big");
        return DOCA_ERROR_TOO_BIG;
    }

    result = doca_eth_rxq_create(dev_resrc->mlxdev, cfg->max_burst_size,
                                 cfg->max_pkt_size, &dev_resrc->eth_rxq);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create ETH RXQ context: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_eth_rxq_set_type(dev_resrc->eth_rxq,
                                   DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set ETH RXQ type: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result =
        doca_eth_rxq_set_pkt_buf(dev_resrc->eth_rxq, dev_resrc->mmap_resrc.mmap,
                                 0, dev_resrc->mmap_resrc.mmap_size);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set packet buffer: %s",
                     doca_error_get_descr(result));
        return result;
    }

    user_data.ptr = data;
    result = doca_eth_rxq_event_batch_managed_recv_register(
        dev_resrc->eth_rxq, DOCA_EVENT_BATCH_EVENTS_NUMBER_128,
        DOCA_EVENT_BATCH_EVENTS_NUMBER_128, user_data, rx_success_cb,
        rx_error_cb);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to register managed receive event batch: %s",
                     doca_error_get_descr(result));
        return result;
    }

    dev_resrc->eth_rxq_ctx = doca_eth_rxq_as_doca_ctx(dev_resrc->eth_rxq);
    if (dev_resrc->eth_rxq_ctx == NULL) {
        DOCA_LOG_ERR(
            "Failed to retrieve DOCA ETH RXQ context as DOCA context: %s",
            doca_error_get_descr(result));
        return result;
    }

    result = doca_pe_connect_ctx(pe, dev_resrc->eth_rxq_ctx);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set PE for ETH RXQ context: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_ctx_start(dev_resrc->eth_rxq_ctx);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start DOCA context: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_eth_rxq_get_flow_queue_id(dev_resrc->eth_rxq,
                                            &dev_resrc->rxq_flow_queue_id);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get flow queue ID of RXQ: %s",
                     doca_error_get_descr(result));
        return result;
    }

    return DOCA_SUCCESS;
}

/*
 * Forward packets
 *
 * @pe [in]: DOCA progress engine to run
 * @return: DOCA_SUCCESS on success and DOCA_ERROR_... otherwise
 */
static doca_error_t forward_pkts(struct doca_pe *pe) {
    struct timespec t;

    if (clock_gettime(CLOCK_REALTIME, &t) != 0) {
        DOCA_LOG_ERR("Failed to get time specification with clock_gettime()");
        return DOCA_ERROR_IO_FAILED;
    }

    stats.initial_time_ns =
        (uint64_t)t.tv_nsec + (uint64_t)t.tv_sec * NS_PER_SEC;

    DOCA_LOG_INFO("Starting packets forwarding");

    /* Check if max forwardings was set to 0 and therefore there's no limit */
    while (!force_app_stop) {
        struct timespec cur_t;
        if (clock_gettime(CLOCK_REALTIME, &cur_t) != 0) {
            DOCA_LOG_ERR(
                "Failed to get time specification with clock_gettime()");
            return DOCA_ERROR_IO_FAILED;
        }

        if (cur_t.tv_sec - t.tv_sec >= 30)
            force_app_stop = true;

        (void)doca_pe_progress(pe);
    }

    eth_l2_fwd_show_stats();

    DOCA_LOG_INFO("Finished packets forwarding");
    return DOCA_SUCCESS;
}

doca_error_t eth_l2_fwd_execute(struct eth_l2_fwd_cfg *cfg,
                                struct eth_l2_fwd_resources *state) {
    struct eth_rxq_flow_config flow_cfg;
    doca_error_t result;

    result = open_doca_device_with_ibdev_name(
        (uint8_t *)cfg->mlxdev_name1, strlen(cfg->mlxdev_name1),
        check_device_caps, &state->dev_resrc1.mlxdev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR(
            "Failed to open the first device based on IB device name: %s",
            cfg->mlxdev_name1);
        return result;
    }

    result = doca_pe_create(&state->pe);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create DOCA progress engine: %s",
                     doca_error_get_descr(result));
        return result;
    }

    uint32_t recommended_size;
    result = doca_eth_rxq_estimate_packet_buf_size(
        DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL, cfg->pkts_recv_rate,
        cfg->pkt_max_process_time, cfg->max_pkt_size, cfg->max_burst_size,
        ETH_L2_FWD_LOG_MAX_LRO_DEFAULT, &recommended_size);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to estimate buffer size: %s",
                     doca_error_get_descr(result));
        return result;
    }

    /* Check if forwarding from device 1 to device 2 is desired */
    state->dev_resrc1.mmap_resrc.mmap_size = recommended_size;
    result =
        create_mmap(state->dev_resrc1.mlxdev, &state->dev_resrc1.mmap_resrc);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create mmap for device 1: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = rxq_common_init_doca_flow(state->dev_resrc1.mlxdev,
                                       &state->dev_resrc1.flow_resrc);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to initialize DOCA flow port for device 1: %s",
                     doca_error_get_descr(result));
        return result;
    }

    int res = posix_memalign((void *)&flow_stat_table, 64,
                             sizeof(flow_stat_t) * MAX_NB_FLOWS);
    if (res != 0 || flow_stat_table == NULL) {
        DOCA_LOG_ERR("Failed to allocate memory for flow stat table");
        return DOCA_ERROR_DRIVER;
    }

    // Sending the second device's ETH TXQ context (pointer) to save as
    // user_data and allow forwarding via completion callback (see
    // rx_success_cb)
    result = init_eth_rxq_ctx(cfg, &state->dev_resrc1, state->pe,
                              rx_success_cb1, (void *)flow_stat_table);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to initialize ETH RXQ context for device 1: %s",
                     doca_error_get_descr(result));
        return result;
    }

    flow_cfg.dev = state->dev_resrc1.mlxdev;
    flow_cfg.rxq_flow_queue_ids = &(state->dev_resrc1.rxq_flow_queue_id);
    flow_cfg.nb_queues = 1;

    result = allocate_eth_rxq_flow_resources(&flow_cfg,
                                             &state->dev_resrc1.flow_resrc);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR(
            "Failed to allocate ETH RXQ flow resources for device 1: %s",
            doca_error_get_descr(result));
        return result;
    }

    result = forward_pkts(state->pe);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to forward packets: %s",
                     doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO(
        "Ethernet L2 Forwarding Application execution finished successfully");
    return DOCA_SUCCESS;
}

void eth_l2_fwd_force_stop(void) { force_app_stop = true; }

doca_error_t eth_l2_fwd_cleanup(struct eth_l2_fwd_resources *state) {
    doca_error_t result;
    enum doca_ctx_states ctx_state;
    uint8_t num_ctx_in_progress = 0;

    if (state->dev_resrc1.flow_resrc.root_pipe != NULL)
        doca_flow_pipe_destroy(state->dev_resrc1.flow_resrc.root_pipe);

    if (state->dev_resrc1.eth_rxq_ctx != NULL) {
        result = doca_ctx_stop(state->dev_resrc1.eth_rxq_ctx);
        if (result == DOCA_ERROR_IN_PROGRESS)
            ++num_ctx_in_progress;
        else if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to stop device 1 ETH RXQ DOCA CTX: %s",
                         doca_error_get_descr(result));
            return result;
        } else /* DOCA_SUCCESS */
            state->dev_resrc1.eth_rxq_ctx = NULL;
    }

    if (state->dev_resrc1.eth_txq_ctx != NULL) {
        result = doca_ctx_stop(state->dev_resrc1.eth_txq_ctx);
        if (result == DOCA_ERROR_IN_PROGRESS)
            ++num_ctx_in_progress;
        else if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to stop device 1 ETH TXQ DOCA CTX: %s",
                         doca_error_get_descr(result));
            return result;
        } else /* DOCA_SUCCESS */
            state->dev_resrc1.eth_txq_ctx = NULL;
    }

    /* Draining till all contexts states are IDLE */
    while (num_ctx_in_progress > 0) {
        (void)doca_pe_progress(state->pe);

        if (state->dev_resrc1.eth_rxq_ctx != NULL) {
            result =
                doca_ctx_get_state(state->dev_resrc1.eth_rxq_ctx, &ctx_state);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR(
                    "Failed get status of ETH RXQ DOCA CTX for device 1: %s",
                    doca_error_get_name(result));
                return result;
            }

            if (ctx_state == DOCA_CTX_STATE_IDLE) {
                state->dev_resrc1.eth_rxq_ctx = NULL;
                --num_ctx_in_progress;
            }
        }

        if (state->dev_resrc1.eth_txq_ctx != NULL) {
            result =
                doca_ctx_get_state(state->dev_resrc1.eth_txq_ctx, &ctx_state);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR(
                    "Failed get status of ETH TXQ DOCA CTX for device 1: %s",
                    doca_error_get_name(result));
                return result;
            }

            if (ctx_state == DOCA_CTX_STATE_IDLE) {
                state->dev_resrc1.eth_txq_ctx = NULL;
                --num_ctx_in_progress;
            }
        }
    }

    if (state->dev_resrc1.eth_txq != NULL) {
        result = doca_eth_txq_destroy(state->dev_resrc1.eth_txq);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to destroy device 1 ETH TXQ CTX: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    if (state->dev_resrc1.eth_rxq != NULL) {
        result = doca_eth_rxq_destroy(state->dev_resrc1.eth_rxq);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to destroy device 1 ETH RXQ CTX: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    if (state->dev_resrc1.mmap_resrc.mmap != NULL) {
        result = doca_mmap_destroy(state->dev_resrc1.mmap_resrc.mmap);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to destroy DOCA mmap of device 1: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    if (state->dev_resrc1.mmap_resrc.mmap_buffer != NULL)
        free(state->dev_resrc1.mmap_resrc.mmap_buffer);

    if (state->pe != NULL) {
        result = doca_pe_destroy(state->pe);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to destroy DOCA progress engine: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    if (state->dev_resrc1.flow_resrc.df_port != NULL) {
        result = doca_flow_port_stop(state->dev_resrc1.flow_resrc.df_port);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to stop DOCA flow port for device 1: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    if (state->dev_resrc1.mlxdev != NULL) {
        result = doca_dev_close(state->dev_resrc1.mlxdev);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to close DOCA device 1: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    doca_flow_destroy();

    // free(flow_stat_table);

    DOCA_LOG_INFO(
        "Ethernet L2 Forwarding Application resources cleanup finished "
        "successfully");
    return DOCA_SUCCESS;
}
