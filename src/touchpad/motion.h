//------------------------------------------------------------------------------
//
//  File:  motion.h
//
//  Turns aux touch panel samples into mouse movement and clicks.
//
//------------------------------------------------------------------------------
#ifndef __MOTION_H__
#define __MOTION_H__

typedef struct
{
    DWORD Speed;
    BOOL bAcceleration;
    BOOL bTapToClick;
} MOTION_SETTINGS;

void MotionReset();
void MotionHandleSample(const MOTION_SETTINGS* pSettings, DWORD Sample);
void MotionFlush();

#endif /* __MOTION_H__ */
