# Galaxy S10 (SM-G973N/F) — Ubuntu Server USB KVM

pulpoff/linux-beyond0lte(S10e, Linux 7.2.13)를 S10(beyond1lte)용으로 옮긴 브랜치입니다.
목표: **유전원 PD 허브**에 폰을 꽂고, 허브에 단 **USB 시리얼→HID 젠더**와 **캡처카드**를 쓰면서
**PD 패스스루로 충전**하는 헤드리스 KVM.

> ⚠️ **S10 실기기에서 아직 부팅해 보지 않았습니다.** 여기 있는 건 S10e에서 검증된 커널과
> S10 DTS를 정적으로 대조해 만든 것입니다. 처음 플래시할 때는 복구 방법(아래)을 먼저 준비하세요.

## 왜 butztill/s10-linux가 아니라 pulpoff 기반인가

| 항목 | butztill/s10-linux (S10) | pulpoff (S10e) → 이 브랜치 |
|---|---|---|
| Type-C ↔ dwc3 role switch 연결 | **없음** (커넥터 그래프 없음) | 있음 |
| 허브 인식 (GFLADJ 버그) | **미수정** → 허브 열거 실패 예상 | 수정됨 |
| USB3 PHY | 없음 (USB2만) | 있음 (기본 off, 옵션) |
| DR_Swap 요청 | 없음 | DP 독일 때만 → **이 브랜치에서 PD 허브로 확장** |
| suspend | s2idle, deep sleep 미해결 | s2idle + WFI, PHY 전원 차단 등 개선 / deep sleep 미해결 |

S10e DTS 주석대로 S10e와 S10은 같은 보드(터치, PMIC 레일, 버튼, MAX77705)이고 패널·배터리만 다릅니다.
KVM 서버에는 패널 드라이버가 필요 없으므로(S-Boot가 켜 둔 framebuffer로 콘솔 표시) 옮기기 쉽습니다.

## 바뀐 것

- `drivers/usb/typec/max77705.c`: `pd_host` (기본 on). PD 싱크 계약이 잡히고 폰이 아직 device 역할이면
  1.5초 뒤 폰이 **DR_Swap(호스트 요청)** 을 보냄, 거절되면 3초 간격 5회 재시도.
  끄기: 커널 인자 `max77705.pd_host=0` 또는 `/sys/module/max77705/parameters/pd_host`.
- `s10/uboot/exynos9820-beyond1lte.dts`: S10e DTS에서 모델, 1440x3040 framebuffer, 배터리(3300mAh),
  패널/DECON off, 오디오 off만 변경.
- `s10/uboot/exynos-mobile.env`: U-Boot는 **boot 파티션**, Ubuntu는 **userdata 파티션** 전체(ext4).
  `/boot/s10/Image`를 읽어 `root=PARTUUID=…`로 부팅. 볼륨 다운 = 메뉴(이전 커널, verbose, download mode).
- `s10/build-kernel.sh`: CH341/CP210x/FTDI/PL2303/ACM 내장, UVC·USB 오디오, USB 이더넷(r8152, ax88179).
- `s10/build-uboot.sh`, `s10/build-rootfs.sh`, `.github/workflows/s10-images.yml`: Odin용 파일 생성.

## USB 허브 + PD 패스스루가 되는 조건

1. 허브가 **USB PD 소스**여야 함(허브에 PD 충전기 연결). PD 없이 Type-C 전류만 주는 허브는
   계약이 없어 DR_Swap을 할 수 없고, 폰은 device로 남음 → 주변기기 안 보임.
2. 허브(또는 폰의 요청)로 **DR_Swap이 성사**되어야 함. 대부분의 PD 입력 허브는 노트북용이라 받아들이지만 보장은 없음.
3. 충전은 **최대 9V / 15W 고정 PDO**(PPS 차단, PR_Swap 차단). 폰이 허브에 전원을 주는 구성은 불가.
4. 기본 USB2(480Mbps). MJPEG 1080p 캡처카드(MS2109/MS2130 등)와 CH9329 계열은 USB2로 충분.
   USB3가 필요하면 `phy_exynos5_usbdrd.exynos9820_ss=1` (S10e에서도 기본 off).
5. HDMI 달린 허브가 DP alt mode로 레인을 가져가지 않도록 `max77705.dp_altmode=0`이 기본.

기기에서 확인:
```sh
dmesg | grep -iE 'max77705|dwc3|hub'      # "contract: 9000 mV", "PD source: asking for the host role"
cat /sys/class/typec/port0/{power_role,data_role,power_operation_mode}   # [sink] / [host] / usb_power_delivery
lsusb -t; ls /dev/ttyUSB* /dev/ttyACM* /dev/video*
cat /sys/class/power_supply/*/status
```

## 절전에 대해

