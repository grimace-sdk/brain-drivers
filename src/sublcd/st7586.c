//------------------------------------------------------------------------------
//
//  File:  st7586.c
//
//  Sitronix ST7586 on the PW-A7300's sub LCD. Words are 9 bits, bit 8 set
//  for data. Sharp's SPI2: driver configures the port as Sharp's
//  EDNA2_slcd_ST7586.dll does; transfers then go by DMA (ssp_dma.c), or
//  through SPI2: if DMA is off or fails. The init sequence is Sharp's except
//  that the controller runs in 4-level grey mode instead of monochrome.
//
//  Display RAM is 128 byte columns by 160 rows. The panel shows columns
//  0-79 (three pixels per byte) of rows 40-159.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "cspddk.h"
#include "spi2.h"
#include "st7586.h"
#include "ssp_dma.h"

#define RESET_PIN                   DDK_IOMUX_SSP2_D0_1
#define ROW_OFFSET                  40
#define RAM_COLUMNS                 128
#define RAM_ROWS                    160

#define SPI_WORD_LENGTH_9BITS       8
#define SPI_CTRL0_IGNORE_CRC        0x04000000
#define SPI_WORD_BITS               9
#define DATA_BIT                    0x100

#define SHARP_SPI_ACTIVE            ((volatile DWORD*)0xA00EDC58)
#define SHARP_CALIBRATION           ((volatile LONG*)0xA00EE400)
#define CALIBRATION_MAGIC           0x3037
#define CALIBRATION_VOP_INDEX       16
#define VOP_BYTE_BIAS               25

#define COMMAND_SLEEP_IN            0x10
#define COMMAND_SLEEP_OUT           0x11
#define COMMAND_INVERSION_OFF       0x20
#define COMMAND_DISPLAY_OFF         0x28
#define COMMAND_DISPLAY_ON          0x29
#define COMMAND_COLUMN_ADDRESS      0x2A
#define COMMAND_ROW_ADDRESS         0x2B
#define COMMAND_MEMORY_WRITE        0x2C
#define COMMAND_PARTIAL_AREA        0x30
#define COMMAND_SCAN_DIRECTION      0x36
#define COMMAND_START_LINE          0x37
#define COMMAND_GREY_MODE           0x38
#define COMMAND_DDRAM_INTERFACE     0x3A
#define COMMAND_DUTY                0xB0
#define COMMAND_FIRST_COM           0xB1
#define COMMAND_FOSC_DIVIDER        0xB4
#define COMMAND_N_LINE_INVERSION    0xB5
#define COMMAND_VOP                 0xC0
#define COMMAND_VOP_UP              0xC1
#define COMMAND_VOP_DOWN            0xC2
#define COMMAND_BIAS                0xC3
#define COMMAND_BOOSTER             0xC4
#define COMMAND_VOP_OFFSET          0xC7
#define COMMAND_ANALOG_CONTROL      0xD0
#define COMMAND_AUTO_READ           0xD7
#define COMMAND_OTP_CONTROL         0xE0
#define COMMAND_OTP_CONTROL_OUT     0xE1
#define COMMAND_OTP_READ            0xE3
#define COMMAND_FRAME_RATE          0xF1

#define WINDOW_HEADER_WORDS         11
#define MAX_WORDS                   (WINDOW_HEADER_WORDS + ST7586_COLUMNS * ST7586_ROWS)

static HANDLE s_hSpi = INVALID_HANDLE_VALUE;
static UINT16 s_LocalWords[MAX_WORDS];
static UINT16* s_Words = s_LocalWords;
static BOOL s_bDmaChecked;

static BOOL s_bNoYield;

