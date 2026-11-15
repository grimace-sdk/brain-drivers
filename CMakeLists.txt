cmake_minimum_required(VERSION 3.20)
project(brain_drivers LANGUAGES C)

include(Grimace)

set(BRAIN_BOARD "" CACHE STRING "Board to build for: pwsh2, pwsh4 or pwa7300")
set_property(CACHE BRAIN_BOARD PROPERTY STRINGS pwsh2 pwsh4 pwa7300)
if(NOT BRAIN_BOARD MATCHES "^(pwsh2|pwsh4|pwa7300)$")
    message(FATAL_ERROR "Set BRAIN_BOARD to pwsh2, pwsh4 or pwa7300")
endif()

function(driver_options target)
    target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-parameter -O2 -g)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE src/compat)
endfunction()

function(ce_options target)
    target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-parameter)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
endfunction()

grimace_add_library(board src/board/board.c src/board/cpu_scaling.c src/board/power_policy.c
    src/board/register_dump.c src/board/pin_sampler.c
    OUTPUT board.dll EXPORTS BRD_Init BRD_Deinit BRD_Open BRD_Close BRD_IOControl BRD_PowerDown BRD_PowerUp)
target_link_libraries(board PRIVATE grimace::ceddk)
ce_options(board)

if(BRAIN_BOARD STREQUAL "pwsh2")
    grimace_add_library(touchraw src/touchraw/touchraw.c
        OUTPUT touchraw.dll EXPORTS TRD_Init TRD_Deinit TRD_Open TRD_Close TRD_Read TRD_IOControl TRD_PowerDown TRD_PowerUp)
    target_link_libraries(touchraw PRIVATE grimace::ceddk)
    ce_options(touchraw)
endif()

if(BRAIN_BOARD STREQUAL "pwa7300")
    grimace_add_library(touchpad src/touchpad/touchpad.c src/touchpad/motion.c
        OUTPUT touchpad.dll EXPORTS PAD_Init PAD_Deinit PAD_Open PAD_Close PAD_IOControl)
    driver_options(touchpad)

    set(CSPDDK_IMPORTS "${CMAKE_BINARY_DIR}/cspddk.S")
    add_custom_command(
        OUTPUT "${CSPDDK_IMPORTS}"
        COMMAND "${Python3_EXECUTABLE}" "${GRIMACE_TOOLCHAIN_ROOT}/tools/mkimplib.py"
                "${CMAKE_SOURCE_DIR}/src/sublcd/cspddk.txt" CSPDDK.dll "${CSPDDK_IMPORTS}" --arch ${GRIMACE_IMPLIB_ARCH}
        DEPENDS "${CMAKE_SOURCE_DIR}/src/sublcd/cspddk.txt"
        VERBATIM)
    add_library(cspddk_imports STATIC EXCLUDE_FROM_ALL "${CSPDDK_IMPORTS}")

    grimace_add_library(sublcd src/sublcd/st7586.c src/sublcd/ssp_dma.c src/sublcd/sub_backlight.c
        src/sublcd/buzzer_tone.c src/sublcd/sld.c
        OUTPUT EDNA2_slcd_ST7586.dll
        EXPORTS SLD_Init SLD_Deinit SLD_Open SLD_Close SLD_Read SLD_Write SLD_Seek SLD_IOControl
                SLD_PowerDown SLD_PowerUp SubBacklightSetPinMux SubBacklightWriteDataPin)
    target_link_libraries(sublcd PRIVATE grimace::ceddk cspddk_imports)
    driver_options(sublcd)
endif()
