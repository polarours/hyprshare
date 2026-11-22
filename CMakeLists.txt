cmake_minimum_required(VERSION 3.28)

project(hyprshare VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HYPRSHARE_BUILD_TESTS "Build hyprshare tests" ON)

find_package(OpenSSL REQUIRED)

add_library(hyprshare_build_config INTERFACE)
target_compile_features(hyprshare_build_config INTERFACE cxx_std_23)
target_include_directories(hyprshare_build_config INTERFACE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(
  hyprshare_build_config
  INTERFACE
    HYPRSHARE_VERSION=\"${PROJECT_VERSION}\"
    HYPRSHARE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"
    HYPRSHARE_INSTALLED_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/hyprshare\"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    hyprshare_build_config
    INTERFACE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
      -Wold-style-cast
      -Wnon-virtual-dtor
      -Woverloaded-virtual
  )
endif()

add_library(
  hyprshare_model
  STATIC
    src/lib/model/types.cpp
)
target_link_libraries(hyprshare_model PUBLIC hyprshare_build_config)

add_library(
  hyprshare_platform_linux
  STATIC
    src/lib/platform/linux/process.cpp
    src/lib/platform/linux/paths.cpp
    src/lib/platform/linux/net.cpp
)
target_link_libraries(hyprshare_platform_linux PUBLIC hyprshare_build_config hyprshare_model)

add_library(
  hyprshare_core
  STATIC
    src/lib/core/config.cpp
    src/lib/core/backend_registry.cpp
    src/lib/core/adapter_registry.cpp
    src/lib/core/clipboard_share.cpp
    src/lib/core/discovery.cpp
    src/lib/core/file_share.cpp
    src/lib/core/inbox.cpp
    src/lib/core/inbox_history.cpp
    src/lib/core/trust_store.cpp
    src/lib/core/launcher.cpp
    src/lib/core/planner.cpp
)
target_link_libraries(
  hyprshare_core
  PUBLIC
    hyprshare_build_config
    hyprshare_model
    hyprshare_platform_linux
)

add_library(
  hyprshare_diagnostics
  STATIC
    src/lib/diagnostics/doctor.cpp
    src/lib/diagnostics/support_bundle.cpp
    src/lib/diagnostics/check_registry.cpp
    src/lib/diagnostics/checks/network.cpp
    src/lib/diagnostics/checks/environment.cpp
    src/lib/diagnostics/checks/config.cpp
    src/lib/diagnostics/network_diagnostics.cpp
)
target_link_libraries(
  hyprshare_diagnostics
  PUBLIC
    hyprshare_build_config
    hyprshare_model
    hyprshare_platform_linux
    hyprshare_core
)

add_library(
  hyprshare_daemon
  STATIC
    src/lib/daemon/ipc.cpp
    src/lib/daemon/daemon_state.cpp
    src/lib/daemon/server.cpp
)
target_link_libraries(
  hyprshare_daemon
  PUBLIC
    hyprshare_build_config
    hyprshare_model
    hyprshare_platform_linux
    hyprshare_core
)

add_library(
  hyprshare_tls
  STATIC
    src/lib/tls/tls.cpp
    src/lib/tls/cert_store.cpp
    src/lib/tls/tls_transport.cpp
)
target_link_libraries(
  hyprshare_tls
  PUBLIC
    hyprshare_build_config
    OpenSSL::SSL
    OpenSSL::Crypto
)

# HTTP/1.1 client built on the OpenSSL transport that is already part of the
# project. It is used by the cloud client and the update checker.
add_library(
  hyprshare_http
  STATIC
    src/lib/http/http_client.cpp
)
target_link_libraries(
  hyprshare_http
  PUBLIC
    hyprshare_build_config
    hyprshare_platform_linux
    hyprshare_tls
)

target_link_libraries(
  hyprshare_core
  PUBLIC
    hyprshare_tls
)

option(HYPRSHARE_HAS_PORTAL "Build with portal/PipeWire support" ON)

if(HYPRSHARE_HAS_PORTAL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDBUS_CPP REQUIRED sdbus-c++)
  pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

  add_library(
    hyprshare_portal
    STATIC
      src/lib/portal/portal_session.cpp
      src/lib/portal/pipewire_stream.cpp
  )
  target_link_libraries(
    hyprshare_portal
    PUBLIC
      hyprshare_build_config
      ${SDBUS_CPP_LIBRARIES}
      ${PIPEWIRE_LIBRARIES}
  )
  # The external headers are included as SYSTEM so that the project's strict
  # warning flags do not fire inside code that is not ours.
  target_include_directories(
    hyprshare_portal
    SYSTEM
    PUBLIC
      ${SDBUS_CPP_INCLUDE_DIRS}
      ${PIPEWIRE_INCLUDE_DIRS}
  )
  target_compile_definitions(hyprshare_build_config INTERFACE HYPRSHARE_HAS_PORTAL)
endif()

add_library(
  hyprshare_mobile
  STATIC
    src/lib/mobile/mobile_transfer.cpp
    src/lib/remote/remote_control.cpp
)
target_link_libraries(
  hyprshare_mobile
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_sync
  STATIC
    src/lib/sync/file_sync.cpp
    src/lib/sync/clipboard_sync.cpp
)
target_link_libraries(
  hyprshare_sync
  PUBLIC
    hyprshare_build_config
    hyprshare_core
)

add_library(
  hyprshare_recording
  STATIC
    src/lib/recording/screen_recorder.cpp
)
target_link_libraries(
  hyprshare_recording
  PUBLIC
    hyprshare_build_config
    hyprshare_platform_linux
)
# The capture pipeline needs the portal integration, but the encoder itself
# does not, so the dependency is only added when the portal is available.
if(HYPRSHARE_HAS_PORTAL)
  target_link_libraries(hyprshare_recording PUBLIC hyprshare_portal)
  target_compile_definitions(hyprshare_recording PUBLIC HYPRSHARE_HAS_PORTAL)
endif()

add_library(
  hyprshare_web
  STATIC
    src/lib/web/api_server.cpp
    src/lib/web/multipart.cpp
)
target_link_libraries(
  hyprshare_web
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_shell
  STATIC
    src/lib/shell/interactive_shell.cpp
)
target_link_libraries(
  hyprshare_shell
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_batch
  STATIC
    src/lib/batch/batch_runner.cpp
)
target_link_libraries(
  hyprshare_batch
  PUBLIC
    hyprshare_build_config
    hyprshare_platform_linux
)

add_library(
  hyprshare_plugin
  STATIC
    src/lib/plugin/plugin_manager.cpp
    src/lib/plugin/plugin_registry.cpp
)
target_link_libraries(
  hyprshare_plugin
  PUBLIC
    hyprshare_build_config
    ${CMAKE_DL_LIBS}
)

# Example plugin, built as a loadable module. It is the reference implementation
# of the plugin ABI and is used by the plugin tests.
add_library(
  hyprshare_example_plugin
  MODULE
    examples/plugins/hello_plugin.cpp
)
target_link_libraries(
  hyprshare_example_plugin
  PRIVATE
    hyprshare_build_config
)
target_include_directories(
  hyprshare_example_plugin
  PRIVATE
    "${CMAKE_SOURCE_DIR}/examples/plugins"
)
set_target_properties(
  hyprshare_example_plugin
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hello_plugin"
)

add_library(
  hyprshare_monitor
  STATIC
    src/lib/monitor/system_monitor.cpp
)
target_link_libraries(
  hyprshare_monitor
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_security
  STATIC
    src/lib/security/security_auditor.cpp
)
target_link_libraries(
  hyprshare_security
  PUBLIC
    hyprshare_build_config
    hyprshare_platform_linux
)

add_library(
  hyprshare_backup
  STATIC
    src/lib/backup/backup_manager.cpp
)
target_link_libraries(
  hyprshare_backup
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_perf
  STATIC
    src/lib/perf/performance.cpp
)
target_link_libraries(
  hyprshare_perf
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_log
  STATIC
    src/lib/log/logger.cpp
)
target_link_libraries(
  hyprshare_log
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_config_validator
  STATIC
    src/lib/config/config_validator.cpp
)
target_link_libraries(
  hyprshare_config_validator
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_error
  STATIC
    src/lib/error/retry_manager.cpp
)
target_link_libraries(
  hyprshare_error
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_update
  STATIC
    src/lib/update/update_checker.cpp
)
target_link_libraries(
  hyprshare_update
  PUBLIC
    hyprshare_build_config
    hyprshare_http
)

add_library(
  hyprshare_cloud
  STATIC
    src/lib/cloud/cloud_sync.cpp
)
target_link_libraries(
  hyprshare_cloud
  PUBLIC
    hyprshare_build_config
    hyprshare_http
)

add_library(
  hyprshare_profile
  STATIC
    src/lib/profile/profile_manager.cpp
)
target_link_libraries(
  hyprshare_profile
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_search
  STATIC
    src/lib/search/search_engine.cpp
)
target_link_libraries(
  hyprshare_search
  PUBLIC
    hyprshare_build_config
)

add_library(
  hyprshare_profiling
  STATIC
    src/lib/profiling/profiler.cpp
)
target_link_libraries(
  hyprshare_profiling
  PUBLIC
    hyprshare_build_config
)

add_executable(
  hyprshare
  src/bin/hyprshare/main.cpp
  src/bin/hyprshare/command_registry.cpp
  src/bin/hyprshare/command_utils.cpp
  src/bin/hyprshare/output_text.cpp
  src/bin/hyprshare/output_json.cpp
  src/bin/hyprshare/cmd_app.cpp
  src/bin/hyprshare/cmd_audit.cpp
  src/bin/hyprshare/cmd_backend.cpp
  src/bin/hyprshare/cmd_backup.cpp
  src/bin/hyprshare/cmd_batch.cpp
  src/bin/hyprshare/cmd_clipboard.cpp
  src/bin/hyprshare/cmd_clipboard_sync.cpp
  src/bin/hyprshare/cmd_cloud.cpp
  src/bin/hyprshare/cmd_compat.cpp
  src/bin/hyprshare/cmd_config.cpp
  src/bin/hyprshare/cmd_daemon.cpp
  src/bin/hyprshare/cmd_discovery.cpp
  src/bin/hyprshare/cmd_doctor.cpp
  src/bin/hyprshare/cmd_inbox.cpp
  src/bin/hyprshare/cmd_mobile.cpp
  src/bin/hyprshare/cmd_monitor.cpp
  src/bin/hyprshare/cmd_netdiag.cpp
  src/bin/hyprshare/cmd_peer.cpp
  src/bin/hyprshare/cmd_plugin.cpp
  src/bin/hyprshare/cmd_profile.cpp
  src/bin/hyprshare/cmd_profiling.cpp
  src/bin/hyprshare/cmd_record.cpp
  src/bin/hyprshare/cmd_search.cpp
  src/bin/hyprshare/cmd_session.cpp
  src/bin/hyprshare/cmd_share.cpp
  src/bin/hyprshare/cmd_shell.cpp
  src/bin/hyprshare/cmd_status.cpp
  src/bin/hyprshare/cmd_sync.cpp
  src/bin/hyprshare/cmd_tls.cpp
  src/bin/hyprshare/cmd_update.cpp
  src/bin/hyprshare/cmd_web.cpp
)
target_link_libraries(
  hyprshare
  PRIVATE
    hyprshare_build_config
    hyprshare_model
    hyprshare_platform_linux
    hyprshare_core
    hyprshare_diagnostics
    hyprshare_daemon
    hyprshare_tls
    hyprshare_mobile
    hyprshare_sync
    hyprshare_recording
    hyprshare_shell
    hyprshare_web
    hyprshare_perf
    hyprshare_batch
    hyprshare_plugin
    hyprshare_monitor
    hyprshare_security
    hyprshare_backup
    hyprshare_log
    hyprshare_config_validator
    hyprshare_error
    hyprshare_update
    hyprshare_cloud
    hyprshare_profile
    hyprshare_search
    hyprshare_profiling
)

if(HYPRSHARE_HAS_PORTAL)
  target_link_libraries(hyprshare PRIVATE hyprshare_portal)
endif()

add_executable(
  hyprshared
  src/bin/hyprshared/main.cpp
)
target_link_libraries(
  hyprshared
  PRIVATE
    hyprshare_build_config
    hyprshare_model
    hyprshare_platform_linux
    hyprshare_core
    hyprshare_daemon
)

if(HYPRSHARE_BUILD_TESTS)
  add_executable(
    hyprshare_core_tests
    tests/unit/core_tests.cpp
  )
  target_link_libraries(
    hyprshare_core_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_model
      hyprshare_platform_linux
      hyprshare_core
      hyprshare_diagnostics
  )
  add_test(NAME hyprshare_core_tests COMMAND hyprshare_core_tests)

  add_executable(
    hyprshare_tls_tests
    tests/unit/tls_tests.cpp
  )
  target_link_libraries(
    hyprshare_tls_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_tls
  )
  add_test(NAME hyprshare_tls_tests COMMAND hyprshare_tls_tests)

  add_executable(
    hyprshare_http_tests
    tests/unit/http_tests.cpp
  )
  target_link_libraries(
    hyprshare_http_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_http
      hyprshare_tls
  )
  add_test(NAME hyprshare_http_tests COMMAND hyprshare_http_tests)

  add_executable(
    hyprshare_cloud_tests
    tests/unit/cloud_tests.cpp
  )
  target_link_libraries(
    hyprshare_cloud_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_cloud
  )
  add_test(NAME hyprshare_cloud_tests COMMAND hyprshare_cloud_tests)

  add_executable(
    hyprshare_sync_tests
    tests/unit/sync_tests.cpp
  )
  target_link_libraries(
    hyprshare_sync_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_core
      hyprshare_sync
  )
  add_test(NAME hyprshare_sync_tests COMMAND hyprshare_sync_tests)

  add_executable(
    hyprshare_recording_tests
    tests/unit/recording_tests.cpp
  )
  target_link_libraries(
    hyprshare_recording_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_recording
  )
  add_test(NAME hyprshare_recording_tests COMMAND hyprshare_recording_tests)

  add_executable(
    hyprshare_monitor_tests
    tests/unit/monitor_tests.cpp
  )
  target_link_libraries(
    hyprshare_monitor_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_monitor
  )
  add_test(NAME hyprshare_monitor_tests COMMAND hyprshare_monitor_tests)

  add_executable(
    hyprshare_update_tests
    tests/unit/update_tests.cpp
  )
  target_link_libraries(
    hyprshare_update_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_update
  )
  add_test(NAME hyprshare_update_tests COMMAND hyprshare_update_tests)

  add_executable(
    hyprshare_web_tests
    tests/unit/web_tests.cpp
  )
  target_link_libraries(
    hyprshare_web_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_web
  )
  add_test(NAME hyprshare_web_tests COMMAND hyprshare_web_tests)

  add_executable(
    hyprshare_daemon_tests
    tests/unit/daemon_tests.cpp
  )
  target_link_libraries(
    hyprshare_daemon_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_daemon
      hyprshare_core
  )
  add_test(NAME hyprshare_daemon_tests COMMAND hyprshare_daemon_tests)

  add_executable(
    hyprshare_service_tests
    tests/unit/services_tests.cpp
  )
  target_link_libraries(
    hyprshare_service_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_diagnostics
      hyprshare_mobile
      hyprshare_web
      hyprshare_shell
      hyprshare_security
      hyprshare_backup
      hyprshare_perf
      hyprshare_monitor
      hyprshare_profile
  )
  add_test(NAME hyprshare_service_tests COMMAND hyprshare_service_tests)

  add_executable(
    hyprshare_cli_tests
    tests/unit/cli_dispatch_tests.cpp
  )
  target_link_libraries(
    hyprshare_cli_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_http
  )
  add_dependencies(hyprshare_cli_tests hyprshare)
  add_test(
    NAME hyprshare_cli_tests
    COMMAND hyprshare_cli_tests $<TARGET_FILE:hyprshare> $<TARGET_FILE:hyprshare_example_plugin>
  )

  add_library(
    hyprshare_no_symbol_plugin
    MODULE
      tests/fixtures/no_symbol_plugin.cpp
  )
  target_link_libraries(
    hyprshare_no_symbol_plugin
    PRIVATE
      hyprshare_build_config
  )
  set_target_properties(
    hyprshare_no_symbol_plugin
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "no_symbol_plugin"
  )

  add_executable(
    hyprshare_plugin_tests
    tests/unit/plugin_tests.cpp
  )
  target_link_libraries(
    hyprshare_plugin_tests
    PRIVATE
      hyprshare_build_config
      hyprshare_plugin
  )
  add_dependencies(
    hyprshare_plugin_tests
    hyprshare_example_plugin
    hyprshare_no_symbol_plugin
  )
  add_test(
    NAME hyprshare_plugin_tests
    COMMAND hyprshare_plugin_tests
      $<TARGET_FILE:hyprshare_example_plugin>
      $<TARGET_FILE:hyprshare_no_symbol_plugin>
  )
endif()

# ---------------------------------------------------------------------------
# Installation. The CLI package installs the binaries and their assets; the
# Tauri GUI lives in desktop/ and is packaged separately for AUR.
# ---------------------------------------------------------------------------
install(TARGETS hyprshare hyprshared
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  completions/hyprshare.bash
  DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
  RENAME hyprshare
)
install(FILES
  completions/hyprshare.zsh
  DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
  RENAME _hyprshare
)
install(FILES
  completions/hyprshare.fish
  DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
)

install(FILES
  data/systemd/hyprshare-daemon.service
  data/systemd/hyprshare-daemon.socket
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user
)

install(FILES
  README.md
  docs/api-reference.md
  docs/user-guide.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# CPack: `cpack -G DEB` from the build dir (used by CI's package job).
set(CPACK_PACKAGE_NAME "hyprshare")
set(CPACK_PACKAGE_VENDOR "polarours")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Linux-native sharing platform for Hyprland and Wayland")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_PACKAGE_CONTACT "polarours@zohomail.com")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_GENERATOR "DEB")
include(CPack)
