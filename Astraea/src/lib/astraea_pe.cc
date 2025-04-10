#include <cstdint>
#include <mutex>
#include <semaphore.h>

#include <doca_error.h>
#include <doca_pe.h>
#include <utility>
#include <vector>

#include "astraea_ctx.h"
#include "astraea_ec.h"
#include "astraea_pe.h"

#include "doca_buf.h"
#include "doca_mmap.h"
#include "resource_mgmt.h"

bool has_finished_task = false;

doca_error_t astraea_pe_create(astraea_pe **pe) {
    *pe = new astraea_pe;

    doca_error_t status = doca_pe_create(&(*pe)->pe);

    if (status != DOCA_SUCCESS) {
        delete *pe;
        *pe = nullptr;
    }

    return status;
}

doca_error_t astraea_pe_destroy(astraea_pe *pe) {
    for (astraea_ctx *ctx : pe->ctxs) {
        delete ctx;
    }

    doca_error_t status = doca_pe_destroy(pe->pe);

    delete pe;

    return status;
}

uint8_t astraea_pe_progress(astraea_pe *pe) {
    std::vector<std::unique_lock<std::mutex>> locks;
    for (astraea_ctx *ctx : pe->ctxs) {
        locks.push_back(std::unique_lock<std::mutex>{ctx->ctx_lock});
    }
    doca_pe_progress(pe->pe);

    if (has_finished_task) {
        has_finished_task = false;
        return 1;
    }
    return 0;
}

doca_error_t astraea_task_submit(astraea_task *task) {
    if (task->type == EC_CREATE) {
        std::lock_guard<std::mutex> guard{
            task->ec_task_create->ec->ec_create_tasks_lock};
        for (_astraea_ec_subtask_create *subtask :
             task->ec_task_create->subtasks) {
            task->ec_task_create->ec->ec_create_tasks.push(subtask->task);
        }
    }
    return DOCA_SUCCESS;
}

void astraea_task_free(astraea_task *task) {
    if (task->type == EC_CREATE) {
        for (_astraea_ec_subtask_create *subtask :
             task->ec_task_create->subtasks) {
            doca_task_free(doca_ec_task_create_as_task(subtask->task));
            delete subtask->user_data;
            delete subtask;
        }
        for (std::pair<doca_buf *, doca_buf *> sub_buf_pair :
             task->ec_task_create->sub_buf_pairs) {
            doca_buf_dec_refcount(sub_buf_pair.first, nullptr);
            doca_buf_dec_refcount(sub_buf_pair.second, nullptr);
        }

        delete task->ec_task_create;
    }
    delete task;
}

doca_error_t astraea_pe_connect_ctx(astraea_pe *pe, astraea_ctx *ctx) {
    std::lock_guard<std::mutex> guard{ctx->ctx_lock};
    doca_error_t status = doca_pe_connect_ctx(pe->pe, ctx->ctx);
    if (status == DOCA_SUCCESS) {
        pe->ctxs.push_back(ctx);
    }
    return status;
}