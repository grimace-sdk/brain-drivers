#ifndef __BACKLIGHT_CURVE_H__
#define __BACKLIGHT_CURVE_H__

typedef struct
{
    DWORD ActiveCounts;
    BOOL bLowCurrent;
} BACKLIGHT_OUTPUT;

void BacklightCurve(DWORD Level, DWORD PeriodCounts, DWORD LowCurrentPercent, DWORD FloorLuminance, BACKLIGHT_OUTPUT* pOutput);

#endif
