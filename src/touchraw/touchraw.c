//------------------------------------------------------------------------------
//
//  File:  touchraw.c
//
//  TRD1:, a pass-through to Sharp's PW-SH2 touchraw.dll (in ROM as
//  touchra_.dll once the boot bundle replaces it). It records every sample
//  Sharp's touch driver reads, for TRD_IOCTL_READ_LOG, and stretches the
//  raw bands next to the left, top and bottom edges. It also raises Sharp's
//  pressure limit so lighter touches register.
//
//  Sharp's touch driver maps raw to screen with the unit's factory
//  calibration, which puts the furthest reachable raw values about 13 px
//  inside the screen. Each stretched edge maps raw Band..Reach onto
//  Band..Target, where Target is the raw value the factory calibration puts
//  on the screen edge; the rest of the screen is left alone. Settings are
//  in HKLM\Software\Brain\Touch, read at TRD_Init.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include "touchraw_ioctl.h"

#define ORIGINAL_DLL                TEXT("touchra_.dll")
#define SETTINGS_KEY                TEXT("Software\\Brain\\Touch")
#define DISPLAY_KEY                 TEXT("Software\\Brain\\Display")
#define CALIBRATION_DEVICE          TEXT("VMC1:")
#define RAW_MAXIMUM                 4095
#define CALIBRATED_WIDTH            800
#define TRD_IOCTL_SET_PRESSURE_LIMIT 0x80002043
#define VMC_IOCTL_TOUCH_CALIBRATION 0x80002093

#if defined(BRAIN_BOARD_pwsh4)
#define DEFAULT_STRETCH             FALSE
#define DEFAULT_PRESSURE_LIMIT      0
#else
#define DEFAULT_STRETCH             TRUE
#define DEFAULT_PRESSURE_LIMIT      3072
#endif

typedef struct
{
    LPCTSTR pName;
    LONG Band;
    LONG Reach;
    LONG Target;
} EDGE;

typedef struct
{
    SHORT XNumerator;
    SHORT XDenominator;
    SHORT XOffset;
    SHORT YNumerator;
    SHORT YDenominator;
    SHORT YOffset;
} FACTORY_CALIBRATION;

typedef DWORD (*INIT_FUNCTION)(DWORD);
typedef BOOL (*CONTEXT_FUNCTION)(DWORD);
typedef DWORD (*OPEN_FUNCTION)(DWORD, DWORD, DWORD);
typedef DWORD (*READ_FUNCTION)(DWORD, LPVOID, DWORD);
typedef BOOL (*IOCONTROL_FUNCTION)(DWORD, DWORD, PBYTE, DWORD, PBYTE, DWORD, PDWORD);
typedef void (*POWER_FUNCTION)(DWORD);

static HMODULE s_hOriginal;
static INIT_FUNCTION s_pInit;
static CONTEXT_FUNCTION s_pDeinit;
static OPEN_FUNCTION s_pOpen;
static CONTEXT_FUNCTION s_pClose;
static READ_FUNCTION s_pRead;
static IOCONTROL_FUNCTION s_pIOControl;
static POWER_FUNCTION s_pPowerDown;
static POWER_FUNCTION s_pPowerUp;
static CRITICAL_SECTION s_LogLock;
static TRD_LOG s_Log;
static BOOL s_bStretch = DEFAULT_STRETCH;
static DWORD s_PressureLimit = DEFAULT_PRESSURE_LIMIT;
static BOOL s_bScaleResolved;
static LONG s_ScreenWidth;
static LONG s_ScaleOffset;

static EDGE s_LeftEdge = { TEXT("Left"), 400, 205, 147 };
static EDGE s_TopEdge = { TEXT("Top"), 3600, 3833, 3933 };
static EDGE s_BottomEdge = { TEXT("Bottom"), 560, 325, 253 };

