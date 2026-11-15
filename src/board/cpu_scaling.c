//------------------------------------------------------------------------------
//
//  File:  cpu_scaling.c
//
//  CPU clock scaling. Sharp boots the i.MX28 at 240 MHz CPU / 120 MHz AHB
//  with VDDD already at 1.55 V, NXP's voltage for its 454 MHz setpoint, and
//  never loads NXP's dvfc.dll (which also reclocks DDR with EVK memory
//  timings). This switches only the CPU and AHB clocks between Sharp's boot
//  setpoint and NXP's high one (CPUFRAC 19, CPU divider 1, AHB divider 3),
//  in NXP's order, leaving DDR and VDDD alone.
//
//  Modes (HKLM\Software\Brain\CPU\Scaling, or BRD_IOCTL_CPU_SCALING):
//  0 keeps Sharp's clock, 1 stays at 454 MHz, 2 (the default) follows load:
//  up as soon as a 50 ms window is half busy, down after 500 ms under a
//  quarter busy. If the kernel keeps no idle time, dynamic mode stays fast.
//  The registry is read again every few seconds, because it is restored
//  from the card after this driver starts. While the system is idle (see
//  power_policy.c) every mode keeps Sharp's clock.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "cpu_scaling.h"

#define CLKCTRL_BASE                0x80040000
#define CLKCTRL_CPU                 0x050
#define CLKCTRL_HBUS                0x060
#define CLKCTRL_FRAC0               0x1B0
#define CPU_DIV_MASK                0x0000003F
#define CPU_BUSY_MASK               0x30000000
#define HBUS_DIV_MASK               0x0000003F
#define FRAC0_CPUFRAC_MASK          0x0000003F
#define FRAC0_CPU_STABLE            0x00000040
#define FRAC0_CLKGATECPU            0x00000080
#define PLL_MHZ                     480
#define FRAC_NUMERATOR              18
#define BUSY_POLL_LIMIT             100000

#define HIGH_CPUFRAC                19
#define HIGH_CPU_DIV                1
#define HIGH_HBUS_DIV               3

#define SETTINGS_KEY                TEXT("Software\\Brain\\CPU")
#define SCALING_VALUE               TEXT("Scaling")
#define GOVERNOR_PERIOD_MS          50
#define GOVERNOR_PRIORITY           200
#define UP_BUSY_PERCENT             50
#define DOWN_BUSY_PERCENT           25
#define DOWN_WINDOWS                10
#define IDLE_UNSUPPORTED            0xFFFFFFFF
#define SETTINGS_POLL_MS            5000
#define NO_SETTING                  0xFFFFFFFF

typedef struct
{
    DWORD CpuFrac;
    DWORD CpuDiv;
    DWORD HbusDiv;
} SETPOINT;

static volatile UINT32* s_pClkctrl;
static CRITICAL_SECTION s_Lock;
static SETPOINT s_Boot;
static const SETPOINT s_High = { HIGH_CPUFRAC, HIGH_CPU_DIV, HIGH_HBUS_DIV };
static volatile DWORD s_Mode;
static volatile BOOL s_bSuspended;
static volatile BOOL s_bIdle;
static DWORD s_BusyPercent;
static DWORD s_Switches;
static HANDLE s_hWake;
static DWORD s_LastSetting = NO_SETTING;

static UINT32 ReadClk(DWORD Offset)
{
    return s_pClkctrl[Offset / sizeof(UINT32)];
}

static void WriteClk(DWORD Offset, UINT32 Value)
{
    s_pClkctrl[Offset / sizeof(UINT32)] = Value;
}

static void WaitCpuClock()
{
    DWORD Polls = 0;

    while ((ReadClk(CLKCTRL_CPU) & CPU_BUSY_MASK) && ++Polls < BUSY_POLL_LIMIT)
        ;
}

static void SetCpuDiv(DWORD Divider)
{
    WriteClk(CLKCTRL_CPU, (ReadClk(CLKCTRL_CPU) & ~CPU_DIV_MASK) | Divider);
    WaitCpuClock();
}

static void SetCpuFrac(DWORD Frac)
{
    DWORD Polls = 0;

    WriteClk(CLKCTRL_FRAC0, (ReadClk(CLKCTRL_FRAC0) & ~(FRAC0_CPUFRAC_MASK | FRAC0_CLKGATECPU)) | Frac);
    while (!(ReadClk(CLKCTRL_FRAC0) & FRAC0_CPU_STABLE) && ++Polls < BUSY_POLL_LIMIT)
        ;
}

