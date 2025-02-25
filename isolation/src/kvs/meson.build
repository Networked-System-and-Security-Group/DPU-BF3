kvs_srcs = [
	'kvs.c',
	'kvs_core.c',
	doca_dir + '/applications/common/utils.c',
	sample_dir + '/common.c',
	eth_dir + '/eth_rxq_common.c',
]

kvs_inc_dirs = [
	include_directories(ec_dir),
	include_directories(sample_dir),
	include_directories(doca_dir + '/applications/common/'),
	include_directories(doca_dir),
]

executable(
	'kvs',
	kvs_srcs,
	c_args: '-Wno-missing-braces',
	dependencies: [doca_common_dep, doca_argp_dep, doca_eth_dep, doca_flow_dep],
	include_directories: kvs_inc_dirs,
	install: false,
	native: true,
)