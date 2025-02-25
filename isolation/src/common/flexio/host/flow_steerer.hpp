#ifndef __STEERER_H__
#define __STEERER_H__

#include "libflexio/flexio.h"
#include <cstdint>
#include <infiniband/mlx5dv.h>
#include <infiniband/verbs.h>
#include <utility>
#include <vector>

/* Every usage of this value is in bytes */
#define MATCH_VAL_BSIZE 64

enum matcher_criteria {
    MATCHER_CRITERIA_EMPTY = 0,
    MATCHER_CRITERIA_OUTER = 1 << 0,
    MATCHER_CRITERIA_MISC = 1 << 1,
    MATCHER_CRITERIA_INNER = 1 << 2,
    MATCHER_CRITERIA_MISC2 = 1 << 3,
    MATCHER_CRITERIA_MISC3 = 1 << 4,
};

struct mlx5_ifc_dr_match_spec_bits {
    uint8_t smac_47_16[0x20];

    uint8_t smac_15_0[0x10];
    uint8_t ethertype[0x10];

    uint8_t dmac_47_16[0x20];

    uint8_t dmac_15_0[0x10];
    uint8_t first_prio[0x3];
    uint8_t first_cfi[0x1];
    uint8_t first_vid[0xc];

    uint8_t ip_protocol[0x8];
    uint8_t ip_dscp[0x6];
    uint8_t ip_ecn[0x2];
    uint8_t cvlan_tag[0x1];
    uint8_t svlan_tag[0x1];
    uint8_t frag[0x1];
    uint8_t ip_version[0x4];
    uint8_t tcp_flags[0x9];

    uint8_t tcp_sport[0x10];
    uint8_t tcp_dport[0x10];

    uint8_t reserved_at_c0[0x18];
    uint8_t ip_ttl_hoplimit[0x8];

    uint8_t udp_sport[0x10];
    uint8_t udp_dport[0x10];

    uint8_t src_ip_127_96[0x20];

    uint8_t src_ip_95_64[0x20];

    uint8_t src_ip_63_32[0x20];

    uint8_t src_ip_31_0[0x20];

    uint8_t dst_ip_127_96[0x20];

    uint8_t dst_ip_95_64[0x20];

    uint8_t dst_ip_63_32[0x20];

    uint8_t dst_ip_31_0[0x20];
};

class flow_steerer {
  public:
    ibv_context *ibv_ctx;

    mlx5dv_dr_domain *rx_domain;
    mlx5dv_dr_table *rx_root_table;
    mlx5dv_dr_matcher *rx_root_matcher;

    mlx5dv_dr_domain *tx_domain;
    mlx5dv_dr_table *tx_root_table;
    mlx5dv_dr_table *tx_sws_table;
    mlx5dv_dr_matcher *tx_root_matcher;
    mlx5dv_dr_matcher *tx_sws_matcher;

    std::vector<std::pair<mlx5dv_dr_rule *, mlx5dv_dr_action *> *>
        *rule_action_pairs;

    /* Class functions */
    flow_steerer(ibv_context *ibv_ctx);
    ~flow_steerer();

    flexio_status create_shared_objs();
    flexio_status create_rule_rx_mac_match(mlx5dv_devx_obj *tir_obj,
                                           uint64_t dmac);
    flexio_status create_rule_tx_fwd_to_sws_table(uint64_t smac);
    flexio_status create_rule_tx_fwd_to_vport(uint64_t smac);

  private:
    flexio_status create_shared_rx_objs();
    flexio_status create_shared_tx_objs();
};

#endif