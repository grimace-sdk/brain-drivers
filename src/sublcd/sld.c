//------------------------------------------------------------------------------
//
//  File:  sld.c
//
//  SLD1:, a replacement for Sharp's EDNA2_slcd_ST7586.dll. It keeps Sharp's
//  DeviceIoControl codes and adds 2-bit greyscale drawing, an overlay,
//  backlight brightness and pitched buzzer tones (see sublcd_ioctl.h).
//
//  A shadow of the panel's 120 rows of 80 display bytes is kept so every
//  draw can be partial and the picture is restored after resume. Each byte
//  holds three pixels in bits 7-5, 4-2 and 1-0; levels white to black are
//  0, 2, 4 and 7 in a 3-bit field, and the top two of those bits in the
//  2-bit field.
//
//  While an overlay is shown, draws still go to the shadow but the panel
//  shows the overlay over its rectangle; hiding it sends the shadow again.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include <ceddk.h>
#include "st7586.h"
#include "sub_backlight.h"
#include "buzzer_tone.h"
#include "sublcd_ioctl.h"

#define DRIVER_VERSION              4
#define GROUP_PIXELS                3
#define SHARP_GROUP_PIXELS          24
#define DEFAULT_CONTRAST            50
#define RESUME_THREAD_PRIORITY      250
#define SUSPENDED_POLL_MS           100
#define DEVICE_CONTEXT              0x534C4431

typedef struct
{
    int X;
    int Y;
    int Width;
    int Height;
    BOOL bSet;
} WINDOW;

typedef struct
{
    UINT FirstColumn;
    UINT LastColumn;
    UINT FirstRow;
    UINT LastRow;
    BOOL bShown;
} OVERLAY;

static CRITICAL_SECTION s_Lock;
static BYTE s_Shadow[ST7586_ROWS][ST7586_COLUMNS];
static BYTE s_OverlayPixels[ST7586_ROWS][ST7586_COLUMNS];
static BYTE s_Panel[ST7586_ROWS][ST7586_COLUMNS];
static OVERLAY s_Overlay;
static WINDOW s_Window;
static int s_Contrast = DEFAULT_CONTRAST;
static int s_VopBase;
static BOOL s_bPanelOn;
static volatile BOOL s_bSuspended;
static HANDLE s_hResumeEvent;

static const BYTE s_LevelBits[4] = { 0x0, 0x2, 0x4, 0x7 };

static BYTE PackGroup(UINT Level0, UINT Level1, UINT Level2)
{
    return (BYTE)((s_LevelBits[Level0] << 5) | (s_LevelBits[Level1] << 2) | (s_LevelBits[Level2] >> 1));
}

static BYTE ApplyFlags(BYTE Group, UINT16 Flags)
{
    if (Flags & SLD_DRAW_BLANK)
        return 0;
    if (Flags & SLD_DRAW_INVERT)
        return (BYTE)(~Group & PackGroup(SLD_GREY_BLACK, SLD_GREY_BLACK, SLD_GREY_BLACK));
    return Group;
}

static BYTE PackGreyGroup(const BYTE* pLine, int Column)
{
    UINT Levels[GROUP_PIXELS];
    int Index;

    for (Index = 0; Index < GROUP_PIXELS; Index++)
    {
        int Pixel = Column * GROUP_PIXELS + Index;
        Levels[Index] = (pLine[Pixel / 4] >> (6 - 2 * (Pixel % 4))) & 3;
    }
    return PackGroup(Levels[0], Levels[1], Levels[2]);
}

static void SendShadow(UINT FirstColumn, UINT LastColumn, UINT FirstRow, UINT LastRow)
{
    UINT Row, Column;

    if (!s_Overlay.bShown)
    {
        St7586WriteWindow(FirstColumn, LastColumn, FirstRow, LastRow, &s_Shadow[FirstRow][FirstColumn], ST7586_COLUMNS);
        return;
    }
    for (Row = FirstRow; Row <= LastRow; Row++)
        for (Column = FirstColumn; Column <= LastColumn; Column++)
        {
            BOOL bCovered = Row >= s_Overlay.FirstRow && Row <= s_Overlay.LastRow &&
                            Column >= s_Overlay.FirstColumn && Column <= s_Overlay.LastColumn;
            s_Panel[Row][Column] = bCovered ? s_OverlayPixels[Row][Column] : s_Shadow[Row][Column];
        }
    St7586WriteWindow(FirstColumn, LastColumn, FirstRow, LastRow, &s_Panel[FirstRow][FirstColumn], ST7586_COLUMNS);
}

