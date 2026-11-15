//------------------------------------------------------------------------------
//
//  File:  touchraw_ioctl.h
//
//  TRD1: codes added by the PW-SH2 touchraw shim. Every other code goes to
//  Sharp's driver.
//
//------------------------------------------------------------------------------
#ifndef __TOUCHRAW_IOCTL_H__
#define __TOUCHRAW_IOCTL_H__

#define TRD_IOCTL_SAMPLE            0x8000202F
#define TRD_IOCTL_READ_LOG          0x80002FF0

#define TRD_LOG_ENTRIES             4096

typedef struct
{
    DWORD Milliseconds;
    LONG Status;
    LONG X;
    LONG Y;
} TRD_LOG_ENTRY;

typedef struct
{
    DWORD Next;
    DWORD Count;
    TRD_LOG_ENTRY Entries[TRD_LOG_ENTRIES];
} TRD_LOG;

#endif /* __TOUCHRAW_IOCTL_H__ */
