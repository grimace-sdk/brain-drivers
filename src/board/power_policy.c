//------------------------------------------------------------------------------
//
//  File:  power_policy.c
//
//  Makes Sharp's power manager follow the Control Panel's power schemes.
//  Sharp's pm.dll runs Microsoft's On, UserIdle, SystemIdle and Suspend
//  timeouts on its own EdUserActivity and EdSystemActivity timers. Only
//  Sharp's touch drivers reset EdUserActivity, while GWES resets the standard
//  UserActivity timer for every input, keys included, so while that is active
//  this resets EdUserActivity every second. The standard SystemActivity timer
//  is not passed on: on these Brains something resets it about 20 times a
//  second, idle or not, which would keep the system out of suspend. Programs
//  that must stay awake reset EdSystemActivity, as Sharp's SharpLib does.
//  Sharp's power manager reads the timeouts at boot, before they are restored
//  from the card, so any change to them is passed on as a reload.
//
//  On UserIdle and SystemIdle it dims the backlight through Sharp's driver
//  and holds the CPU at Sharp's clock until the system is On again. Turning
//  the backlight and display off in SystemIdle, and suspending, are left to
//  Sharp's power manager. Sharp's edna2_powermgr.dll (PWR1:) has its own
//  fixed backlight-off and power-off timers; both are turned off so the
//  timeouts have one owner.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <msgqueue.h>
#include <pm.h>
#include "cpu_scaling.h"
#include "power_policy.h"

#define PULSE_MS                    1000
#define OPEN_RETRY_MS               1000
#define OPEN_ATTEMPTS               60
#define QUEUE_MESSAGES              4
#define STATE_NAME_CHARACTERS       32
#define BACKLIGHT_ACTIVATING_EVENT  TEXT("Display_Event_Backlight_Activating")
#define BACKLIGHT_BLANKING_EVENT    TEXT("Display_Event_Backlight_DIMBlanking")
#define BACKLIGHT_APPLY_EVENT       TEXT("Display_Event_PowerManager_Update")
#define SHARP_POWER_DEVICE          TEXT("PWR1:")
#define IOCTL_SHARP_POWER_OFF_TIME  0x0022002C
#define IOCTL_SHARP_LIGHT_OFF_TIME  0x00220030
#define TIMEOUTS_KEY                TEXT("System\\CurrentControlSet\\Control\\Power\\Timeouts")
#define RELOAD_TIMEOUTS_EVENT       TEXT("PowerManager/ReloadActivityTimeouts")

typedef struct
{
    LPCTSTR pActiveName;
    LPCTSTR pResetName;
    HANDLE hActive;
    HANDLE hReset;
    BOOL bActive;
    DWORD LastPulse;
} ACTIVITY_BRIDGE;

typedef struct
{
    POWER_BROADCAST Broadcast;
    WCHAR Name[STATE_NAME_CHARACTERS];
} POWER_MESSAGE;

static ACTIVITY_BRIDGE s_Bridges[] =
{
    { TEXT("PowerManager/UserActivity_Active"), TEXT("PowerManager/ActivityTimer/EdUserActivity") },
};

#define BRIDGE_COUNT                (sizeof(s_Bridges) / sizeof(s_Bridges[0]))

HANDLE WINAPI CeFindFirstRegChange(HKEY hKey, BOOL bWatchSubtree, DWORD Filter);
BOOL WINAPI CeFindNextRegChange(HANDLE hChange);

static HANDLE s_hQueue;
static HANDLE s_hTimeoutsChange = INVALID_HANDLE_VALUE;
static BOOL s_bIdle;

static BOOL OpenBridges()
{
    DWORD Index, Attempt;

    for (Attempt = 0; Attempt < OPEN_ATTEMPTS; Attempt++)
    {
        BOOL bOpened = TRUE;

        for (Index = 0; Index < BRIDGE_COUNT; Index++)
        {
            ACTIVITY_BRIDGE* pBridge = &s_Bridges[Index];

            if (!pBridge->hActive)
                pBridge->hActive = OpenEvent(EVENT_ALL_ACCESS, FALSE, pBridge->pActiveName);
            if (!pBridge->hActive)
                bOpened = FALSE;
        }
        if (bOpened)
            break;
        Sleep(OPEN_RETRY_MS);
    }
    for (Index = 0; Index < BRIDGE_COUNT; Index++)
    {
        ACTIVITY_BRIDGE* pBridge = &s_Bridges[Index];

        if (!pBridge->hActive)
        {
            RETAILMSG(TRUE, (TEXT("PowerPolicy: no %s\r\n"), pBridge->pActiveName));
            return FALSE;
        }
        pBridge->hReset = CreateEvent(NULL, FALSE, FALSE, pBridge->pResetName);
        if (!pBridge->hReset)
            return FALSE;
    }
    return TRUE;
}

static void OpenPowerQueue()
{
    MSGQUEUEOPTIONS Options;

    memset(&Options, 0, sizeof(Options));
    Options.dwSize = sizeof(Options);
    Options.dwMaxMessages = QUEUE_MESSAGES;
    Options.cbMaxMessage = sizeof(POWER_MESSAGE);
    Options.bReadAccess = TRUE;
    s_hQueue = CreateMsgQueue(NULL, &Options);
    if (s_hQueue && !RequestPowerNotifications(s_hQueue, PBT_TRANSITION))
    {
        CloseMsgQueue(s_hQueue);
        s_hQueue = NULL;
    }
}

