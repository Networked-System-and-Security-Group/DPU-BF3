#include "flow_steerer.hpp"
#include "libflexio/flexio.h"
#include <cstddef>
#include <cstdio>
#include <infiniband/mlx5dv.h>
#include <infiniband/verbs.h>
#include <malloc.h>
#include <utility>
#include <vector>

flow_steerer::flow_steerer(ibv_context *ibv_ctx)
    : ibv_ctx(ibv_ctx), rx_domain(nullptr), rx_root_table(nullptr),
      rx_root_matcher(nullptr), tx_domain(nullptr), tx_root_table(nullptr),
      tx_sws_table(nullptr), tx_root_matcher(nullptr), tx_sws_matcher(nullptr),
      rule_action_pairs(nullptr) {
    rule_action_pairs =
        new std::vector<std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *> *>();
}

flow_steerer::~flow_steerer() {
    for (std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *> *ra_pair :
         *rule_action_pairs) {
        mlx5dv_dr_rule *rule = ra_pair->first;
        if (rule != nullptr && mlx5dv_dr_rule_destroy(rule)) {
            printf("Failed to destroy rule\n");
        }

        mlx5dv_dr_action *action = ra_pair->second;
        if (action != nullptr && mlx5dv_dr_action_destroy(action)) {
            printf("Failed to destroy action\n");
        }
        delete ra_pair;
    }
    delete rule_action_pairs;

    if (rx_root_matcher != nullptr &&
        mlx5dv_dr_matcher_destroy(rx_root_matcher)) {
        printf("Failed to destroy rx root matcher\n");
    }
    if (rx_root_table != nullptr && mlx5dv_dr_table_destroy(rx_root_table)) {
        printf("Failed to destroy rx root table\n");
    }
    if (rx_domain != nullptr && mlx5dv_dr_domain_destroy(rx_domain)) {
        printf("Failed to destroy rx domain\n");
    }

    if (tx_root_matcher != nullptr &&
        mlx5dv_dr_matcher_destroy(tx_root_matcher)) {
        printf("Failed to destroy tx root matcher\n");
    }
    if (tx_root_table != nullptr && mlx5dv_dr_table_destroy(tx_root_table)) {
        printf("Failed to destroy rx root table\n");
    }

    if (tx_sws_matcher != nullptr &&
        mlx5dv_dr_matcher_destroy(tx_sws_matcher)) {
        printf("Failed to destroy tx sws matcher\n");
    }
    if (tx_sws_table != nullptr && mlx5dv_dr_table_destroy(tx_sws_table)) {
        printf("Failed to destroy rx root table\n");
    }

    if (tx_domain != nullptr && mlx5dv_dr_domain_destroy(tx_domain)) {
        printf("Failed to destroy tx domain\n");
    }
}

flexio_status flow_steerer::create_shared_objs() {
    if (create_shared_rx_objs() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create shared rx objs\n");
        return FLEXIO_STATUS_FAILED;
    }
    if (create_shared_tx_objs() != FLEXIO_STATUS_SUCCESS) {
        printf("Failed to create shared tx objs\n");
        return FLEXIO_STATUS_FAILED;
    }
    return FLEXIO_STATUS_SUCCESS;
}

