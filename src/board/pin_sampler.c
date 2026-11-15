//------------------------------------------------------------------------------
//
//  File:  pin_sampler.c
//
//  Samples HW_PINCTRL_DIN0-4 in a loop for BRD_IOCTL_SAMPLE_PINS and counts
//  rising edges per pin. Pin control is only read, never reconfigured.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "pin_sampler.h"

#define PINCTRL_BASE                0x80018000
#define PINCTRL_SIZE                0x1000
#define PINCTRL_DIN0                0x900
#define PINCTRL_BANK_STRIDE         0x10

static volatile UINT32* s_pPinctrl;

//------------------------------------------------------------------------------
//
// Function: PinSample
//
// Reads the GPIO inputs for a while and counts rising edges on each pin.
//
// Parameters:
//      Milliseconds
//          [in] How long to sample, up to BRD_PIN_SAMPLE_LIMIT_MS.
//
//      pResult
//          [out] Sample count, rising edges per pin and their first and last
//          ticks.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL PinSample(DWORD Milliseconds, BRD_PIN_SAMPLE* pResult)
{
    UINT32 Previous[BRD_PIN_BANKS];
    DWORD Bank, Start, Now;

    if (Milliseconds > BRD_PIN_SAMPLE_LIMIT_MS)
        Milliseconds = BRD_PIN_SAMPLE_LIMIT_MS;
    if (!s_pPinctrl)
    {
        PHYSICAL_ADDRESS Address;
        Address.QuadPart = PINCTRL_BASE;
        s_pPinctrl = (volatile UINT32*)MmMapIoSpace(Address, PINCTRL_SIZE, FALSE);
        if (!s_pPinctrl)
            return FALSE;
    }
    memset(pResult, 0, sizeof(*pResult));
    for (Bank = 0; Bank < BRD_PIN_BANKS; Bank++)
        Previous[Bank] = s_pPinctrl[(PINCTRL_DIN0 + Bank * PINCTRL_BANK_STRIDE) / 4];
    Start = GetTickCount();
    do
    {
        Now = GetTickCount();
        for (Bank = 0; Bank < BRD_PIN_BANKS; Bank++)
        {
            UINT32 Value = s_pPinctrl[(PINCTRL_DIN0 + Bank * PINCTRL_BANK_STRIDE) / 4];
            UINT32 Rising = Value & ~Previous[Bank];
            DWORD Pin;

            Previous[Bank] = Value;
            for (Pin = 0; Rising; Pin++, Rising >>= 1)
            {
                if (!(Rising & 1))
                    continue;
                if (!pResult->Rising[Bank][Pin])
                    pResult->FirstRise[Bank][Pin] = Now - Start;
                pResult->LastRise[Bank][Pin] = Now - Start;
                pResult->Rising[Bank][Pin]++;
            }
        }
        pResult->Samples++;
    } while (Now - Start < Milliseconds);
    pResult->Milliseconds = Now - Start;
    return TRUE;
}
