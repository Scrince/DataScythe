add_library(datascythe_core STATIC
    core/logger.cpp
    core/sha256.cpp
    core/pattern_generator.cpp
    core/pass_scheduler.cpp
    core/path_collector.cpp
    core/erase_engine.cpp
    core/drive_clone_engine.cpp
    core/preflight_format.cpp
    core/certificate.cpp
    core/clone_report.cpp
    core/partition_table.cpp
    core/verification.cpp
    platform/platform_factory.cpp
    platform/ads_enumerator_stub.cpp
)

target_include_directories(datascythe_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    target_sources(datascythe_core PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/windows/drive_enumerator.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/drive_analyzer.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/raw_device.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/volume_manager.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/secure_erase.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/preflight.cpp
        ${CMAKE_SOURCE_DIR}/platform/windows/ads_enumerator.cpp
    )
    target_compile_definitions(datascythe_core PRIVATE DATASCYTHE_HAS_WINDOWS_ADS=1)
    target_compile_definitions(datascythe_core PUBLIC DATASCYTHE_PLATFORM_WINDOWS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(datascythe_core PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/linux/drive_enumerator.cpp
        ${CMAKE_SOURCE_DIR}/platform/linux/drive_analyzer.cpp
        ${CMAKE_SOURCE_DIR}/platform/linux/raw_device.cpp
        ${CMAKE_SOURCE_DIR}/platform/linux/volume_manager.cpp
        ${CMAKE_SOURCE_DIR}/platform/linux/secure_erase.cpp
        ${CMAKE_SOURCE_DIR}/platform/linux/preflight.cpp
    )
    target_compile_definitions(datascythe_core PUBLIC DATASCYTHE_PLATFORM_LINUX=1)
elseif(APPLE)
    target_sources(datascythe_core PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/macos/drive_enumerator.cpp
        ${CMAKE_SOURCE_DIR}/platform/macos/drive_analyzer.cpp
        ${CMAKE_SOURCE_DIR}/platform/macos/raw_device.cpp
        ${CMAKE_SOURCE_DIR}/platform/macos/volume_manager.cpp
        ${CMAKE_SOURCE_DIR}/platform/macos/secure_erase.cpp
        ${CMAKE_SOURCE_DIR}/platform/macos/preflight.cpp
    )
    target_compile_definitions(datascythe_core PUBLIC DATASCYTHE_PLATFORM_MACOS=1)
endif()

if(WIN32)
    target_link_libraries(datascythe_core PUBLIC setupapi)
endif()
