# Backlight Driver

`backlight.dll` (`BKL1:`) replaces Sharp's `backlight.dll`. It keeps Sharp's codes, events and registry key, so `pm.dll`, `edna2_powermgr.dll`, SharpLib, bstart and the brightness applets work unchanged, and adds levels 1-100 on a perceptual curve, a lower minimum and a dim level setting.

Built for the PW-A7300 and the PW-SH2. The PW-SH4 has no values.

```
node tools/make-boot-nkp.mjs ... --skip --replace-module backlight.dll=out/backlight.dll
```

| File | Role |
| --- | --- |
| `backlight_ioctl.h` | Codes, levels and structures for apps |
| `backlight.c` | Stream driver, events, PWM and power states |
| `backlight_curve.*` | Level to PWM counts |
| `backlight_board.h` | Per-board PWM period, divider, pads and Sharp's RAM addresses |

## PWM

The driver owns PWM0 and PWM1 and writes them in full; nothing else may. `CTRL` is written only through `SET`/`CLR`, and only bits 0 and 1. `SFTRST` and `CLKGATE` are cleared on `D0` if set, never set.

| | PW-A7300 | PW-SH2 |
| --- | --- | --- |
| Clock | 24 MHz, `CDIV` 0 | 12 MHz, `CDIV` 1 |
| Period | 2000 counts, 12 kHz | 500 counts, 24 kHz |
| PWM0 | Main duty | Main duty |
| PWM1 | Constant high, or constant low for the low-current range | Same |

PWM1 low gives about a quarter of the brightness at the same PWM0 duty.

## Curve

Lightness (cube root of luminance) rises linearly from level 1 to 100:

- Level 1 is `FloorLuminance`, 0.36% of full by default.
- Level 100 is PWM0 constant high with PWM1 high.
- Below 5% of full luminance, PWM1 is low and PWM0 runs at twice the duty, scaled by `LowCurrentPercent`.

PW-A7300, `LowCurrentPercent` 24:

| Level | PWM0 counts | PWM1 | Of full |
| --- | --- | --- | --- |
| 1 | 30 | low | 0.36% |
| 5 | 55 | low | 0.66% |
| 10 | 102 | low | 1.2% |
| 20 | 262 | low | 3.2% |
| 26 | 412 | low | 4.9% |
| 27 | 106 | high | 5.3% |
| 50 | 375 | high | 18.7% |
| 75 | 972 | high | 48.6% |
| 100 | 2000 | high | 100% |

Sharp's levels 1, 25, 50, 75 and 100 are about 27, 38, 61, 79 and 91 here.

## Levels

Applied on `Display_Event_PowerManager_Update`, and at once when a code changes a level:

1. `Display_Event_Backlight_Keep` exists: the keep level.
2. `Display_Event_Backlight_Activating` set: the user level.
3. `Display_Event_Backlight_DIMBlanking` set: off.
4. Otherwise dim: `DimLevel`, or the user level if that is lower.

Then, as Sharp's driver does:

- Sharp's flag bit 2 set and on AC: `MaximumLevel`, dim included.
- Sharp's low-battery flag set: at most `DimLevel`.
- PW-SH2 with the battery low or critical: at most 50.

Every level except off is then held within `MinimumLevel` and `MaximumLevel`. `D4` turns PWM0 and PWM1 off; `D0` restores the state above.

## Sub Backlight

On the PW-A7300 the sub backlight is on while `Display_Event_Backlight_SubActive` is set and the driver is in `D0`. `edna2_powermgr.dll` sets and times out that event. The driver calls `SubBacklightWriteDataPin` in `EDNA2_slcd_ST7586.dll`, which drives PWM2 at the sub LCD brightness; without that export it drives the pad as a GPIO, as Sharp's driver does.

## Registry

| `HKLM\Software\Brain\Backlight` | |
| --- | --- |
| `DimLevel` | 1-100, default 20 |
| `MinimumLevel` | 1-100, default 1 |
| `MaximumLevel` | 1-100, default 100 |
| `LowCurrentPercent` | Brightness with PWM1 low, as a percentage of PWM1 high at the same PWM0 duty; 1-100, default 24 |
| `FloorLuminance` | Level 1, in hundredths of a percent of full; 1-10000, default 36 |

Read at every apply. Sharp's `HKCU\ControlPanel\Backlight` timeouts are not used; `board.dll`'s power policy owns idle.

## Codes

`backlight_ioctl.h`:

| Code | Name | |
| --- | --- | --- |
| 0x00222EE4 | `BKL_IOCTL_LEVEL` | in DWORD: -1 queries, 1-100 sets the user level (101-120 is 100); out DWORD previous. Signals `Display_Event_Backlight_Update` |
| 0x00222EE8 | `BKL_IOCTL_KEEP_LEVEL` | in DWORD: -1 queries, 127 the user level, -2 dim, 0 off, 1-100; out DWORD previous. Default -2 |
| 0x00222EEC | `BKL_IOCTL_SET_FACTORY_ROW` | PW-SH2: in 16 bytes, written to Sharp's factory row given by the output size (0-7) |
| 0x100 | `BKL_IOCTL_GET_INFO` | out `BKL_INFO`: `Magic` `BKL2`, `Version`, capabilities, applied level, PWM0 counts, period, low current |
| 0x00321000-0x0032100C | `IOCTL_POWER_*` | `D0` and `D4` |

Sharp's driver returns its five buckets for any level; check `BKL_IOCTL_GET_INFO`'s magic before offering levels 1-100.
