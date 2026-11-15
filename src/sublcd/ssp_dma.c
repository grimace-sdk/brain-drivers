//------------------------------------------------------------------------------
//
//  File:  ssp_dma.c
//
//  Sends 9-bit SPI words on SSP2 by APBH DMA instead of Sharp's SPI2:
//  driver, which feeds the FIFO one word at a time while the caller spins.
//  SPI2: still sets up the port (mode, word length, pins); this then sets
//  the clock, enables DMA in CTRL1 and runs each transfer as one APBH
//  descriptor that writes CTRL0, CMD0, CMD1 and XFER_SIZE before the data,
//  as U-Boot's mxs_spi does. Words are 16 bits each in memory. The caller
//  sleeps while a long transfer runs.
//
//  HKLM\Software\Brain\SubLCD: SpiDma (DWORD, default 1) turns DMA off,
//  SpiClockKHz (default 16000, the Linux driver's rate) sets the clock.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "ce_compat.h"
#include "cspddk.h"
#include "ssp_dma.h"

#define SSP2_BASE                   0x80014000
#define SSP_CTRL0                   0x000
#define SSP_TIMING                  0x070
#define SSP_CTRL1_SET               0x084
#define SSP_CTRL1_CLR               0x088
#define SSP_CTRL0_READ              0x02000000
#define SSP_CTRL0_DATA_XFER         0x01000000
#define SSP_CTRL0_LOCK_CS           0x08000000
#define SSP_CTRL0_IGNORE_CRC        0x04000000
#define SSP_CTRL0_RUN               0x20000000
#define SSP_CTRL1_DMA_ENABLE        0x00002000
#define SSP_TIMING_TIMEOUT_MASK     0xFFFF0000
#define SSP_TIMING_DIVIDE           2

#define CLKCTRL_BASE                0x80040000
#define CLKCTRL_SSP2                0x0B0
#define CLKCTRL_SSP2_DIV_MASK       0x000003FF
#define CLKCTRL_SSP2_BUSY           0x20000000
#define REF_IO1_KHZ                 480000

#define DMA_CHANNEL                 DDK_APBH_CHANNEL_SSP2
#define DMA_COMMAND_READ            2
#define DMA_SEMAPHORE               0x00000040
#define DMA_WAIT4ENDCMD             0x00000080
#define DMA_HALT_ON_TERMINATE       0x00000100
#define DMA_PIO_WORDS_SHIFT         12
#define DMA_BYTES_SHIFT             16
#define DMA_PIO_WORDS               4
#define DMA_MAX_BYTES               0xFF00

#define SPIN_POLLS                  2000
#define TIMEOUT_MS                  200

#define SETTINGS_KEY                TEXT("Software\\Brain\\SubLCD")
#define DMA_VALUE                   TEXT("SpiDma")
#define CLOCK_VALUE                 TEXT("SpiClockKHz")
#define DEFAULT_CLOCK_KHZ           16000

typedef struct
{
    DWORD Next;
    DWORD Command;
    DWORD Buffer;
    DWORD Pio[DMA_PIO_WORDS];
} DMA_DESCRIPTOR;

static volatile UINT32* s_pSsp;
static volatile UINT32* s_pClkctrl;
static DMA_DESCRIPTOR* s_pDescriptor;
static DWORD s_DescriptorPhysical;
static UINT16* s_pWords;
static DWORD s_WordsPhysical;
static DWORD s_WordCapacity;
static BOOL s_bEnabled;

