//------------------------------------------------------------------------------
//
//  File:  register_dump.c
//
//  Reads i.MX28 registers for diagnostics (BRD_IOCTL_READ_REGISTERS). Only
//  blocks whose reads have no side effects are allowed: the clock
//  controller, power, digital control, pin control, SSP2 below its data
//  FIFO, the LCDIF apart from its data port, the PXP, and the start of the
//  kernel debug log ring in RAM.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "register_dump.h"

typedef struct
{
    DWORD Base;
    DWORD Size;
    volatile UINT32* pMapped;
} REGISTER_BLOCK;

static REGISTER_BLOCK s_Blocks[] =
{
    { 0x80040000, 0x2000, NULL },
    { 0x80044000, 0x2000, NULL },
    { 0x8001C000, 0x2000, NULL },
    { 0x80018000, 0x2000, NULL },
    { 0x80030000, 0x0180, NULL },
    { 0x800301B0, 0x0050, NULL },
    { 0x80014000, 0x0090, NULL },
    { 0x8002A000, 0x0300, NULL },
    { 0x401E0000, 0x0100, NULL },
};

//------------------------------------------------------------------------------
//
// Function: RegisterDump
//
// Reads consecutive registers from one allowed block.
//
// Parameters:
//      Physical
//          [in] Physical address of the first register, 4-byte aligned.
//
//      pValues
//          [out] Register values.
//
//      Count
//          [in] Number of registers to read.
//
// Returns:
//      Number of registers read; 0 if the range is not allowed.
//
//------------------------------------------------------------------------------
DWORD RegisterDump(DWORD Physical, DWORD* pValues, DWORD Count)
{
    DWORD Index;

    if (Physical & 3)
        return 0;
    for (Index = 0; Index < sizeof(s_Blocks) / sizeof(s_Blocks[0]); Index++)
    {
        REGISTER_BLOCK* pBlock = &s_Blocks[Index];
        DWORD Register;

        if (Physical < pBlock->Base || Physical + Count * sizeof(UINT32) > pBlock->Base + pBlock->Size)
            continue;
        if (!pBlock->pMapped)
        {
            PHYSICAL_ADDRESS Address;
            Address.QuadPart = pBlock->Base;
            pBlock->pMapped = (volatile UINT32*)MmMapIoSpace(Address, pBlock->Size, FALSE);
            if (!pBlock->pMapped)
                return 0;
        }
        for (Register = 0; Register < Count; Register++)
            pValues[Register] = pBlock->pMapped[(Physical - pBlock->Base) / sizeof(UINT32) + Register];
        return Count;
    }
    return 0;
}
