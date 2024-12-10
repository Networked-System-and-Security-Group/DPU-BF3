project('DOCA_SAMPLE', 'C', 'CPP',
	# Get version number from file.
	version: run_command(find_program('cat'),
		files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
	license: 'Proprietary',
	default_options: ['buildtype=debug'],
	meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'flow_drop'

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

sample_dependencies = []
# Required for all DOCA programs
sample_dependencies += dependency('doca-common')
# The DOCA library of the sample itself
sample_dependencies += dependency('doca-flow')
# Utility DOCA library for executables
sample_dependencies += dependency('doca-argp')
# Required DOCA Driver
sample_dependencies += dependency('libdpdk')

doca_dir = '/opt/mellanox/doca'
sample_dir = doca_dir + '/samples'
flow_dir = sample_dir + '/doca_flow'

sample_srcs = [
	# The sample itself
	SAMPLE_NAME + '_sample.c',
	# Main function for the sample's executable
	SAMPLE_NAME + '_main.c',
	# Common code for the DOCA library samples
	flow_dir + '/flow_common.c',
	# Common code for all DOCA applications
	doca_dir + '/applications/common/dpdk_utils.c',
]

sample_inc_dirs  = []
# Common DOCA library logic
sample_inc_dirs += include_directories(flow_dir)
# Common DOCA logic (applications)
sample_inc_dirs += include_directories(doca_dir + '/applications/common/')

executable('doca_' + SAMPLE_NAME, sample_srcs,
	c_args : '-Wno-missing-braces',
	dependencies : sample_dependencies,
	include_directories: sample_inc_dirs,
	install: false)
