#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <semaphore.h>
#include <stop_token>
#include <thread>

#include <doca_ctx.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>

#include "astraea_ctx.h"
#include "astraea_ec.h"
#include "doca_erasure_coding.h"
#include "resource_mgmt.h"

DOCA_LOG_REGISTER(ASTRAEA : CTX);

extern sem_t *ec_token_sem;
extern shared_resources *shm_data;
extern uint32_t app_id;

static void worker(std::stop_token stoken, astraea_ctx *ctx) {
    while (!stoken.stop_requested()) {
        switch (ctx->type) {
        case EC:
            if (sem_wait(ec_token_sem)) {
                DOCA_LOG_ERR("Failed to get ec_token_sem");
                break;
            }

            std::lock_guard<std::mutex> task_queue_guard{
                ctx->ec->ec_create_tasks_lock};
            std::lock_guard<std::mutex> ctx_guard{ctx->ctx_lock};

            bool will_submit_tasks = shm_data->ec_tokens[app_id] > 0 &&
                                     !ctx->ec->ec_create_tasks.empty();
            while (shm_data->ec_tokens[app_id] > 0 &&
                   !ctx->ec->ec_create_tasks.empty()) {
                doca_error_t status =
                    doca_task_submit_ex(doca_ec_task_create_as_task(
                                            ctx->ec->ec_create_tasks.front()),
                                        DOCA_TASK_SUBMIT_FLAG_NONE);
                if (status == DOCA_SUCCESS) {
                    ctx->ec->ec_create_tasks.pop();
                    shm_data->ec_tokens[app_id]--;
                } else {
                    DOCA_LOG_ERR("Failed to submit sub task: %s",
                                 doca_error_get_descr(status));
                }
            }
            if (will_submit_tasks)
                doca_ctx_flush_tasks(ctx->ctx);

            if (sem_post(ec_token_sem)) {
                DOCA_LOG_ERR("Failed to post ec_token_sem");
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

doca_error_t astraea_ctx_start(astraea_ctx *ctx) {
    doca_error_t status = doca_ctx_start(ctx->ctx);
    if (status != DOCA_SUCCESS) {
        return status;
    }
    ctx->submitter = new std::jthread{worker, ctx};
    return status;
}

doca_error_t astraea_ctx_stop(astraea_ctx *ctx) {
    /**
     * It is mandatory to check whether submitter is null
     * As user may call this function many times to ensure doca_ctx fully
     * stopped
     */
    if (ctx->submitter) {
        ctx->submitter->request_stop();
        delete ctx->submitter;
        ctx->submitter = nullptr;
    }
    /* Astraea will release astraea_ctx's memory in astraea_pe_progress */
    return doca_ctx_stop(ctx->ctx);
}