static BOOL Exchange(const UINT16* pWords, UINT32 Count)
{
    SPI_BUSCONFIG_T Bus;
    SPI_XCH_PKT_T Packet;
    BOOL bResult;

    if (s_hSpi == INVALID_HANDLE_VALUE || Count == 0)
        return FALSE;
    *SHARP_SPI_ACTIVE = 1;
    bResult = SspDmaSend(pWords, Count);
    *SHARP_SPI_ACTIVE = 0;
    if (bResult || s_bNoYield)
        return bResult;
    memset(&Bus, 0, sizeof(Bus));
    Bus.SspCtrl0.U = SPI_CTRL0_IGNORE_CRC;
    Bus.usepolling = TRUE;
    Bus.bitcount = SPI_WORD_BITS;
    memset(&Packet, 0, sizeof(Packet));
    Packet.pBusCnfg = &Bus;
    Packet.pBuf = (LPVOID)pWords;
    Packet.xchCnt = Count;

    *SHARP_SPI_ACTIVE = 1;
    bResult = DeviceIoControl(s_hSpi, SPI_IOCTL_EXCHANGE, &Packet, sizeof(Packet), NULL, 0, NULL, NULL);
    *SHARP_SPI_ACTIVE = 0;
    return bResult;
}

static void Command(UINT8 Code, const UINT8* pData, UINT Count)
{
    UINT16 Words[8];
    UINT Index;

    Words[0] = Code;
    for (Index = 0; Index < Count && Index < 7; Index++)
        Words[Index + 1] = DATA_BIT | pData[Index];
    Exchange(Words, Index + 1);
}

static void Command0(UINT8 Code)
{
    Command(Code, NULL, 0);
}

static void Command1(UINT8 Code, UINT8 Data)
{
    Command(Code, &Data, 1);
}

static void SetResetLine(BOOL bHigh)
{
    DDKGpioWriteDataPin(RESET_PIN, bHigh ? 1 : 0);
}

static UINT16* WriteWindowHeader(UINT FirstColumn, UINT LastColumn, UINT FirstRow, UINT LastRow)
{
    UINT16* pWord = s_Words;

    *pWord++ = COMMAND_COLUMN_ADDRESS;
    *pWord++ = DATA_BIT | (FirstColumn >> 8);
    *pWord++ = DATA_BIT | (FirstColumn & 0xFF);
    *pWord++ = DATA_BIT | (LastColumn >> 8);
    *pWord++ = DATA_BIT | (LastColumn & 0xFF);
    *pWord++ = COMMAND_ROW_ADDRESS;
    *pWord++ = DATA_BIT | (FirstRow >> 8);
    *pWord++ = DATA_BIT | (FirstRow & 0xFF);
    *pWord++ = DATA_BIT | (LastRow >> 8);
    *pWord++ = DATA_BIT | (LastRow & 0xFF);
    *pWord++ = COMMAND_MEMORY_WRITE;
    return pWord;
}

static void ClearRam()
{
    UINT16* pData = WriteWindowHeader(0, RAM_COLUMNS - 1, 0, RAM_ROWS - 1);
    UINT Index;
    UINT Row;

    Exchange(s_Words, WINDOW_HEADER_WORDS);
    for (Index = 0; Index < RAM_COLUMNS; Index++)
        pData[Index] = DATA_BIT;
    for (Row = 0; Row < RAM_ROWS; Row++)
        Exchange(pData, RAM_COLUMNS);
}

