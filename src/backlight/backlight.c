#include <windows.h>
#include <ceddk.h>
#include <pm.h>
#include "cspddk.h"
#include "backlight_board.h"
#include "backlight_curve.h"
#include "backlight_ioctl.h"

#define DEVICE_CONTEXT              0x424B4C31

#define ACTIVATING_EVENT            TEXT("Display_Event_Backlight_Activating")
#define BLANKING_EVENT              TEXT("Display_Event_Backlight_DIMBlanking")
#define APPLY_EVENT                 TEXT("Display_Event_PowerManager_Update")
#define KEEP_EVENT                  TEXT("Display_Event_Backlight_Keep")
#define UPDATE_EVENT                TEXT("Display_Event_Backlight_Update")
#define SUB_ACTIVE_EVENT            TEXT("Display_Event_Backlight_SubActive")

#define SUB_MODULE                  TEXT("EDNA2_slcd_ST7586.dll")
#define SUB_WRITE_DATA_PIN          TEXT("SubBacklightWriteDataPin")
#define SUB_PIN                     DDK_IOMUX_PWM2
#define SUB_POLL_MS                 250

#define DEFAULT_LEVEL               50
#define DEFAULT_DIM_LEVEL           20
#define DEFAULT_LOW_CURRENT_PERCENT 50
#define SHARP_BRIGHTEST_LEVEL       120

#define IOCTL_POWER_QUERY           0x0032100C

#define PWM_BASE                    0x80064000
#define PWM_CTRL                    0x000
#define PWM_CTRL_SET                0x004
#define PWM_CTRL_CLR                0x008
#define PWM_ACTIVE0                 0x010
#define PWM_PERIOD0                 0x020
#define PWM_ACTIVE1                 0x030
#define PWM_PERIOD1                 0x040
#define PWM_CTRL_SFTRST             0x80000000
#define PWM_CTRL_CLKGATE            0x40000000
#define PWM_CTRL_MAIN_ENABLE        0x00000003
#define PWM_ACTIVE_INACTIVE_SHIFT   16
#define PWM_PERIOD_ACTIVE_LOW       (2 << 16)
#define PWM_PERIOD_ACTIVE_HIGH      (3 << 16)
#define PWM_PERIOD_INACTIVE_LOW     (2 << 18)
#define PWM_PERIOD_INACTIVE_HIGH    (3 << 18)
#define PWM_PERIOD_CDIV_SHIFT       20

#define CLKCTRL_BASE                0x80040000
#define CLKCTRL_XTAL_CLR            0x088
#define CLKCTRL_XTAL_PWM_GATE       0x20000000

#define LOW_BATTERY_FLAG_MASK       0x80000001
#define STATUS_FLAG_BRIGHTEST       0x00000004
#define FACTORY_ROW_BYTES           16

typedef BOOL (*SUB_WRITE_DATA_PIN_FUNCTION)(DDK_IOMUX_PIN Pin, UINT32 Data);

typedef struct
{
    DWORD DimLevel;
    DWORD MinimumLevel;
    DWORD MaximumLevel;
    DWORD LowCurrentPercent;
} BACKLIGHT_SETTINGS;

static CRITICAL_SECTION s_Lock;
static volatile UINT32* s_pPwm;
static volatile UINT32* s_pClkctrl;
static HANDLE s_hActivatingEvent;
static HANDLE s_hBlankingEvent;
static HANDLE s_hApplyEvent;
static HANDLE s_hSubActiveEvent;
static HANDLE s_hWakeEvent;
static HANDLE s_hQuitEvent;
static HANDLE s_hWorker;
static HMODULE s_hSubModule;
static SUB_WRITE_DATA_PIN_FUNCTION s_pSubWriteDataPin;
static DWORD s_UserLevel = DEFAULT_LEVEL;
static DWORD s_KeepLevel = BKL_LEVEL_DIM;
static CEDEVICE_POWER_STATE s_PowerState = D0;
static DWORD s_AppliedLevel;
static BACKLIGHT_OUTPUT s_AppliedOutput;
static BOOL s_bSubOn;
static BOOL s_bInitialised;

