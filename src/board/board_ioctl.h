//------------------------------------------------------------------------------
//
//  File:  board_ioctl.h
//
//  DeviceIoControl codes for BRD1:, the PW-A7300 board driver.
//
//------------------------------------------------------------------------------
#ifndef __BOARD_IOCTL_H__
#define __BOARD_IOCTL_H__

#define BRD_IOCTL_GET_INFO          0x100
#define BRD_IOCTL_READ_REGISTERS    0x101
#define BRD_IOCTL_CPU_SCALING       0x102
#define BRD_IOCTL_SAMPLE_PINS       0x103
#define BRD_IOCTL_READ_FRAMEBUFFER  0x104

#define BRD_INFO_MAGIC              0x31445242

typedef struct
{
    DWORD Magic;
    DWORD Version;
} BRD_INFO;

//
// Input for BRD_IOCTL_READ_REGISTERS; the output is Count DWORDs. Only the
// clock controller, power, digital control and SSP2 control registers can
// be read.
//
typedef struct
{
    DWORD Physical;
    DWORD Count;
} BRD_REGISTER_READ;

//
// BRD_IOCTL_CPU_SCALING: optional input DWORD mode (0 Sharp's clock, 1 always
// 454 MHz, 2 follow load; add 0x100 to save it for later boots); output
// BRD_CPU_STATUS.
//
#define BRD_CPU_SCALING_SAVE        0x100

typedef struct
{
    DWORD Mode;
    DWORD CpuMHz;
    DWORD AhbMHz;
    DWORD BusyPercent;
    DWORD Switches;
} BRD_CPU_STATUS;

//
// BRD_IOCTL_SAMPLE_PINS: input DWORD milliseconds (up to
// BRD_PIN_SAMPLE_LIMIT_MS); output BRD_PIN_SAMPLE. Reads the five GPIO input
// registers (HW_PINCTRL_DIN0-4) in a loop and counts rising edges per pin,
// with the tick of the first and last. Only reads; finds signals such as a
// panel's TE output.
//
#define BRD_PIN_BANKS               5
#define BRD_PIN_SAMPLE_LIMIT_MS     1000

typedef struct
{
    DWORD Samples;
    DWORD Milliseconds;
    DWORD Rising[BRD_PIN_BANKS][32];
    DWORD FirstRise[BRD_PIN_BANKS][32];
    DWORD LastRise[BRD_PIN_BANKS][32];
} BRD_PIN_SAMPLE;

//
// BRD_IOCTL_READ_FRAMEBUFFER: no input; output BRD_FRAMEBUFFER followed by
// Width x Height RGB565 pixels, top row first. The display driver records
// where its framebuffer is; reading it needs no GWES, so it works while an
// app holds GWES. With an output buffer too small for the pixels only the
// header is written, with ERROR_INSUFFICIENT_BUFFER.
//
typedef struct
{
    DWORD Width;
    DWORD Height;
    DWORD BitsPerPixel;
} BRD_FRAMEBUFFER;

#endif /* __BOARD_IOCTL_H__ */
