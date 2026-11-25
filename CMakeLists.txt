
cmake_minimum_required(VERSION 3.20)

project(USBIndependent
    VERSION 0.1.0
    DESCRIPTION "Independent USB Audio Synchronization Bridge"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)

add_executable(usb_independent
    src/main.cpp
    src/alsa/alsa_device.cpp
    src/alsa/alsa_capture_worker.cpp
    src/asrc/asrc_processor.cpp
    src/asrc/asrc_worker.cpp
    src/asrc/asrc_ratio_controller.cpp
)

target_include_directories(usb_independent
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(usb_independent
    PRIVATE
        PkgConfig::ALSA
        PkgConfig::JACK
        PkgConfig::SAMPLERATE
        PkgConfig::UDEV
)

target_compile_options(usb_independent
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Continuous USB audio input runtime
add_executable(usb_independent_runtime
    src/input_runtime.cpp
    src/alsa/alsa_device.cpp
    src/alsa/alsa_capture_worker.cpp
    src/asrc/asrc_processor.cpp
    src/asrc/asrc_worker.cpp
    src/asrc/asrc_ratio_controller.cpp
    src/jack/jack_output.cpp
)

target_include_directories(usb_independent_runtime
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(usb_independent_runtime
    PRIVATE
        PkgConfig::ALSA
        PkgConfig::JACK
        PkgConfig::SAMPLERATE
        PkgConfig::UDEV
)

target_compile_options(usb_independent_runtime
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# SPSC Ring Buffer Unit Test
add_executable(test_spsc_ring_buffer
    tests/test_spsc_ring_buffer.cpp
)

# ASRC Unit Test
add_executable(test_asrc_processor
    tests/test_asrc_processor.cpp
    src/asrc/asrc_processor.cpp
)

target_include_directories(test_asrc_processor
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_asrc_processor
    PRIVATE
        PkgConfig::SAMPLERATE
)

target_compile_options(test_asrc_processor
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME ASRC_Processor
    COMMAND test_asrc_processor
)

target_include_directories(test_spsc_ring_buffer
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(test_spsc_ring_buffer
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ASRC Ratio Controller Unit Test
add_executable(test_asrc_ratio_controller
    tests/test_asrc_ratio_controller.cpp
    src/asrc/asrc_ratio_controller.cpp
)

target_include_directories(test_asrc_ratio_controller
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(test_asrc_ratio_controller
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ASRC Worker Integration Test
add_executable(test_asrc_worker
    tests/test_asrc_worker.cpp
    src/asrc/asrc_processor.cpp
    src/asrc/asrc_worker.cpp
    src/asrc/asrc_ratio_controller.cpp
)

target_include_directories(test_asrc_worker
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_asrc_worker
    PRIVATE
        PkgConfig::SAMPLERATE
)

target_compile_options(test_asrc_worker
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Physical ALSA + ASRC Integration Test
add_executable(test_alsa_asrc_integration
    tests/test_alsa_asrc_integration.cpp
    src/alsa/alsa_device.cpp
    src/alsa/alsa_capture_worker.cpp
    src/asrc/asrc_processor.cpp
    src/asrc/asrc_worker.cpp
    src/jack/jack_output.cpp
    src/asrc/asrc_ratio_controller.cpp
)

target_include_directories(test_alsa_asrc_integration
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_alsa_asrc_integration
    PRIVATE
        PkgConfig::ALSA
        PkgConfig::JACK
        PkgConfig::UDEV
        PkgConfig::SAMPLERATE
)

target_compile_options(test_alsa_asrc_integration
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

enable_testing()

add_test(
    NAME ASRC_Ratio_Controller
    COMMAND test_asrc_ratio_controller
)

add_test(
    NAME SPSC_Ring_Buffer
    COMMAND test_spsc_ring_buffer
)

add_test(
    NAME ASRC_Worker
    COMMAND test_asrc_worker
)

install(TARGETS usb_independent
    RUNTIME DESTINATION bin
)