static DWORD ReadValue(HKEY hKey, LPCTSTR pName, DWORD Default)
{
    DWORD Value, Size = sizeof(Value), Type;

    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        return Default;
    return Value;
}

static DWORD ClampLevel(DWORD Level)
{
    if (Level < BKL_LEVEL_MINIMUM)
        return BKL_LEVEL_MINIMUM;
    if (Level > BKL_LEVEL_MAXIMUM)
        return BKL_LEVEL_MAXIMUM;
    return Level;
}

static void ReadSettings(BACKLIGHT_SETTINGS* pSettings)
{
    HKEY hKey;

    pSettings->DimLevel = DEFAULT_DIM_LEVEL;
    pSettings->MinimumLevel = BKL_LEVEL_MINIMUM;
    pSettings->MaximumLevel = BKL_LEVEL_MAXIMUM;
    pSettings->LowCurrentPercent = DEFAULT_LOW_CURRENT_PERCENT;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, BKL_SETTINGS_KEY, 0, 0, &hKey) == ERROR_SUCCESS)
    {
        pSettings->DimLevel = ReadValue(hKey, TEXT("DimLevel"), DEFAULT_DIM_LEVEL);
        pSettings->MinimumLevel = ReadValue(hKey, TEXT("MinimumLevel"), BKL_LEVEL_MINIMUM);
        pSettings->MaximumLevel = ReadValue(hKey, TEXT("MaximumLevel"), BKL_LEVEL_MAXIMUM);
        pSettings->LowCurrentPercent = ReadValue(hKey, TEXT("LowCurrentPercent"), DEFAULT_LOW_CURRENT_PERCENT);
        RegCloseKey(hKey);
    }
    pSettings->DimLevel = ClampLevel(pSettings->DimLevel);
    pSettings->MinimumLevel = ClampLevel(pSettings->MinimumLevel);
    pSettings->MaximumLevel = ClampLevel(pSettings->MaximumLevel);
    if (pSettings->MaximumLevel < pSettings->MinimumLevel)
        pSettings->MaximumLevel = pSettings->MinimumLevel;
    if (pSettings->LowCurrentPercent < 1 || pSettings->LowCurrentPercent > 100)
        pSettings->LowCurrentPercent = DEFAULT_LOW_CURRENT_PERCENT;
}

static BOOL EventExists(LPCTSTR pName)
{
    HANDLE hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, pName);

    if (!hEvent)
        return FALSE;
    CloseHandle(hEvent);
    return TRUE;
}

static void SignalNamedEvent(LPCTSTR pName)
{
    HANDLE hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, pName);

    if (!hEvent)
        return;
    SetEvent(hEvent);
    CloseHandle(hEvent);
}

static BOOL IsSignalled(HANDLE hEvent)
{
    return hEvent && WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0;
}

static BOOL OnAcPower()
{
    SYSTEM_POWER_STATUS_EX Status;

    return GetSystemPowerStatusEx(&Status, FALSE) && Status.ACLineStatus == AC_LINE_ONLINE;
}

static BOOL BatteryLow()
{
    SYSTEM_POWER_STATUS_EX Status;

    return GetSystemPowerStatusEx(&Status, FALSE) && (Status.BatteryFlag & (BATTERY_FLAG_LOW | BATTERY_FLAG_CRITICAL)) != 0;
}

static BOOL LowBatteryFlagSet()
{
    return (*(volatile DWORD*)s_Board.LowBatteryFlagsAddress & LOW_BATTERY_FLAG_MASK) == LOW_BATTERY_FLAG_MASK;
}

static BOOL BrightestFlagSet()
{
    return (*(volatile DWORD*)s_Board.StatusFlagsAddress & STATUS_FLAG_BRIGHTEST) != 0;
}

