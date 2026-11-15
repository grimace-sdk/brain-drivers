# Clean Room

The sub LCD, touchpad and PW-A7300 framebuffer code is ported from the user's ce600 prototype, which was written alongside Sharp's drivers and NXP's BSP. The board and touchraw drivers come from pwsh2-boot at 1134d04. The backlight driver is new, written from the interface facts below. Only the user's own code was ported. Nothing in this repository is copied from Platform Builder, NXP/Freescale BSP sources or headers, or Sharp's binaries.

## Not allowed

- NXP's WinCE BSP or PDK sources and headers (`csp.h`, `mx28_ddk.h`, `common_ddkvs.h`, `hw_spi.h`, and so on).
- Microsoft Platform Builder sources and headers.
- Sharp's binaries, disassembled or otherwise.

If something you need is only in one of these, ask the user.

## Accepted interface facts

The user accepted these on 2026-11-15 as non-protectable platform interface facts, despite their source: first those for the sub LCD and touchpad, then those for the board, touchraw and PW-A7300 framebuffer code, then those for the backlight driver ("Green for hardware facts"). `src/compat/cspddk.h` and `src/compat/spi2.h` declare only the names, values and layouts the drivers use; they are not copies of NXP's headers.

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
- Sharp's backlight driver (`backlight.dll`, `BKL1:`), in `src/backlight`:
  - its `BKL_*` exports and codes: 0x00222EE4 (user level; -1 queries; out the previous level), 0x00222EE8 (keep level, used while `Display_Event_Backlight_Keep` exists; -1 queries, 127 the user level, -2 dim, 0 off), and on the PW-SH2 0x00222EEC (16-byte factory row, row index in the output size); both level codes signal `Display_Event_Backlight_Update`;
  - its events and how it creates them: `Display_Event_Backlight_Activating` manual and set, `Display_Event_Backlight_DIMBlanking` manual and clear, `Display_Event_Backlight_SubActive` (PW-A7300) manual and clear, `Display_Event_PowerManager_Update` auto;
  - the apply order on `Display_Event_PowerManager_Update`: keep level if `Display_Event_Backlight_Keep` exists, else the user level if Activating is set, else off if DIMBlanking is set, else dim;
  - power states `D0` and `D4`: `D4` disables PWM0 and PWM1 and turns the PW-A7300 sub backlight off; `D0` clears `SFTRST`/`CLKGATE` and muxes the pads again;
  - the factory block: rows of {ACTIVE0, PERIOD0, ACTIVE1, PERIOD1}, at PA 0x400EA800 (rows from 0xA00EA850) on the PW-SH2 and PA 0x400EE400 (rows from 0xA00EE44C) on the PW-A7300, and Sharp's default PERIOD0, 0x001B01F3 on the PW-SH2 and 0x000B07CF on the PW-A7300;
  - the low-battery flag (`& 0x80000001` both set; 0xA00EA074 PW-SH2, 0xA00EDC68 PW-A7300), which caps the level at the dimmest; flag bit 2 (0xA00EA008 PW-SH2, 0xA00EDC08 PW-A7300), which with AC power gives the brightest level; and on the PW-SH2, battery low or critical capping the level at 50;
  - PWM0 is the main duty; PWM1 is constant high, and low or partly on in the dimmest row, which roughly halves the brightness; both on `CSPDDK.dll` pins 100 and 101, mode 2, pad config 4 mA, pull 0, 3.3 V; the PW-A7300 sub backlight on pad PWM2.

### NXP BSP interface

- `CSPDDK.dll` functions and their prototypes: `DDKIomuxSetPinMux`, `DDKIomuxSetPadConfig`, `DDKGpioEnableDataPin`, `DDKGpioWriteDataPin`, `DDKApbhStartDma`, `DDKApbhDmaInitChan`, `DDKApbhDmaResetChan`, `DDKApbhDmaGetPhore`.
- `DDK_IOMUX_PIN` values `DDK_IOMUX_SSP2_D0_1` 0x52, `DDK_IOMUX_AUART1_RX` 0x64, `DDK_IOMUX_AUART1_TX` 0x65 and `DDK_IOMUX_PWM2` 0x72; `DDK_IOMUX_MODE_00` 0, `DDK_IOMUX_MODE_02` 2 and `DDK_IOMUX_MODE_GPIO` 3; `DDK_IOMUX_PAD_DRIVE_4MA` 0, `DDK_IOMUX_PAD_PULL_ENABLE` 0, `DDK_IOMUX_PAD_PULL_DISABLE` 1, `DDK_IOMUX_PAD_VOLTAGE_3V3` 1; `DDK_APBH_CHANNEL_SSP2` 2.
- The `SPI2:` driver interface: `SPI_IOCTL_EXCHANGE`, `SPI_IOCTL_SSPCONFIGURE` and `SPI_IOCTL_SSPCONFIGTIMING` (`FILE_DEVICE_BUS_EXTENDER` functions 3030, 3033 and 3038), the `SSP_INIT`, `SPI_BUSCONFIG_T` and `SPI_XCH_PKT_T` layouts, `SSP_MODE_SPI` 0, and `SPI_IDENTIFY_SPEED` 0.
- The 454 MHz setpoint (CPUFRAC 19, CPU divider 1, AHB divider 3) and the order of the clock switch, from NXP's `dvfc.dll`, in `src/board/cpu_scaling.c`.

### Platform Builder constant

- `SH_WMGR` = 17, the window manager API set for `WaitForAPIReady`, used by `src/touchpad/touchpad.c` and declared by grimace's `pkfuncs.h`.
- `CACHE_SYNC_WRITEBACK` = 0x004 for `CacheSync`, used by `src/board/framebuffer.c` and declared by grimace's `pkfuncs.h`. Also accepted for brain-display.

## Project choices

Not provenance items:

- The backlight curve, the 5% low-current crossover, `DimLevel` 20, `FloorLuminance` and the `HKLM\Software\Brain\Backlight` settings. The 1/2000 minimum, the 24% `LowCurrentPercent` and the 0.36% `FloorLuminance` defaults come from the user's PW-A7300 tests on 2026-11-15.
- The kernel debug log ring that `src/board/register_dump.c` reads: 0x401E0000 on the PW-SH2 (u-boot-brain's `pwsh2_ce_defconfig`) and 0x40180000 on the PW-A7300 (u-boot-brain's Kconfig default, which `pwa7300_ce_defconfig` keeps), matching make-boot-nkp's `--debug-log-address`. `pwsh4_ce_defconfig` does not enable the log; the PW-SH4 build uses 0x401E0000.

## Registers

PWM (0x80064000), SSP2 (0x80014000), CLKCTRL (0x80040000), POWER (0x80044000), DIGCTL (0x8001C000), PINCTRL (0x80018000), LCDIF (0x80030000), PXP (0x8002A000) and APBH DMA descriptor fields are from the public i.MX28 Reference Manual.
