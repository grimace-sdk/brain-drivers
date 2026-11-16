#ifndef __BACKLIGHT_BOARD_H__
#define __BACKLIGHT_BOARD_H__

typedef struct
{
    DWORD PeriodCounts;
    DWORD ClockDivider;
    DDK_IOMUX_PIN MainPin;
    DDK_IOMUX_PIN CurrentPin;
    DWORD StatusFlagsAddress;
    DWORD LowBatteryFlagsAddress;
    DWORD FactoryRowsAddress;
    DWORD FactoryRowCount;
    DWORD BatteryLowLevelCap;
    BOOL bSubBacklight;
} BACKLIGHT_BOARD;

#if defined(BRAIN_BOARD_pwa7300)

static const BACKLIGHT_BOARD s_Board =
{
    2000,
    0,
    DDK_IOMUX_AUART1_RX,
    DDK_IOMUX_AUART1_TX,
    0xA00EDC08,
    0xA00EDC68,
    0,
    0,
    0,
    TRUE
};

#elif defined(BRAIN_BOARD_pwsh2) || defined(BRAIN_BOARD_pwsh4)

static const BACKLIGHT_BOARD s_Board =
{
    500,
    1,
    DDK_IOMUX_AUART1_RX,
    DDK_IOMUX_AUART1_TX,
    0xA00EA008,
    0xA00EA074,
    0xA00EA850,
    8,
    50,
    FALSE
};

#else
#error "backlight.dll has no values for this board"
#endif

#endif
