# Touch Shim

`touchraw.dll` (`TRD1:`) for the PW-SH2 and PW-SH4: a pass-through to Sharp's raw touch driver, which the boot bundle leaves in ROM as `touchra_.dll`.

```
node tools/make-boot-nkp.mjs ... --skip --replace-module touchraw.dll=out/touchraw.dll
```

Sharp's touch driver maps raw to screen with the unit's factory calibration, read from `VMC1:` (code 0x80002093, input length 0, six 16-bit values `a, b, c, d, e, f`) as `x = c + a*rx/b`.

On the PW-SH2 the shim stretches the raw band next to the left, top and bottom edges so a finger can reach them. The factory calibration puts the furthest reachable raw values about 13 px inside the screen.

On the PW-SH4 the factory calibration is made for an 800 px wide screen, so touch stops at about x = 804 on the 848 px desktop. The shim scales raw X so the calibration lands on the full width `W`:

```
rx' = rx*W/800 + c*b*(W - 800)/(800*a)
```

clamped to 0..4095. `a`, `b` and `c` are read from `VMC1:` on the first touch. With the PW-SH2's calibration (640, 2775, -34) and `W` 848 the offset is -9.

## Settings

`HKLM\Software\Brain\Touch`, DWORDs, read when the driver starts:

| Value | Default | Meaning |
| --- | --- | --- |
| `Stretch` | 1, PW-SH4 0 | 0 turns the edge stretch off |
| `PressureLimit` | 3072, PW-SH4 0 | Sharp's per-reading pressure limit (its default is 2048); higher accepts lighter touches. 0 keeps Sharp's |
| `ScreenWidth` | Display width | Screen width in pixels the X scale targets. Unset uses the larger of `FramebufferWidth` and `FramebufferHeight` in `HKLM\Software\Brain\Display`, read on the first touch, or 800 if those are missing. 800 turns the scale off |
| `LeftBand`, `LeftReach`, `LeftTarget` | 400, 205, 147 | Raw X `Band`..`Reach` is mapped onto `Band`..`Target` |
| `TopBand`, `TopReach`, `TopTarget` | 3600, 3833, 3933 | The same for raw Y at the top |
| `BottomBand`, `BottomReach`, `BottomTarget` | 560, 325, 253 | The same for raw Y at the bottom |

`Target` is the raw value the factory calibration puts on the screen edge; the defaults are from one PW-SH2.

## Sample Log

IOCTL `0x80002FF0` returns the last 4096 samples Sharp's driver read (time, status, raw X, raw Y, before stretching) and clears them.
