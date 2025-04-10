#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_erasure_coding.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_types.h>
#include <pthread.h>
#include <utility>

#include "astraea_ctx.h"
#include "astraea_ec.h"
#include "astraea_pe.h"
#include "doca_pe.h"

DOCA_LOG_REGISTER(ASTRAEA : EC);

extern bool has_finished_task;

void subtask_success_cb(doca_ec_task_create *task, doca_data task_user_data,
                        doca_data ctx_user_data) {
    (void)ctx_user_data;
    const _astraea_ec_subtask_create_user_data *user_data =
        static_cast<_astraea_ec_subtask_create_user_data *>(task_user_data.ptr);

    if (user_data->is_sub) {
        const doca_buf *sub_dst_buf = doca_ec_task_create_get_rdnc_blocks(task);
        uint8_t *dst_data;
        doca_buf_get_data(sub_dst_buf, (void **)&dst_data);

        const size_t origin_block_size =
            user_data->origin_task->origin_block_size;
        const size_t sub_block_size = user_data->origin_task->sub_block_size;
        const uint32_t nb_rdnc_blocks =
            user_data->origin_task->matrix->nb_rdnc_blocks;
        uint8_t *dst_base_addr =
            static_cast<uint8_t *>(user_data->origin_task->dst_base_addr);

        for (uint32_t i = 0; i < nb_rdnc_blocks; i++) {
            memcpy(dst_base_addr + i * origin_block_size +
                       user_data->strip_id * sub_block_size,
                   dst_data + i * sub_block_size, sub_block_size);
        }
    }

    if (user_data->is_last) {
        user_data->origin_task->ec->success_cb(
            user_data->origin_task, user_data->origin_task->user_data,
            {.u64 = 0});
        has_finished_task = true;
    }
}

void subtask_error_cb(doca_ec_task_create *task, doca_data task_user_data,
                      doca_data ctx_user_data) {
    (void)ctx_user_data;
    _astraea_ec_subtask_create_user_data *user_data =
        static_cast<_astraea_ec_subtask_create_user_data *>(task_user_data.ptr);

    if (user_data->is_last) {
        user_data->origin_task->ec->error_cb(user_data->origin_task,
                                             user_data->origin_task->user_data,
                                             {.u64 = 0});
        has_finished_task = true;
    }
}

doca_error_t astraea_ec_create(doca_dev *dev, astraea_ec **ec) {
    astraea_ec *new_ec = new astraea_ec;
    *ec = nullptr;

    new_ec->dev = dev;

    doca_error_t status = doca_ec_create(dev, &new_ec->ec);
    if (status != DOCA_SUCCESS) {
        delete new_ec;
        return status;
    }

    int ret =
        posix_memalign(&new_ec->tmp_rdnc_buffer, 64, TMP_RDNC_BUFFER_SIZE);
    if (ret) {
        DOCA_LOG_ERR("Failed to alloc memory");
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }
    status = doca_mmap_create(&new_ec->dst_mmap);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create dst mmap: %s",
                     doca_error_get_descr(status));
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    status = doca_mmap_add_dev(new_ec->dst_mmap, dev);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to add dev to dst mmap: %s",
                     doca_error_get_descr(status));
        doca_mmap_destroy(new_ec->dst_mmap);
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    status = doca_mmap_set_memrange(new_ec->dst_mmap, new_ec->tmp_rdnc_buffer,
                                    TMP_RDNC_BUFFER_SIZE);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set mmap memrange: %s",
                     doca_error_get_descr(status));
        doca_mmap_destroy(new_ec->dst_mmap);
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    status = doca_mmap_start(new_ec->dst_mmap);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start dst mmap: %s",
                     doca_error_get_descr(status));
        doca_mmap_destroy(new_ec->dst_mmap);
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    status = doca_buf_inventory_create(MAX_NB_CTX_BUFS, &new_ec->buf_inventory);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create buf inventory: %s",
                     doca_error_get_descr(status));
        doca_mmap_destroy(new_ec->dst_mmap);
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    status = doca_buf_inventory_start(new_ec->buf_inventory);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start buf inventory: %s",
                     doca_error_get_descr(status));
        doca_buf_inventory_destroy(new_ec->buf_inventory);
        doca_mmap_destroy(new_ec->dst_mmap);
        doca_ec_destroy(new_ec->ec);
        delete new_ec;
        return status;
    }

    *ec = new_ec;

    return DOCA_SUCCESS;
}