static void ResetWindow()
{
    s_Window.X = 0;
    s_Window.Y = 0;
    s_Window.Width = SLD_WIDTH;
    s_Window.Height = SLD_HEIGHT;
    s_Window.bSet = FALSE;
}

static int ContrastToVopOffset(int Contrast)
{
    int Offset = (Contrast - 50) * 127 / 100;

    if (Offset > 63)
        return 63;
    if (Offset < -64)
        return -64;
    return Offset;
}

static void SetContrast(int Contrast, int Minimum, int Maximum)
{
    if (Contrast < Minimum)
        Contrast = Minimum;
    if (Contrast > Maximum)
        Contrast = Maximum;
    s_Contrast = Contrast;
    St7586SetVopOffset(ContrastToVopOffset(Contrast));
}

static void PowerOn()
{
    St7586PowerOn(s_VopBase);
    St7586SetVopOffset(ContrastToVopOffset(s_Contrast));
    SendShadow(0, ST7586_COLUMNS - 1, 0, ST7586_ROWS - 1);
    s_bPanelOn = TRUE;
}

static void DrawSharpRect(const SLD_DRAW* pDraw)
{
    int X = pDraw->A, Y = pDraw->B, Width = pDraw->C, Height = pDraw->D;
    const BYTE* pBits = (const BYTE*)pDraw->pBits;
    int FirstPixel, EndPixel, Row, Pixel;

    if (X >= s_Window.Width || Y >= s_Window.Height || Width == 0 || Height == 0)
        return;
    if (X + Width > s_Window.Width)
        Width = s_Window.Width - X;
    if (Y + Height > s_Window.Height)
        Height = s_Window.Height - Y;
    FirstPixel = X / SHARP_GROUP_PIXELS * SHARP_GROUP_PIXELS;
    EndPixel = (X + Width + SHARP_GROUP_PIXELS - 1) / SHARP_GROUP_PIXELS * SHARP_GROUP_PIXELS;
    if (EndPixel > SLD_WIDTH)
        EndPixel = SLD_WIDTH;

    for (Row = Y; Row < Y + Height; Row++)
    {
        for (Pixel = FirstPixel; Pixel < EndPixel; Pixel += GROUP_PIXELS)
        {
            UINT Levels[GROUP_PIXELS] = { 0, 0, 0 };
            int Index;

            if (pBits)
                for (Index = 0; Index < GROUP_PIXELS; Index++)
                {
                    int Column = Pixel + Index;
                    if (pBits[(Column / 8) * SLD_HEIGHT + Row] & (0x80 >> (Column % 8)))
                        Levels[Index] = SLD_GREY_BLACK;
                }
            s_Shadow[Row][Pixel / GROUP_PIXELS] = ApplyFlags(PackGroup(Levels[0], Levels[1], Levels[2]),
                                                             pBits ? pDraw->Flags : SLD_DRAW_BLANK);
        }
    }
    SendShadow(FirstPixel / GROUP_PIXELS, EndPixel / GROUP_PIXELS - 1, Y, Y + Height - 1);
}

static void DrawSharpRow(const SLD_DRAW* pDraw)
{
    UINT Row = pDraw->A;
    UINT FirstColumn = pDraw->B / GROUP_PIXELS;
    UINT Groups = pDraw->C * 8 / GROUP_PIXELS;
    const UINT16* pWords = (const UINT16*)pDraw->pBits;
    UINT Group;

    if (Row >= ST7586_ROWS || FirstColumn >= ST7586_COLUMNS || Groups == 0)
        return;
    if (FirstColumn + Groups > ST7586_COLUMNS)
        Groups = ST7586_COLUMNS - FirstColumn;

    for (Group = 0; Group < Groups; Group++)
    {
        UINT Levels[GROUP_PIXELS] = { 0, 0, 0 };
        UINT Index;

        if (pWords)
            for (Index = 0; Index < GROUP_PIXELS; Index++)
            {
                UINT Pixel = Group * GROUP_PIXELS + Index;
                if (pWords[pDraw->D + Pixel / 8] & (0x80 >> (Pixel % 8)))
                    Levels[Index] = SLD_GREY_BLACK;
            }
        s_Shadow[Row][FirstColumn + Group] = ApplyFlags(PackGroup(Levels[0], Levels[1], Levels[2]),
                                                        pWords ? pDraw->Flags : SLD_DRAW_BLANK);
    }
    SendShadow(FirstColumn, FirstColumn + Groups - 1, Row, Row);
}

