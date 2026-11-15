cmake_minimum_required(VERSION 3.20)
project(a7300_drivers LANGUAGES C)

include(Grimace)

option(A7300_SUBLCD "Build the sub LCD driver" ON)

function(driver_options target)
    target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-parameter -O2 -g)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE src/compat)
endfunction()

grimace_add_library(touchpad src/touchpad/touchpad.c src/touchpad/motion.c
    OUTPUT touchpad.dll EXPORTS PAD_Init PAD_Deinit PAD_Open PAD_Close PAD_IOControl)
driver_options(touchpad)

if(A7300_SUBLCD)
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