static DWORD RequestedLevelLocked()
{
    if (EventExists(KEEP_EVENT))
        return s_KeepLevel == BKL_LEVEL_USER ? s_UserLevel : s_KeepLevel;
    if (IsSignalled(s_hActivatingEvent))
        return s_UserLevel;
    if (IsSignalled(s_hBlankingEvent))
        return BKL_LEVEL_OFF;
    return BKL_LEVEL_DIM;
}

static DWORD EffectiveLevel(DWORD Requested, const BACKLIGHT_SETTINGS* pSettings)
{
    DWORD Level;

    if (Requested == BKL_LEVEL_OFF)
        return BKL_LEVEL_OFF;
    if (BrightestFlagSet() && OnAcPower())
        return pSettings->MaximumLevel;
    if (Requested >= BKL_LEVEL_MINIMUM && Requested <= BKL_LEVEL_MAXIMUM)
        Level = Requested;
    else if (Requested > BKL_LEVEL_MAXIMUM && Requested <= SHARP_BRIGHTEST_LEVEL)
        Level = BKL_LEVEL_MAXIMUM;
    else
        Level = pSettings->DimLevel < s_UserLevel ? pSettings->DimLevel : s_UserLevel;
    if (LowBatteryFlagSet() && Level > pSettings->DimLevel)
        Level = pSettings->DimLevel;
    if (s_Board.BatteryLowLevelCap && Level > s_Board.BatteryLowLevelCap && BatteryLow())
        Level = s_Board.BatteryLowLevelCap;
    if (Level < pSettings->MinimumLevel)
        Level = pSettings->MinimumLevel;
    if (Level > pSettings->MaximumLevel)
        Level = pSettings->MaximumLevel;
    return Level;
}

static void WritePwm(DWORD Offset, UINT32 Value)
{
    s_pPwm[Offset / sizeof(UINT32)] = Value;
}

static UINT32 PeriodValue(UINT32 States)
{
    return (s_Board.PeriodCounts - 1) | States | (s_Board.ClockDivider << PWM_PERIOD_CDIV_SHIFT);
}

static void WriteMain(const BACKLIGHT_OUTPUT* pOutput)
{
    UINT32 MainStates = pOutput->ActiveCounts >= s_Board.PeriodCounts
        ? PWM_PERIOD_ACTIVE_HIGH | PWM_PERIOD_INACTIVE_HIGH
        : PWM_PERIOD_ACTIVE_HIGH | PWM_PERIOD_INACTIVE_LOW;
    UINT32 CurrentStates = pOutput->bLowCurrent
        ? PWM_PERIOD_ACTIVE_LOW | PWM_PERIOD_INACTIVE_LOW
        : PWM_PERIOD_ACTIVE_HIGH | PWM_PERIOD_INACTIVE_HIGH;
    BOOL bCurrentFirst = pOutput->bLowCurrent && !s_AppliedOutput.bLowCurrent;

    if (bCurrentFirst)
    {
        WritePwm(PWM_ACTIVE1, 0);
        WritePwm(PWM_PERIOD1, PeriodValue(CurrentStates));
    }
    WritePwm(PWM_ACTIVE0, pOutput->ActiveCounts << PWM_ACTIVE_INACTIVE_SHIFT);
    WritePwm(PWM_PERIOD0, PeriodValue(MainStates));
    if (!bCurrentFirst)
    {
        WritePwm(PWM_ACTIVE1, 0);
        WritePwm(PWM_PERIOD1, PeriodValue(CurrentStates));
    }
    WritePwm(PWM_CTRL_SET, PWM_CTRL_MAIN_ENABLE);
}

