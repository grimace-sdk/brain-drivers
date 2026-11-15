//------------------------------------------------------------------------------
//
//  File:  board.c
//
//  BRD1:, the PW-A7300 board driver: CPU clock scaling, the idle power
//  policy, a read-only register dump and a GPIO edge counter (see
//  board_ioctl.h). Loaded at boot from
//  HKLM\Drivers\BuiltIn\Board, which the boot bundle adds to Sharp's
//  registry.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "cpu_scaling.h"
#include "power_policy.h"
#include "register_dump.h"
#include "pin_sampler.h"
#include "board_ioctl.h"
#ifdef BRAIN_BOARD_pwa7300
#include "framebuffer.h"
#endif

#define DRIVER_VERSION              1
#define DEVICE_CONTEXT              0x42524431

//------------------------------------------------------------------------------
//
// Function: BRD_Init
//
// Starts the CPU clock governor and the idle power policy.
//
// Parameters:
//      pContext
//          [in] Registry path of the driver's key.
//
// Returns:
//      Device context.
//
//------------------------------------------------------------------------------
DWORD BRD_Init(LPCTSTR pContext)
{
    RETAILMSG(TRUE, (TEXT("Board: driver v%u\r\n"), DRIVER_VERSION));
    CpuScalingInit();
    PowerPolicyInit();
    return DEVICE_CONTEXT;
}

BOOL BRD_Deinit(DWORD Context)
{
    return TRUE;
}

DWORD BRD_Open(DWORD Context, DWORD Access, DWORD ShareMode)
{
    return Context;
}

BOOL BRD_Close(DWORD OpenContext)
{
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: BRD_PowerDown
//
// Returns the CPU to Sharp's clock before suspend.
//
// Parameters:
//      Context
//          [in] Device context.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void BRD_PowerDown(DWORD Context)
{
    CpuScalingSuspend();
}

//------------------------------------------------------------------------------
//
// Function: BRD_PowerUp
//
// Lets the governor run again after resume.
//
// Parameters:
//      Context
//          [in] Device context.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void BRD_PowerUp(DWORD Context)
{
    CpuScalingResume();
}

//------------------------------------------------------------------------------
//
// Function: BRD_IOControl
//
// Handles the codes in board_ioctl.h.
//
// Parameters:
//      OpenContext
//          [in] Open context.
//
//      Code
//          [in] BRD_IOCTL_*.
//
//      pIn, InSize
//          [in] Input buffer.
//
//      pOut, OutSize
//          [out] Output buffer.
//
//      pReturned
//          [out] Bytes written to pOut.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL BRD_IOControl(DWORD OpenContext, DWORD Code, PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    if (pReturned)
        *pReturned = 0;

    if (Code == BRD_IOCTL_GET_INFO)
    {
        BRD_INFO* pInfo = (BRD_INFO*)pOut;

        if (!pInfo || OutSize < sizeof(BRD_INFO))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        pInfo->Magic = BRD_INFO_MAGIC;
        pInfo->Version = DRIVER_VERSION;
        if (pReturned)
            *pReturned = sizeof(BRD_INFO);
        return TRUE;
    }

    if (Code == BRD_IOCTL_CPU_SCALING)
    {
        if (pIn && InSize >= sizeof(DWORD))
        {
            DWORD Request = *(const DWORD*)pIn;
            if (!CpuScalingSetMode(Request & ~BRD_CPU_SCALING_SAVE, (Request & BRD_CPU_SCALING_SAVE) != 0))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
        }
        if (pOut && OutSize >= sizeof(BRD_CPU_STATUS))
        {
            CpuScalingGetStatus((CPU_SCALING_STATUS*)pOut);
            if (pReturned)
                *pReturned = sizeof(BRD_CPU_STATUS);
        }
        return TRUE;
    }

    if (Code == BRD_IOCTL_READ_REGISTERS)
    {
        const BRD_REGISTER_READ* pRead = (const BRD_REGISTER_READ*)pIn;
        DWORD Read;

        if (!pRead || InSize < sizeof(BRD_REGISTER_READ) || !pOut || OutSize < pRead->Count * sizeof(DWORD))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        Read = RegisterDump(pRead->Physical, (DWORD*)pOut, pRead->Count);
        if (pReturned)
            *pReturned = Read * sizeof(DWORD);
        if (!Read)
            SetLastError(ERROR_ACCESS_DENIED);
        return Read != 0;
    }

    if (Code == BRD_IOCTL_SAMPLE_PINS)
    {
        if (!pIn || InSize < sizeof(DWORD) || !pOut || OutSize < sizeof(BRD_PIN_SAMPLE))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (!PinSample(*(const DWORD*)pIn, (BRD_PIN_SAMPLE*)pOut))
            return FALSE;
        if (pReturned)
            *pReturned = sizeof(BRD_PIN_SAMPLE);
        return TRUE;
    }

#ifdef BRAIN_BOARD_pwa7300
    if (Code == BRD_IOCTL_READ_FRAMEBUFFER)
        return FramebufferRead(pOut, OutSize, pReturned);
#endif

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