static void SetHbusDiv(DWORD Divider)
{
    WriteClk(CLKCTRL_HBUS, (ReadClk(CLKCTRL_HBUS) & ~HBUS_DIV_MASK) | Divider);
}

static DWORD CpuMHz(const SETPOINT* pSetpoint)
{
    return PLL_MHZ * FRAC_NUMERATOR / pSetpoint->CpuFrac / pSetpoint->CpuDiv;
}

static void ReadSetpoint(SETPOINT* pSetpoint)
{
    pSetpoint->CpuFrac = ReadClk(CLKCTRL_FRAC0) & FRAC0_CPUFRAC_MASK;
    pSetpoint->CpuDiv = ReadClk(CLKCTRL_CPU) & CPU_DIV_MASK;
    pSetpoint->HbusDiv = ReadClk(CLKCTRL_HBUS) & HBUS_DIV_MASK;
}

static void ApplyLocked(const SETPOINT* pTarget)
{
    SETPOINT From;

    ReadSetpoint(&From);
    if (From.CpuFrac == pTarget->CpuFrac && From.CpuDiv == pTarget->CpuDiv && From.HbusDiv == pTarget->HbusDiv)
        return;
    if (!From.CpuFrac || !From.CpuDiv || CpuMHz(pTarget) > CpuMHz(&From))
    {
        SetHbusDiv(pTarget->HbusDiv);
        SetCpuFrac(pTarget->CpuFrac);
        SetCpuDiv(pTarget->CpuDiv);
    }
    else
    {
        SetCpuDiv(pTarget->CpuDiv);
        SetCpuFrac(pTarget->CpuFrac);
        SetHbusDiv(pTarget->HbusDiv);
    }
    s_Switches++;
}

static void Apply(const SETPOINT* pTarget)
{
    EnterCriticalSection(&s_Lock);
    if (!s_bSuspended)
        ApplyLocked(pTarget);
    LeaveCriticalSection(&s_Lock);
}

static DWORD ReadSetting()
{
    DWORD Value = NO_SETTING, Size = sizeof(Value), Type;
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return NO_SETTING;
    if (RegQueryValueEx(hKey, SCALING_VALUE, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD ||
        Value > CPU_SCALING_DYNAMIC)
        Value = NO_SETTING;
    RegCloseKey(hKey);
    return Value;
}

static void SaveMode(DWORD Mode)
{
    HKEY hKey;
    DWORD Disposition;

    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, NULL, 0, 0, NULL, &hKey, &Disposition) != ERROR_SUCCESS)
        return;
    RegSetValueEx(hKey, SCALING_VALUE, 0, REG_DWORD, (const BYTE*)&Mode, sizeof(Mode));
    RegCloseKey(hKey);
}

static DWORD WINAPI GovernorThread(LPVOID pContext)
{
    DWORD LastIdle = GetIdleTime();
    DWORD LastTick = GetTickCount();
    DWORD QuietWindows = 0;
    DWORD LastSettingsCheck = GetTickCount();

    RETAILMSG(TRUE, (TEXT("CpuScaling: governor thread %08x\r\n"), GetCurrentThreadId()));
    for (;;)
    {
        DWORD Idle, Tick, Elapsed;

        WaitForSingleObject(s_hWake, s_Mode == CPU_SCALING_DYNAMIC && !s_bIdle ? GOVERNOR_PERIOD_MS : SETTINGS_POLL_MS);
        Idle = GetIdleTime();
        Tick = GetTickCount();
        Elapsed = Tick - LastTick;
        if (Tick - LastSettingsCheck >= SETTINGS_POLL_MS)
        {
            DWORD Setting = ReadSetting();
            if (Setting != s_LastSetting && Setting != NO_SETTING)
                s_Mode = Setting;
            s_LastSetting = Setting;
            LastSettingsCheck = Tick;
        }

        if (s_Mode == CPU_SCALING_OFF || s_bIdle)
            Apply(&s_Boot);
        else if (s_Mode == CPU_SCALING_FAST || Idle == IDLE_UNSUPPORTED)
            Apply(&s_High);
        else if (Elapsed)
        {
            DWORD IdleElapsed = min(Idle - LastIdle, Elapsed);

            s_BusyPercent = 100 - IdleElapsed * 100 / Elapsed;
            if (s_BusyPercent >= UP_BUSY_PERCENT)
            {
                QuietWindows = 0;
                Apply(&s_High);
            }
            else if (s_BusyPercent < DOWN_BUSY_PERCENT && ++QuietWindows >= DOWN_WINDOWS)
            {
                Apply(&s_Boot);
            }
        }
        LastIdle = Idle;
        LastTick = Tick;
    }
    return 0;
}