static void ApplyMainLocked()
{
    BACKLIGHT_SETTINGS Settings;
    BACKLIGHT_OUTPUT Output;
    DWORD Level;

    if (!s_pPwm || s_PowerState != D0)
        return;
    ReadSettings(&Settings);
    Level = EffectiveLevel(RequestedLevelLocked(), &Settings);
    if (Level == BKL_LEVEL_OFF)
    {
        WritePwm(PWM_CTRL_CLR, PWM_CTRL_MAIN_ENABLE);
        s_AppliedLevel = BKL_LEVEL_OFF;
        s_AppliedOutput.ActiveCounts = 0;
        s_AppliedOutput.bLowCurrent = FALSE;
        return;
    }
    BacklightCurve(Level, s_Board.PeriodCounts, Settings.LowCurrentPercent, &Output);
    WriteMain(&Output);
    s_AppliedLevel = Level;
    s_AppliedOutput = Output;
}

static void DriveSub(BOOL bOn)
{
    if (s_pSubWriteDataPin)
    {
        s_pSubWriteDataPin(SUB_PIN, bOn ? 1 : 0);
        return;
    }
    DDKIomuxSetPinMux(SUB_PIN, DDK_IOMUX_MODE_GPIO);
    DDKGpioEnableDataPin(SUB_PIN, 1);
    DDKGpioWriteDataPin(SUB_PIN, bOn ? 1 : 0);
}

static void UpdateSubLocked(BOOL bForce)
{
    BOOL bOn;

    if (!s_Board.bSubBacklight)
        return;
    bOn = s_PowerState == D0 && IsSignalled(s_hSubActiveEvent);
    if (bOn == s_bSubOn && !bForce)
        return;
    s_bSubOn = bOn;
    DriveSub(bOn);
}

static void EnableBlock()
{
    if (s_pClkctrl)
        s_pClkctrl[CLKCTRL_XTAL_CLR / sizeof(UINT32)] = CLKCTRL_XTAL_PWM_GATE;
    if (s_pPwm[PWM_CTRL / sizeof(UINT32)] & PWM_CTRL_SFTRST)
        WritePwm(PWM_CTRL_CLR, PWM_CTRL_SFTRST);
    if (s_pPwm[PWM_CTRL / sizeof(UINT32)] & PWM_CTRL_CLKGATE)
        WritePwm(PWM_CTRL_CLR, PWM_CTRL_CLKGATE);
    DDKIomuxSetPinMux(s_Board.MainPin, DDK_IOMUX_MODE_02);
    DDKIomuxSetPadConfig(s_Board.MainPin, DDK_IOMUX_PAD_DRIVE_4MA, DDK_IOMUX_PAD_PULL_ENABLE, DDK_IOMUX_PAD_VOLTAGE_3V3);
    DDKIomuxSetPinMux(s_Board.CurrentPin, DDK_IOMUX_MODE_02);
    DDKIomuxSetPadConfig(s_Board.CurrentPin, DDK_IOMUX_PAD_DRIVE_4MA, DDK_IOMUX_PAD_PULL_ENABLE, DDK_IOMUX_PAD_VOLTAGE_3V3);
}

static void SetPowerStateLocked(CEDEVICE_POWER_STATE State)
{
    s_PowerState = State == D0 ? D0 : D4;
    if (s_PowerState == D0)
    {
        EnableBlock();
        s_AppliedOutput.bLowCurrent = FALSE;
        ApplyMainLocked();
        UpdateSubLocked(TRUE);
        return;
    }
    WritePwm(PWM_CTRL_CLR, PWM_CTRL_MAIN_ENABLE);
    s_AppliedLevel = BKL_LEVEL_OFF;
    s_AppliedOutput.ActiveCounts = 0;
    s_AppliedOutput.bLowCurrent = FALSE;
    UpdateSubLocked(TRUE);
}