static LONG ReadSetting(HKEY hKey, LPCTSTR pName, LPCTSTR pSuffix, LONG Default)
{
    TCHAR Name[32];
    DWORD Value, Size = sizeof(Value), Type;

    wsprintf(Name, TEXT("%s%s"), pName, pSuffix);
    if (RegQueryValueEx(hKey, Name, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        return Default;
    return (LONG)Value;
}

static void ReadEdge(HKEY hKey, EDGE* pEdge)
{
    pEdge->Band = ReadSetting(hKey, pEdge->pName, TEXT("Band"), pEdge->Band);
    pEdge->Reach = ReadSetting(hKey, pEdge->pName, TEXT("Reach"), pEdge->Reach);
    pEdge->Target = ReadSetting(hKey, pEdge->pName, TEXT("Target"), pEdge->Target);
}

static void ReadSettings()
{
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return;
    s_bStretch = ReadSetting(hKey, TEXT("Stretch"), TEXT(""), s_bStretch) != 0;
    s_PressureLimit = (DWORD)ReadSetting(hKey, TEXT("PressureLimit"), TEXT(""), s_PressureLimit);
    ReadEdge(hKey, &s_LeftEdge);
    ReadEdge(hKey, &s_TopEdge);
    ReadEdge(hKey, &s_BottomEdge);
    RegCloseKey(hKey);
}

static LONG StretchEdge(LONG Value, const EDGE* pEdge)
{
    BOOL bBeyondBand = pEdge->Reach < pEdge->Band ? Value < pEdge->Band : Value > pEdge->Band;

    if (!bBeyondBand || pEdge->Reach == pEdge->Band)
        return Value;
    Value = pEdge->Band + (Value - pEdge->Band) * (pEdge->Target - pEdge->Band) / (pEdge->Reach - pEdge->Band);
    return Value < 0 ? 0 : Value > RAW_MAXIMUM ? RAW_MAXIMUM : Value;
}

static LONG ReadDisplayWidth()
{
    HKEY hKey;
    LONG Width, Height;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, DISPLAY_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return CALIBRATED_WIDTH;
    Width = ReadSetting(hKey, TEXT("FramebufferWidth"), TEXT(""), 0);
    Height = ReadSetting(hKey, TEXT("FramebufferHeight"), TEXT(""), 0);
    RegCloseKey(hKey);
    if (Height > Width)
        Width = Height;
    return Width > 0 ? Width : CALIBRATED_WIDTH;
}

static LONG ReadScreenWidth()
{
    HKEY hKey;
    LONG Width = 0;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) == ERROR_SUCCESS)
    {
        Width = ReadSetting(hKey, TEXT("ScreenWidth"), TEXT(""), 0);
        RegCloseKey(hKey);
    }
    return Width > 0 ? Width : ReadDisplayWidth();
}

static BOOL ReadFactoryCalibration(FACTORY_CALIBRATION* pCalibration)
{
    HANDLE hDevice;
    DWORD Returned = 0;
    BOOL bResult;

    hDevice = CreateFile(CALIBRATION_DEVICE, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        RETAILMSG(1, (TEXT("TouchRaw: cannot open %s (%u)\r\n"), CALIBRATION_DEVICE, GetLastError()));
        return FALSE;
    }
    bResult = DeviceIoControl(hDevice, VMC_IOCTL_TOUCH_CALIBRATION, NULL, 0, pCalibration, sizeof(*pCalibration), &Returned, NULL);
    CloseHandle(hDevice);
    if (!bResult || Returned < sizeof(*pCalibration) || pCalibration->XNumerator == 0 || pCalibration->XDenominator == 0)
    {
        RETAILMSG(1, (TEXT("TouchRaw: no factory calibration (%u)\r\n"), bResult ? 0 : GetLastError()));
        return FALSE;
    }
    return TRUE;
}

static void ResolveScale()
{
    FACTORY_CALIBRATION Calibration;
    LONG Width = ReadScreenWidth();

    s_bScaleResolved = TRUE;
    if (Width == CALIBRATED_WIDTH || !ReadFactoryCalibration(&Calibration))
        return;
    s_ScaleOffset = MulDiv(Calibration.XOffset * (Width - CALIBRATED_WIDTH), Calibration.XDenominator,
        CALIBRATED_WIDTH * Calibration.XNumerator);
    s_ScreenWidth = Width;
    RETAILMSG(1, (TEXT("TouchRaw: x scaled %d/%d, offset %d\r\n"), Width, CALIBRATED_WIDTH, s_ScaleOffset));
}

static LONG ScaleX(LONG Value)
{
    if (!s_ScreenWidth)
        return Value;
    Value = MulDiv(Value, s_ScreenWidth, CALIBRATED_WIDTH) + s_ScaleOffset;
    return Value < 0 ? 0 : Value > RAW_MAXIMUM ? RAW_MAXIMUM : Value;
}

static void StretchSample(LONG* pSample)
{
    if (pSample[0] <= 0)
        return;
    if (!s_bScaleResolved)
        ResolveScale();
    if (s_bStretch)
    {
        pSample[1] = StretchEdge(pSample[1], &s_LeftEdge);
        pSample[2] = StretchEdge(StretchEdge(pSample[2], &s_TopEdge), &s_BottomEdge);
    }
    pSample[1] = ScaleX(pSample[1]);
}

