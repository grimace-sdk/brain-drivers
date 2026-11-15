# a7300-drivers

Windows CE 6.0 drivers for the Sharp Brain PW-A7300 (i.MX28), built with [grimace](https://github.com/grimace-sdk/grimace).

| Driver | Output | Device | Replaces |
| --- | --- | --- | --- |
| Touchpad | `touchpad.dll` | `PAD1:` | Nothing; new driver |
| Sub LCD | `EDNA2_slcd_ST7586.dll` | `SLD1:` | Sharp's `EDNA2_slcd_ST7586.dll` |

Read [CLEANROOM.md](CLEANROOM.md) before changing anything. It lists the Sharp and NXP interface facts the user has accepted.

## Build

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$GRIMACE/cmake/toolchain.cmake" \
    -DGRIMACE_ARCH=arm -DGRIMACE_CE_VERSION=6 -DGRIMACE_PLATFORM=brain \
    -DGRIMACE_LLVM_ROOT="$GRIMACE_LLVM"
cmake --build build
```

CI pins grimace `0aa10fc` and llvm-builds `v5`.

## Touchpad

`touchpad.dll` (`PAD1:`) makes the aux touch panel (`TPD2:`, Sharp's `EDNA2_TouchSub.dll`) CE's mouse. It runs in `udevice.exe` so it can call `mouse_event`, and waits for the window manager before opening `TPD2:`. `src/touchpad/touchpad.reg` loads it from `Drivers\BuiltIn\Touchpad`.

Tap to click: finger within 5 px of where it landed, up to 220 ms. Tap then touch again within 250 ms and drag to hold the button. Speed 1-5 scales movement by 4, 6, 10, 13 or 16 sixteenths; acceleration raises that up to 2.5x.

`TPD2:` sends samples only to the most recently opened handle. An app that reads it opens `PAD1:`, sends `PAD_IOCTL_CLAIM` and keeps the handle while it uses `TPD2:`. When the last claiming handle closes, including on process exit, the driver takes the panel back.

| `HKLM\Software\Brain\Touchpad` | |
| --- | --- |
| `Enabled` | 1 (default) uses the panel as a touchpad |
| `Speed` | 1-5, default 3 |
| `Acceleration` | 1 (default) on |
| `TapToClick` | 1 (default) on |

Read again on `PAD_IOCTL_RELOAD` and every 5 s.

Codes, in `src/touchpad/touchpad_ioctl.h`:

| Code | Name | |
| --- | --- | --- |
| 0x100 | `PAD_IOCTL_GET_INFO` | out `PAD_INFO` (`Magic` `PAD1`, `Version`, `Claims`, `Active`) |
| 0x101 | `PAD_IOCTL_CLAIM` | this handle claims `TPD2:` |
| 0x102 | `PAD_IOCTL_RELEASE` | drops this handle's claim |
| 0x103 | `PAD_IOCTL_RELOAD` | read the settings again |

## Sub LCD

`EDNA2_slcd_ST7586.dll` (`SLD1:`) drives the 240x120 ST7586 sub LCD. It keeps Sharp's codes 1-14 and adds 4-level greyscale, an overlay, backlight brightness and buzzer tones. It keeps Sharp's file name because pw-a7300's patched `backlight.dll` imports `SubBacklightSetPinMux` and `SubBacklightWriteDataPin` from it.

| File | Role |
| --- | --- |
| `sublcd_ioctl.h` | Codes and structures for apps |
| `sld.c` | Stream driver, shadow buffer, Sharp's codes |
| `st7586.*` | Controller setup and transfers |
| `ssp_dma.*` | SSP2 DMA at 16 MHz, falling back to `SPI2:` |
| `sub_backlight.*` | PWM2 brightness, stand-ins for `backlight.dll` |
| `buzzer_tone.*` | Buzzer tones on PWM4/PWM7 |
| `cspddk.txt` | Functions imported from the ROM's `CSPDDK.dll` |

| `HKLM\Software\Brain\SubLCD` | |
| --- | --- |
| `Brightness` | 1-100, default 100 |
| `SpiDma` | 1 (default) sends by DMA |
| `SpiClockKHz` | SPI clock, default 16000 |

Codes, in `src/sublcd/sublcd_ioctl.h`. Sharp's driver returns TRUE for unknown codes without writing output, so check the magic.

| Code | Name | Data |
| --- | --- | --- |
| 0x100 | `SLD_IOCTL_GET_INFO` | out `SLD_INFO`: magic `SLD2`, version, capabilities |
| 0x101 | `SLD_IOCTL_DRAW_GREY` | in `SLD_GREY_DRAW` |
| 0x102 | `SLD_IOCTL_SET_BACKLIGHT` | in DWORD 1-100, saved to the registry |
| 0x103 | `SLD_IOCTL_GET_BACKLIGHT` | out DWORD |
| 0x104 | `SLD_IOCTL_SET_TONE` | in DWORD hertz, 0 stops |
| 0x105 | `SLD_IOCTL_SET_OVERLAY` | in `SLD_GREY_DRAW`; zero width or height hides |

## grimace gaps

`src/compat/ce_compat.h` covers what grimace's headers lack: `WaitForAPIReady` and `SH_WMGR`, and a `RETAILMSG` that prints through `NKDbgPrintfW` (grimace's is empty). `src/compat/cspddk.h` and `src/compat/spi2.h` declare the `CSPDDK.dll` functions and the `SPI2:` interface. The sub LCD target makes its `CSPDDK.dll` import library from `src/sublcd/cspddk.txt`.

## License

MIT. See [LICENSE](LICENSE).
