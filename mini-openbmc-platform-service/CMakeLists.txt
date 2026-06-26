cmake_minimum_required(VERSION 3.20)
project(mini_openbmc_platform_service VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

if(PkgConfig_FOUND)
  pkg_check_modules(HTTPLIB QUIET IMPORTED_TARGET cpp-httplib)
endif()
if(HTTPLIB_FOUND)
  add_library(mini_httplib INTERFACE)
  target_link_libraries(mini_httplib INTERFACE PkgConfig::HTTPLIB)
  add_library(httplib::httplib ALIAS mini_httplib)
else()
  find_path(HTTPLIB_INCLUDE_DIR httplib.h)
endif()
if(NOT TARGET httplib::httplib AND NOT HTTPLIB_INCLUDE_DIR)
  FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.16.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(httplib)
elseif(NOT TARGET httplib::httplib)
  add_library(httplib_header_only INTERFACE)
  target_include_directories(httplib_header_only INTERFACE ${HTTPLIB_INCLUDE_DIR})
  add_library(httplib::httplib ALIAS httplib_header_only)
endif()

if(PkgConfig_FOUND)
  pkg_check_modules(SYSTEMD QUIET IMPORTED_TARGET libsystemd)
endif()

add_library(mini_common
  libs/common/logger.cpp
  libs/sensor/threshold_event_engine.cpp)
target_include_directories(mini_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_common PUBLIC nlohmann_json::nlohmann_json Threads::Threads)

add_library(mini_platform
  libs/pcie/pci_sysfs_reader.cpp
  libs/pcie/mini_pcie_backend.cpp
  libs/pcie/mini_pcie_ioctl.cpp
  libs/hwmon/hwmon_sensor_backend.cpp)
target_include_directories(mini_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_platform PUBLIC mini_common)

add_library(mini_protocol
  libs/mctp/mctp_packet.cpp
  libs/mctp/mctp_reassembler.cpp
  libs/mctp/uds_mctp_transport.cpp
  libs/pldm/pldm_common.cpp
  libs/pldm/pldm_type0.cpp
  libs/pldm/pldm_type2.cpp
  libs/pldm/pldm_pdr.cpp)
target_include_directories(mini_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_protocol PUBLIC mini_common Threads::Threads)

add_library(mini_dbus
  libs/dbus/dbus_server.cpp
  libs/dbus/dbus_client.cpp)
target_include_directories(mini_dbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_dbus PUBLIC mini_common)
if(SYSTEMD_FOUND)
  target_compile_definitions(mini_dbus PUBLIC MINI_HAS_SYSTEMD=1)
  target_link_libraries(mini_dbus PUBLIC PkgConfig::SYSTEMD)
else()
  message(WARNING "libsystemd development files not found; building D-Bus stubs. Run scripts/install_deps.sh for the real session-bus demo.")
endif()

add_library(mini_redfish libs/redfish/redfish_mapper.cpp)
target_include_directories(mini_redfish PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_redfish PUBLIC mini_common)

function(add_mini_executable target output_dir)
  add_executable(${target} ${ARGN})
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${output_dir}")
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

add_mini_executable(pldm-endpoint-agent services/pldm-endpoint-agent
  services/pldm-endpoint-agent/main.cpp
  services/pldm-endpoint-agent/endpoint_agent.cpp
  services/pldm-endpoint-agent/gpu_endpoint.cpp
  services/pldm-endpoint-agent/nic_endpoint.cpp)
target_link_libraries(pldm-endpoint-agent PRIVATE mini_protocol mini_common)

add_mini_executable(bmc-sensor-service services/bmc-sensor-service
  services/bmc-sensor-service/main.cpp
  services/bmc-sensor-service/app.cpp
  services/bmc-sensor-service/sensor_manager.cpp)
target_link_libraries(bmc-sensor-service PRIVATE
  mini_common mini_platform mini_protocol mini_dbus mini_redfish)

add_mini_executable(redfish-service services/redfish-service
  services/redfish-service/main.cpp
  services/redfish-service/rest_server.cpp
  services/redfish-service/sensor_controller.cpp
  services/redfish-service/pcie_controller.cpp
  services/redfish-service/event_log_controller.cpp)
target_link_libraries(redfish-service PRIVATE
  mini_common mini_dbus mini_redfish httplib::httplib)

add_mini_executable(pci_scan tools tools/pci_scan.cpp)
target_link_libraries(pci_scan PRIVATE mini_platform)
add_mini_executable(dbus_dump tools tools/dbus_dump.cpp)
target_link_libraries(dbus_dump PRIVATE mini_dbus)
add_mini_executable(trace_cli tools tools/trace_cli.cpp)
target_link_libraries(trace_cli PRIVATE mini_common)
add_mini_executable(mctp_ping tools tools/mctp_ping.cpp)
target_link_libraries(mctp_ping PRIVATE mini_protocol)

if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  function(add_mini_test name source)
    add_executable(${name} ${source})
    set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE
      GTest::gtest_main mini_common mini_platform mini_protocol mini_dbus mini_redfish)
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  add_mini_test(test_dbus_mapping test/test_dbus_mapping.cpp)
  add_mini_test(test_redfish_mapper test/test_redfish_mapper.cpp)
  add_mini_test(test_pci_sysfs_reader test/test_pci_sysfs_reader.cpp)
  add_mini_test(test_mini_pcie_backend test/test_mini_pcie_backend.cpp)
  add_mini_test(test_hwmon_sensor_backend test/test_hwmon_sensor_backend.cpp)
  add_mini_test(test_mctp_packet test/test_mctp_packet.cpp)
  add_mini_test(test_mctp_fragmentation test/test_mctp_fragmentation.cpp)
  add_mini_test(test_pldm_type0 test/test_pldm_type0.cpp)
  add_mini_test(test_pldm_type2 test/test_pldm_type2.cpp)
  add_mini_test(test_threshold_event_engine test/test_threshold_event_engine.cpp)
  add_mini_test(test_e2e_redfish_dbus_sensor test/test_e2e_redfish_dbus_sensor.cpp)
endif()
