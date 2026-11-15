//------------------------------------------------------------------------------
//
//  File:  buzzer_tone.c
//
//  Pitched tones on the piezo buzzer. The piezo sits between pads PWM4_1 and
//  PWM7, which Sharp's EDNA2_Buzzer.dll muxes at start-up and drives in
//  antiphase (channel 4 active high, channel 7 active low) at a fixed 4 kHz,
//  50% duty. This programs the same two channels at any frequency. Sharp's
//  driver still plays its key and tap clicks on the same channels, so a
//  click during a tone ends the tone.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "buzzer_tone.h"

#define PWM_BASE                    0x80064000
#define PWM_CTRL                    0x000
#define PWM_CTRL_SET                0x004
#define PWM_CTRL_CLR                0x008
#define PWM_ACTIVE(Channel)         (0x010 + (Channel) * 0x20)
#define PWM_PERIOD(Channel)         (0x020 + (Channel) * 0x20)
#define PWM_CTRL_BLOCKED            0xC0000000
#define PWM_STATE_LOW               2
#define PWM_STATE_HIGH              3
#define PWM_ACTIVE_STATE_SHIFT      16
#define PWM_INACTIVE_STATE_SHIFT    18
#define PWM_DIVIDER_SHIFT           20
#define PWM_INACTIVE_SHIFT          16
#define PWM_MAX_PERIOD              65536

#define BUZZER_CHANNEL_HIGH         4
#define BUZZER_CHANNEL_LOW          7
#define BUZZER_ENABLE_MASK          ((1 << BUZZER_CHANNEL_HIGH) | (1 << BUZZER_CHANNEL_LOW))
#define PWM_CLOCK_HZ                24000000
#define MINIMUM_FREQUENCY           20
#define MAXIMUM_FREQUENCY           20000

static const DWORD s_Dividers[] = { 1, 2, 4, 8, 16, 64, 256, 1024 };
static volatile UINT32* s_pPwm;

static void WritePwm(DWORD Offset, UINT32 Value)
{
    s_pPwm[Offset / sizeof(UINT32)] = Value;
}

static BOOL MapPwm()
{
    if (!s_pPwm)
    {
        PHYSICAL_ADDRESS Physical;
        Physical.QuadPart = PWM_BASE;
        s_pPwm = (volatile UINT32*)MmMapIoSpace(Physical, 0x1000, FALSE);
    }
    return s_pPwm && (s_pPwm[PWM_CTRL / sizeof(UINT32)] & PWM_CTRL_BLOCKED) == 0;
}

static void ProgramChannel(DWORD Channel, DWORD Period, DWORD DividerIndex, DWORD ActiveState, DWORD InactiveState)
{
    WritePwm(PWM_ACTIVE(Channel), (Period / 2) << PWM_INACTIVE_SHIFT);
    WritePwm(PWM_PERIOD(Channel), (Period - 1) | (ActiveState << PWM_ACTIVE_STATE_SHIFT) |
        (InactiveState << PWM_INACTIVE_STATE_SHIFT) | (DividerIndex << PWM_DIVIDER_SHIFT));
}

//------------------------------------------------------------------------------
//
// Function: BuzzerToneSet
//
// Starts a continuous square-wave tone, or stops the buzzer.
//
// Parameters:
//      Frequency
//          [in] Pitch in hertz (20-20000), or 0 to stop.
//
// Returns:
//      TRUE on success, FALSE if the frequency is out of range or the PWM
//      block is unavailable.
//
//------------------------------------------------------------------------------
BOOL BuzzerToneSet(DWORD Frequency)
{
    DWORD DividerIndex;
    DWORD Period = 0;

    if (!MapPwm())
        return FALSE;
    if (Frequency == 0)
    {
        WritePwm(PWM_CTRL_CLR, BUZZER_ENABLE_MASK);
        return TRUE;
    }
    if (Frequency < MINIMUM_FREQUENCY || Frequency > MAXIMUM_FREQUENCY)
        return FALSE;

    for (DividerIndex = 0; DividerIndex < sizeof(s_Dividers) / sizeof(s_Dividers[0]); DividerIndex++)
    {
        Period = PWM_CLOCK_HZ / (s_Dividers[DividerIndex] * Frequency);
        if (Period <= PWM_MAX_PERIOD)
            break;
    }
    if (Period < 2 || Period > PWM_MAX_PERIOD)
        return FALSE;

    ProgramChannel(BUZZER_CHANNEL_HIGH, Period, DividerIndex, PWM_STATE_HIGH, PWM_STATE_LOW);
    ProgramChannel(BUZZER_CHANNEL_LOW, Period, DividerIndex, PWM_STATE_LOW, PWM_STATE_HIGH);
    WritePwm(PWM_CTRL_SET, BUZZER_ENABLE_MASK);
    return TRUE;
}
