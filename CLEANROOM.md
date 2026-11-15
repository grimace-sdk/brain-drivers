# Clean Room

The drivers are ported from the user's ce600 prototype, which was written alongside Sharp's drivers and NXP's BSP. Only the user's own code was ported. Nothing in this repository is copied from Platform Builder, NXP/Freescale BSP sources or headers, or Sharp's binaries.

## Not allowed

- NXP's WinCE BSP or PDK sources and headers (`csp.h`, `mx28_ddk.h`, `common_ddkvs.h`, `hw_spi.h`, and so on).
- Microsoft Platform Builder sources and headers.
- Sharp's binaries, disassembled or otherwise.

If something you need is only in one of these, ask the user.

## Accepted interface facts

The user accepted these on 2026-11-15 as non-protectable platform interface facts, despite their source. `src/compat/cspddk.h` and `src/compat/spi2.h` declare only the names, values and layouts the drivers use; they are not copies of NXP's headers.

### Sharp image or binary

- The ST7586 init sequence in `src/sublcd/st7586.c` (Sharp's, with grey mode `38h` in place of monochrome `39h`), and the VOP encoding in `St7586SetVopBase`.
- The factory calibration block at 0xA00EE400: magic 0x3037, VOP trim at index 16, and Sharp's range fix-ups (below -185 becomes 96, above 79 becomes 360).
- The busy flag of Sharp's `SPI2:` driver at 0xA00EDC58, set around each transfer.
- Sharp's `SLD1:` codes 1-14 and their data, in `src/sublcd/sublcd_ioctl.h`.
- Sharp's `TPD2:` codes 0x80002007 (create queue), 0x8000200B (set mode), 0x8000200F (set event), and its sample format (x in the low 16 bits and y in the high 16 bits, each shifted left by 2, state in the low 2 bits), in `src/touchpad`.

### NXP BSP interface

- `CSPDDK.dll` functions and their prototypes: `DDKIomuxSetPinMux`, `DDKIomuxSetPadConfig`, `DDKGpioEnableDataPin`, `DDKGpioWriteDataPin`, `DDKApbhStartDma`, `DDKApbhDmaInitChan`, `DDKApbhDmaResetChan`, `DDKApbhDmaGetPhore`.
- `DDK_IOMUX_PIN` values `DDK_IOMUX_SSP2_D0_1` 0x52 and `DDK_IOMUX_PWM2` 0x72; `DDK_IOMUX_MODE_00` 0 and `DDK_IOMUX_MODE_GPIO` 3; `DDK_IOMUX_PAD_DRIVE_4MA` 0, `DDK_IOMUX_PAD_PULL_DISABLE` 1, `DDK_IOMUX_PAD_VOLTAGE_3V3` 1; `DDK_APBH_CHANNEL_SSP2` 2.
- The `SPI2:` driver interface: `SPI_IOCTL_EXCHANGE`, `SPI_IOCTL_SSPCONFIGURE` and `SPI_IOCTL_SSPCONFIGTIMING` (`FILE_DEVICE_BUS_EXTENDER` functions 3030, 3033 and 3038), the `SSP_INIT`, `SPI_BUSCONFIG_T` and `SPI_XCH_PKT_T` layouts, `SSP_MODE_SPI` 0, and `SPI_IDENTIFY_SPEED` 0.

### Platform Builder constant

- `SH_WMGR` = 17, the window manager API set for `WaitForAPIReady`, in `src/compat/ce_compat.h`.

## Registers

PWM (0x80064000), SSP2 (0x80014000), CLKCTRL (0x80040000) and APBH DMA descriptor fields are from the public i.MX28 Reference Manual.
