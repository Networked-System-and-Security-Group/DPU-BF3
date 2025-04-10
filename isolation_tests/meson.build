project('isolation', 'cpp', 'c', version: '0.1.0')

# Dependencies for host side code
ibverbs_dep = dependency('libibverbs', required: true)
mlx5_dep = dependency('libmlx5', required: true)
flexio_dep = dependency('libflexio', required: true)
dpdk_dep = dependency('libdpdk', required: true)

doca_common_dep = dependency('doca-common', required: true)
doca_argp_dep = dependency('doca-argp', required: true)
doca_ec_dep = dependency('doca-erasure-coding', required: true)
doca_flow_dep = dependency('doca-flow', required: true)
doca_eth_dep = dependency('doca-eth', required: true)

add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'], native: true)

# Get program for build device side library and applications
build_dpacc = find_program('doca_build_dpacc.sh', required: true)

# List of arguments that should be pass to build_dpacc
dpacc_devicecc_array = [
	'-DE_MODE_LE',
	'-Wall',
	'-Wextra',
	'-Wpedantic',
	'-Wdouble-promotion',
	'-Wno-empty-translation-unit',
	'-Wmissing-prototypes',
	'-Wstrict-prototypes',
	'-ffreestanding',
	'-mcmodel=medany',
	# for select include files in device/host shared srcs
	'-DON_DEVICE',
]

# Set optimization type for device side libraries and applications
# according of optimization type of host side
meson_build_type = get_option('buildtype')

dpacc_options = ['-mcpu=nv-dpa-bf3']

if (meson_build_type == 'debug')
	dpacc_devicecc_array += ['-g', '-O0', '-gdwarf-4']
elif (meson_build_type == 'debugoptimized' or meson_build_type == '')
	dpacc_devicecc_array += ['-g', '-O2', '-gdwarf-4']
	dpacc_options += ['-flto']
elif (meson_build_type == 'release')
	dpacc_devicecc_array += ['-O3']
	dpacc_options += ['-flto']
else
	error('Unsupported build mode ', meson_build_type)
endif

doca_dir = '/opt/mellanox/doca'
sample_dir = doca_dir + '/samples'
ec_dir = sample_dir + '/doca_erasure_coding'
eth_dir = sample_dir + '/doca_eth'

# List of the samples
apps = [
	'src/kv_aggregation',
	'src/check_header',
	'src/erasure_coding',
	'src/flow_statistic',
	'src/counter',
	'src/kvs',
]

# Build common host and device side libraries
subdir('src/common')

# Build the samples
foreach app : apps
	subdir(app)
endforeach