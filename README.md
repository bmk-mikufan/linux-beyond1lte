# 갤럭시 S10 (SM-G973N/F) 메인라인 리눅스 — Ubuntu 24.04 USB KVM

갤럭시 S10(엑시노스 9820, `beyond1lte`)에서 **메인라인 리눅스 7.2.13**과 **Ubuntu 24.04**를 돌리는 포트입니다.
목표는 폰을 **유전원 PD 허브**에 꽂아 두고, 허브에 단 **USB 시리얼→HID 젠더**와 **캡처카드**로 KVM을 하면서
**PD 패스스루로 충전**하는 것. 화면에는 Ubuntu Desktop(터치 + 화상 키보드)이 떠서 폰에서 바로 관리할 수 있습니다.

[pulpoff/linux-beyond0lte](https://github.com/pulpoff/linux-beyond0lte)(S10e)를 S10용으로 옮긴 것이고,
S10 패널 명령은 [butztill/s10-linux](https://github.com/butztill/s10-linux)에서 가져왔습니다.
원래 S10e 설명은 [README.s10e.md](README.s10e.md)에 있습니다.

> ⚠️ **S10 실기기에서 아직 부팅해 보지 않았습니다.** S10e에서 검증된 커널과 S10용 DTS·패널 드라이버를
> 정적으로 대조해 만들었고, 아래 "펌웨어 대조" 항목은 SKT 순정 펌웨어로 확인했습니다.
> 처음 플래시하기 전에 복구 방법(아래)을 먼저 준비하세요.

## 다운로드

[릴리스](https://github.com/bmk-mikufan/linux-beyond1lte/releases)에서 받습니다.

| 파일 | Odin 슬롯 | 내용 |
|---|---|---|
| `s10-uboot-boot.tar.md5` | AP (1회차) | U-Boot → boot, 검증 해제 vbmeta → vbmeta |
| `s10-ubuntu-desktop-userdata.tar.md5.xz` | 압축 풀고 AP (2회차) | **Ubuntu Desktop** (GNOME, 화상 키보드) — 기본 |
| `s10-ubuntu-lomiri-userdata.tar.md5.xz` | 〃 | Ubuntu Touch 셸(Lomiri) — 실험적 옵션 |
| `s10-ubuntu-server-userdata.tar.md5.xz` | 〃 | 화면 UI 없는 서버 |
| `s10-kernel-<버전>.tar.gz` | – | 커널 Image + 모듈 (업데이트용) |

userdata 이미지는 셋 중 **하나만** 고르면 됩니다. 모두 ssh, NetworkManager, ustreamer, pyserial이 들어 있습니다.

## 무엇이 되나 (예상)

| 항목 | 상태 | 비고 |
|---|---|---|
| 부팅 (S-Boot → U-Boot → 리눅스) | 미검증 | 같은 방식이 S10+에서 동작 |
| 화면 | DECON KMS + S6E3HA9 패널 드라이버 | 밝기, 화면 끄기/켜기(번인 방지) |
| 터치 | S6SY761 | S10e와 같은 칩·버스 |
| GPU | panfrost (Mali-G76) | G3D 전원 도메인이 켜져 있을 때만 로드, 아니면 소프트웨어 렌더링 |
| USB 호스트 + 허브 | ✅ 코드상 | 9820 dwc3 허브 버그(GFLADJ) 수정 포함 |
| **USB 3** | 켜짐 | S10e에서 독으로 검증된 경로 |
| **DP alt mode** (USB-C → HDMI) | 켜짐, 2레인 | 1080p60, USB 3와 동시 사용 |
| PD 충전 | 최대 9V / 15W | PPS·PR_Swap 차단 |
| Wi-Fi | BCM4375 | 펌웨어는 첫 부팅 때 폰의 vendor 파티션에서 복사 |
| 시스템 절전 | 꺼 둠 | KVM은 항상 깨어 있어야 함 |
| LTE·통화·오디오·카메라 | 범위 밖 | |

## USB 허브 + PD 패스스루 조건

1. 허브가 **USB PD 소스**여야 합니다(허브에 PD 충전기 연결). PD 없이 전류만 주는 허브에서는 충전만 되고 기기가 안 보입니다.
2. 이 포트는 PD 계약 1.5초 뒤 **폰이 먼저 DR_Swap(호스트 요청)** 을 보냅니다(`max77705.pd_host`, 기본 켜짐).
   허브가 거절하면 3초 간격으로 5번 재시도합니다. 끄기: 커널 인자 `max77705.pd_host=0`.
3. HDMI가 달린 허브는 DP를 **2레인(pin D)** 으로 잡아서 USB 3 레인을 남깁니다.
   4K60이 필요하면 `max77705.dp_four_lanes=1`(그 대신 USB 2로 떨어짐).
4. 시리얼→HID 젠더(CH9329 등 CH340/CP2102 기반), FTDI, PL2303, CDC-ACM 드라이버는 커널에 내장.
   UVC 캡처카드, USB 오디오, USB 이더넷(RTL8152/AX88179)은 모듈.

기기에서 확인:
```sh
dmesg | grep -iE 'max77705|dwc3|hub'   # "contract: 9000 mV", "PD source: asking for the host role"
cat /sys/class/typec/port0/{power_role,data_role,power_operation_mode}   # [sink] [host] usb_power_delivery
lsusb -t; ls /dev/ttyUSB* /dev/ttyACM* /dev/video*
```

## 순정 펌웨어 대조 (SM-G973N SKC G973NKSU7HXJ2)

SamMobile 기준 MD5(`423dcc6bc787704e814d7ff9d4a3235e`)와 일치하는 SKT 순정 펌웨어를 열어서 코드의 가정과 비교했습니다.

| 가정 | 순정 펌웨어에서 확인한 값 | 결과 |
|---|---|---|
| boot.img 헤더 (v1, page 2048, kernel 0x10008000, tags 0x10000100, board `SRPRI28D007KU`, 12.0.0/2023-04) | 같음 (10개 필드 전부 일치) | ✅ |
| `SEANDROIDENFORCE`가 커널 바로 뒤 | 같음 (순정은 그 뒤에 삼성 서명, 언락 상태에서는 불필요) | ✅ |
| vbmeta | 순정은 flags 0(검증 켬)이고 **boot를 해시로 검증** → U-Boot boot.img는 불통과 | ⚠️ → 검증 해제 vbmeta(flags 2, 삼성 데이터 없음)를 boot와 같은 tar에 넣음 |
| U-Boot가 bootloader 문자열 앞 4글자 `G973`으로 S10 인식 | 펌웨어 `G973NKSU7HXJ2`, PIT `BEYOND1LTE_KOR_SINGLE` | ✅ |
| 파티션 이름 `userdata`, `vendor`, `boot` (ext4 vendor) | GPT 소문자(TWRP fstab), Odin PIT는 대문자 | ✅ (둘 다 시도) |
| Wi-Fi/BT 펌웨어 파일 (`bcmdhd_sta.bin_b1`, `bcmdhd_clm.blob`, `nvram.txt_CS01_semco_b1`, `bcm4375B1_semco.hcd`) | vendor 파티션에 모두 있음 | ✅ |
| 패널 S6E3HA9 "beyond1 A3 S0" | 순정 커널에 `s6e3ha9`, `beyond1_a3_s0` 다수 | ✅ |
| 패널 WQHD+ 명령 (scaler `ba 01 26 08 08 f3`, CASET 0x59f, PASET 0xbdf, AOR 표) | 순정 커널 표와 같음 | ✅ |
| 메모리 예약 영역 | S-Boot DT의 고정 영역(디버그 로그 15곳, sec_rdx 등)을 모두 덮음 | ✅ |

Wi-Fi가 안 되면 NVRAM이 다른 모듈용일 수 있습니다:
`sudo ln -sf nvram.txt_1rh_es43_b1 /usr/lib/firmware/brcm/brcmfmac4375b1-pcie.txt` 후 재부팅.

## 부트로더 언락 (필수)

1. 설정 → 휴대전화 정보 → 소프트웨어 정보 → 빌드번호 7번 탭 → 개발자 옵션 → **OEM 잠금 해제** 켜기.
   (메뉴가 없으면 인터넷 연결, 날짜 자동 상태로 며칠 쓴 뒤 다시 확인. KG/RMM 상태 때문에 숨겨질 수 있음)
2. 전원 끄기 → **볼륨업 + 볼륨다운을 누른 채 USB 연결** → 경고 화면에서 **볼륨업 길게** = 언락(전체 초기화).
3. 다시 켜서 OEM 잠금 해제 확인, download mode에서 `OEM LOCK: OFF (U)`, `KG STATE: Checking`(Prenormal 아님) 확인.

- **Knox 워런티 비트가 영구히 깨집니다**(삼성페이·보안폴더 영구 불가). 되돌릴 수 없습니다.

## 플래시 (Windows, Odin 3.14 이상)

1. 릴리스에서 `s10-uboot-boot.tar.md5`와, 고른 userdata 파일(`.xz`)을 받고 7-Zip 등으로 `.xz`를 풉니다.
2. download mode 진입, Odin에서 **BL/CP/CSC는 비우고 Re-Partition은 끕니다**.
3. **AP = `s10-uboot-boot.tar.md5`** → Start. (boot.img와 vbmeta.img가 함께 들어갑니다. 순정 vbmeta가
   boot를 해시로 검증하기 때문에, 검증을 끈 vbmeta 없이는 부팅이 막힐 수 있습니다.)
4. 다시 download mode → **AP = `s10-ubuntu-<변형>-userdata.tar.md5`** → Start (**Android 데이터 전부 삭제**).
5. 재부팅. 첫 부팅은 파일시스템 확장 + Wi-Fi 펌웨어 복사 후 **한 번 자동 재부팅**합니다.

Linux에서는 `heimdall flash --BOOT boot.img`, `heimdall flash --USERDATA userdata.img`.
efs(IMEI)·vendor·modem 파티션은 건드리지 않습니다.

### 원복
삼성 정식 펌웨어(SM-G973N, 본인 CSC: SKC/KTC/LUC/KOO)를 BL/AP/CP/CSC에 모두 넣고 Odin으로 플래시합니다.
download mode는 언제든 **볼륨업 + 볼륨다운 + USB 연결**로 들어갈 수 있고, U-Boot 메뉴(볼륨다운)에도 있습니다.

## 첫 로그인

- 사용자 `ubuntu` / 암호 `ubuntu` → **로그인하면 바로 암호를 바꾸라고 나옵니다**.
- Desktop: GDM 로그인 화면에서 터치로 입력(화상 키보드 켜짐), 화면 배율 300%. Wi-Fi는 설정 앱에서.
- Server: 허브에 USB 키보드를 꽂아 콘솔에서 로그인 → `sudo nmcli dev wifi connect <SSID> password <암호>`.
- 유선: 허브에 USB 이더넷을 꽂으면 DHCP로 바로 ssh.
- KVM: 캡처 `ustreamer --device=/dev/video0 --host=0.0.0.0 --port=8080`, HID 젠더는 `/dev/ttyUSB0`.

U-Boot 메뉴(부팅 중 볼륨다운): Ubuntu / 이전 커널 / 자세한 로그 / download mode / 콘솔 / 전원 끄기.
커널 인자는 `/boot/s10/bootargs.env`(`s10_args=...` 한 줄)로 다시 플래시하지 않고 바꿀 수 있습니다.

## 바뀐 것 (pulpoff S10e 트리 대비)

- `drivers/usb/typec/max77705.c`: `pd_host` — PD 싱크인데 device 역할이면 폰이 호스트 역할을 요청.
- `drivers/gpu/drm/panel/panel-samsung-s6e3ha9-beyond1.c`: S10 패널(인수인계형, DCS만, AOR 밝기).
- `drivers/gpu/drm/tiny/exynos9820-decon-kms.c`: 해상도를 하드코딩(1080x2280) 대신 S-Boot 레지스터에서 읽음.
- `s10/uboot/exynos9820-beyond1lte.dts`: S10e DTS 기반, 모델·1440x3040·배터리 3300mAh·패널·오디오 off.
- `s10/uboot/exynos-mobile.env`: U-Boot는 boot 파티션, Ubuntu는 userdata 전체(ext4), `root=PARTUUID`.
- `s10/build-kernel.sh`, `s10/build-uboot.sh`, `s10/mkbootimg-v1.py`, `s10/build-rootfs.sh`,
  `.github/workflows/s10-images.yml`: 빌드와 Odin 패키징.

## 빌드

```sh
# 커널: WSL/리눅스, clang + lld
sh s10/build-kernel.sh                         # out-s10/s10-kernel-<rel>.tar.gz
gh release create <태그> out-s10/s10-kernel-*.tar.gz
# U-Boot + rootfs(sudo 필요) → GitHub Actions arm64 러너
gh workflow run s10-images.yml -f tag=<태그> -f variants='["desktop","lomiri","server"]'
```
로컬: `CROSS_COMPILE=aarch64-linux-gnu- sh s10/build-uboot.sh`, `sudo DE=desktop sh s10/build-rootfs.sh out-s10/s10-kernel-*.tar.gz`.

## 아직 모르는 것

- S10 S-Boot가 boot 파티션의 U-Boot를 그대로 실행하는지(S10+에서는 동작).
- 새 S6E3HA9 패널 드라이버와 DECON KMS의 S10 첫 인수인계(명령 바이트는 순정과 일치).
- 쓰시는 허브가 DR_Swap을 받아들이는지.
- Lomiri(Mir)가 이 GPU/디스플레이 조합에서 뜨는지 — 실험적.

## 라이선스

커널은 GPL-2.0(각 파일의 고지가 우선). 펌웨어는 포함하지 않으며 폰의 vendor 파티션에서 복사합니다.
