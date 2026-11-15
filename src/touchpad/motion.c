//------------------------------------------------------------------------------
//
//  File:  motion.c
//
//  Laptop-style touchpad motion from TPD2: samples, moved here from the
//  Touchpad app. A sample holds x in the low 16 bits and y in the high 16
//  bits, each shifted left by 2, with the state in the low 2 bits.
//
//  Tap to click: the finger stays within 5 px of where it landed for up to
//  220 ms. Tap then touch again within 250 ms and drag to hold the button.
//  The first two samples after touch-down are ignored, single-sample jumps
//  over 24 px are dropped as noise, positions are smoothed (each step half
//  the way to the new sample, in 1/16 px), and processing runs two samples
//  behind so the noisy samples just before a lift are discarded.
//
//------------------------------------------------------------------------------
#include <windows.h>
#include "motion.h"

#define SAMPLE_FIRST                3
#define SAMPLE_MOVE                 1
#define SAMPLE_RELEASE              2
#define TAP_MAXIMUM_MILLISECONDS    220
#define TAP_MAXIMUM_DISTANCE        5
#define DRAG_WINDOW_MILLISECONDS    250
#define FIXED_POINT_ONE             16
#define SETTLE_SAMPLES              2
#define DELAYED_SAMPLES             2
#define SPIKE_DISTANCE              24
#define SMOOTHING_DIVISOR           2
#define ACCELERATION_START          2
#define ACCELERATION_STEP           3
#define ACCELERATION_LIMIT          40

static const int s_SpeedGains[] = { 4, 6, 10, 13, 16 };

static struct
{
    int RemainderX;
    int RemainderY;
    int PendingX;
    int PendingY;
    int SmoothedX;
    int SmoothedY;
    int SamplesSinceTouch;
    POINT Delayed[DELAYED_SAMPLES + 1];
    int DelayedCount;
    POINT LastRaw;
    BOOL bTouching;
    BOOL bDragging;
    DWORD TouchStarted;
    DWORD LastTapReleased;
    int TouchStartX;
    int TouchStartY;
    int TouchDistance;
} s_Pad;

static int ScaleMotion(const MOTION_SETTINGS* pSettings, int Delta, int Speed)
{
    int Acceleration = FIXED_POINT_ONE;
    DWORD Index = pSettings->Speed - 1;

    if (Index >= sizeof(s_SpeedGains) / sizeof(s_SpeedGains[0]))
        Index = 2;
    if (pSettings->bAcceleration && Speed > ACCELERATION_START)
    {
        Acceleration += (Speed - ACCELERATION_START) * ACCELERATION_STEP;
        if (Acceleration > ACCELERATION_LIMIT)
            Acceleration = ACCELERATION_LIMIT;
    }
    return Delta * s_SpeedGains[Index] * Acceleration / (FIXED_POINT_ONE * FIXED_POINT_ONE);
}

static void AddMotion(const MOTION_SETTINGS* pSettings, int DeltaX, int DeltaY)
{
    int Speed = (abs(DeltaX) + abs(DeltaY)) / FIXED_POINT_ONE;

    s_Pad.RemainderX += ScaleMotion(pSettings, DeltaX, Speed);
    s_Pad.RemainderY += ScaleMotion(pSettings, DeltaY, Speed);
    s_Pad.PendingX += s_Pad.RemainderX / FIXED_POINT_ONE;
    s_Pad.PendingY += s_Pad.RemainderY / FIXED_POINT_ONE;
    s_Pad.RemainderX %= FIXED_POINT_ONE;
    s_Pad.RemainderY %= FIXED_POINT_ONE;
}

static void FollowPosition(const MOTION_SETTINGS* pSettings, int X, int Y)
{
    int StepX, StepY;

    s_Pad.SamplesSinceTouch++;
    if (s_Pad.SamplesSinceTouch <= SETTLE_SAMPLES)
    {
        s_Pad.SmoothedX = X * FIXED_POINT_ONE;
        s_Pad.SmoothedY = Y * FIXED_POINT_ONE;
        return;
    }
    StepX = (X * FIXED_POINT_ONE - s_Pad.SmoothedX) / SMOOTHING_DIVISOR;
    StepY = (Y * FIXED_POINT_ONE - s_Pad.SmoothedY) / SMOOTHING_DIVISOR;
    s_Pad.SmoothedX += StepX;
    s_Pad.SmoothedY += StepY;
    AddMotion(pSettings, StepX, StepY);
}

