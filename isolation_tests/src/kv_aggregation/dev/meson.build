app_dev_srcs = ['kv_aggregation_dev.c']
app_out_name = dev_app_name + '.a'

# Device side DPA application build by meson command custom_target
# through doca_build_dpacc.sh script
kv_aggregation_dev_target = custom_target(
	'apps_' + dev_app_name,
	command: [
		build_dpacc,
		'--application',
		'--app_name', dev_app_name,
		'--srcs', '@INPUT@',
		'--dpacc_build_dir', '@OUTDIR@',
		'--additional_dpacc_options', dpacc_options,
		'--external_cc_options', ','.join(dpacc_devicecc_array),
		'--additional_include_directories', common_flexio_dev_include_dir,
		'--additional_ld_libs', common_flexio_dev_name,
		'--additional_lib_paths', common_flexio_dev_lib_path,
	],
	output: app_out_name,
	input: app_dev_srcs,
	depends: [common_flexio_dev_target],
	depend_files: shared_include_files,
)

# Create dependency from DPA application for link with host side app
kv_aggregation_device_dep = declare_dependency(link_args: join_paths(meson.current_build_dir(), app_out_name))