//------------------------------------------------------------------------------
//
// Function: CpuScalingInit
//
// Records the boot setpoint and starts the governor in the saved mode.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void CpuScalingInit()
{
    PHYSICAL_ADDRESS Physical;
    HANDLE hThread;

    InitializeCriticalSection(&s_Lock);
    Physical.QuadPart = CLKCTRL_BASE;
    s_pClkctrl = (volatile UINT32*)MmMapIoSpace(Physical, 0x1000, FALSE);
    if (!s_pClkctrl)
        return;
    ReadSetpoint(&s_Boot);
    if (!s_Boot.CpuFrac || !s_Boot.CpuDiv)
    {
        s_pClkctrl = NULL;
        return;
    }
    s_LastSetting = ReadSetting();
    s_Mode = s_LastSetting != NO_SETTING ? s_LastSetting : CPU_SCALING_DYNAMIC;
    s_hWake = CreateEvent(NULL, FALSE, TRUE, NULL);
    hThread = CreateThread(NULL, 0, GovernorThread, NULL, 0, NULL);
    if (hThread)
    {
        CeSetThreadPriority(hThread, GOVERNOR_PRIORITY);
        CloseHandle(hThread);
    }
    RETAILMSG(TRUE, (TEXT("CpuScaling: boot %u MHz, mode %u, idle time %s\r\n"), CpuMHz(&s_Boot), s_Mode,
        GetIdleTime() == IDLE_UNSUPPORTED ? TEXT("unsupported") : TEXT("available")));
}

//------------------------------------------------------------------------------
//
// Function: CpuScalingSetMode
//
// Parameters:
//      Mode
//          [in] CPU_SCALING_OFF, CPU_SCALING_FAST or CPU_SCALING_DYNAMIC.
//
//      bSave
//          [in] TRUE to keep it in the registry for later boots.
//
// Returns:
//      TRUE on success, FALSE for an unknown mode or if scaling is
//      unavailable.
//
//------------------------------------------------------------------------------
BOOL CpuScalingSetMode(DWORD Mode, BOOL bSave)
{
    if (!s_pClkctrl || Mode > CPU_SCALING_DYNAMIC)
        return FALSE;
    s_Mode = Mode;
    if (bSave)
    {
        SaveMode(Mode);
        s_LastSetting = Mode;
    }
    SetEvent(s_hWake);
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: CpuScalingSetIdle
//
// Parameters:
//      bIdle
//          [in] TRUE to keep Sharp's clock until called again with FALSE.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void CpuScalingSetIdle(BOOL bIdle)
{
    if (!s_pClkctrl)
        return;
    s_bIdle = bIdle;
    SetEvent(s_hWake);
}

//------------------------------------------------------------------------------
//
// Function: CpuScalingGetStatus
//
// Parameters:
//      pStatus
//          [out] Mode, current clocks, last busy percentage and number of
//          setpoint switches.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void CpuScalingGetStatus(CPU_SCALING_STATUS* pStatus)
{
    memset(pStatus, 0, sizeof(*pStatus));
    if (!s_pClkctrl)
        return;
    pStatus->Mode = s_Mode;
    pStatus->CpuMHz = PLL_MHZ * FRAC_NUMERATOR / (ReadClk(CLKCTRL_FRAC0) & FRAC0_CPUFRAC_MASK) / (ReadClk(CLKCTRL_CPU) & CPU_DIV_MASK);
    pStatus->AhbMHz = pStatus->CpuMHz / (ReadClk(CLKCTRL_HBUS) & HBUS_DIV_MASK);
    pStatus->BusyPercent = s_BusyPercent;
    pStatus->Switches = s_Switches;
}

//------------------------------------------------------------------------------
//
// Function: CpuScalingSuspend, CpuScalingResume
//
// Return to the boot setpoint before suspend, so Sharp's suspend and resume
// code sees the clocks it set, and pick up again afterwards. Suspend runs
// without blocking calls.
//
//------------------------------------------------------------------------------
void CpuScalingSuspend()
{
    if (!s_pClkctrl)
        return;
    s_bSuspended = TRUE;
    if (TryEnterCriticalSection(&s_Lock))
    {
        ApplyLocked(&s_Boot);
        LeaveCriticalSection(&s_Lock);
    }
}

void CpuScalingResume()
{
    if (!s_pClkctrl)
        return;
    s_bSuspended = FALSE;
    SetEvent(s_hWake);
}
