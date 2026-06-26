cmake_minimum_required(VERSION 3.22)

project(openbmc-ai-server-management-stack VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GoogleTest)

# 專案使用的第三方套件集中在這裡宣告，建置失敗時可先從這一段確認必要套件是否已安裝。
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

# 核心邏輯做成 library，讓主程式與單元測試共用同一份實作。
add_library(openbmc_ai_stack_core
    src/app/Application.cpp
    src/common/TimeUtils.cpp
    src/hardware/AIServerProfile.cpp
    src/hardware/HardwareModel.cpp
    src/services/EventLogger.cpp
    src/services/ThermalManager.cpp
    src/services/PowerManager.cpp
    src/services/HealthMonitor.cpp
    src/services/SensorService.cpp
    src/services/FaultInjectionManager.cpp
    src/services/FirmwareUpdateManager.cpp
    src/services/ManagementService.cpp
    src/dbus/DbusBridge.cpp
    src/redfish/RedfishApiServer.cpp
)

# 對外只暴露 src/ 作為 include 根目錄，避免測試或主程式寫出過長的 include path。
target_include_directories(openbmc_ai_stack_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 保持基本警告開啟，讓型別轉換、未使用變數與可疑寫法在開發階段就被看到。
target_compile_options(openbmc_ai_stack_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# sd-bus 由 pkg-config 匯入，其餘套件使用 CMake package target。
target_link_libraries(openbmc_ai_stack_core
    PUBLIC
        Boost::system
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        Threads::Threads
        PkgConfig::SYSTEMD
)

# 主程式只負責啟動 Application，核心行為都在 openbmc_ai_stack_core。
add_executable(openbmc-ai-server-management-stack
    src/main.cpp
)

# 輸出檔名固定為 ai-bmc-manager，讓 scripts/03_run.sh 可以用穩定路徑啟動。
set_target_properties(openbmc-ai-server-management-stack
    PROPERTIES
        OUTPUT_NAME ai-bmc-manager
)

target_link_libraries(openbmc-ai-server-management-stack
    PRIVATE
        openbmc_ai_stack_core
)

# 測試執行檔直接連核心 library，避免測試用到和主程式不同的實作。
add_executable(openbmc_ai_stack_tests
    tests/ProfileParsingTest.cpp
    tests/ThermalManagerTest.cpp
    tests/PowerManagerTest.cpp
    tests/EventLoggerTest.cpp
    tests/FaultInjectionManagerTest.cpp
)

target_include_directories(openbmc_ai_stack_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(openbmc_ai_stack_tests
    PRIVATE
        openbmc_ai_stack_core
        GTest::gtest_main
)

# 交給 GoogleTest 自動探索測試案例，ctest 可直接列出每個 TEST。
gtest_discover_tests(openbmc_ai_stack_tests)
