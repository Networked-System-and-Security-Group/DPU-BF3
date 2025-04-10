common_flexio_dev_sources = ['com_dev.c']

common_flexio_dev_name = 'commondev'
common_flexio_dev_full_name = 'lib' + common_flexio_dev_name + '.a'
dpacc_mode = '--library'
common_flexio_dev_include_dir = meson.current_source_dir()
common_flexio_dev_lib_path = meson.current_build_dir()

common_flexio_dev_includes = [join_paths(common_flexio_dev_include_dir, 'com_dev.h')]

# Device side DPA library build by meson command custom_target
# through doca_build_dpacc.sh script
common_flexio_dev_target = custom_target(
	'archive_' + common_flexio_dev_name,
	command: [
		build_dpacc,
		'--library',
		'--archive_name', '@OUTPUT@',
		'--external_cc_options', ','.join(dpacc_devicecc_array),
		'--additional_dpacc_options', dpacc_options,
		'--srcs', '@INPUT@',
	],
	output: common_flexio_dev_full_name,
	input: common_flexio_dev_sources,
	depend_files: common_flexio_dev_includes,
)