KVM은 항상 깨어 있어야 하므로 **시스템 suspend는 쓰지 않습니다**(잠들면 HID/캡처가 끊김). 충전기에 늘 물려 있어
배터리 소모도 문제가 되지 않습니다. pulpoff 커널의 s2idle 개선(VTS pinctrl 제외, genpd 수정, WFI s2idle, USB PHY
LDO 차단)은 이미 들어 있습니다. SoC deep sleep(SYS_SLEEP)은 butztill·pulpoff·gangg111 중 S10/S10e에서 푼 곳이 없고,
실기기 없이 진척시킬 수 없어 이번 작업에서 제외했습니다. 배터리 보호가 목적이면 충전 상한(예: 85%)이 더 유효하며, 다음 단계 후보입니다.

## 부트로더 언락 (필수)

1. 설정 → 휴대전화 정보 → 소프트웨어 정보 → 빌드번호 7회 → 개발자 옵션 → **OEM 잠금 해제** 켬.
   (안 보이면 인터넷 연결 상태로 날짜 자동, 며칠 사용 후 다시 확인. KG/RMM 상태 때문에 숨겨질 수 있음)
2. 전원 끔 → **볼륨업+볼륨다운 누른 채 USB 연결** → 경고 화면에서 **볼륨업 길게** = 언락(전체 초기화).
3. 다시 OEM 잠금 해제 확인, download mode에서 `OEM LOCK: OFF (U)`, `KG STATE: Checking`(Prenormal 아님) 확인.
- **Knox 워런티 비트가 영구히 깨집니다**(삼성페이·보안폴더 영구 불가). 되돌릴 수 없음.
- 국내판(G973N)도 엑시노스라 OEM 언락 메뉴가 있는 모델이지만, 통신사 펌웨어에 따라 다를 수 있습니다.

## 플래시 (Odin, Windows)

릴리스에서 받기: `s10-uboot-boot.tar.md5`, `s10-ubuntu-userdata.tar.md5.xz`(→ `xz -d` 또는 7-Zip으로 풀기).

1. download mode 진입, Odin 3.14+ (BL/CP/CSC 비움, **Re-Partition 끔**).
2. **AP = `s10-uboot-boot.tar.md5`** → Start (boot 파티션에 U-Boot).
3. 다시 download mode → **AP = `s10-ubuntu-userdata.tar.md5`** → Start (**userdata 전체가 지워짐**).
4. 재부팅. 첫 부팅은 파일시스템 확장 + vendor에서 Wi-Fi 펌웨어 복사 후 **한 번 자동 재부팅**.

Linux: `heimdall flash --BOOT boot.img`, `heimdall flash --USERDATA userdata.img`.
efs(IMEI)·vendor·modem 파티션은 건드리지 않습니다. 부팅이 막히면 "AVB fail"이 뜨는 경우
vbmeta를 검증 해제 이미지로 바꿔야 할 수 있습니다(같은 계열 S10+ 포트는 필요 없었음).

### 원복
삼성 정식 펌웨어(G973N, 본인 CSC: SKC/KTC/LUC/KOO)를 BL/AP/CP/CSC 모두 넣고 Odin으로 플래시.
download mode는 언제든 **볼륨업+볼륨다운+USB**로 들어갈 수 있습니다(U-Boot 메뉴에도 있음).

## 첫 로그인

- 콘솔: 허브에 USB 키보드를 꽂으면 화면의 tty에서 로그인. 사용자 `ubuntu` / 암호 `ubuntu` → **즉시 변경 강제**.
- Wi-Fi: `sudo nmcli dev wifi connect <SSID> password <암호>` → 이후 ssh.
- 유선: 허브에 USB 이더넷(RTL8152/AX88179) → DHCP로 바로 ssh.
- KVM: 캡처 `ustreamer --device=/dev/video0 --host=0.0.0.0 --port=8080`, HID 젠더는 `/dev/ttyUSB0`(pyserial 설치됨).

## 빌드

```sh
# WSL (Ubuntu 24.04): clang/lld만 있으면 됨
sh s10/build-kernel.sh                       # out-s10/s10-kernel-<rel>.tar.gz
gh release create s10-v1 out-s10/s10-kernel-*.tar.gz
# 이미지(U-Boot + rootfs, sudo 필요) → GitHub Actions arm64 러너
gh workflow run s10-images.yml -f tag=s10-v1
```
로컬에서 직접: `CROSS_COMPILE=aarch64-linux-gnu- sh s10/build-uboot.sh`, `sudo sh s10/build-rootfs.sh out-s10/s10-kernel-*.tar.gz`.

## 아직 모르는 것 (실기기에서 확인 필요)

- S10 S-Boot가 이 U-Boot를 boot 파티션에서 그대로 실행하는지(같은 방식이 S10+에서는 동작).
- S10e에서 가져온 메모리 예약 영역이 8GB S10에서 충분한지(공통 dtsi의 S10 값 포함, 과잉 예약은 안전한 쪽).
- userdata의 GPT 이름 대소문자(env에서 `userdata`, `USERDATA` 둘 다 시도).
- 사용하는 허브가 DR_Swap을 받아들이는지.