static BOOL LoadOriginal()
{
    if (s_hOriginal)
        return TRUE;
    s_hOriginal = LoadLibrary(ORIGINAL_DLL);
    if (!s_hOriginal)
    {
        RETAILMSG(1, (TEXT("TouchRaw: cannot load %s (%u)\r\n"), ORIGINAL_DLL, GetLastError()));
        return FALSE;
    }
    s_pInit = (INIT_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_Init"));
    s_pDeinit = (CONTEXT_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_Deinit"));
    s_pOpen = (OPEN_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_Open"));
    s_pClose = (CONTEXT_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_Close"));
    s_pRead = (READ_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_Read"));
    s_pIOControl = (IOCONTROL_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_IOControl"));
    s_pPowerDown = (POWER_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_PowerDown"));
    s_pPowerUp = (POWER_FUNCTION)GetProcAddress(s_hOriginal, TEXT("TRD_PowerUp"));
    InitializeCriticalSection(&s_LogLock);
    return s_pInit && s_pIOControl;
}

static void RecordSample(const LONG* pSample)
{
    TRD_LOG_ENTRY* pEntry;

    EnterCriticalSection(&s_LogLock);
    pEntry = &s_Log.Entries[s_Log.Next];
    pEntry->Milliseconds = GetTickCount();
    pEntry->Status = pSample[0];
    pEntry->X = pSample[1];
    pEntry->Y = pSample[2];
    s_Log.Next = (s_Log.Next + 1) % TRD_LOG_ENTRIES;
    if (s_Log.Count < TRD_LOG_ENTRIES)
        s_Log.Count++;
    LeaveCriticalSection(&s_LogLock);
}

DWORD TRD_Init(DWORD Context)
{
    DWORD DeviceContext, Returned;

    if (!LoadOriginal())
        return 0;
    ReadSettings();
    DeviceContext = s_pInit(Context);
    if (DeviceContext && s_PressureLimit)
        s_pIOControl(DeviceContext, TRD_IOCTL_SET_PRESSURE_LIMIT, (PBYTE)&s_PressureLimit, sizeof(s_PressureLimit), NULL, 0, &Returned);
    return DeviceContext;
}

BOOL TRD_Deinit(DWORD Context)
{
    return s_pDeinit ? s_pDeinit(Context) : TRUE;
}

DWORD TRD_Open(DWORD Context, DWORD Access, DWORD ShareMode)
{
    return s_pOpen ? s_pOpen(Context, Access, ShareMode) : Context;
}

BOOL TRD_Close(DWORD OpenContext)
{
    return s_pClose ? s_pClose(OpenContext) : TRUE;
}

DWORD TRD_Read(DWORD OpenContext, LPVOID pBuffer, DWORD Count)
{
    return s_pRead ? s_pRead(OpenContext, pBuffer, Count) : 0;
}

void TRD_PowerDown(DWORD Context)
{
    if (s_pPowerDown)
        s_pPowerDown(Context);
}

void TRD_PowerUp(DWORD Context)
{
    if (s_pPowerUp)
        s_pPowerUp(Context);
}

//------------------------------------------------------------------------------
//
// Function: TRD_IOControl
//
// Answers TRD_IOCTL_READ_LOG with the sample log and clears it; passes
// everything else to Sharp's driver, recording TRD_IOCTL_SAMPLE results as
// read and then stretching their edges.
//
//------------------------------------------------------------------------------
BOOL TRD_IOControl(DWORD OpenContext, DWORD Code, PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    BOOL bResult;

    if (Code == TRD_IOCTL_READ_LOG)
    {
        if (!pOut || OutSize < sizeof(TRD_LOG))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        EnterCriticalSection(&s_LogLock);
        memcpy(pOut, &s_Log, sizeof(TRD_LOG));
        s_Log.Next = 0;
        s_Log.Count = 0;
        LeaveCriticalSection(&s_LogLock);
        if (pReturned)
            *pReturned = sizeof(TRD_LOG);
        return TRUE;
    }

    bResult = s_pIOControl(OpenContext, Code, pIn, InSize, pOut, OutSize, pReturned);
    if (bResult && Code == TRD_IOCTL_SAMPLE && pOut && OutSize >= 3 * sizeof(LONG))
    {
        RecordSample((const LONG*)pOut);
        StretchSample((LONG*)pOut);
    }
    return bResult;
}
