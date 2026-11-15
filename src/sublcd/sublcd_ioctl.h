//------------------------------------------------------------------------------
//
//  File:  sublcd_ioctl.h
//
//  DeviceIoControl codes for SLD1:, the PW-A7300's 240x120 sub LCD.
//
//  Codes 1-14 are Sharp's, kept so SharpLib and the dictionary shell work
//  unchanged. Codes from SLD_IOCTL_GET_INFO up are ours. Sharp's driver
//  returns TRUE for unknown codes without writing any output, so check
//  SLD_INFO.Magic to tell the two apart.
//
//------------------------------------------------------------------------------
#ifndef __SUBLCD_IOCTL_H__
#define __SUBLCD_IOCTL_H__

#define SLD_WIDTH                   240
#define SLD_HEIGHT                  120

#define SLD_IOCTL_PANEL_ON          1
#define SLD_IOCTL_PANEL_OFF         2
#define SLD_IOCTL_DRAW_ROW          3
#define SLD_IOCTL_SET_CONTRAST      4
#define SLD_IOCTL_GET_CONTRAST      5
#define SLD_IOCTL_CONTRAST_UP       6
#define SLD_IOCTL_CONTRAST_DOWN     7
#define SLD_IOCTL_DRAW_RECT         8
#define SLD_IOCTL_STEP_VOP          9
#define SLD_IOCTL_TEST_PATTERN      10
#define SLD_IOCTL_SET_VOP_OFFSET    11
#define SLD_IOCTL_SET_WINDOW        12
#define SLD_IOCTL_SET_VOP_BASE      13
#define SLD_IOCTL_SET_CONTRAST_100  14

#define SLD_IOCTL_GET_INFO          0x100
#define SLD_IOCTL_DRAW_GREY         0x101
#define SLD_IOCTL_SET_BACKLIGHT     0x102
#define SLD_IOCTL_GET_BACKLIGHT     0x103
#define SLD_IOCTL_SET_TONE          0x104
#define SLD_IOCTL_SET_OVERLAY       0x105

#define SLD_INFO_MAGIC              0x32444C53
#define SLD_CAPS_GREY               0x00000001
#define SLD_CAPS_BACKLIGHT_LEVEL    0x00000002
#define SLD_CAPS_TONE               0x00000004
#define SLD_CAPS_OVERLAY            0x00000008

#define SLD_DRAW_BLANK              0x0001
#define SLD_DRAW_INVERT             0x0002

#define SLD_GREY_STRIDE             (SLD_WIDTH / 4)
#define SLD_GREY_WHITE              0
#define SLD_GREY_LIGHT              1
#define SLD_GREY_DARK               2
#define SLD_GREY_BLACK              3

//
// Sharp's descriptor for SLD_IOCTL_DRAW_RECT and SLD_IOCTL_DRAW_ROW.
//
// DRAW_RECT: A, B, C, D are x, y, width and height in pixels. pBits is a
// whole-screen 1-bit buffer in column-group order: the byte for (x, y) is
// at (x / 8) * SLD_HEIGHT + y, leftmost pixel in the top bit. Set bits are
// black. The driver widens x to whole 24-pixel groups.
//
// DRAW_ROW: A is the row, B the first pixel column, C the width in 8-pixel
// groups and D the first source word. pBits points at 16-bit words with 8
// pixels in each low byte, most significant bit first.
//
typedef struct
{
    UINT16 A;
    UINT16 B;
    UINT16 C;
    UINT16 D;
    const void* pBits;
    UINT16 Flags;
    UINT16 Reserved;
} SLD_DRAW;

typedef struct
{
    DWORD Magic;
    DWORD Version;
    DWORD Capabilities;
} SLD_INFO;

//
// Input for SLD_IOCTL_DRAW_GREY: the rectangle to send and the whole screen
// at 2 bits per pixel, row by row, SLD_GREY_STRIDE bytes per row, leftmost
// pixel in the top two bits. 0 is white, 3 is black.
//
// Also the input for SLD_IOCTL_SET_OVERLAY: the rectangle is shown over
// everything drawn until the next SET_OVERLAY; a zero width or height
// hides it. The rectangle is widened to whole 3-pixel groups.
//
typedef struct
{
    UINT16 X;
    UINT16 Y;
    UINT16 Width;
    UINT16 Height;
    BYTE Pixels[SLD_GREY_STRIDE * SLD_HEIGHT];
} SLD_GREY_DRAW;

#endif /* __SUBLCD_IOCTL_H__ */
