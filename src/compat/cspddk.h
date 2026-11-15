#ifndef __CSPDDK_H__
#define __CSPDDK_H__

typedef enum
{
    DDK_IOMUX_SSP2_D0_1             = 0x52,
    DDK_IOMUX_AUART1_RX             = 0x64,
    DDK_IOMUX_AUART1_TX             = 0x65,
    DDK_IOMUX_PWM2                  = 0x72
} DDK_IOMUX_PIN;

typedef enum
{
    DDK_IOMUX_MODE_00               = 0,
    DDK_IOMUX_MODE_02               = 2,
    DDK_IOMUX_MODE_GPIO             = 3
} DDK_IOMUX_PIN_MUXMODE;

typedef enum
{
    DDK_IOMUX_PAD_DRIVE_4MA         = 0
} DDK_IOMUX_PAD_DRIVE;

typedef enum
{
    DDK_IOMUX_PAD_PULL_ENABLE       = 0,
    DDK_IOMUX_PAD_PULL_DISABLE      = 1
} DDK_IOMUX_PAD_PULL;

typedef enum
{
    DDK_IOMUX_PAD_VOLTAGE_3V3       = 1
} DDK_IOMUX_PAD_VOLTAGE;

#define DDK_APBH_CHANNEL_SSP2       0x02

BOOL DDKIomuxSetPinMux(DDK_IOMUX_PIN Pin, DDK_IOMUX_PIN_MUXMODE Mode);
BOOL DDKIomuxSetPadConfig(DDK_IOMUX_PIN Pin, DDK_IOMUX_PAD_DRIVE Drive, DDK_IOMUX_PAD_PULL Pull, DDK_IOMUX_PAD_VOLTAGE Voltage);
BOOL DDKGpioEnableDataPin(DDK_IOMUX_PIN Pin, UINT32 Data);
BOOL DDKGpioWriteDataPin(DDK_IOMUX_PIN Pin, UINT32 Data);

BOOL DDKApbhStartDma(UINT8 Channel, PVOID pDescriptorPhysical, UINT8 Semaphore);
BOOL DDKApbhDmaInitChan(UINT8 Channel, BOOL bEnableIrq);
BOOL DDKApbhDmaResetChan(UINT8 Channel, BOOL bReset);
UINT32 DDKApbhDmaGetPhore(UINT32 Channel);

#endif /* __CSPDDK_H__ */
