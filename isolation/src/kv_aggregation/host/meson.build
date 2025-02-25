executable(
	'kv_aggregation',
	['kv_aggregation_host.cpp', 'kv_agg_eh_context.cpp'],
	cpp_args: '-DDEV_APP_NAME=' + dev_app_name,
	native: true,
	dependencies: [
		ibverbs_dep,
		mlx5_dep,
		flexio_dep,
		common_flexio_host_dep,
		kv_aggregation_device_dep,
	],
	link_depends: kv_aggregation_dev_target,
)