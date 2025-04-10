#include "../kv_aggregation_com.h"
#include "eh_context.hpp"
#include <libflexio/flexio.h>

class kv_agg_eh_context : public eh_context {
  public:
    flexio_uintptr_t kv_buf_ptr;

    /* Class functions */
    kv_agg_eh_context(flexio_process *process, uint64_t thread_id,
                      flexio_uintptr_t kv_buf_ptr);
    ~kv_agg_eh_context() override;

    flexio_status copy_app_data_to_dpa() override;
};