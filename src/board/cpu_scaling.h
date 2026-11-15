//------------------------------------------------------------------------------
//
//  File:  cpu_scaling.h
//
//  CPU clock scaling between Sharp's boot setpoint and NXP's 454 MHz one.
//
//------------------------------------------------------------------------------
#ifndef __CPU_SCALING_H__
#define __CPU_SCALING_H__

#define CPU_SCALING_OFF             0
#define CPU_SCALING_FAST            1
#define CPU_SCALING_DYNAMIC         2

typedef struct
{
    DWORD Mode;
    DWORD CpuMHz;
    DWORD AhbMHz;
    DWORD BusyPercent;
    DWORD Switches;
} CPU_SCALING_STATUS;

void CpuScalingInit();
BOOL CpuScalingSetMode(DWORD Mode, BOOL bSave);
void CpuScalingSetIdle(BOOL bIdle);
void CpuScalingGetStatus(CPU_SCALING_STATUS* pStatus);
void CpuScalingSuspend();
void CpuScalingResume();

#endif /* __CPU_SCALING_H__ */