static void WatchTimeouts()
{
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TIMEOUTS_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return;
    s_hTimeoutsChange = CeFindFirstRegChange(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET);
    RegCloseKey(hKey);
    if (s_hTimeoutsChange == NULL)
        s_hTimeoutsChange = INVALID_HANDLE_VALUE;
}

static void ReloadTimeouts()
{
    HANDLE hReload = OpenEvent(EVENT_ALL_ACCESS, FALSE, RELOAD_TIMEOUTS_EVENT);

    RETAILMSG(TRUE, (TEXT("PowerPolicy: timeouts changed\r\n")));
    if (hReload)
    {
        SetEvent(hReload);
        CloseHandle(hReload);
    }
}

static BOOL DisableSharpTimers()
{
    HANDLE hDevice = CreateFile(SHARP_POWER_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, 0, NULL);
    DWORD Never = 0, Returned;

    if (hDevice == INVALID_HANDLE_VALUE)
        return FALSE;
    DeviceIoControl(hDevice, IOCTL_SHARP_POWER_OFF_TIME, &Never, sizeof(Never), NULL, 0, &Returned, NULL);
    DeviceIoControl(hDevice, IOCTL_SHARP_LIGHT_OFF_TIME, &Never, sizeof(Never), NULL, 0, &Returned, NULL);
    CloseHandle(hDevice);
    RETAILMSG(TRUE, (TEXT("PowerPolicy: Sharp timers off\r\n")));
    return TRUE;
}

static void SetNamedEvent(LPCTSTR pName, BOOL bSignalled)
{
    HANDLE hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, pName);

    if (!hEvent)
        return;
    if (bSignalled)
        SetEvent(hEvent);
    else
        ResetEvent(hEvent);
    CloseHandle(hEvent);
}

static void SetBacklightDimmed(BOOL bDimmed)
{
    SetNamedEvent(BACKLIGHT_ACTIVATING_EVENT, !bDimmed);
    SetNamedEvent(BACKLIGHT_BLANKING_EVENT, !bDimmed);
    SetNamedEvent(BACKLIGHT_APPLY_EVENT, TRUE);
}

static void ApplyPowerState(LPCWSTR pName)
{
    BOOL bIdle;

    if (!_wcsicmp(pName, L"useridle") || !_wcsicmp(pName, L"systemidle"))
        bIdle = TRUE;
    else if (!_wcsicmp(pName, L"on"))
        bIdle = FALSE;
    else
        return;
    RETAILMSG(TRUE, (TEXT("PowerPolicy: %s\r\n"), pName));
    if (bIdle == s_bIdle)
        return;
    s_bIdle = bIdle;
    SetBacklightDimmed(bIdle);
    CpuScalingSetIdle(bIdle);
}

static void ReadPowerMessages()
{
    POWER_MESSAGE Message;
    DWORD Read, Flags;

    while (ReadMsgQueue(s_hQueue, &Message, sizeof(Message), &Read, 0, &Flags))
    {
        if (Message.Broadcast.Message != PBT_TRANSITION || Read < sizeof(POWER_BROADCAST))
            continue;
        Message.Name[STATE_NAME_CHARACTERS - 1] = 0;
        ApplyPowerState(Message.Broadcast.SystemPowerState);
    }
}

static DWORD WINAPI PolicyThread(LPVOID pContext)
{
    DWORD Attempt;

    RETAILMSG(TRUE, (TEXT("PowerPolicy: thread %08x\r\n"), GetCurrentThreadId()));
    if (!OpenBridges())
        return 0;
    OpenPowerQueue();
    WatchTimeouts();
    ReloadTimeouts();
    for (Attempt = 0; Attempt < OPEN_ATTEMPTS && !DisableSharpTimers(); Attempt++)
        Sleep(OPEN_RETRY_MS);
    for (;;)
    {
        HANDLE Handles[BRIDGE_COUNT + 2];
        DWORD Count = 0, Timeout = INFINITE, Now, Index;

        if (s_hQueue)
            Handles[Count++] = s_hQueue;
        if (s_hTimeoutsChange != INVALID_HANDLE_VALUE)
            Handles[Count++] = s_hTimeoutsChange;
        for (Index = 0; Index < BRIDGE_COUNT; Index++)
        {
            if (s_Bridges[Index].bActive)
                Timeout = PULSE_MS;
            else
                Handles[Count++] = s_Bridges[Index].hActive;
        }
        if (Count)
            WaitForMultipleObjects(Count, Handles, FALSE, Timeout);
        else
            Sleep(Timeout);
        if (s_hQueue)
            ReadPowerMessages();
        if (s_hTimeoutsChange != INVALID_HANDLE_VALUE && WaitForSingleObject(s_hTimeoutsChange, 0) == WAIT_OBJECT_0)
        {
            CeFindNextRegChange(s_hTimeoutsChange);
            ReloadTimeouts();
        }
        Now = GetTickCount();
        for (Index = 0; Index < BRIDGE_COUNT; Index++)
        {
            ACTIVITY_BRIDGE* pBridge = &s_Bridges[Index];

            pBridge->bActive = WaitForSingleObject(pBridge->hActive, 0) == WAIT_OBJECT_0;
            if (pBridge->bActive && Now - pBridge->LastPulse >= PULSE_MS)
            {
                SetEvent(pBridge->hReset);
                pBridge->LastPulse = Now;
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
//
// Function: PowerPolicyInit
//
// Starts the activity bridge and the idle policy.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void PowerPolicyInit()
{
    HANDLE hThread = CreateThread(NULL, 0, PolicyThread, NULL, 0, NULL);

    if (hThread)
        CloseHandle(hThread);
}
