common_host_sources = [
	'fp_context.cpp',
	'eh_context.cpp',
	'doca_application.cpp',
	'flow_steerer.cpp',
	'queue/cq_context.cpp',
	'queue/rq_context.cpp',
	'queue/sq_context.cpp',
]

common_host_inc = '.'

# Common host library created by command library
common_host_library = library(
	'common_host',
	common_host_sources,
	native: true,
	include_directories: [common_host_inc],
	dependencies: [ibverbs_dep, mlx5_dep, flexio_dep, doca_common_dep, doca_argp_dep],
)

common_host_dep = declare_dependency(include_directories: common_host_inc, link_with: common_host_library)