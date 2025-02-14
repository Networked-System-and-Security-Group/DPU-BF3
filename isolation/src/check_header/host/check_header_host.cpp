#define BASE_FAKE_MAC 0xa088c2320440

/* Used for geteuid function. */
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

/* Used for IBV device operations. */
#include <infiniband/mlx5dv.h>

/* Flex IO SDK host side API header. */
#include <libflexio/flexio.h>

/* Common header for communication between host and DPA. */
#include "../check_header_com.h"

#include "eh_context.hpp"
#include "fp_context.hpp"

extern "C" {
/* Flex IO packet processor application struct.
 * Created by DPACC during compilation. The DEV_APP_NAME
 * is a macro transferred from Meson through gcc, with the
 * same name as the created application.
 */
extern struct flexio_app *DEV_APP_NAME;
/* Flex IO packet processor device (DPA) side function stub. */
flexio_func_t check_header_event_handler;
flexio_func_t check_header_init;
flexio_func_t check_header_stop;
flexio_func_t check_header_query;
};
static bool force_quit;
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("Signal %d received, preparing to exit\n", signum);
        force_quit = true;
    }
}

/* Main host side function.
 * Responsible for allocating resources and making preparations for DPA side
 * envocatin.
 */
int main(int argc, char **argv) {
    fp_context *fp_ctx = new fp_context();

    /* Check if the application run with root privileges */
    if (geteuid()) {
        printf("Failed - the application must run with root privileges\n");
        return EXIT_FAILURE;
    }

    /* Create an IBV device context by opening the provided IBV device. */
    char *device = argv[1];
    if (fp_ctx->open_ibv_ctx(device) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to open ibv ctx\n");
        return EXIT_FAILURE;
    }

    /* Init flow steering context */
    if (fp_ctx->init_flow_steer() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to init flow steer\n");
        return EXIT_FAILURE;
    }

    /* Create a Flex IO process.
     * The flexio_app struct (created by DPACC) is passed to load the program.
     * No process creation attributes are needed for this application (default
     * outbox). Created SW struct will be returned through the given pointer.
     */
    if (fp_ctx->create_process(DEV_APP_NAME) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create Flex IO process.\n");
        return EXIT_FAILURE;
    }

    /* Create a Flex IO message stream for process.
     * Size of single message stream is MSG_HOST_BUFF_BSIZE.
     * Working mode is synchronous.
     * Level of debug in INFO.
     * Output is stdout.
     */
    if (fp_ctx->create_msg_stream() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to init device messaging environment, exiting App\n");
        return EXIT_FAILURE;
    }

    uint8_t nb_threads = atoi(argv[2]);
    for (uint8_t thread_id = 0; thread_id < nb_threads; thread_id++) {
        eh_context *ck_hdr_eh_ctx = new eh_context(fp_ctx->process, thread_id);
        if (ck_hdr_eh_ctx->create_event_handler(check_header_event_handler) !=
            FLEXIO_STATUS_SUCCESS) {
            printf("Failed to create event handler for thread %d\n", thread_id);
            return EXIT_FAILURE;
        }

        if (ck_hdr_eh_ctx->create_queues() != FLEXIO_STATUS_SUCCESS) {
            printf("Failed to create queues for Flex IO threads\n");
            return EXIT_FAILURE;
        }

        uint64_t fake_mac = BASE_FAKE_MAC + thread_id;
        if (fp_ctx->steerer->create_rule_rx_mac_match(
                flexio_rq_get_tir(ck_hdr_eh_ctx->rq_ctx->flexio_rq_ptr),
                fake_mac) != FLEXIO_STATUS_SUCCESS) {
            printf("Failed to create RX steering rule\n");
            return EXIT_FAILURE;
        }

        if (fp_ctx->steerer->create_rule_tx_fwd_to_sws_table(fake_mac) !=
            FLEXIO_STATUS_SUCCESS) {
            printf("Failed to create TX table steering rule\n");
            return EXIT_FAILURE;
        }

        if (fp_ctx->steerer->create_rule_tx_fwd_to_vport(fake_mac) !=
            FLEXIO_STATUS_SUCCESS) {
            printf("Failed to create TX vport steering rule\n");
            return EXIT_FAILURE;
        }

        if (ck_hdr_eh_ctx->copy_app_data_to_dpa() != FLEXIO_STATUS_SUCCESS) {
            printf("Failed to copy app data to DPA\n");
            return EXIT_FAILURE;
        }

        uint64_t ret_val = 0;
        if (flexio_process_call(fp_ctx->process, check_header_init, &ret_val,
                                ck_hdr_eh_ctx->app_data_daddr) !=
                FLEXIO_STATUS_SUCCESS ||
            ret_val != 0) {
            printf("Failed to init app\n");
            return EXIT_FAILURE;
        }

        fp_ctx->eh_ctxs->push_back(ck_hdr_eh_ctx);
    }

    force_quit = false;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Start event handler - move from the init state to the running state.
     * Event handlers in the running state may be invoked by an incoming CQE.
     * On other states, the invocation is blocked and lost.
     * Pass the address of common information as a user argument to be used on
     * the DPA side.
     */

    for (eh_context *eh_ctx : *fp_ctx->eh_ctxs) {
        if (flexio_event_handler_run(eh_ctx->eh, eh_ctx->thread_id) !=
            FLEXIO_STATUS_SUCCESS) {
            printf("Failed to run event handler on thread %d\n",
                   eh_ctx->thread_id);
            return EXIT_FAILURE;
        }
    }

    printf("App began, press ctrl + c to terminate\n");

    uint32_t nb_seconds = atoi(argv[3]);
    while (!force_quit) {
        sleep(nb_seconds);
        force_quit = true;
    }

    uint64_t ret_val = 0;
    if (flexio_process_call(fp_ctx->process, check_header_stop, &ret_val) !=
            FLEXIO_STATUS_SUCCESS ||
        ret_val != 0) {
        printf("Failed to stop event handlers\n");
        return EXIT_FAILURE;
    }

    printf("Wait 2 seconds for all threads to stop\n");
    sleep(2);

    uint64_t packets_count = 0;
    if (flexio_process_call(fp_ctx->process, check_header_query, &packets_count,
                            PACKETS_COUNT) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to query packets count\n");
        return EXIT_FAILURE;
    }

    uint64_t reschedule_times = 0;
    if (flexio_process_call(fp_ctx->process, check_header_query,
                            &reschedule_times,
                            RESCHEDULE_TIMES) != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to query reschedule times\n");
        return EXIT_FAILURE;
    }

    printf("App processed %lu packets and reschedule %lu times in %u seconds, "
           "pps is %f\n",
           packets_count, reschedule_times, nb_seconds,
           static_cast<double>(packets_count) / nb_seconds);

    return EXIT_SUCCESS;
}
