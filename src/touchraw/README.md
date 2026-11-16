# Touch Shim

`touchraw.dll` (`TRD1:`) for the PW-SH2 and PW-SH4: a pass-through to Sharp's raw touch driver, which the boot bundle leaves in ROM as `touchra_.dll`.

```
node tools/make-boot-nkp.mjs ... --skip --replace-module touchraw.dll=out/touchraw.dll
```

Sharp's touch driver maps raw to screen with the unit's factory calibration, read from `VMC1:` (code 0x80002093, input length 0, six 16-bit values `a, b, c, d, e, f`) as `x = c + a*rx/b`. Normal pressure leaves the furthest reachable raw values about 13 px inside the screen at some edges, so the shim stretches the raw band next to them.

On the PW-SH2 the bands are wide, covering the left, top and bottom edges. On the PW-SH4 they cover only the last few pixels a normal press reaches at the top and bottom (y 11 to 15, and 460 to 463), so taps on menus along the top stay where they land. Its left edge is not stretched.

## Settings

`HKLM\Software\Brain\Touch`, DWORDs, read when the driver starts:

| Value | Default | Meaning |
| --- | --- | --- |
| `Stretch` | 1 | 0 passes samples through unchanged |
| `PressureLimit` | 3072, PW-SH4 0 | Sharp's per-reading pressure limit (its default is 2048); higher accepts lighter touches. 0 keeps Sharp's |
| `LeftBand`, `LeftReach`, `LeftTarget` | 400, 205, 147; PW-SH4 200, 200, 200 (off) | Raw X `Band`..`Reach` is mapped onto `Band`..`Target` |
| `TopBand`, `TopReach`, `TopTarget` | 3600, 3833, 3933; PW-SH4 3840, 3870, 3955 | The same for raw Y at the top |
| `BottomBand`, `BottomReach`, `BottomTarget` | 560, 325, 253; PW-SH4 370, 345, 218 | The same for raw Y at the bottom |

`Target` is the raw value the factory calibration puts on the screen edge; the defaults are from one unit of each model.

## Sample Log

IOCTL `0x80002FF0` returns the last 4096 samples Sharp's driver read (time, status, raw X, raw Y, before stretching) and clears them.