static DWORD WINAPI WorkerThread(LPVOID pParameter)
{
    for (;;)
    {
        HANDLE Handles[4] = { s_hQuitEvent, s_hWakeEvent, s_hApplyEvent, s_hSubActiveEvent };
        DWORD Count = 3, Timeout = INFINITE, Result;

        EnterCriticalSection(&s_Lock);
        if (s_bSubOn)
            Timeout = SUB_POLL_MS;
        else if (s_hSubActiveEvent && s_PowerState == D0)
            Count = 4;
        LeaveCriticalSection(&s_Lock);

        Result = WaitForMultipleObjects(Count, Handles, FALSE, Timeout);
        if (Result == WAIT_OBJECT_0 || Result == WAIT_FAILED)
            break;
        EnterCriticalSection(&s_Lock);
        if (Result == WAIT_OBJECT_0 + 2)
            ApplyMainLocked();
        UpdateSubLocked(FALSE);
        LeaveCriticalSection(&s_Lock);
    }
    return 0;
}

static void FindSubFunctions()
{
    if (!s_Board.bSubBacklight)
        return;
    s_hSubModule = LoadLibrary(SUB_MODULE);
    if (!s_hSubModule)
        return;
    s_pSubWriteDataPin = (SUB_WRITE_DATA_PIN_FUNCTION)GetProcAddress(s_hSubModule, SUB_WRITE_DATA_PIN);
    if (!s_pSubWriteDataPin)
    {
        FreeLibrary(s_hSubModule);
        s_hSubModule = NULL;
    }
}

static void CloseHandles()
{
    HANDLE* Handles[] = { &s_hActivatingEvent, &s_hBlankingEvent, &s_hApplyEvent, &s_hSubActiveEvent, &s_hWakeEvent, &s_hQuitEvent };
    DWORD Index;

    for (Index = 0; Index < sizeof(Handles) / sizeof(Handles[0]); Index++)
    {
        if (*Handles[Index])
            CloseHandle(*Handles[Index]);
        *Handles[Index] = NULL;
    }
}

static BOOL WriteDword(PBYTE pOut, DWORD OutSize, PDWORD pReturned, DWORD Value)
{
    if (!pOut || OutSize < sizeof(DWORD))
        return TRUE;
    *(DWORD*)pOut = Value;
    if (pReturned)
        *pReturned = sizeof(DWORD);
    return TRUE;
}

static BOOL ReadDword(PBYTE pIn, DWORD InSize, DWORD* pValue)
{
    if (!pIn || InSize < sizeof(DWORD))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pValue = *(DWORD*)pIn;
    return TRUE;
}

