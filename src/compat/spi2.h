#ifndef __SPI2_H__
#define __SPI2_H__

#include <winioctl.h>

#define SPI_IOCTL_EXCHANGE          CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 3030, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define SPI_IOCTL_SSPCONFIGURE      CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 3033, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define SPI_IOCTL_SSPCONFIGTIMING   CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 3038, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum
{
    SSP_MODE_SPI                    = 0
} SSP_MODE;

typedef enum
{
    SSP_WORD_LENGTH_8BITS           = 7
} SSP_WORDLENGTH;

typedef enum
{
    SPI_IDENTIFY_SPEED              = 0
} SSP_SPEED;

typedef struct
{
    SSP_MODE eMode;
    SSP_WORDLENGTH eLength;
    BOOL bDmaEnable;
    BOOL bCeataCcsErrorEnable;
    BOOL bPhase;
    BOOL bPolarity;
    BOOL bSlave;
    BOOL bIgnoreCrc;
    BOOL bBusWidth4;
    BOOL bWaitIrq;
    BOOL bLongResp;
    BOOL bCheckResp;
    BOOL bGetResp;
    UINT16 u16TransferCount;
    BOOL bEnable;
    BOOL bWaitCmd;
    BOOL bDataTransfr;
    BOOL bRead;
    BOOL bSlaveOutDisable;
    BOOL bHalfDuplex;
    BOOL bLockCs;
} SSP_INIT;

typedef union
{
    UINT32 U;
} SSP_REGISTER;

typedef struct
{
    SSP_REGISTER SspCtrl0;
    SSP_REGISTER SspCmd;
    SSP_REGISTER SspArg;
    BOOL usedma;
    BOOL usepolling;
    UINT8 bitcount;
    BOOL bRead;
    BOOL bCmd;
} SPI_BUSCONFIG_T;

typedef struct
{
    SPI_BUSCONFIG_T* pBusCnfg;
    LPVOID pBuf;
    UINT32 xchCnt;
    LPWSTR xchEvent;
    UINT32 xchEventLength;
} SPI_XCH_PKT_T;

#endif /* __SPI2_H__ */
