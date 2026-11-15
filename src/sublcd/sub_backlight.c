//------------------------------------------------------------------------------
//
//  File:  sub_backlight.c
//
//  Sub LCD backlight brightness. The backlight sits on pad PWM2, which
//  Sharp's backlight.dll muxes as a GPIO and switches on and off. Our copy of
//  backlight.dll has its DDKIomuxSetPinMux and DDKGpioWriteDataPin imports
//  pointed at SubBacklightSetPinMux and SubBacklightWriteDataPin here, so
//  Sharp's timeout and touch-to-wake logic is untouched and "on" becomes PWM
//  channel 2 at the brightness in HKLM\Software\Brain\SubLCD\Brightness.
//
//  At 100%, or if the PWM block is unavailable, the pad stays a GPIO driven
//  high, exactly as Sharp's driver leaves it.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "cspddk.h"
#include "sub_backlight.h"

#define BACKLIGHT_PIN               DDK_IOMUX_PWM2
#define SETTINGS_KEY                TEXT("Software\\Brain\\SubLCD")
#define BRIGHTNESS_VALUE            TEXT("Brightness")
#define FULL_LEVEL                  100

#define PWM_BASE                    0x80064000
#define PWM_CTRL_SET                0x004
#define PWM_CTRL_CLR                0x008
#define PWM_ACTIVE2                 0x050
#define PWM_PERIOD2                 0x060
#define PWM_CTRL_PWM2_ENABLE        0x00000004
#define PWM_CTRL_BLOCKED            0xC0000000
#define PWM_PERIOD_ACTIVE_HIGH      (3 << 16)
#define PWM_PERIOD_INACTIVE_LOW     (2 << 18)
#define PWM_ACTIVE_INACTIVE_SHIFT   16

#define PWM_CLOCK_HZ                24000000
#define PWM_FREQUENCY_HZ            20000
#define PWM_PERIOD_CLOCKS           (PWM_CLOCK_HZ / PWM_FREQUENCY_HZ)

static CRITICAL_SECTION s_Lock;
static volatile UINT32* s_pPwm;
static DWORD s_Level = FULL_LEVEL;
static BOOL s_bOn;

static DWORD ClampLevel(DWORD Level)
{
    if (Level < 1)
        return 1;
    if (Level > FULL_LEVEL)
        return FULL_LEVEL;
    return Level;
}

static DWORD ReadLevel()
{
    DWORD Value = FULL_LEVEL, Size = sizeof(Value), Type;
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return FULL_LEVEL;
    if (RegQueryValueEx(hKey, BRIGHTNESS_VALUE, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Value = FULL_LEVEL;
    RegCloseKey(hKey);
    return ClampLevel(Value);
}

static void WriteLevel(DWORD Level)
{
    HKEY hKey;
    DWORD Disposition;

    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, NULL, 0, 0, NULL, &hKey, &Disposition) != ERROR_SUCCESS)
        return;
    RegSetValueEx(hKey, BRIGHTNESS_VALUE, 0, REG_DWORD, (const BYTE*)&Level, sizeof(Level));
    RegCloseKey(hKey);
}

static void WritePwm(DWORD Offset, UINT32 Value)
{
    s_pPwm[Offset / sizeof(UINT32)] = Value;
}

static BOOL PwmUsable()
{
    if (!s_pPwm)
    {
        PHYSICAL_ADDRESS Physical;
        Physical.QuadPart = PWM_BASE;
        s_pPwm = (volatile UINT32*)MmMapIoSpace(Physical, 0x1000, FALSE);
        if (!s_pPwm)
            return FALSE;
    }
    return (s_pPwm[0] & PWM_CTRL_BLOCKED) == 0;
}

static void DriveGpio(BOOL bHigh)
{
    if (s_pPwm)
        WritePwm(PWM_CTRL_CLR, PWM_CTRL_PWM2_ENABLE);
    DDKIomuxSetPinMux(BACKLIGHT_PIN, DDK_IOMUX_MODE_GPIO);
    DDKGpioWriteDataPin(BACKLIGHT_PIN, bHigh ? 1 : 0);
}

static void ApplyLocked()
{
    DWORD Active;

    if (!s_bOn || s_Level >= FULL_LEVEL || !PwmUsable())
    {
        DriveGpio(s_bOn);
        return;
    }
    Active = PWM_PERIOD_CLOCKS * s_Level * s_Level / (FULL_LEVEL * FULL_LEVEL);
    if (Active < 1)
        Active = 1;
    WritePwm(PWM_ACTIVE2, Active << PWM_ACTIVE_INACTIVE_SHIFT);
    WritePwm(PWM_PERIOD2, (PWM_PERIOD_CLOCKS - 1) | PWM_PERIOD_ACTIVE_HIGH | PWM_PERIOD_INACTIVE_LOW);
    WritePwm(PWM_CTRL_SET, PWM_CTRL_PWM2_ENABLE);
    DDKIomuxSetPinMux(BACKLIGHT_PIN, DDK_IOMUX_MODE_00);
}

//------------------------------------------------------------------------------
//
// Function: SubBacklightInit
//
// Prepares the lock. Called from DllMain, which may run first because
// backlight.dll imports from this DLL.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void SubBacklightInit()
{
    InitializeCriticalSection(&s_Lock);
}

//------------------------------------------------------------------------------
//
// Function: SubBacklightSetLevel
//
// Sets and saves the brightness, applying it now if the backlight is on.
//
// Parameters:
//      Level
//          [in] Brightness, 1-100.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void SubBacklightSetLevel(DWORD Level)
{
    Level = ClampLevel(Level);
    WriteLevel(Level);
    EnterCriticalSection(&s_Lock);
    s_Level = Level;
    ApplyLocked();
    LeaveCriticalSection(&s_Lock);
}

//------------------------------------------------------------------------------
//
// Function: SubBacklightGetLevel
//
// Parameters:
//      None.
//
// Returns:
//      The saved brightness, 1-100.
//
//------------------------------------------------------------------------------
DWORD SubBacklightGetLevel()
{
    return ReadLevel();
}

//------------------------------------------------------------------------------
//
// Function: SubBacklightSetPinMux
//
// Stands in for DDKIomuxSetPinMux in backlight.dll. Calls for the backlight
// pad reapply the current state (backlight.dll calls this at start-up and on
// resume); others pass through.
//
// Parameters:
//      Pin, Mode
//          [in] As DDKIomuxSetPinMux.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL SubBacklightSetPinMux(DDK_IOMUX_PIN Pin, DDK_IOMUX_PIN_MUXMODE Mode)
{
    if (Pin != BACKLIGHT_PIN)
        return DDKIomuxSetPinMux(Pin, Mode);
    EnterCriticalSection(&s_Lock);
    ApplyLocked();
    LeaveCriticalSection(&s_Lock);
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: SubBacklightWriteDataPin
//
// Stands in for DDKGpioWriteDataPin in backlight.dll. Switching the
// backlight pad on applies the saved brightness; others pass through.
//
// Parameters:
//      Pin, Data
//          [in] As DDKGpioWriteDataPin.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL SubBacklightWriteDataPin(DDK_IOMUX_PIN Pin, UINT32 Data)
{
    if (Pin != BACKLIGHT_PIN)
        return DDKGpioWriteDataPin(Pin, Data);
    EnterCriticalSection(&s_Lock);
    s_bOn = Data != 0;
    if (s_bOn)
        s_Level = ReadLevel();
    ApplyLocked();
    LeaveCriticalSection(&s_Lock);
    return TRUE;
}
