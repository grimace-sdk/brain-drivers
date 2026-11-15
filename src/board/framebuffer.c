//------------------------------------------------------------------------------
//
//  File:  framebuffer.c
//
//  Copies the screen for BRD_IOCTL_READ_FRAMEBUFFER from the framebuffer
//  the display driver records in HKLM\Software\Brain\Display
//  (FramebufferPhysical, FramebufferWidth, FramebufferHeight; RGB565 with
//  the width as stride). The data cache is written back first, so drawing
//  still in it is included. In present mode this is the desktop behind the
//  app, not what the panel shows.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "ce_compat.h"
#include "board_ioctl.h"
#include "framebuffer.h"

#define DISPLAY_KEY                 TEXT("Software\\Brain\\Display")
#define FRAMEBUFFER_BITS            16
#define FRAMEBUFFER_SIZE_LIMIT      (4 * 1024 * 1024)

static DWORD ReadDisplayValue(HKEY hKey, LPCTSTR pName)
{
    DWORD Value = 0, Size = sizeof(Value), Type;

    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        return 0;
    return Value;
}

//------------------------------------------------------------------------------
//
// Function: FramebufferRead
//
// Parameters:
//      pOut, OutSize
//          [out] BRD_FRAMEBUFFER, then the pixels.
//
//      pReturned
//          [out] Bytes written to pOut.
//
// Returns:
//      TRUE if the header and pixels were copied.
//
//------------------------------------------------------------------------------
BOOL FramebufferRead(PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    BRD_FRAMEBUFFER* pHeader = (BRD_FRAMEBUFFER*)pOut;
    PHYSICAL_ADDRESS Physical;
    DWORD Bytes;
    HKEY hKey;
    PVOID pPixels;

    if (!pOut || OutSize < sizeof(BRD_FRAMEBUFFER))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, DISPLAY_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
    {
        SetLastError(ERROR_NOT_READY);
        return FALSE;
    }
    Physical.QuadPart = ReadDisplayValue(hKey, TEXT("FramebufferPhysical"));
    pHeader->Width = ReadDisplayValue(hKey, TEXT("FramebufferWidth"));
    pHeader->Height = ReadDisplayValue(hKey, TEXT("FramebufferHeight"));
    pHeader->BitsPerPixel = FRAMEBUFFER_BITS;
    RegCloseKey(hKey);

    Bytes = pHeader->Width * pHeader->Height * FRAMEBUFFER_BITS / 8;
    if (!Physical.QuadPart || !Bytes || Bytes > FRAMEBUFFER_SIZE_LIMIT)
    {
        SetLastError(ERROR_NOT_READY);
        return FALSE;
    }
    if (pReturned)
        *pReturned = sizeof(BRD_FRAMEBUFFER);
    if (OutSize < sizeof(BRD_FRAMEBUFFER) + Bytes)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    pPixels = MmMapIoSpace(Physical, Bytes, FALSE);
    if (!pPixels)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    CacheSync(CACHE_SYNC_WRITEBACK);
    memcpy(pOut + sizeof(BRD_FRAMEBUFFER), pPixels, Bytes);
    MmUnmapIoSpace(pPixels, Bytes);
    if (pReturned)
        *pReturned = sizeof(BRD_FRAMEBUFFER) + Bytes;
    return TRUE;
}
