cmake_minimum_required(VERSION 3.20)
project(a7300_drivers LANGUAGES C)

include(Grimace)

function(driver_options target)
    target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-parameter -O2 -g)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE src/compat)
endfunction()

grimace_add_library(touchpad src/touchpad/touchpad.c src/touchpad/motion.c
    OUTPUT touchpad.dll EXPORTS PAD_Init PAD_Deinit PAD_Open PAD_Close PAD_IOControl)
driver_options(touchpad)