doca_error_t astraea_ec_destroy(astraea_ec *ec) {
    doca_error_t status = doca_ec_destroy(ec->ec);
    status = doca_buf_inventory_destroy(ec->buf_inventory);
    status = doca_mmap_destroy(ec->dst_mmap);
    free(ec->tmp_rdnc_buffer);

    delete ec;

    return status;
}

astraea_ctx *astraea_ec_as_ctx(astraea_ec *ec) {
    astraea_ctx *ctx = new astraea_ctx;

    ctx->ctx = doca_ec_as_ctx(ec->ec);
    if (ctx->ctx == nullptr) {
        delete ctx;
        return nullptr;
    }
    ctx->type = EC;
    ctx->ec = ec;
    ctx->submitter = nullptr;

    return ctx;
}

doca_error_t astraea_ec_task_create_set_conf(
    astraea_ec *ec,
    astraea_ec_task_create_completion_cb_t successful_task_completion_cb,
    astraea_ec_task_create_completion_cb_t error_task_completion_cb,
    uint32_t num_tasks) {
    (void)num_tasks;
    ec->success_cb = successful_task_completion_cb;
    ec->error_cb = error_task_completion_cb;
    return doca_ec_task_create_set_conf(ec->ec, subtask_success_cb,
                                        subtask_error_cb, 8192);
}

/**
 * This function should be modify later!
 * The granularity should be calculated according to real time metadata
 * Which must incurs semaphore operations
 */
static size_t calc_granularity() { return 1024; }

/* Only use to reduce function parameter */
struct subtask_create_ctx {
    doca_buf *sub_src_buf;
    doca_buf *sub_dst_buf;
    uint32_t strip_id;
    bool is_last;
    bool is_sub;
    astraea_ec_task_create *origin_task;
};

static inline doca_error_t
create_subtask(const subtask_create_ctx &stsk_ctx,
               _astraea_ec_subtask_create **subtask) {
    *subtask = nullptr;
    _astraea_ec_subtask_create *new_subtask = new _astraea_ec_subtask_create;

    /* Set user data that will be pass to the real doca_task */
    new_subtask->user_data = new _astraea_ec_subtask_create_user_data;

    new_subtask->user_data->is_sub = stsk_ctx.is_sub;
    new_subtask->user_data->is_last = stsk_ctx.is_last;
    new_subtask->user_data->strip_id = stsk_ctx.strip_id;
    new_subtask->user_data->origin_task = stsk_ctx.origin_task;

    doca_error_t status = doca_ec_task_create_allocate_init(
        stsk_ctx.origin_task->ec->ec, stsk_ctx.origin_task->matrix->matrix,
        stsk_ctx.sub_src_buf, stsk_ctx.sub_dst_buf,
        {.ptr = new_subtask->user_data}, &new_subtask->task);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to allocate and init ec create task: %s",
                     doca_error_get_descr(status));
        delete new_subtask->user_data;
        delete new_subtask;
        return status;
    }

    *subtask = new_subtask;
    return DOCA_SUCCESS;
}

