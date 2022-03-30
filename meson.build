project(
    'nf_boot_ctrl',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

executable(
    'nf_boot_ctrl',
    [
        'nf_boot_ctrl.cpp',
    ],
    dependencies: [
        dependency('boost'),
        dependency('sdbusplus'),
        dependency('threads'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
  input: 'xyz.openbmc_project.nf.boot.manager.service.in',
  output: 'xyz.openbmc_project.nf.boot.manager.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'))
