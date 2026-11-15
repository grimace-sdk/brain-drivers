//------------------------------------------------------------------------------
//
//  File:  touchpad_ioctl.h
//
//  DeviceIoControl codes for PAD1:, the PW-A7300 touchpad driver.
//
//  An app that reads the aux touch panel (TPD2:) itself claims it first:
//  open PAD1:, send PAD_IOCTL_CLAIM and keep the handle open while it uses
//  TPD2:. The touchpad stops moving the cursor and takes TPD2: back when the
//  last claiming handle is closed, including when its process exits.
//
//------------------------------------------------------------------------------
#ifndef __TOUCHPAD_IOCTL_H__
#define __TOUCHPAD_IOCTL_H__

#define PAD_IOCTL_GET_INFO          0x100
#define PAD_IOCTL_CLAIM             0x101
#define PAD_IOCTL_RELEASE           0x102
#define PAD_IOCTL_RELOAD            0x103

#define PAD_INFO_MAGIC              0x31444150

#define PAD_SETTINGS_KEY            TEXT("Software\\Brain\\Touchpad")
#define PAD_SPEED_MINIMUM           1
#define PAD_SPEED_MAXIMUM           5

typedef struct
{
    DWORD Magic;
    DWORD Version;
    DWORD Claims;
    DWORD Active;
} PAD_INFO;

#endif /* __TOUCHPAD_IOCTL_H__ */
