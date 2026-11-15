#ifndef __BACKLIGHT_CURVE_H__
#define __BACKLIGHT_CURVE_H__

typedef struct
{
    DWORD ActiveCounts;
    BOOL bLowCurrent;
} BACKLIGHT_OUTPUT;

void BacklightCurve(DWORD Level, DWORD PeriodCounts, DWORD LowCurrentPercent, BACKLIGHT_OUTPUT* pOutput);

#endif
