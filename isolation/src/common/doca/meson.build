common_doca_sources = ['doca_application.cpp', 'eth_application.cpp', 'utils.cpp']

common_doca_inc = '.'

# Common host library created by command library
common_doca_library = library(
    'common_doca',
    common_doca_sources,
    native: true,
    include_directories: [common_doca_inc],
    dependencies: [doca_common_dep, doca_argp_dep, doca_eth_dep],
)

common_doca_dep = declare_dependency(include_directories: common_doca_inc, link_with: common_doca_library)