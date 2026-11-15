#ifndef __BACKLIGHT_IOCTL_H__
#define __BACKLIGHT_IOCTL_H__

#define BKL_SETTINGS_KEY            TEXT("Software\\Brain\\Backlight")

#define BKL_IOCTL_LEVEL             0x00222EE4
#define BKL_IOCTL_KEEP_LEVEL        0x00222EE8
#define BKL_IOCTL_SET_FACTORY_ROW   0x00222EEC
#define BKL_IOCTL_GET_INFO          0x100

#define BKL_LEVEL_QUERY             0xFFFFFFFF
#define BKL_LEVEL_DIM               0xFFFFFFFE
#define BKL_LEVEL_OFF               0
#define BKL_LEVEL_MINIMUM           1
#define BKL_LEVEL_MAXIMUM           100
#define BKL_LEVEL_USER              127

#define BKL_INFO_MAGIC              0x324C4B42
#define BKL_INFO_VERSION            1
#define BKL_CAPS_CONTINUOUS_LEVEL   0x00000001
#define BKL_CAPS_SUB_BACKLIGHT      0x00000002

typedef struct
{
    DWORD Magic;
    DWORD Version;
    DWORD Capabilities;
    DWORD AppliedLevel;
    DWORD ActiveCounts;
    DWORD PeriodCounts;
    DWORD LowCurrent;
} BKL_INFO;

#endif