//------------------------------------------------------------------------------
//
// Function: MotionFlush
//
// Sends the movement gathered since the last call as one mouse_event.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void MotionFlush()
{
    if (!s_Pad.PendingX && !s_Pad.PendingY)
        return;
    mouse_event(MOUSEEVENTF_MOVE, s_Pad.PendingX, s_Pad.PendingY, 0, 0);
    s_Pad.PendingX = 0;
    s_Pad.PendingY = 0;
}

//------------------------------------------------------------------------------
//
// Function: MotionReset
//
// Forgets the current touch, releasing the button if a drag holds it. Call
// when the touch panel is handed to another app or the touchpad is turned
// off.
//
// Parameters:
//      None.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void MotionReset()
{
    if (s_Pad.bDragging)
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    memset(&s_Pad, 0, sizeof(s_Pad));
}

//------------------------------------------------------------------------------
//
// Function: MotionHandleSample
//
// Moves the cursor or clicks for one TPD2: sample. Movement is gathered
// until MotionFlush.
//
// Parameters:
//      pSettings
//          [in] Speed, acceleration and tap to click.
//
//      Sample
//          [in] One TPD2: sample.
//
// Returns:
//      None.
//
//------------------------------------------------------------------------------
void MotionHandleSample(const MOTION_SETTINGS* pSettings, DWORD Sample)
{
    int X = (short)(Sample & 0xffff) >> 2;
    int Y = (short)(Sample >> 16) >> 2;
    int State = Sample & 3;
    DWORD Now = GetTickCount();

    if (State == SAMPLE_FIRST)
    {
        MotionFlush();
        s_Pad.bTouching = TRUE;
        s_Pad.TouchStarted = Now;
        s_Pad.TouchStartX = X;
        s_Pad.TouchStartY = Y;
        s_Pad.TouchDistance = 0;
        s_Pad.RemainderX = 0;
        s_Pad.RemainderY = 0;
        s_Pad.SmoothedX = X * FIXED_POINT_ONE;
        s_Pad.SmoothedY = Y * FIXED_POINT_ONE;
        s_Pad.SamplesSinceTouch = 0;
        s_Pad.DelayedCount = 0;
        s_Pad.LastRaw.x = X;
        s_Pad.LastRaw.y = Y;
        if (pSettings->bTapToClick && Now - s_Pad.LastTapReleased < DRAG_WINDOW_MILLISECONDS)
        {
            s_Pad.bDragging = TRUE;
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        }
        return;
    }
    if (State == SAMPLE_MOVE && s_Pad.bTouching)
    {
        int Distance = abs(X - s_Pad.TouchStartX) + abs(Y - s_Pad.TouchStartY);

        if (abs(X - s_Pad.LastRaw.x) + abs(Y - s_Pad.LastRaw.y) > SPIKE_DISTANCE)
            return;
        if (Distance > s_Pad.TouchDistance)
            s_Pad.TouchDistance = Distance;
        s_Pad.LastRaw.x = X;
        s_Pad.LastRaw.y = Y;
        s_Pad.Delayed[s_Pad.DelayedCount].x = X;
        s_Pad.Delayed[s_Pad.DelayedCount].y = Y;
        s_Pad.DelayedCount++;
        if (s_Pad.DelayedCount <= DELAYED_SAMPLES)
            return;
        FollowPosition(pSettings, s_Pad.Delayed[0].x, s_Pad.Delayed[0].y);
        memmove(&s_Pad.Delayed[0], &s_Pad.Delayed[1], sizeof(POINT) * DELAYED_SAMPLES);
        s_Pad.DelayedCount--;
        return;
    }
    if (State == SAMPLE_RELEASE && s_Pad.bTouching)
    {
        MotionFlush();
        s_Pad.bTouching = FALSE;
        s_Pad.LastTapReleased = 0;
        if (s_Pad.bDragging)
        {
            s_Pad.bDragging = FALSE;
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            return;
        }
        if (pSettings->bTapToClick && Now - s_Pad.TouchStarted <= TAP_MAXIMUM_MILLISECONDS
            && s_Pad.TouchDistance <= TAP_MAXIMUM_DISTANCE)
        {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            s_Pad.LastTapReleased = Now;
        }
    }
}