static void DrawGrey(const SLD_GREY_DRAW* pDraw)
{
    int X = pDraw->X, Y = pDraw->Y, Width = pDraw->Width, Height = pDraw->Height;
    int FirstColumn, LastColumn, Row, Column;

    if (X >= SLD_WIDTH || Y >= SLD_HEIGHT || Width == 0 || Height == 0)
        return;
    if (X + Width > SLD_WIDTH)
        Width = SLD_WIDTH - X;
    if (Y + Height > SLD_HEIGHT)
        Height = SLD_HEIGHT - Y;
    FirstColumn = X / GROUP_PIXELS;
    LastColumn = (X + Width - 1) / GROUP_PIXELS;

    for (Row = Y; Row < Y + Height; Row++)
        for (Column = FirstColumn; Column <= LastColumn; Column++)
            s_Shadow[Row][Column] = PackGreyGroup(&pDraw->Pixels[Row * SLD_GREY_STRIDE], Column);
    SendShadow(FirstColumn, LastColumn, Y, Y + Height - 1);
}

static void SetOverlay(const SLD_GREY_DRAW* pDraw)
{
    OVERLAY Previous = s_Overlay;
    int X = pDraw->X, Y = pDraw->Y, Width = pDraw->Width, Height = pDraw->Height;
    int Row, Column;

    s_Overlay.bShown = FALSE;
    if (X < SLD_WIDTH && Y < SLD_HEIGHT && Width != 0 && Height != 0)
    {
        if (X + Width > SLD_WIDTH)
            Width = SLD_WIDTH - X;
        if (Y + Height > SLD_HEIGHT)
            Height = SLD_HEIGHT - Y;
        s_Overlay.FirstColumn = X / GROUP_PIXELS;
        s_Overlay.LastColumn = (X + Width - 1) / GROUP_PIXELS;
        s_Overlay.FirstRow = Y;
        s_Overlay.LastRow = Y + Height - 1;
        for (Row = Y; Row < Y + Height; Row++)
            for (Column = (int)s_Overlay.FirstColumn; Column <= (int)s_Overlay.LastColumn; Column++)
                s_OverlayPixels[Row][Column] = PackGreyGroup(&pDraw->Pixels[Row * SLD_GREY_STRIDE], Column);
        s_Overlay.bShown = TRUE;
    }
    if (Previous.bShown)
        SendShadow(Previous.FirstColumn, Previous.LastColumn, Previous.FirstRow, Previous.LastRow);
    if (s_Overlay.bShown)
        SendShadow(s_Overlay.FirstColumn, s_Overlay.LastColumn, s_Overlay.FirstRow, s_Overlay.LastRow);
}

static void DrawTestPattern()
{
    int Row, Column;

    for (Row = 0; Row < ST7586_ROWS; Row++)
        for (Column = 0; Column < ST7586_COLUMNS; Column++)
        {
            UINT Level = Column * 4 / ST7586_COLUMNS;
            s_Shadow[Row][Column] = PackGroup(Level, Level, Level);
        }
    SendShadow(0, ST7586_COLUMNS - 1, 0, ST7586_ROWS - 1);
}

static void SetWindow(const LONG* pRectangle)
{
    if (!pRectangle)
    {
        ResetWindow();
        return;
    }
    s_Window.X = pRectangle[0] / SHARP_GROUP_PIXELS * SHARP_GROUP_PIXELS;
    s_Window.Y = pRectangle[1];
    s_Window.Width = (pRectangle[2] + SHARP_GROUP_PIXELS - 1) / SHARP_GROUP_PIXELS * SHARP_GROUP_PIXELS;
    s_Window.Height = pRectangle[3];
    s_Window.bSet = TRUE;
}

