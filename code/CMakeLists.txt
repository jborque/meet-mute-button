cmake_minimum_required(VERSION 3.17)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")

include(pico_sdk_import.cmake)

project(mute_button)

pico_sdk_init()

add_compile_options(-Wall)

add_subdirectory(RP2040-Button button)
add_subdirectory(RP2040-Rotary-Encoder pico_rotary_encoder)

add_executable(mute_button src/mute_button.cc src/tinyusb_stuff.cc src/our_descriptor.cc src/me.cc src/ws2812.cc)

pico_generate_pio_header(mute_button ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

# Add a compile definition for debugging based on the build type.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(mute_button PRIVATE SERIAL_DEBUG=1)
endif()

pico_enable_stdio_usb(mute_button 0)
pico_enable_stdio_uart(mute_button 1)

target_include_directories(mute_button PRIVATE src)

target_link_libraries(mute_button pico_stdlib pico_unique_id hardware_pio hardware_pwm tinyusb_device tinyusb_board pico_rotary_encoder button)

pico_add_extra_outputs(mute_button)
