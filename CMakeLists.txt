cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Atlas)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
option(ATLAS_BRINGUP "Build the non-actuating PCB diagnostic application" OFF)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/Src/atlas_adxl375.c
    App/Src/atlas_analog.c
    App/Src/atlas_ble.c
    App/Src/atlas_bno085.c
    App/Src/atlas_board.c
    App/Src/atlas_bringup.c
    App/Src/atlas_bringup_protocol.c
    App/Src/atlas_buzzer.c
    App/Src/atlas_gnss.c
    App/Src/atlas_expansion.c
    App/Src/atlas_io.c
    App/Src/atlas_led.c
    App/Src/atlas_lsm6dsv16b.c
    App/Src/atlas_mmc5983ma.c
    App/Src/atlas_ms5611.c
    App/Src/atlas_pyro_policy.c
    App/Src/atlas_rfd900x.c
    App/Src/atlas_rtos.c
    App/Src/atlas_rtos_policy.c
    App/Src/atlas_spi_device.c
    App/Src/atlas_status.c
    App/Src/atlas_storage.c
    App/Src/atlas_time.c
    App/Src/atlas_uart_transport.c
    App/Src/atlas_usb.c
    ThirdParty/CEVA/sh2/sh2.c
    ThirdParty/CEVA/sh2/sh2_SensorValue.c
    ThirdParty/CEVA/sh2/sh2_util.c
    ThirdParty/CEVA/sh2/shtp.c
    ThirdParty/FreeRTOS-Kernel/list.c
    ThirdParty/FreeRTOS-Kernel/queue.c
    ThirdParty/FreeRTOS-Kernel/stream_buffer.c
    ThirdParty/FreeRTOS-Kernel/tasks.c
    ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM7/r0p1/port.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/Inc
    ThirdParty/CEVA/sh2
    ThirdParty/FreeRTOS-Kernel/include
    ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM7/r0p1
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ATLAS_USE_FREERTOS=1
    ATLAS_BRINGUP=$<BOOL:${ATLAS_BRINGUP}>
)

if(ATLAS_BRINGUP)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME "Atlas-Bringup")
    set(ATLAS_PROFILE_NAME "bringup")
else()
    set(ATLAS_PROFILE_NAME "normal")
endif()

# Program the generated HEX (embedded addresses) or BIN at 0x08000000, never the .ioc.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.bin
    COMMAND ${CMAKE_COMMAND}
        -DIMAGE=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        -DPROFILE=${ATLAS_PROFILE_NAME}
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -P ${CMAKE_SOURCE_DIR}/cmake/stamp_firmware.cmake
    VERBATIM
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