static DWORD WINAPI PanelResumeThread(LPVOID pContext)
{
    RETAILMSG(TRUE, (TEXT("SubLCD: resume thread %08x\r\n"), GetCurrentThreadId()));
    for (;;)
    {
        WaitForSingleObject(s_hResumeEvent, INFINITE);
        EnterCriticalSection(&s_Lock);
        PowerOn();
        LeaveCriticalSection(&s_Lock);
        s_bSuspended = FALSE;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD Reason, LPVOID pReserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls((HMODULE)hInstance);
        InitializeCriticalSection(&s_Lock);
        SubBacklightInit();
    }
    return TRUE;
}

//------------------------------------------------------------------------------
//
// Function: SLD_Init
//
// Device manager entry point. Starts the panel with a blank picture and the
// thread that restarts it after resume.
//
// Parameters:
//      pContext
//          [in] Registry path of the device key (unused).
//
// Returns:
//      A device context, or 0 on failure.
//
//------------------------------------------------------------------------------
DWORD SLD_Init(LPCTSTR pContext)
{
    HANDLE hThread;

    s_hResumeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s_hResumeEvent)
        return 0;
    hThread = CreateThread(NULL, 0, PanelResumeThread, NULL, 0, NULL);
    if (!hThread)
        return 0;
    CeSetThreadPriority(hThread, RESUME_THREAD_PRIORITY);
    CloseHandle(hThread);

    ResetWindow();
    memset(s_Shadow, 0, sizeof(s_Shadow));
    s_VopBase = St7586CalibratedVopBase();
    EnterCriticalSection(&s_Lock);
    PowerOn();
    LeaveCriticalSection(&s_Lock);
    RETAILMSG(TRUE, (TEXT("SubLCD: grey driver v%u, VOP base %d\r\n"), DRIVER_VERSION, s_VopBase));
    return DEVICE_CONTEXT;
}

BOOL SLD_Deinit(DWORD Context)
{
    return TRUE;
}

DWORD SLD_Open(DWORD Context, DWORD Access, DWORD ShareMode)
{
    return Context;
}

BOOL SLD_Close(DWORD OpenContext)
{
    return TRUE;
}

DWORD SLD_Read(DWORD OpenContext, LPVOID pBuffer, DWORD Count)
{
    return 0;
}

DWORD SLD_Write(DWORD OpenContext, LPCVOID pBuffer, DWORD Count)
{
    return 0;
}

DWORD SLD_Seek(DWORD OpenContext, LONG Distance, DWORD Method)
{
    return 0;
}

//------------------------------------------------------------------------------
//
// Function: SLD_PowerDown
//
// Turns the panel off before suspend, as Sharp's driver does, and holds
// further calls until SLD_PowerUp has restarted it. If a draw holds the lock
// the panel is left for the power cut; blocking here would hang suspend.
//
// Parameters:
//      Context
//          [in] Device context.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void SLD_PowerDown(DWORD Context)
{
    s_bSuspended = TRUE;
    BuzzerToneSet(0);
    if (!TryEnterCriticalSection(&s_Lock))
        return;
    if (s_bPanelOn)
        St7586SleepWithoutYield();
    s_bPanelOn = FALSE;
    LeaveCriticalSection(&s_Lock);
}

//------------------------------------------------------------------------------
//
// Function: SLD_PowerUp
//
// Wakes the resume thread, which restarts the panel and redraws it from the
// shadow.
//
// Parameters:
//      Context
//          [in] Device context.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void SLD_PowerUp(DWORD Context)
{
    SetEvent(s_hResumeEvent);
}