doca_error_t astraea_ec_task_create_allocate_init(
    astraea_ec *ec, astraea_ec_matrix *coding_matrix, doca_mmap *src_mmap,
    doca_buf *original_data_blocks, doca_buf *rdnc_blocks, doca_data user_data,
    astraea_ec_task_create **task) {
    *task = nullptr;
    astraea_ec_task_create *new_task = new astraea_ec_task_create;

    size_t src_buf_size;
    doca_error_t status =
        doca_buf_get_data_len(original_data_blocks, &src_buf_size);
    if (status != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get block size: %s",
                     doca_error_get_descr(status));
        delete new_task;
        return status;
    }

    const size_t origin_block_size =
        src_buf_size / coding_matrix->nb_data_blocks;
    const size_t sub_block_size = calc_granularity();

    new_task->sub_block_size = sub_block_size;
    new_task->origin_block_size = origin_block_size;

    new_task->user_data = user_data;
    new_task->original_data_blocks = original_data_blocks;
    new_task->rdnc_blocks = rdnc_blocks;
    new_task->ec = ec;
    new_task->matrix = coding_matrix;

    if (origin_block_size > sub_block_size) {
        void *dst_base_addr = nullptr;
        status = doca_buf_get_data(rdnc_blocks, &dst_base_addr);
        if (status != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to get rdnc buf addr: %s",
                         doca_error_get_descr(status));
            delete new_task;
            return status;
        }

        new_task->dst_base_addr = static_cast<uint8_t *>(dst_base_addr);

        void *src_base_addr = nullptr;
        status = doca_buf_get_data(original_data_blocks, &src_base_addr);
        if (status != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to get data buf addr: %s",
                         doca_error_get_descr(status));
            delete new_task;
            return status;
        }

        const uint32_t nb_strips = origin_block_size / sub_block_size;
        for (uint32_t i = 0; i < nb_strips; i++) {
            doca_buf *sub_src_buf, *sub_dst_buf;

            doca_buf *tmp_bufs[MAX_NB_DATA_BLOCKS];
            // create scatter gather list
            for (uint32_t j = 0; j < coding_matrix->nb_data_blocks; j++) {
                uint8_t *addr = static_cast<uint8_t *>(src_base_addr) +
                                j * origin_block_size + i * sub_block_size;

                status = doca_buf_inventory_buf_get_by_addr(
                    ec->buf_inventory, src_mmap, addr, sub_block_size,
                    &tmp_bufs[j]);
                if (status != DOCA_SUCCESS) {
                    DOCA_LOG_ERR("Failed to alloc buf for data blocks: %s",
                                 doca_error_get_descr(status));
                    return status;
                }

                status = doca_buf_set_data(tmp_bufs[j], addr, sub_block_size);
                if (status != DOCA_SUCCESS) {
                    DOCA_LOG_ERR("Failed to set buf data for data bufs: %s",
                                 doca_error_get_descr(status));
                    return status;
                }

                if (j == 0) {
                    sub_src_buf = tmp_bufs[j];
                } else {
                    status = doca_buf_chain_list(sub_src_buf, tmp_bufs[j]);
                    if (status != DOCA_SUCCESS) {
                        DOCA_LOG_ERR("Failed to chain list: %s",
                                     doca_error_get_descr(status));
                        return status;
                    }
                }
            }

            status = doca_buf_inventory_buf_get_by_addr(
                ec->buf_inventory, ec->dst_mmap,
                static_cast<uint8_t *>(ec->tmp_rdnc_buffer) +
                    i * sub_block_size * coding_matrix->nb_rdnc_blocks,
                sub_block_size * coding_matrix->nb_rdnc_blocks, &sub_dst_buf);
            if (status != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to alloc buf for rdnc blocks: %s",
                             doca_error_get_descr(status));
                return status;
            }

            const subtask_create_ctx stsk_ctx = {
                .sub_src_buf = sub_src_buf,
                .sub_dst_buf = sub_dst_buf,
                .strip_id = i,
                .is_last = i == nb_strips - 1 ? true : false,
                .is_sub = true,
                .origin_task = new_task};

            _astraea_ec_subtask_create *subtask = nullptr;
            status = create_subtask(stsk_ctx, &subtask);
            if (status != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to create sub task");
                delete new_task;
                return status;
            }
            new_task->subtasks.push_back(subtask);
            new_task->sub_buf_pairs.push_back(
                std::make_pair(stsk_ctx.sub_src_buf, stsk_ctx.sub_dst_buf));
        }
    } else {
        const subtask_create_ctx stsk_ctx = {.sub_src_buf =
                                                 original_data_blocks,
                                             .sub_dst_buf = rdnc_blocks,
                                             .strip_id = 0,
                                             .is_last = true,
                                             .is_sub = false,
                                             .origin_task = new_task};
        _astraea_ec_subtask_create *subtask = nullptr;
        status = create_subtask(stsk_ctx, &subtask);
        if (status != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to create sub task");
            delete new_task;
            return status;
        }

        new_task->subtasks.push_back(subtask);
    }
    *task = new_task;
    return DOCA_SUCCESS;
}

astraea_task *astraea_ec_task_create_as_task(astraea_ec_task_create *task) {
    astraea_task *general_task = new astraea_task;
    general_task->type = EC_CREATE;
    general_task->ec_task_create = task;
    return general_task;
}

doca_error_t astraea_ec_matrix_create(astraea_ec *ec, doca_ec_matrix_type type,
                                      size_t data_block_count,
                                      size_t rdnc_block_count,
                                      astraea_ec_matrix **matrix) {
    *matrix = new astraea_ec_matrix;
    (*matrix)->nb_data_blocks = data_block_count;
    (*matrix)->nb_rdnc_blocks = rdnc_block_count;

    doca_error_t status = doca_ec_matrix_create(
        ec->ec, type, data_block_count, rdnc_block_count, &(*matrix)->matrix);
    if (status != DOCA_SUCCESS) {
        delete *matrix;
        *matrix = nullptr;
    }

    return status;
}

doca_error_t astraea_ec_matrix_destroy(astraea_ec_matrix *matrix) {
    doca_error_t status = doca_ec_matrix_destroy(matrix->matrix);

    delete matrix;

    return status;
}
