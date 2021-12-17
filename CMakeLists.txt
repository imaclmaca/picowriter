cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Define the project name and type
project(picowriter-project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The tinyusb API changed a bit, we use the later version now
if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

# Extra compiler settings
add_compile_options(-Os -fwrapv -Wall )

# Say what exe we are trying to build
add_executable(picowriter
# source files needed are:
                kb-main.c
                usb-stack.c
                usb_descriptors.c
        )

# For testing, we echo a lot of stuff to the serial console (output only). Will probably be removed in due course!
pico_enable_stdio_uart(picowriter 1)

# Where do we need to look to find stuff?
target_include_directories(picowriter PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico_stdlib which aggregates commonly used features
target_link_libraries(picowriter PRIVATE pico_stdlib pico_multicore tinyusb_device tinyusb_board)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(picowriter)

# add url via pico_set_program_url
# example_auto_set_url(picowriter)
