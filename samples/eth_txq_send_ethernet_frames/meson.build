project('DOCA_SAMPLE', 'C', 'CPP',
	# Get version number from file.
	version: run_command(find_program('cat'),
		files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
	license: 'Proprietary',
	default_options: ['buildtype=debug'],
	meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'eth_txq_send_ethernet_frames'

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

sample_dependencies = []
# Required for all DOCA programs
sample_dependencies += dependency('doca-common')
# The DOCA library of the sample itself
sample_dependencies += dependency('doca-eth')
# Utility DOCA library for executables
sample_dependencies += dependency('doca-argp')
# Additional DOCA library that is relevant for this sample
sample_dependencies += dependency('doca-flow')

doca_dir = '/opt/mellanox/doca'
sample_dir = doca_dir + '/samples'
eth_dir = sample_dir + '/doca_eth'

sample_srcs = [
	# The sample itself
	SAMPLE_NAME + '_sample.c',
	# Main function for the sample's executable
	SAMPLE_NAME + '_main.c',
	# Common code for the DOCA library samples
	eth_dir + '/eth_common.c',
	# Common code for all DOCA samples
	sample_dir + '/common.c',
]

sample_inc_dirs  = []
# Common DOCA library logic
sample_inc_dirs += include_directories(eth_dir)
# Common DOCA logic (samples)
sample_inc_dirs += include_directories(sample_dir)
# Common DOCA logic
# sample_inc_dirs += include_directories('../../..')
# Common DOCA logic (applications)
sample_inc_dirs += include_directories(doca_dir + '/applications/common/')

executable('doca_' + SAMPLE_NAME, sample_srcs,
	c_args : '-Wno-missing-braces',
	dependencies : sample_dependencies,
	include_directories: sample_inc_dirs,
	install: false)
