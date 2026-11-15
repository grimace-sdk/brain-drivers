# Touch Shim

`touchraw.dll` (`TRD1:`) for the PW-SH2: a pass-through to Sharp's raw touch driver, which the boot bundle leaves in ROM as `touchra_.dll`.

```
node tools/make-boot-nkp.mjs ... --skip --replace-module touchraw.dll=out/touchraw.dll
```

It stretches the raw band next to the left, top and bottom edges so a finger can reach them. Sharp's touch driver maps raw to screen with the unit's factory calibration, which puts the furthest reachable raw values about 13 px inside the screen.

## Settings

`HKLM\Software\Brain\Touch`, DWORDs, read when the driver starts:

| Value | Default | Meaning |
| --- | --- | --- |
| `Stretch` | 1 | 0 passes samples through unchanged |
| `PressureLimit` | 3072 | Sharp's per-reading pressure limit (its default is 2048); higher accepts lighter touches. 0 keeps Sharp's |
| `LeftBand`, `LeftReach`, `LeftTarget` | 400, 205, 147 | Raw X `Band`..`Reach` is mapped onto `Band`..`Target` |
| `TopBand`, `TopReach`, `TopTarget` | 3600, 3833, 3933 | The same for raw Y at the top |
| `BottomBand`, `BottomReach`, `BottomTarget` | 560, 325, 253 | The same for raw Y at the bottom |

`Target` is the raw value the factory calibration puts on the screen edge; the defaults are from one unit.

## Sample Log

IOCTL `0x80002FF0` returns the last 4096 samples Sharp's driver read (time, status, raw X, raw Y, before stretching) and clears them.
