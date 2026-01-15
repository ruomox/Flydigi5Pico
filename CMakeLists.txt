# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Flydigi5Pico C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# 添加 pico-pio-usb 库
add_subdirectory(lib/pico-pio-usb)

# 配置 hcd_pio_usb.c 路径
set(TINYUSB_PIO_SRC "${PICO_SDK_PATH}/lib/tinyusb/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c")

# Add executable.
# 添加 usb_descriptors.c (New)
add_executable(Flydigi5Pico 
        Flydigi5Pico.cpp
        ${TINYUSB_PIO_SRC}
        )

target_sources(Flydigi5Pico PRIVATE
    flydigiHost/Flydigi5Host.c
    flydigiHost/Flydigi5HID.c
    flydigiDevice/Flydigi5Device.c
    Descriptors/xbox360_descriptors.c
    SRAM/SRAM_Buffer.c
)

pico_set_program_name(Flydigi5Pico "Flydigi5Pico")
pico_set_program_version(Flydigi5Pico "1.0.0")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Flydigi5Pico 1)
pico_enable_stdio_usb(Flydigi5Pico 0)

# Link 库
target_link_libraries(Flydigi5Pico
        # 标准库
        pico_stdlib
        # pico-pio-usb 库
        pico_pio_usb
        # 用户需求库
        hardware_dma
        hardware_pio
        hardware_timer
        hardware_clocks
        tinyusb_device
        tinyusb_host
        # 双核库
        pico_multicore
        )

# 设置 pico-pio-usb 状态
target_compile_definitions(Flydigi5Pico PRIVATE
    PICO_PIO_USB=1
    PIO_USB_HOST=1
    # 告诉 PIO USB 库我们跑在 12 PIO
    PIO_USB_DP_PIN_DEFAULT=12
)

# Add the standard include files to the build
target_include_directories(Flydigi5Pico PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/flydigiHost
        #${CMAKE_CURRENT_LIST_DIR}/flydigiDevice
        ${CMAKE_CURRENT_LIST_DIR}/Descriptors
        ${CMAKE_CURRENT_LIST_DIR}/SRAM
)

pico_add_extra_outputs(Flydigi5Pico)