//------------------------------------------------------------------------------
//
// Function: St7586Open
//
// Opens SPI2: and configures it as Sharp's driver does: SPI master, 9-bit
// words, half duplex. Then hands transfers to DMA when it is available.
// Runs on every power-on, so the port is set up again after resume.
//
// Parameters:
//      None.
//
// Returns:
//      TRUE on success.
//
//------------------------------------------------------------------------------
BOOL St7586Open()
{
    SSP_INIT Init;
    SSP_SPEED Speed = SPI_IDENTIFY_SPEED;

    if (s_hSpi == INVALID_HANDLE_VALUE)
        s_hSpi = CreateFile(TEXT("SPI2:"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (s_hSpi == INVALID_HANDLE_VALUE)
    {
        RETAILMSG(TRUE, (TEXT("SubLCD: SPI2: did not open (%u)\r\n"), GetLastError()));
        return FALSE;
    }
    if (!s_bDmaChecked)
    {
        UINT16* pDmaWords = SspDmaInit(MAX_WORDS);
        if (pDmaWords)
            s_Words = pDmaWords;
        s_bDmaChecked = TRUE;
    }

    memset(&Init, 0, sizeof(Init));
    Init.eMode = SSP_MODE_SPI;
    Init.eLength = (SSP_WORDLENGTH)SPI_WORD_LENGTH_9BITS;
    Init.bIgnoreCrc = TRUE;
    Init.u16TransferCount = 1;
    Init.bHalfDuplex = TRUE;
    DeviceIoControl(s_hSpi, SPI_IOCTL_SSPCONFIGURE, &Init, sizeof(Init), NULL, 0, NULL, NULL);
    DeviceIoControl(s_hSpi, SPI_IOCTL_SSPCONFIGTIMING, &Speed, sizeof(Speed), NULL, 0, NULL, NULL);
    SspDmaConfigure();
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: St7586CalibratedVopBase
//
// Reads the factory VOP trim Sharp's boot code leaves at 0xA00EE400, with
// Sharp's range fix-ups.
//
// Parameters:
//      None.
//
// Returns:
//      The VOP base, 0 if no calibration block is present.
//
//------------------------------------------------------------------------------
int St7586CalibratedVopBase()
{
    LONG Trim;

    if (SHARP_CALIBRATION[0] != CALIBRATION_MAGIC)
        return 0;
    Trim = SHARP_CALIBRATION[CALIBRATION_VOP_INDEX];
    if (Trim < -185)
        Trim = 96;
    else if (Trim > 79)
        Trim = 360;
    return Trim;
}

//------------------------------------------------------------------------------
//
// Function: St7586PowerOn
//
// Resets the controller and runs the init sequence: OTP load, sleep out,
// analog setup, 4-level grey mode, 120-row duty starting at COM 40, cleared
// display RAM, display on.
//
// Parameters:
//      VopBase
//          [in] VOP base; see St7586SetVopBase.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586PowerOn(int VopBase)
{
    static const UINT8 PartialArea[] = { 0x00, 0x00, 0x00, RAM_ROWS - 1 };
    static const UINT8 FrameRate[] = { 0x0B, 0x0B, 0x0B, 0x0B };
    DWORD Start;

    DDKIomuxSetPinMux(RESET_PIN, DDK_IOMUX_MODE_GPIO);
    DDKIomuxSetPadConfig(RESET_PIN, DDK_IOMUX_PAD_DRIVE_4MA, DDK_IOMUX_PAD_PULL_DISABLE, DDK_IOMUX_PAD_VOLTAGE_3V3);
    DDKGpioEnableDataPin(RESET_PIN, 1);
    SetResetLine(FALSE);
    Sleep(10);
    SetResetLine(TRUE);
    Sleep(150);

    if (!St7586Open())
        return;

    Command1(COMMAND_AUTO_READ, 0x9F);
    Command1(COMMAND_OTP_CONTROL, 0x00);
    Sleep(10);
    Command0(COMMAND_OTP_READ);
    Sleep(20);
    Command0(COMMAND_OTP_CONTROL_OUT);
    Command0(COMMAND_SLEEP_OUT);
    Command0(COMMAND_DISPLAY_OFF);
    Sleep(50);
    Command1(COMMAND_VOP_OFFSET, 0x00);
    St7586SetVopBase(VopBase);
    Command1(COMMAND_BIAS, 0x03);
    Command1(COMMAND_BOOSTER, 0x07);
    Command1(COMMAND_ANALOG_CONTROL, 0x1D);
    Command1(COMMAND_N_LINE_INVERSION, 0x00);
    Command0(COMMAND_GREY_MODE);
    Command1(COMMAND_DDRAM_INTERFACE, 0x02);
    Command1(COMMAND_SCAN_DIRECTION, 0xC8);
    Command1(COMMAND_DUTY, ST7586_ROWS - 1);
    Command1(COMMAND_FIRST_COM, ROW_OFFSET);
    Command1(COMMAND_FOSC_DIVIDER, 0xA0);
    Command(COMMAND_PARTIAL_AREA, PartialArea, sizeof(PartialArea));
    Command0(COMMAND_INVERSION_OFF);
    Start = GetTickCount();
    ClearRam();
    RETAILMSG(TRUE, (TEXT("SubLCD: display RAM cleared in %u ms\r\n"), GetTickCount() - Start));
    Command(COMMAND_FRAME_RATE, FrameRate, sizeof(FrameRate));
    Command1(COMMAND_START_LINE, 0x00);
    Command0(COMMAND_DISPLAY_ON);
}

//------------------------------------------------------------------------------
//
// Function: St7586PowerOff
//
// Display off, sleep in, then holds the controller in reset.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586PowerOff()
{
    Command0(COMMAND_DISPLAY_OFF);
    Command0(COMMAND_SLEEP_IN);
    Sleep(120);
    SetResetLine(FALSE);
    Sleep(200);
}

void St7586SleepWithoutYield()
{
    s_bNoYield = TRUE;
    SspDmaSetNoYield(TRUE);
    Command0(COMMAND_DISPLAY_OFF);
    Command0(COMMAND_SLEEP_IN);
    SspDmaSetNoYield(FALSE);
    s_bNoYield = FALSE;
}

//------------------------------------------------------------------------------
//
// Function: St7586SetVopBase
//
// Sets the LCD drive voltage (command C0h) as Sharp does: VopBase + 25 in
// the low byte, with the ninth bit carried into the second.
//
// Parameters:
//      VopBase
//          [in] Calibrated base, see St7586CalibratedVopBase.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586SetVopBase(int VopBase)
{
    UINT8 Data[2];

    Data[0] = (UINT8)((VopBase + VOP_BYTE_BIAS) & 0xFF);
    Data[1] = (UINT8)(((VopBase + 0x100 + VOP_BYTE_BIAS) >> 8) & 0xFF);
    Command(COMMAND_VOP, Data, sizeof(Data));
}

//------------------------------------------------------------------------------
//
// Function: St7586SetVopOffset
//
// Sets the VOP offset (command C7h), which is how contrast is adjusted.
//
// Parameters:
//      Offset
//          [in] Signed offset, -64 to 63.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586SetVopOffset(int Offset)
{
    Command1(COMMAND_VOP_OFFSET, (UINT8)(Offset & 0x7F));
}

//------------------------------------------------------------------------------
//
// Function: St7586StepVop
//
// Steps VOP up (C1h) or down (C2h) one unit at a time.
//
// Parameters:
//      Steps
//          [in] Positive to raise, negative to lower.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586StepVop(int Steps)
{
    for (; Steps > 0; Steps--)
        Command0(COMMAND_VOP_UP);
    for (; Steps < 0; Steps++)
        Command0(COMMAND_VOP_DOWN);
}

//------------------------------------------------------------------------------
//
// Function: St7586WriteWindow
//
// Writes a block of display bytes to the panel in one SPI exchange.
//
// Parameters:
//      FirstColumn, LastColumn
//          [in] Byte columns, 0-79 (three pixels each).
//
//      FirstRow, LastRow
//          [in] Panel rows, 0-119.
//
//      pRows
//          [in] The byte for FirstColumn of FirstRow.
//
//      Stride
//          [in] Bytes from one row to the next in pRows.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void St7586WriteWindow(UINT FirstColumn, UINT LastColumn, UINT FirstRow, UINT LastRow, const BYTE* pRows, UINT Stride)
{
    UINT16* pWord;
    UINT Columns = LastColumn - FirstColumn + 1;
    UINT Row;
    UINT Column;

    if (LastColumn >= ST7586_COLUMNS || LastRow >= ST7586_ROWS || FirstColumn > LastColumn || FirstRow > LastRow)
        return;
    pWord = WriteWindowHeader(FirstColumn, LastColumn, FirstRow + ROW_OFFSET, LastRow + ROW_OFFSET);
    for (Row = FirstRow; Row <= LastRow; Row++, pRows += Stride)
        for (Column = 0; Column < Columns; Column++)
            *pWord++ = DATA_BIT | pRows[Column];
    Exchange(s_Words, (UINT32)(pWord - s_Words));
}
