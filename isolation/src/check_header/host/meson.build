executable(
	'check_header',
	['check_header_host.cpp'],
	cpp_args: '-DDEV_APP_NAME=' + dev_app_name,
	native: true,
	dependencies: [
		ibverbs_dep,
		mlx5_dep,
		flexio_dep,
		common_flexio_host_dep,
		check_header_device_dep,
	],
	link_depends: check_header_dev_target,
)