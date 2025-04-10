common_flexio_host_sources = [
	'fp_context.cpp',
	'eh_context.cpp',
	'flow_steerer.cpp',
	'queue/cq_context.cpp',
	'queue/rq_context.cpp',
	'queue/sq_context.cpp',
]

common_flexio_host_inc = '.'

# Common host library created by command library
common_flexio_host_library = library(
	'common_host',
	common_flexio_host_sources,
	native: true,
	include_directories: [common_flexio_host_inc],
	dependencies: [ibverbs_dep, mlx5_dep, flexio_dep, doca_common_dep, doca_argp_dep],
)

common_flexio_host_dep = declare_dependency(
	include_directories: common_flexio_host_inc,
	link_with: common_flexio_host_library,
)