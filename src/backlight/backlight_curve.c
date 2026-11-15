#include <windows.h>
#include "backlight_curve.h"

#define LIGHTNESS_FULL              10000ULL
#define LUMINANCE_FULL              (LIGHTNESS_FULL * LIGHTNESS_FULL * LIGHTNESS_FULL)
#define CROSSOVER_PERCENT           5ULL
#define LUMINANCE_FLOOR_SCALE       10000ULL
#define LEVEL_MINIMUM               1
#define LEVEL_MAXIMUM               100

static unsigned long long CubeRoot(unsigned long long Value)
{
    unsigned long long Low = 0, High = LIGHTNESS_FULL;

    while (Low < High)
    {
        unsigned long long Middle = (Low + High + 1) / 2;

        if (Middle * Middle * Middle <= Value)
            Low = Middle;
        else
            High = Middle - 1;
    }
    return Low;
}

static unsigned long long DivideRounded(unsigned long long Numerator, unsigned long long Denominator)
{
    return (Numerator + Denominator / 2) / Denominator;
}

void BacklightCurve(DWORD Level, DWORD PeriodCounts, DWORD LowCurrentPercent, DWORD FloorLuminance, BACKLIGHT_OUTPUT* pOutput)
{
    unsigned long long FloorLightness, Lightness, Luminance, LowCounts, FullCounts;

    if (Level < LEVEL_MINIMUM)
        Level = LEVEL_MINIMUM;
    if (Level > LEVEL_MAXIMUM)
        Level = LEVEL_MAXIMUM;

    FloorLightness = CubeRoot(LUMINANCE_FULL * FloorLuminance / LUMINANCE_FLOOR_SCALE);
    Lightness = FloorLightness + (LIGHTNESS_FULL - FloorLightness) * (Level - LEVEL_MINIMUM) / (LEVEL_MAXIMUM - LEVEL_MINIMUM);
    Luminance = Lightness * Lightness * Lightness;

    LowCounts = DivideRounded(Luminance * PeriodCounts * 100ULL, LUMINANCE_FULL * LowCurrentPercent);
    if (Luminance * 100ULL < LUMINANCE_FULL * CROSSOVER_PERCENT && LowCounts <= PeriodCounts)
    {
        pOutput->ActiveCounts = LowCounts ? (DWORD)LowCounts : 1;
        pOutput->bLowCurrent = TRUE;
        return;
    }
    FullCounts = DivideRounded(Luminance * PeriodCounts, LUMINANCE_FULL);
    pOutput->ActiveCounts = FullCounts ? (DWORD)FullCounts : 1;
    pOutput->bLowCurrent = FALSE;
}
