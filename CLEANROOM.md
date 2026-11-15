# Clean Room

The sub LCD, touchpad and PW-A7300 framebuffer code is ported from the user's ce600 prototype, which was written alongside Sharp's drivers and NXP's BSP. The board and touchraw drivers come from pwsh2-boot at 1134d04. Only the user's own code was ported. Nothing in this repository is copied from Platform Builder, NXP/Freescale BSP sources or headers, or Sharp's binaries.

## Not allowed

- NXP's WinCE BSP or PDK sources and headers (`csp.h`, `mx28_ddk.h`, `common_ddkvs.h`, `hw_spi.h`, and so on).
- Microsoft Platform Builder sources and headers.
- Sharp's binaries, disassembled or otherwise.

If something you need is only in one of these, ask the user.

## Accepted interface facts

The user accepted these on 2026-11-15 as non-protectable platform interface facts, despite their source: first those for the sub LCD and touchpad, then those for the board, touchraw and PW-A7300 framebuffer code. `src/compat/cspddk.h` and `src/compat/spi2.h` declare only the names, values and layouts the drivers use; they are not copies of NXP's headers.

### Sharp image or binary

- The ST7586 init sequence in `src/sublcd/st7586.c` (Sharp's, with grey mode `38h` in place of monochrome `39h`), and the VOP encoding in `St7586SetVopBase`.
- The factory calibration block at 0xA00EE400: magic 0x3037, VOP trim at index 16, and Sharp's range fix-ups (below -185 becomes 96, above 79 becomes 360).
- The busy flag of Sharp's `SPI2:` driver at 0xA00EDC58, set around each transfer.
- Sharp's `SLD1:` codes 1-14 and their data, in `src/sublcd/sublcd_ioctl.h`.
- Sharp's `TPD2:` codes 0x80002007 (create queue), 0x8000200B (set mode), 0x8000200F (set event), and its sample format (x in the low 16 bits and y in the high 16 bits, each shifted left by 2, state in the low 2 bits), in `src/touchpad`.
- `PWR1:` (`edna2_powermgr.dll`) codes 0x0022002C (power-off time) and 0x00220030 (light-off time), in `src/board/power_policy.c`.
- The events `PowerManager/ActivityTimer/EdUserActivity` and `EdSystemActivity` of Sharp's `pm.dll`, and `Display_Event_Backlight_Activating`, `Display_Event_Backlight_DIMBlanking` and `Display_Event_PowerManager_Update` of Sharp's backlight handling.
- Sharp's boot clock (240 MHz CPU, 120 MHz AHB, VDDD 1.55 V), in `src/board/cpu_scaling.c`.
- Sharp's raw touch driver (`touchra_.dll`): its `TRD_*` exports, `TRD_IOCTL_SAMPLE` 0x8000202F and the pressure limit code 0x80002043, in `src/touchraw`.

### NXP BSP interface

- `CSPDDK.dll` functions and their prototypes: `DDKIomuxSetPinMux`, `DDKIomuxSetPadConfig`, `DDKGpioEnableDataPin`, `DDKGpioWriteDataPin`, `DDKApbhStartDma`, `DDKApbhDmaInitChan`, `DDKApbhDmaResetChan`, `DDKApbhDmaGetPhore`.
- `DDK_IOMUX_PIN` values `DDK_IOMUX_SSP2_D0_1` 0x52 and `DDK_IOMUX_PWM2` 0x72; `DDK_IOMUX_MODE_00` 0 and `DDK_IOMUX_MODE_GPIO` 3; `DDK_IOMUX_PAD_DRIVE_4MA` 0, `DDK_IOMUX_PAD_PULL_DISABLE` 1, `DDK_IOMUX_PAD_VOLTAGE_3V3` 1; `DDK_APBH_CHANNEL_SSP2` 2.
- The `SPI2:` driver interface: `SPI_IOCTL_EXCHANGE`, `SPI_IOCTL_SSPCONFIGURE` and `SPI_IOCTL_SSPCONFIGTIMING` (`FILE_DEVICE_BUS_EXTENDER` functions 3030, 3033 and 3038), the `SSP_INIT`, `SPI_BUSCONFIG_T` and `SPI_XCH_PKT_T` layouts, `SSP_MODE_SPI` 0, and `SPI_IDENTIFY_SPEED` 0.
- The 454 MHz setpoint (CPUFRAC 19, CPU divider 1, AHB divider 3) and the order of the clock switch, from NXP's `dvfc.dll`, in `src/board/cpu_scaling.c`.

### Platform Builder constant

- `SH_WMGR` = 17, the window manager API set for `WaitForAPIReady`, used by `src/touchpad/touchpad.c` and declared by grimace's `pkfuncs.h`.
- `CACHE_SYNC_WRITEBACK` = 0x004 for `CacheSync`, used by `src/board/framebuffer.c` and declared by grimace's `pkfuncs.h`. Also accepted for brain-display.

## Project choices

Not provenance items:

- The kernel debug log ring that `src/board/register_dump.c` reads: 0x401E0000 on the PW-SH2 (u-boot-brain's `pwsh2_ce_defconfig`) and 0x40180000 on the PW-A7300 (u-boot-brain's Kconfig default, which `pwa7300_ce_defconfig` keeps), matching make-boot-nkp's `--debug-log-address`. `pwsh4_ce_defconfig` does not enable the log; the PW-SH4 build uses 0x401E0000.

## Registers

PWM (0x80064000), SSP2 (0x80014000), CLKCTRL (0x80040000), POWER (0x80044000), DIGCTL (0x8001C000), PINCTRL (0x80018000), LCDIF (0x80030000), PXP (0x8002A000) and APBH DMA descriptor fields are from the public i.MX28 Reference Manual.
