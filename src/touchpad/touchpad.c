//------------------------------------------------------------------------------
//
//  File:  touchpad.c
//
//  PAD1:, the PW-A7300 touchpad: turns the aux touch panel (TPD2:,
//  EDNA2_TouchSub.dll) into CE's mouse. Runs in udevice.exe (user mode) so
//  it can call mouse_event, and waits for the window manager before opening
//  TPD2:.
//
//  TPD2: sends samples only to the most recently opened handle. An app that
//  reads it claims it through PAD1: (see touchpad_ioctl.h); while any claim
//  is held this driver closes its handle, and when the last one goes it
//  opens a new one, which takes the panel back.
//
//  Settings (HKLM\Software\Brain\Touchpad): Enabled (default 1), Speed 1-5
//  (default 3), Acceleration (default 1), TapToClick (default 1). Read again
//  on PAD_IOCTL_RELOAD and every 5 s, as the registry is restored from the
//  card after this driver starts.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <pkfuncs.h>
#include "ce_compat.h"
#include "motion.h"
#include "touchpad_ioctl.h"

#define DRIVER_VERSION              1
#define DEVICE_CONTEXT              0x50414431
#define TOUCH_DEVICE                TEXT("TPD2:")
#define IOCTL_TPD_CREATE_QUEUE      0x80002007
#define IOCTL_TPD_SET_MODE          0x8000200B
#define IOCTL_TPD_SET_EVENT         0x8000200F
#define SAMPLE_QUEUE_LENGTH         256
#define READ_SAMPLES                32
#define RETRY_MS                    1000
#define SETTINGS_POLL_MS            5000
#define DEFAULT_SPEED               3

typedef struct
{
    BOOL bClaimed;
} OPEN_CONTEXT;

static CRITICAL_SECTION s_Lock;
static HANDLE s_hWake;
static HANDLE s_hSampleEvent;
static HANDLE s_hTouch = INVALID_HANDLE_VALUE;
static DWORD s_Claims;
static BOOL s_bEnabled = TRUE;
static MOTION_SETTINGS s_Settings = { DEFAULT_SPEED, TRUE, TRUE };

static DWORD ReadValue(HKEY hKey, LPCTSTR pName, DWORD Default)
{
    DWORD Value, Size = sizeof(Value), Type;

    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        return Default;
    return Value;
}

static void ReadSettings()
{
    MOTION_SETTINGS Settings = { DEFAULT_SPEED, TRUE, TRUE };
    BOOL bEnabled = TRUE;
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, PAD_SETTINGS_KEY, 0, 0, &hKey) == ERROR_SUCCESS)
    {
        bEnabled = ReadValue(hKey, TEXT("Enabled"), 1) != 0;
        Settings.Speed = ReadValue(hKey, TEXT("Speed"), DEFAULT_SPEED);
        Settings.bAcceleration = ReadValue(hKey, TEXT("Acceleration"), 1) != 0;
        Settings.bTapToClick = ReadValue(hKey, TEXT("TapToClick"), 1) != 0;
        RegCloseKey(hKey);
    }
    if (Settings.Speed < PAD_SPEED_MINIMUM || Settings.Speed > PAD_SPEED_MAXIMUM)
        Settings.Speed = DEFAULT_SPEED;
    EnterCriticalSection(&s_Lock);
    s_Settings = Settings;
    s_bEnabled = bEnabled;
    LeaveCriticalSection(&s_Lock);
}