static BOOL SetUserLevel(PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    DWORD Level, Previous;

    if (!ReadDword(pIn, InSize, &Level))
        return FALSE;
    EnterCriticalSection(&s_Lock);
    Previous = s_UserLevel;
    if (Level != BKL_LEVEL_QUERY)
    {
        if (Level == BKL_LEVEL_OFF || Level > SHARP_BRIGHTEST_LEVEL)
        {
            LeaveCriticalSection(&s_Lock);
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        s_UserLevel = ClampLevel(Level);
        ApplyMainLocked();
    }
    LeaveCriticalSection(&s_Lock);
    if (Level != BKL_LEVEL_QUERY)
        SignalNamedEvent(UPDATE_EVENT);
    return WriteDword(pOut, OutSize, pReturned, Previous);
}

static BOOL SetKeepLevel(PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    DWORD Level, Previous;

    if (!ReadDword(pIn, InSize, &Level))
        return FALSE;
    EnterCriticalSection(&s_Lock);
    Previous = s_KeepLevel;
    if (Level != BKL_LEVEL_QUERY)
    {
        s_KeepLevel = Level;
        ApplyMainLocked();
    }
    LeaveCriticalSection(&s_Lock);
    if (Level != BKL_LEVEL_QUERY)
        SignalNamedEvent(UPDATE_EVENT);
    return WriteDword(pOut, OutSize, pReturned, Previous);
}

static BOOL SetFactoryRow(PBYTE pIn, DWORD InSize, DWORD Row)
{
    if (!s_Board.FactoryRowsAddress || !pIn || InSize < FACTORY_ROW_BYTES || Row >= s_Board.FactoryRowCount)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy((void*)(s_Board.FactoryRowsAddress + Row * FACTORY_ROW_BYTES), pIn, FACTORY_ROW_BYTES);
    return TRUE;
}

static BOOL GetInfo(PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    BKL_INFO Info;

    if (!pOut || OutSize < sizeof(Info))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    EnterCriticalSection(&s_Lock);
    Info.Magic = BKL_INFO_MAGIC;
    Info.Version = BKL_INFO_VERSION;
    Info.Capabilities = BKL_CAPS_CONTINUOUS_LEVEL | (s_Board.bSubBacklight ? BKL_CAPS_SUB_BACKLIGHT : 0);
    Info.AppliedLevel = s_AppliedLevel;
    Info.ActiveCounts = s_AppliedOutput.ActiveCounts;
    Info.PeriodCounts = s_Board.PeriodCounts;
    Info.LowCurrent = s_AppliedOutput.bLowCurrent;
    LeaveCriticalSection(&s_Lock);
    memcpy(pOut, &Info, sizeof(Info));
    if (pReturned)
        *pReturned = sizeof(Info);
    return TRUE;
}

static BOOL PowerCapabilities(PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    POWER_CAPABILITIES Capabilities;

    if (!pOut || OutSize < sizeof(Capabilities))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(&Capabilities, 0, sizeof(Capabilities));
    Capabilities.DeviceDx = DX_MASK(D0) | DX_MASK(D4);
    memcpy(pOut, &Capabilities, sizeof(Capabilities));
    if (pReturned)
        *pReturned = sizeof(Capabilities);
    return TRUE;
}

static BOOL PowerSet(PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    CEDEVICE_POWER_STATE State;

    if (!pOut || OutSize < sizeof(CEDEVICE_POWER_STATE))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    State = *(CEDEVICE_POWER_STATE*)pOut;
    EnterCriticalSection(&s_Lock);
    SetPowerStateLocked(State);
    State = s_PowerState;
    LeaveCriticalSection(&s_Lock);
    SetEvent(s_hWakeEvent);
    *(CEDEVICE_POWER_STATE*)pOut = State;
    if (pReturned)
        *pReturned = sizeof(CEDEVICE_POWER_STATE);
    return TRUE;
}

static BOOL PowerGet(PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    if (!pOut || OutSize < sizeof(CEDEVICE_POWER_STATE))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *(CEDEVICE_POWER_STATE*)pOut = s_PowerState;
    if (pReturned)
        *pReturned = sizeof(CEDEVICE_POWER_STATE);
    return TRUE;
}

static BOOL PowerQuery(PBYTE pOut, DWORD OutSize)
{
    CEDEVICE_POWER_STATE State;

    if (!pOut || OutSize < sizeof(CEDEVICE_POWER_STATE))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    State = *(CEDEVICE_POWER_STATE*)pOut;
    return State >= D0 && State < PwrDeviceMaximum;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD Reason, LPVOID pReserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls((HMODULE)hInstance);
        InitializeCriticalSection(&s_Lock);
    }
    return TRUE;
}

DWORD BKL_Init(LPCTSTR pContext)
{
    PHYSICAL_ADDRESS Physical;

    if (s_bInitialised)
        return 0;
    Physical.QuadPart = PWM_BASE;
    s_pPwm = (volatile UINT32*)MmMapIoSpace(Physical, 0x1000, FALSE);
    Physical.QuadPart = CLKCTRL_BASE;
    s_pClkctrl = (volatile UINT32*)MmMapIoSpace(Physical, 0x1000, FALSE);
    s_hActivatingEvent = CreateEvent(NULL, TRUE, TRUE, ACTIVATING_EVENT);
    s_hBlankingEvent = CreateEvent(NULL, TRUE, FALSE, BLANKING_EVENT);
    s_hApplyEvent = CreateEvent(NULL, FALSE, FALSE, APPLY_EVENT);
    if (s_Board.bSubBacklight)
        s_hSubActiveEvent = CreateEvent(NULL, TRUE, FALSE, SUB_ACTIVE_EVENT);
    s_hWakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    s_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s_pPwm || !s_hActivatingEvent || !s_hBlankingEvent || !s_hApplyEvent || !s_hWakeEvent || !s_hQuitEvent
        || (s_Board.bSubBacklight && !s_hSubActiveEvent))
        goto Failed;
    FindSubFunctions();

    EnterCriticalSection(&s_Lock);
    SetPowerStateLocked(D0);
    LeaveCriticalSection(&s_Lock);

    s_hWorker = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    if (!s_hWorker)
        goto Failed;
    s_bInitialised = TRUE;
    return DEVICE_CONTEXT;