static DWORD ReadSetting(LPCTSTR pName, DWORD Default)
{
    DWORD Value = Default, Size = sizeof(Value), Type;
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return Default;
    if (RegQueryValueEx(hKey, pName, NULL, &Type, (BYTE*)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Value = Default;
    RegCloseKey(hKey);
    return Value;
}

static volatile UINT32* MapBlock(DWORD Physical)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = Physical;
    return (volatile UINT32*)MmMapIoSpace(Address, 0x1000, FALSE);
}

static void ResetChannel()
{
    DDKApbhDmaResetChan(DMA_CHANNEL, TRUE);
    DDKApbhDmaInitChan(DMA_CHANNEL, FALSE);
}

//------------------------------------------------------------------------------
//
// Function: SspDmaInit
//
// Allocates the uncached descriptor and word buffer and maps SSP2.
//
// Parameters:
//      Words
//          [in] Largest transfer in words.
//
// Returns:
//      The word buffer to build transfers in, or NULL if DMA is turned off
//      or unavailable (use SPI2: instead).
//
//------------------------------------------------------------------------------
UINT16* SspDmaInit(DWORD Words)
{
    BYTE* pBuffer;
    ULONG Physical;

    if (!ReadSetting(DMA_VALUE, 1) || Words * sizeof(UINT16) > DMA_MAX_BYTES)
        return NULL;
    s_pSsp = MapBlock(SSP2_BASE);
    s_pClkctrl = MapBlock(CLKCTRL_BASE);
    pBuffer = (BYTE*)AllocPhysMem(sizeof(DMA_DESCRIPTOR) + Words * sizeof(UINT16), PAGE_READWRITE | PAGE_NOCACHE, 0, 0, &Physical);
    if (!s_pSsp || !s_pClkctrl || !pBuffer)
    {
        RETAILMSG(TRUE, (TEXT("SubLCD: SPI DMA unavailable, using SPI2:\r\n")));
        return NULL;
    }
    s_pDescriptor = (DMA_DESCRIPTOR*)pBuffer;
    s_DescriptorPhysical = Physical;
    s_pWords = (UINT16*)(pBuffer + sizeof(DMA_DESCRIPTOR));
    s_WordsPhysical = Physical + sizeof(DMA_DESCRIPTOR);
    s_WordCapacity = Words;
    return s_pWords;
}

//------------------------------------------------------------------------------
//
// Function: SspDmaConfigure
//
// Sets the SPI clock and enables DMA on SSP2. Call after each SPI2: port
// configuration, which resets CTRL1.
//
// Parameters:
//      None.
//
// Returns:
//      TRUE if transfers will use DMA.
//
//------------------------------------------------------------------------------
BOOL SspDmaConfigure()
{
    DWORD ClockKHz, Divider, Polls = 0;

    s_bEnabled = FALSE;
    if (!s_pWords)
        return FALSE;
    ClockKHz = ReadSetting(CLOCK_VALUE, DEFAULT_CLOCK_KHZ);
    if (ClockKHz < 1000)
        ClockKHz = 1000;
    Divider = (REF_IO1_KHZ + SSP_TIMING_DIVIDE * ClockKHz - 1) / (SSP_TIMING_DIVIDE * ClockKHz);
    if (Divider < 1)
        Divider = 1;
    if (Divider > CLKCTRL_SSP2_DIV_MASK)
        Divider = CLKCTRL_SSP2_DIV_MASK;

    s_pClkctrl[CLKCTRL_SSP2 / 4] = (s_pClkctrl[CLKCTRL_SSP2 / 4] & ~CLKCTRL_SSP2_DIV_MASK) | Divider;
    while ((s_pClkctrl[CLKCTRL_SSP2 / 4] & CLKCTRL_SSP2_BUSY) && ++Polls < SPIN_POLLS * 100)
        ;
    s_pSsp[SSP_TIMING / 4] = (s_pSsp[SSP_TIMING / 4] & SSP_TIMING_TIMEOUT_MASK) | (SSP_TIMING_DIVIDE << 8);
    s_pSsp[SSP_CTRL1_SET / 4] = SSP_CTRL1_DMA_ENABLE;
    ResetChannel();
    s_bEnabled = TRUE;
    RETAILMSG(TRUE, (TEXT("SubLCD: SPI DMA at %u kHz\r\n"), REF_IO1_KHZ / Divider / SSP_TIMING_DIVIDE));
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: SspDmaSend
//
// Sends words by DMA and waits for the transfer to finish, sleeping while
// a long one runs. Chip select is released at the end.
//
// Parameters:
//      pWords
//          [in] Words to send, 9 bits each. Words already in the buffer
//          returned by SspDmaInit are sent in place; others are copied.
//
//      Count
//          [in] Number of words.
//
// Returns:
//      TRUE if sent; FALSE if DMA is off or the transfer timed out, in
//      which case DMA stays off and the caller should use SPI2:.
//
//------------------------------------------------------------------------------
BOOL SspDmaSend(const UINT16* pWords, DWORD Count)
{
    DWORD Bytes = Count * sizeof(UINT16);
    DWORD Physical, Polls = 0, Start;

    if (!s_bEnabled || Count == 0 || Count > s_WordCapacity)
        return FALSE;
    if (pWords >= s_pWords && pWords + Count <= s_pWords + s_WordCapacity)
    {
        Physical = s_WordsPhysical + (DWORD)((const BYTE*)pWords - (const BYTE*)s_pWords);
    }
    else
    {
        memcpy(s_pWords, pWords, Bytes);
        Physical = s_WordsPhysical;
    }

    s_pDescriptor->Next = 0;
    s_pDescriptor->Command = DMA_COMMAND_READ | DMA_SEMAPHORE | DMA_WAIT4ENDCMD | DMA_HALT_ON_TERMINATE |
        (DMA_PIO_WORDS << DMA_PIO_WORDS_SHIFT) | (Bytes << DMA_BYTES_SHIFT);
    s_pDescriptor->Buffer = Physical;
    s_pDescriptor->Pio[0] = (s_pSsp[SSP_CTRL0 / 4] & ~(SSP_CTRL0_READ | SSP_CTRL0_LOCK_CS | SSP_CTRL0_RUN)) |
        SSP_CTRL0_DATA_XFER | SSP_CTRL0_IGNORE_CRC;
    s_pDescriptor->Pio[1] = 0;
    s_pDescriptor->Pio[2] = 0;
    s_pDescriptor->Pio[3] = Bytes;

    DDKApbhStartDma(DMA_CHANNEL, (PVOID)s_DescriptorPhysical, 1);
    Start = GetTickCount();
    while (DDKApbhDmaGetPhore(DMA_CHANNEL) != 0)
    {
        if (++Polls < SPIN_POLLS)
            continue;
        if (GetTickCount() - Start > TIMEOUT_MS)
        {
            RETAILMSG(TRUE, (TEXT("SubLCD: SPI DMA timed out, using SPI2:\r\n")));
            ResetChannel();
            s_pSsp[SSP_CTRL1_CLR / 4] = SSP_CTRL1_DMA_ENABLE;
            s_bEnabled = FALSE;
            return FALSE;
        }
        Sleep(1);
    }
    return TRUE;
}
