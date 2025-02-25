#include "fs_application.hpp"
#include "eth_application.hpp"
#include <doca_error.h>

fs_application::fs_application() : eth_application("flow_statistic") {}

fs_application::~fs_application() {}

doca_error_t fs_application::register_params() {
	return DOCA_SUCCESS;
}
