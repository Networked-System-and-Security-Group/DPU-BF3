SAMPLE_NAME = 'erasure_coding_recover'
sample_srcs = [
    # The sample itself
    SAMPLE_NAME
    + '_sample.c',
    # Main function for the sample's executable
    SAMPLE_NAME
    + '_main.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

sample_inc_dirs = []
# Common DOCA logic (samples)
sample_inc_dirs += include_directories('/opt/mellanox/doca/samples')
# Common DOCA logic
sample_inc_dirs += include_directories('/opt/mellanox/doca')
# Common DOCA logic (applications)
sample_inc_dirs += include_directories('/opt/mellanox/doca/applications/common/')

executable(
    'erasure_coding',
    sample_srcs,
    native: true,
    include_directories: sample_inc_dirs,
    dependencies: [doca_common_dep, doca_argp_dep, doca_ec_dep],
)