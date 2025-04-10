ec_srcs = [
	'ec_main.c',
	'ec_core.c',
	sample_dir + '/common.c',
	doca_dir + '/applications/common/utils.c',
]

ec_inc_dirs = [
	include_directories(ec_dir),
	include_directories(sample_dir),
	include_directories(doca_dir + '/applications/common/'),
]
executable(
	'ec_recover',
	ec_srcs,
	c_args: '-Wno-missing-braces',
	dependencies: [doca_ec_dep, doca_common_dep, doca_argp_dep],
	include_directories: ec_inc_dirs,
	install: false,
	native: true,
)