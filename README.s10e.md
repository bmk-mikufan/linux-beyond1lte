Linux 7.2.13 for the Samsung Galaxy S10e (beyond0lte, SM-G970F)
================================================================

Mainline Linux for the Samsung Galaxy S10e (SM-G970F, codename beyond0lte, Exynos 9820). It runs the
phone on **Mobian** (Debian trixie with Mobian's repository) with Phosh, next to LineageOS.

The tree is stable **v7.2.13**, plus the Exynos 9820 work of
[chiffathefox/exynos-9820-mainline-linux](https://github.com/chiffathefox/exynos-9820-mainline-linux)
(7.2-rc2 based, rebased here onto 7.2.13), plus the S10e changes listed below. The device tree is not in
this tree: it is built into U-Boot and handed to Linux from there (see `mobian/uboot/`).

What works
==========

| Part | State |
| --- | --- |
| Display | 1080x2280 AMOLED (S6E3FA7) on DECON, taken over from the bootloader; brightness through `s10e_panel_bl`; calibrated "Natural" colour (the panel's mDNIe, set by the panel driver); panel self-refresh, so an idle screen sends no frames; an Always On Display whose analog clock the panel draws and ticks by itself in its low power mode, so the phone sleeps through it |
| GPU | Mali-G76 MP12 with panfrost, OpenGL ES 3.1, DVFS 156–702 MHz (simple_ondemand) |
| Touchscreen | S6SY761, double tap to wake |
| Wi-Fi | BCM4375 B1 on PCIe, 2.4 and 5 GHz (new Exynos 9820 PCIe driver, brcmfmac support for the 4375); stays associated while the phone sleeps and wakes it for unicast traffic |
| Bluetooth | BCM4375 B1 on UART, Bluetooth LE included |
| Sound | speaker and microphones through the CS47L93 codec and CS35L41 amps; media playback drives ABOX without its DSP firmware, calls use it; a call's downlink and uplink as PCMs of their own, at the rates the modem's voice path uses, for a GSM gateway |
| Modem | Shannon CP boots from the phone's own `radio` partition; voice calls with audio, SMS, LTE data (`rmnet0`, ~47/34 Mbit/s), APN from the SIM |
| Sensors | accelerometer (screen rotation), ambient light, proximity and compass through the BCM4775 sensor hub, through suspend and resume |
| Cameras | rear wide S5K2L4 with AK7375 autofocus, rear ultra-wide S5K3P9, front S5K3J1, through a new CSIS/write-DMA driver and libcamera's software ISP (tuning in `mobian/camera/`) |
| Video | MFC hardware decode: H.264, HEVC, VP9 |
| DisplayPort | USB-C DP alt mode to HDMI docks: 1080p60/120 with audio, a second CRTC on DECON2, HDMI CEC through docks that tunnel it |
| USB | device mode (networking), host mode with hubs, USB 3 SuperSpeed through docks, Type-C orientation from the MAX77705 |
| Battery | MAX77705 charger and fuel gauge; Qi wireless charging at 5 V through the charger's WCIN input; on a 0.5 A USB port it shows as charging, as Android does; without USB PD the input limit follows the Type-C current and BC1.2 detection (up to 1.8 A), as stock does |
| Suspend | the phone sleeps (s2idle) a few seconds after the screen goes off, whenever nothing needs it awake (`s10e-sleepd`); calls, SMS, Wi-Fi, the power key, a double tap and the charger wake it. Kernel timers stop while it sleeps (a WFI idle state for suspend to idle) |
| Watchdog | the SoC watchdog, fed by systemd: a stalled SoC resets by itself, and the kernel log of a panic survives in pstore |
| RAM | 5464 MiB usable, after handing four of the vendor's camera and secure carve-outs (449 MiB) back to the kernel |
| RTC | S2MPS19 real-time clock |
| Buttons | power, volume, haptics |

Not done: GPS, the fingerprint sensor (S-Boot locks its SPI bus to the secure world, so Linux cannot reach it
at all), NFC, VoLTE, IPv6 on mobile data, the front camera's tuning, the SoC's own sleep state (s2idle keeps
the SoC's rails up, so the phone still draws close to 1 W asleep, far from Android's standby), CPU idle states
beyond WFI (C2 hangs the SoC), the display link's power-down in the AOD (Samsung's DOZE_SUSPEND), MFC encode,
the flash LED, fast (10 V) wireless charging and Wireless PowerShare. The charger's BC1.2
limits are checked on a PC port only.

Installing it
=============

`mobian/zip/` builds the LineageOS recovery zip (`adb sideload`): it moves the end of `userdata` into a 40 GiB
`linux` partition, writes the root filesystem and U-Boot, formats Android's data on a first install, and
reboots into Linux. `s10e-firstboot` then copies the phone's own firmware from `/vendor` (see Firmware).

Booting it
==========

The phone keeps LineageOS. **U-Boot goes into the recovery partition**, repacked into LineageOS's
recovery image header (`mobian/uboot/repack.py`). It loads `/boot/s10e/Image`, `initramfs.cpio.gz` and
`bootargs.env` from the partition labelled `linux` (the Debian root filesystem; on the test phone it was
carved out of `userdata`) and passes its own device tree. `adb reboot recovery` or a plain `reboot` from
Linux gets there. Volume Down in U-Boot opens the menu: Mobian, Mobian with the previous kernel
(`Image.prev`), a USB rescue shell, LineageOS, download mode, console, power off.

Build with:

```
make ARCH=arm64 LLVM=1 LOCALVERSION= olddefconfig      # starting from mobian/kernel.config as .config
make ARCH=arm64 LLVM=1 LOCALVERSION= Image modules
```

The kernel release is `7.2.13-s10e`. Pass `LOCALVERSION=` to every make, or it gains a `+`.

Changes for the Galaxy S10e
===========================

On top of chiffathefox's Exynos 9820 tree (clocks, pinctrl, UFS, ACPM, the PMIC, simple display):

* **Display:** `drivers/gpu/drm/tiny/exynos9820-decon-kms.c` keeps the bootloader's DECON/DSIM setup and
  turns it into a real KMS device; `panel-samsung-s6e3fa7-beyond0.c`, which also loads the panel's mDNIe
  colour tables (Samsung's "Natural" mode: the bootloader leaves a violet-tinted one); simpledrm reports a DSI connector
  so Phosh treats the panel as built-in. Panel self-refresh (`exynos9820-decon-kms.psr`, on by default): the TE
  trigger starts a frame only when the picture has changed, so a still screen or the AOD clock costs no frames
  between updates.
* **Always On Display:** the panel driver's `lpm` (sysfs: 0, or 2, 10, 30 or 60 nit) puts the S6E3FA7 in Samsung's
  HLPM, where it refreshes from its own memory at a low rate and voltage; `self_clock` (`analog X Y UTCOFF [12|24]`,
  `off`, `reload`) starts the panel's own analog clock there: it draws hour and minute hands over its frame memory and
  moves them on its own timer, so the AP sleeps for hours with the time right. The hands are a firmware image
  (`samsung/s6e3fa7-beyond0-aod-hands.bin`) made from Samsung's GPL source (`mobian/display/aod/`); a PM notifier
  sets the time again after a resume. The panel's self move is a module parameter (`self_move`, off). On the
  panel's clock the AOD costs 29 mA (about 0.12 W) over the panel being off.
* **Touchscreen:** `s6sy761.c` gets the controller's low power mode with its double tap gesture (`wake_gesture`
  in sysfs). Userspace turns it on when it blanks the screen; the double tap comes from a second input device as
  `KEY_WAKEUP`.
* **GPU:** panfrost on the G76 (the G3D power domain stays on: re-powering it needs a secure-world
  restore that mainline does not do).
* **Wi-Fi:** `drivers/pci/controller/dwc/pcie-exynos9820.c` (root complex and PHY), and brcmfmac support
  for the BCM4375 B1, including the shared `host_cap` the v7 PCIe protocol needs before the firmware
  consumes receive buffers. The 4375's firmware takes mailbox data (D3 inform, D0 inform) as control ring
  messages, as Broadcom's dhd sends it, not through the TCM mailbox, so suspend gets its D3 acknowledgement.
  Its deep sleep requests are left unanswered: brcmfmac has no device-wake handshake to wake it again.
  The 4375's firmware has no wowl, so the dongle wakes the host over its out-of-band line instead
  (`brcmfmac host_wake=gpa0:7`, as Broadcom's `BCMPCIE_OOB_HOST_WAKE`): the link stays up through a suspend,
  wowlan "any" is on by default, and while the phone sleeps the dongle passes only unicast frames (the firmware
  answers ARP and ND) and sends the access point a keepalive every 30 s.
* **Bluetooth:** CMU_PERIC1 clocks for UART_BT, the USI wrapper, hci_bcm power sequencing.
* **Sound:** `sound/soc/samsung/exynos9820-abox.c` drives ABOX's DMA and routing directly for media,
  and loads its DSP firmware (calliope) for the voice path of calls; `s10e_madera.c` machine driver; CS35L41 changes for the S10e amps.
  For a GSM gateway, two more PCMs work during a call: "Call Downlink" (`hw:0,2`, capture: the far end's voice
  as the CP sends it, before anything is mixed in) and "Call Uplink" (`hw:0,3`, playback: with the "Call Uplink
  Switch" control on it goes to the CP in place of the microphones). The downlink is 16 kHz, as the CP's voice
  frames are, and the uplink takes 4 channels, as ABOX's mixer reads them.
* **Modem:** `drivers/misc/exynos9820-cp.c`, the boot and shared-memory link to the Shannon CP. The radio
  stack runs in userspace (`exynos-cpd`, `mobian/modem/`): ModemManager talks AT over a WWAN AT bridge,
  and `exynos-cpd` translates that to Samsung IPC. The CP's mailbox interrupt is a wakeup source: every
  message from the CP keeps the phone awake for `wake_ms` (1 s), the CP's own wakelock bit holds it awake as
  long as it is set, and `exynos-cpd` holds a kernel wakelock while a call rings or is up and for 10 s after an
  SMS, as rild and Android's in-call screen do. Around every suspend the driver tells the CP that the AP sleeps
  (stock's `pda_active`), so it holds back what can wait.
* **Suspend:** suspend to idle works. Pin-controller 7 (VTS) is left out of pinctrl-exynos: it sits in a power
  domain that is off, and saving its banks in the late suspend phase stalled the SoC. genpd no longer powers on
  the domain of a runtime-suspended device in resume: on the 9820 the power-off after it failed and left MFC and
  the ISP domains half on, and the next touch of them stalled the bus. With no idle states in the DT,
  cpuidle-psci registers a WFI state with `enter_s2idle`, so suspend to idle freezes the tick and kernel timers
  (and systemd's watchdogs) stop while the phone sleeps. The USB PHY's own supplies (S2MPS19 LDO10M-12M) go
  down with it, which saves some 150 mW in every sleep. The policy is userspace's: `mobian/sleepd/`.
* **Watchdog:** `s3c2410_wdt` knows the 9820's CLUSTER0 watchdog; U-Boot's DT has the node, and systemd feeds it.
* **RAM:** `reserved_mem_skip=<node>[,...]` hands those `/reserved-memory` carve-outs to the kernel without
  changing the boot loader's DT. The phone's `bootargs.env` frees `sec_rdx_bootdev`, `secure_camera`,
  `camera_contig` and `camera`; freeing the debug-log regions as well moved CMA and swiotlb and crashed two boots in
  five.
* **Sensor hub:** `drivers/iio/common/bcm4775_shub/`. The BCM4775 boots from ROM and needs a RAM patch on
  every power-up; the driver loads it (`brcm/bcm4775-shub.patch`, from the phone's Android), speaks
  Broadcom's transport layer and Samsung's SSP protocol on top, and exposes the accelerometer, light and
  proximity sensors as IIO devices for iio-sensor-proxy. Each sensor runs only while it is read. The
  compass is a tilt-compensated heading worked out from the hub's calibrated magnetometer and the
  accelerometer, as the triggered buffer iio-sensor-proxy expects; it appears once the hub has booted.
  `mobian/sensorhub/` has the protocol notes and the userspace scripts it was worked out with. Through a
  suspend it follows stock's handshake (an AP_SLEEP pulse, then AP_STATUS_RESUME), and when its SPI
  controller comes back from a resume clocking nothing, the driver lets the controller autosuspend and boots
  the hub again, holding the phone awake meanwhile.
* **USB:** `drivers/usb/typec/max77705.c` (Type-C port and orientation); the USB 3 DRD PHY for the
  Exynos 9820 with orientation-aware lane setup; dwc3 quirks stock sets in GUSB3PIPECTL (elastic buffer
  mode, U-state exit in P1-P3, failed U1/U2 exit to Recovery).
* **Video:** MFC v12 decode in s5p-mfc.
* **Cameras:** `drivers/media/platform/samsung/exynos9820-csis/` (MIPI CSI-2 receivers, the PHYs and the write
  DMA, as a V4L2 capture device), `drivers/media/i2c/s5k3p9.c` (data-driven: S5K2L4, S5K3P9 and S5K3J1, with
  register tables), AK7375 changes for the wide camera's lens (powered only while the camera streams), the S2MPB02/S2MPB03 camera PMICs, and CMU_ISPPRE.
  libcamera's simple pipeline and software ISP do the rest (`mobian/camera/libcamera-0.7.2-s10e.patch`).
* **DisplayPort:** `drivers/gpu/drm/tiny/exynos9820-dp.c` (link controller, AUX, EDID, training, CEC over AUX)
  as a bridge on a second CRTC (DECON2) in `exynos9820-decon-kms.c`; DP alt mode and HPD in `max77705.c`.
* **USB hubs:** dwc3 on the 9820 resets GFLADJ with REFCLK_LPM_SEL set while no 24 MHz reference is there, so the
  xHCI frame counter ran almost 3x too fast and every hub failed to enumerate; the bit is cleared now.
* **Power:** MAX77705 charger and MAX17042-family fuel gauge changes, including the WCIN input the IDT
  wireless receiver feeds (selected, limited to 820 mA after a ramp from 480 mA as stock does, reported
  as online). Without a PD contract the input limit is the Type-C current the source advertises or what the
  chip's BC1.2 detection found (a charging port 1.5 A, a dedicated charger 1.8 A), as stock's cable table has it.
  A supplied battery that is not full reports Charging even while a 0.5 A port cannot keep up
  with the screen (`max17042_battery.supplied_is_charging`), as Android shows "charging slowly". S2MPS19 RTC
  and 32 kHz clock over ACPM; Exynos 9820 PMU and power domains.

Firmware
========

None is in this tree. On the phone, `s10e-firstboot` copies it from the phone's own Android
(`/vendor/firmware`) on the first boot: the BCM4375 Wi-Fi firmware, CLM blob and NVRAM, the Bluetooth `.hcd`,
`s5p-mfc-v13.fw`, and the ABOX `calliope_*` and audio files. The sensor hub patch
(`bbd_patch_file_beyond.h` in Samsung's kernel source, converted to text) is not on `/vendor`, so the release
zip carries it. The modem image is read from the `radio` partition at boot. The AOD's hand image is made from
Samsung's GPL kernel source by `mobian/display/aod/` (hour and minute hands only: `hands-clean-second.py ... none`)
into `/lib/firmware/samsung/s6e3fa7-beyond0-aod-hands.bin`; without it the AOD shows the time as text.

mobian/
=======

The userspace half: U-Boot's boot menu and the S10e device tree, the initramfs init, the kernel config,
the services and configuration the hardware needs on Mobian, the sleep daemon, the recovery zip, the camera
stack, and the Neo launcher and apps the image ships. See `mobian/README.md`.