static HANDLE OpenTouchQueue()
{
    HANDLE hTouch = CreateFile(TOUCH_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD Returned;

    if (hTouch == INVALID_HANDLE_VALUE)
        return hTouch;
    if (!DeviceIoControl(hTouch, IOCTL_TPD_CREATE_QUEUE, NULL, 0, NULL, SAMPLE_QUEUE_LENGTH, &Returned, NULL)
        || !DeviceIoControl(hTouch, IOCTL_TPD_SET_MODE, NULL, 1, NULL, 0, &Returned, NULL)
        || !DeviceIoControl(hTouch, IOCTL_TPD_SET_EVENT, &s_hSampleEvent, sizeof(s_hSampleEvent), NULL, 0, &Returned, NULL))
    {
        CloseHandle(hTouch);
        return INVALID_HANDLE_VALUE;
    }
    return hTouch;
}

static void CloseTouchQueue()
{
    if (s_hTouch == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(s_hTouch);
    s_hTouch = INVALID_HANDLE_VALUE;
    MotionReset();
}

static void ReadSamples()
{
    DWORD Samples[READ_SAMPLES], Count, Index;
    MOTION_SETTINGS Settings;

    EnterCriticalSection(&s_Lock);
    Settings = s_Settings;
    LeaveCriticalSection(&s_Lock);
    while (ReadFile(s_hTouch, Samples, sizeof(Samples), &Count, NULL) && Count)
    {
        for (Index = 0; Index < Count / sizeof(DWORD); Index++)
            MotionHandleSample(&Settings, Samples[Index]);
        MotionFlush();
    }
}

static DWORD WINAPI TouchThread(LPVOID pParameter)
{
    HANDLE Handles[2];
    DWORD LastSettings = GetTickCount();

    RETAILMSG(TRUE, (TEXT("Touchpad: thread %08x\r\n"), GetCurrentThreadId()));
    WaitForAPIReady(SH_WMGR, INFINITE);
    Handles[0] = s_hWake;
    Handles[1] = s_hSampleEvent;
    for (;;)
    {
        DWORD Timeout = SETTINGS_POLL_MS, Result;
        BOOL bWanted;

        EnterCriticalSection(&s_Lock);
        bWanted = s_bEnabled && s_Claims == 0;
        LeaveCriticalSection(&s_Lock);
        if (!bWanted)
            CloseTouchQueue();
        else if (s_hTouch == INVALID_HANDLE_VALUE)
        {
            s_hTouch = OpenTouchQueue();
            if (s_hTouch == INVALID_HANDLE_VALUE)
                Timeout = RETRY_MS;
        }

        Result = WaitForMultipleObjects(2, Handles, FALSE, Timeout);
        if (Result == WAIT_OBJECT_0 + 1 && s_hTouch != INVALID_HANDLE_VALUE)
            ReadSamples();
        if (GetTickCount() - LastSettings >= SETTINGS_POLL_MS)
        {
            ReadSettings();
            LastSettings = GetTickCount();
        }
    }
    return 0;
}

static void ChangeClaim(OPEN_CONTEXT* pOpen, BOOL bClaim)
{
    BOOL bLast = FALSE;

    EnterCriticalSection(&s_Lock);
    if (bClaim && !pOpen->bClaimed)
        s_Claims++;
    if (!bClaim && pOpen->bClaimed)
        bLast = --s_Claims == 0;
    pOpen->bClaimed = bClaim;
    LeaveCriticalSection(&s_Lock);
    if (bClaim || bLast)
        SetEvent(s_hWake);
}

//------------------------------------------------------------------------------
//
// Function: PAD_Init
//
// Reads the settings and starts the thread that owns TPD2:.
//
// Parameters:
//      pContext
//          [in] Registry path of the driver's key.
//
// Returns:
//      Device context, or 0 on failure.
//
//------------------------------------------------------------------------------
DWORD PAD_Init(LPCTSTR pContext)
{
    HANDLE hThread;

    InitializeCriticalSection(&s_Lock);
    s_hWake = CreateEvent(NULL, FALSE, FALSE, NULL);
    s_hSampleEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s_hWake || !s_hSampleEvent)
        return 0;
    ReadSettings();
    hThread = CreateThread(NULL, 0, TouchThread, NULL, 0, NULL);
    if (!hThread)
        return 0;
    SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
    CloseHandle(hThread);
    RETAILMSG(TRUE, (TEXT("Touchpad: driver v%u\r\n"), DRIVER_VERSION));
    return DEVICE_CONTEXT;
}

BOOL PAD_Deinit(DWORD Context)
{
    return FALSE;
}

DWORD PAD_Open(DWORD Context, DWORD Access, DWORD ShareMode)
{
    OPEN_CONTEXT* pOpen = (OPEN_CONTEXT*)LocalAlloc(LPTR, sizeof(OPEN_CONTEXT));

    return (DWORD)pOpen;
}

//------------------------------------------------------------------------------
//
// Function: PAD_Close
//
// Drops the handle's claim, if any. Also runs when the owning process exits
// without closing it.
//
// Parameters:
//      OpenContext
//          [in] Open context from PAD_Open.
//
// Returns:
//      TRUE.
//
//------------------------------------------------------------------------------
BOOL PAD_Close(DWORD OpenContext)
{
    OPEN_CONTEXT* pOpen = (OPEN_CONTEXT*)OpenContext;

    if (pOpen)
    {
        ChangeClaim(pOpen, FALSE);
        LocalFree(pOpen);
    }
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: PAD_IOControl
//
// Handles the codes in touchpad_ioctl.h.
//
// Parameters:
//      OpenContext
//          [in] Open context from PAD_Open.
//
//      Code
//          [in] PAD_IOCTL_*.
//
//      pIn, InSize
//          [in] Unused.
//
//      pOut, OutSize
//          [out] PAD_INFO for PAD_IOCTL_GET_INFO.
//
//      pReturned
//          [out] Bytes written to pOut.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL PAD_IOControl(DWORD OpenContext, DWORD Code, PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    OPEN_CONTEXT* pOpen = (OPEN_CONTEXT*)OpenContext;

    if (pReturned)
        *pReturned = 0;
    switch (Code)
    {
    case PAD_IOCTL_GET_INFO:
    {
        PAD_INFO* pInfo = (PAD_INFO*)pOut;

        if (!pInfo || OutSize < sizeof(PAD_INFO))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        EnterCriticalSection(&s_Lock);
        pInfo->Magic = PAD_INFO_MAGIC;
        pInfo->Version = DRIVER_VERSION;
        pInfo->Claims = s_Claims;
        pInfo->Active = s_hTouch != INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&s_Lock);
        if (pReturned)
            *pReturned = sizeof(PAD_INFO);
        return TRUE;
    }
    case PAD_IOCTL_CLAIM:
        ChangeClaim(pOpen, TRUE);
        return TRUE;
    case PAD_IOCTL_RELEASE:
        ChangeClaim(pOpen, FALSE);
        return TRUE;
    case PAD_IOCTL_RELOAD:
        ReadSettings();
        SetEvent(s_hWake);
        return TRUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
