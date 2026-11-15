# Board Driver

`board.dll` (`BRD1:`) holds board services that belong to no device: CPU clock scaling, the idle power policy, a read-only register dump and a GPIO edge counter. It runs on the PW-SH2, PW-SH4 and PW-A7300 (same i.MX28, same 240 MHz and 1.55 V from Sharp). It is a new driver, so `boot.nkp` adds it as a ROM file and its key, `board.reg`, to the boot registry:

```
[HKEY_LOCAL_MACHINE\Drivers\BuiltIn\Board]
"Prefix"="BRD"
"Dll"="board.dll"
"Order"=dword:3
"Index"=dword:1
```

```
node tools/make-boot-nkp.mjs ... --skip --replace-module board.dll=out/board.dll --registry ce/drivers/board/board.reg
```

| File | Role |
| --- | --- |
| `board_ioctl.h` | Codes and structures for apps |
| `board.c` | Stream driver |
| `cpu_scaling.*` | CPU clock governor |
| `power_policy.*` | Activity bridge and idle policy for Sharp's power manager |
| `register_dump.*` | Whitelisted register reads |
| `pin_sampler.*` | GPIO edge counter |

## CPU Scaling

Sharp boots the i.MX28 at 240 MHz CPU / 120 MHz AHB with VDDD already at 1.55 V, NXP's voltage for 454 MHz, and never registers NXP's `dvfc.dll` (which also reclocks DDR with EVK memory timings). `cpu_scaling.c` switches only CPU and AHB between that and NXP's high setpoint (CPUFRAC 19, CPU divider 1, AHB divider 3), in NXP's order; DDR (196 MHz, near the controller's limit) and VDDD are untouched. Dynamic mode goes up when a 50 ms window is half busy and down after 500 ms under a quarter busy.

Tested: 5 min of memory patterns over 32 MB at 454 MHz and 10 min while switching every 50 ms (3,145 switches), no errors. CPU-bound loop 1.85x faster; a cold My Device open 810 ms (before fast blits) to about 570 ms.

## Power Policy

`power_policy.c` makes Control Panel > Power > Schemes (`HKLM\System\CurrentControlSet\Control\Power\Timeouts`) work. Sharp's `pm.dll` (both ROMs) is Microsoft's PDD/DEFAULT state machine (On, UserIdle, SystemIdle, Suspend) on Sharp's `EdUserActivity` and `EdSystemActivity` timers, which only Sharp's touch drivers and SharpLib reset; it reads the timeouts at boot and on `PowerManager/ReloadActivityTimeouts`.

- While the standard `UserActivity` timer (reset by GWES for every input, keys included) is active, `EdUserActivity` is reset once a second.
- A change to the Timeouts key (Control Panel, or the restore from the card) signals the reload.
- Sharp's `edna2_powermgr.dll` (`PWR1:`) backlight-off and power-off timers (IOCTL 0x220030 and 0x22002C) are set to 0, never armed, so the timeouts have one owner.
- On `useridle` and `systemidle` the backlight is dimmed (`Display_Event_Backlight_Activating` and `Display_Event_Backlight_DIMBlanking` reset, then `Display_Event_PowerManager_Update`) and the CPU is held at Sharp's clock; on `on` both are restored. SystemIdle turns the backlight and display off and Suspend suspends, as Sharp's registry already maps them.
- The standard `SystemActivity` timer is not passed on: something resets it about 20 times a second even with every user process stopped, which would block suspend. Programs that must keep the Brain awake reset `PowerManager/ActivityTimer/EdSystemActivity`.

Tested on the PW-SH2 (2026-11-02): dims, turns the screen off and suspends at the scheme's times, and wakes on a key or tap.

## Registry

| Value | |
| --- | --- |
| `HKLM\Software\Brain\CPU` `Scaling` | 0 Sharp's 240 MHz, 1 always 454 MHz, 2 follow load (default); read again every 5 s |

## Codes

`board_ioctl.h`:

| Code | Name | |
| --- | --- | --- |
| 0x100 | `BRD_IOCTL_GET_INFO` | out `BRD_INFO` (`Magic` `BRD1`, `Version`) |
| 0x101 | `BRD_IOCTL_READ_REGISTERS` | in `BRD_REGISTER_READ`; clock, power, digital control and SSP2 control blocks only |
| 0x102 | `BRD_IOCTL_CPU_SCALING` | optional in DWORD mode (+0x100 saves it); out `BRD_CPU_STATUS` |
| 0x103 | `BRD_IOCTL_SAMPLE_PINS` | in DWORD milliseconds (up to 1000); out `BRD_PIN_SAMPLE`: rising edges per GPIO pin with first and last tick, from reading `HW_PINCTRL_DIN0-4` in a loop. Found the PW-SH2's panel TE on 3_30 (59.3 Hz) |