//------------------------------------------------------------------------------
//
// Function: SLD_IOControl
//
// Handles Sharp's codes 1-14 and ours from SLD_IOCTL_GET_INFO; see
// sublcd_ioctl.h. Sharp's codes always return TRUE, as Sharp's driver does.
//
// Parameters:
//      OpenContext
//          [in] Open context.
//
//      Code
//          [in] SLD_IOCTL_*.
//
//      pIn, InSize
//          [in] Input buffer. Codes 9, 11 and 13 take their value from
//          InSize, as Sharp's do.
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
BOOL SLD_IOControl(DWORD OpenContext, DWORD Code, PBYTE pIn, DWORD InSize, PBYTE pOut, DWORD OutSize, PDWORD pReturned)
{
    BOOL bResult = TRUE;

    if (pReturned)
        *pReturned = 0;
    while (s_bSuspended)
        Sleep(SUSPENDED_POLL_MS);

    if (Code == SLD_IOCTL_SET_TONE)
    {
        if (pIn && InSize >= sizeof(DWORD) && BuzzerToneSet(*(const DWORD*)pIn))
            return TRUE;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Code == SLD_IOCTL_SET_BACKLIGHT || Code == SLD_IOCTL_GET_BACKLIGHT)
    {
        if (Code == SLD_IOCTL_SET_BACKLIGHT && pIn && InSize >= sizeof(DWORD))
            SubBacklightSetLevel(*(const DWORD*)pIn);
        else if (Code == SLD_IOCTL_GET_BACKLIGHT && pOut && OutSize >= sizeof(DWORD))
        {
            *(DWORD*)pOut = SubBacklightGetLevel();
            if (pReturned)
                *pReturned = sizeof(DWORD);
        }
        else
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            bResult = FALSE;
        }
        return bResult;
    }

    EnterCriticalSection(&s_Lock);
    switch (Code)
    {
    case SLD_IOCTL_PANEL_ON:
        if (!s_bPanelOn)
            PowerOn();
        break;

    case SLD_IOCTL_PANEL_OFF:
        if (!s_Window.bSet && s_bPanelOn)
        {
            St7586PowerOff();
            s_bPanelOn = FALSE;
        }
        break;

    case SLD_IOCTL_DRAW_ROW:
        if (pIn && InSize == sizeof(SLD_DRAW))
            DrawSharpRow((const SLD_DRAW*)pIn);
        break;

    case SLD_IOCTL_SET_CONTRAST:
        if (pIn && InSize == 2)
            SetContrast(pIn[0], 35, 65);
        break;

    case SLD_IOCTL_GET_CONTRAST:
        if (pOut && OutSize == 2)
        {
            *(UINT16*)pOut = (UINT16)s_Contrast;
            if (pReturned)
                *pReturned = 2;
        }
        break;

    case SLD_IOCTL_CONTRAST_UP:
        SetContrast(s_Contrast + 3, 35, 65);
        break;

    case SLD_IOCTL_CONTRAST_DOWN:
        SetContrast(s_Contrast - 3, 35, 65);
        break;

    case SLD_IOCTL_DRAW_RECT:
        if (pIn && InSize == sizeof(SLD_DRAW))
            DrawSharpRect((const SLD_DRAW*)pIn);
        break;

    case SLD_IOCTL_STEP_VOP:
        s_VopBase += (int)InSize;
        St7586StepVop((int)InSize);
        break;

    case SLD_IOCTL_TEST_PATTERN:
        DrawTestPattern();
        break;

    case SLD_IOCTL_SET_VOP_OFFSET:
        St7586SetVopOffset((int)InSize);
        St7586StepVop(s_VopBase);
        break;

    case SLD_IOCTL_SET_WINDOW:
        SetWindow(pIn && InSize == 4 * sizeof(LONG) ? (const LONG*)pIn : NULL);
        break;

    case SLD_IOCTL_SET_VOP_BASE:
        s_VopBase = (int)InSize;
        St7586SetVopBase(s_VopBase);
        break;

    case SLD_IOCTL_SET_CONTRAST_100:
        if (pIn && InSize == 2)
            SetContrast(pIn[0], 0, 100);
        break;

    case SLD_IOCTL_GET_INFO:
        if (pOut && OutSize >= sizeof(SLD_INFO))
        {
            SLD_INFO* pInfo = (SLD_INFO*)pOut;
            pInfo->Magic = SLD_INFO_MAGIC;
            pInfo->Version = DRIVER_VERSION;
            pInfo->Capabilities = SLD_CAPS_GREY | SLD_CAPS_BACKLIGHT_LEVEL | SLD_CAPS_TONE | SLD_CAPS_OVERLAY;
            if (pReturned)
                *pReturned = sizeof(SLD_INFO);
        }
        else
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            bResult = FALSE;
        }
        break;

    case SLD_IOCTL_DRAW_GREY:
        if (pIn && InSize >= sizeof(SLD_GREY_DRAW))
            DrawGrey((const SLD_GREY_DRAW*)pIn);
        else
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            bResult = FALSE;
        }
        break;

    case SLD_IOCTL_SET_OVERLAY:
        if (pIn && InSize >= sizeof(SLD_GREY_DRAW))
            SetOverlay((const SLD_GREY_DRAW*)pIn);
        else
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            bResult = FALSE;
        }
        break;
    }
    LeaveCriticalSection(&s_Lock);
    return bResult;
}