flexio_status flow_steerer::create_rule_rx_mac_match(mlx5dv_devx_obj *tir_obj,
                                                     uint64_t dmac) {
    struct mlx5dv_flow_match_parameters *match_value;
    int match_value_size;

    /* mask & match value */
    match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
    match_value =
        (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
    if (match_value == nullptr) {
        printf("Failed to alloc mem for match_value\n");
        return FLEXIO_STATUS_FAILED;
    }

    match_value->match_sz = MATCH_VAL_BSIZE;
    DEVX_SET(dr_match_spec, match_value->match_buf, dmac_47_16, dmac >> 16);
    DEVX_SET(dr_match_spec, match_value->match_buf, dmac_15_0,
             dmac % (1 << 16));

    mlx5dv_dr_action *action = mlx5dv_dr_action_create_dest_devx_tir(tir_obj);
    if (action == nullptr) {
        printf("Failed creating TIR action (errno %d).\n", errno);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }
    struct mlx5dv_dr_action *actions[1];
    actions[0] = action;

    mlx5dv_dr_rule *rule =
        mlx5dv_dr_rule_create(rx_root_matcher, match_value, 1, actions);

    if (rule == nullptr) {
        printf("Fail creating dr_rule (errno %d).\n", errno);
        mlx5dv_dr_action_destroy(action);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }

    free(match_value);

    rule_action_pairs->push_back(
        new std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *>(rule, action));

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status flow_steerer::create_rule_tx_fwd_to_sws_table(uint64_t smac) {
    struct mlx5dv_flow_match_parameters *match_value;
    int match_value_size;

    /* mask & match value */
    match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
    match_value =
        (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
    if (match_value == nullptr) {
        printf("Failed to alloc mem for match_value\n");
        return FLEXIO_STATUS_FAILED;
    }

    match_value->match_sz = MATCH_VAL_BSIZE;
    DEVX_SET(dr_match_spec, match_value->match_buf, smac_47_16, smac >> 16);
    DEVX_SET(dr_match_spec, match_value->match_buf, smac_15_0,
             smac % (1 << 16));

    mlx5dv_dr_action *actions[1];
    mlx5dv_dr_action *action = mlx5dv_dr_action_create_dest_table(tx_sws_table);
    if (action == nullptr) {
        printf("Failed creating dest SWS table action (errno %d).\n", errno);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }
    actions[0] = action;

    mlx5dv_dr_rule *rule =
        mlx5dv_dr_rule_create(tx_root_matcher, match_value, 1, actions);
    if (rule == nullptr) {
        printf("Fail creating dr_rule (errno %d).\n", errno);
        mlx5dv_dr_action_destroy(action);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }

    free(match_value);

    rule_action_pairs->push_back(
        new std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *>(rule, action));

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status flow_steerer::create_rule_tx_fwd_to_vport(uint64_t smac) {
    struct mlx5dv_flow_match_parameters *match_value;
    int match_value_size;

    /* mask & match value */
    match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
    match_value =
        (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
    if (match_value == nullptr) {
        printf("Failed to alloc mem for match_value\n");
        return FLEXIO_STATUS_FAILED;
    }

    match_value->match_sz = MATCH_VAL_BSIZE;
    DEVX_SET(dr_match_spec, match_value->match_buf, smac_47_16, smac >> 16);
    DEVX_SET(dr_match_spec, match_value->match_buf, smac_15_0,
             smac % (1 << 16));

    struct mlx5dv_dr_action *actions[1];
    struct mlx5dv_dr_action *action =
        mlx5dv_dr_action_create_dest_vport(tx_domain, 0xFFFF);
    ;
    if (action == nullptr) {
        printf("Failed creating dest vport action (errno %d).\n", errno);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }
    actions[0] = action;

    mlx5dv_dr_rule *rule =
        mlx5dv_dr_rule_create(tx_sws_matcher, match_value, 1, actions);
    if (rule == nullptr) {
        printf("Fail creating dr_rule (errno %d).\n", errno);
        mlx5dv_dr_action_destroy(action);
        free(match_value);
        return FLEXIO_STATUS_FAILED;
    }

    free(match_value);

    rule_action_pairs->push_back(
        new std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *>(rule, action));

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status flow_steerer::create_shared_rx_objs() {
    rx_domain = mlx5dv_dr_domain_create(ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_NIC_RX);
    if (rx_domain == nullptr) {
        printf("Fail creating dr_domain (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    rx_root_table = mlx5dv_dr_table_create(rx_domain, 0);
    if (rx_root_table == nullptr) {
        printf("Fail creating dr_table (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    mlx5dv_flow_match_parameters *match_mask = nullptr;
    int match_mask_size;

    /* mask & match value */
    match_mask_size = sizeof(*match_mask) + MATCH_VAL_BSIZE;
    match_mask = (mlx5dv_flow_match_parameters *)calloc(1, match_mask_size);
    if (match_mask == nullptr) {
        printf("Failed to alloc mem for match_mask\n");
        return FLEXIO_STATUS_FAILED;
    }

    match_mask->match_sz = MATCH_VAL_BSIZE;
    DEVX_SET(dr_match_spec, match_mask->match_buf, dmac_47_16, 0xffffffff);
    DEVX_SET(dr_match_spec, match_mask->match_buf, dmac_15_0, 0xffff);

    rx_root_matcher = mlx5dv_dr_matcher_create(
        rx_root_table, 0, MATCHER_CRITERIA_OUTER, match_mask);
    if (rx_root_matcher == nullptr) {
        printf("Fail creating dr_matcher (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    free(match_mask);

    return FLEXIO_STATUS_SUCCESS;
}

flexio_status flow_steerer::create_shared_tx_objs() {
    tx_domain = mlx5dv_dr_domain_create(ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_FDB);
    if (tx_domain == nullptr) {
        printf("Fail creating dr_domain (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    tx_root_table = mlx5dv_dr_table_create(tx_domain, 0);
    if (tx_root_table == nullptr) {
        printf("Fail creating dr_table_root (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    tx_sws_table = mlx5dv_dr_table_create(tx_domain, 1);
    if (tx_sws_table == nullptr) {

        printf("Fail creating dr_table_sws (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    struct mlx5dv_flow_match_parameters *match_mask;
    int match_mask_size;

    /* mask & match value */
    match_mask_size = sizeof(*match_mask) + MATCH_VAL_BSIZE;
    match_mask =
        (struct mlx5dv_flow_match_parameters *)calloc(1, match_mask_size);
    if (match_mask == nullptr) {
        printf("Failed to alloc mem for match_mask\n");
        return FLEXIO_STATUS_FAILED;
    }

    match_mask->match_sz = MATCH_VAL_BSIZE;
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_47_16, 0xffffffff);
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_15_0, 0xffff);

    tx_root_matcher = mlx5dv_dr_matcher_create(
        tx_root_table, 0, MATCHER_CRITERIA_OUTER, match_mask);
    if (tx_root_matcher == nullptr) {
        printf("Fail creating dr_matcher_root (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    tx_sws_matcher = mlx5dv_dr_matcher_create(
        tx_sws_table, 0, MATCHER_CRITERIA_OUTER, match_mask);
    if (tx_sws_matcher == nullptr) {
        printf("Fail creating dr_matcher_sws (errno %d)\n", errno);
        return FLEXIO_STATUS_FAILED;
    }

    free(match_mask);

    return FLEXIO_STATUS_SUCCESS;
}