Failed:
    CloseHandles();
    if (s_hSubModule)
        FreeLibrary(s_hSubModule);
    s_hSubModule = NULL;
    s_pSubWriteDataPin = NULL;
    if (s_pClkctrl)
        MmUnmapIoSpace((PVOID)s_pClkctrl, 0x1000);
    if (s_pPwm)
        MmUnmapIoSpace((PVOID)s_pPwm, 0x1000);
    s_pClkctrl = NULL;
    s_pPwm = NULL;
    return 0;
}

BOOL BKL_Deinit(DWORD Context)
{
    if (!s_bInitialised)
        return FALSE;
    SetEvent(s_hQuitEvent);
    WaitForSingleObject(s_hWorker, INFINITE);
    CloseHandle(s_hWorker);
    s_hWorker = NULL;
    EnterCriticalSection(&s_Lock);
    SetPowerStateLocked(D4);
    LeaveCriticalSection(&s_Lock);
    CloseHandles();
    if (s_hSubModule)
        FreeLibrary(s_hSubModule);
    s_hSubModule = NULL;
    s_pSubWriteDataPin = NULL;
    if (s_pClkctrl)
        MmUnmapIoSpace((PVOID)s_pClkctrl, 0x1000);
    MmUnmapIoSpace((PVOID)s_pPwm, 0x1000);
    s_pClkctrl = NULL;
    s_pPwm = NULL;
    s_bInitialised = FALSE;
    return TRUE;
}

DWORD BKL_Open(DWORD Context, DWORD Access, DWORD ShareMode)
{
    return Context;
}

BOOL BKL_Close(DWORD OpenContext)
{
    return TRUE;
}

DWORD BKL_Read(DWORD OpenContext, LPVOID pBuffer, DWORD Count)
{
    return 0;
}

DWORD BKL_Write(DWORD OpenContext, LPCVOID pBuffer, DWORD Count)
{
    return 0;
}

DWORD BKL_Seek(DWORD OpenContext, LONG Distance, DWORD Method)
{
    return (DWORD)-1;
}

void BKL_PowerDown(DWORD Context)
{
}

void BKL_PowerUp(DWORD Context)
{
}

BOOL BKL_IOControl(DWORD OpenContext, DWORD Code, PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    if (pReturned)
        *pReturned = 0;
    switch (Code)
    {
    case BKL_IOCTL_LEVEL:
        return SetUserLevel(pIn, InSize, pOut, OutSize, pReturned);
    case BKL_IOCTL_KEEP_LEVEL:
        return SetKeepLevel(pIn, InSize, pOut, OutSize, pReturned);
    case BKL_IOCTL_SET_FACTORY_ROW:
        return SetFactoryRow(pIn, InSize, OutSize);
    case BKL_IOCTL_GET_INFO:
        return GetInfo(pOut, OutSize, pReturned);
    case IOCTL_POWER_CAPABILITIES:
        return PowerCapabilities(pOut, OutSize, pReturned);
    case IOCTL_POWER_SET:
        return PowerSet(pOut, OutSize, pReturned);
    case IOCTL_POWER_GET:
        return PowerGet(pOut, OutSize, pReturned);
    case IOCTL_POWER_QUERY:
        return PowerQuery(pOut, OutSize);
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
