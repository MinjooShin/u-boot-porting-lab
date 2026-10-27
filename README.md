# u-boot-porting-lab

공식 U-Boot 소스를 기반으로 MJ 보드의 부팅 환경을 포팅하는 프로젝트입니다.
현재 구성은 **BL2 → TF-A(BL31) → U-Boot → SD 카드의 Linux FIT 이미지** 순서로 부팅합니다.

사용 순서는 **빌드 → BL2 헤더 추가 → QSPI에 U-Boot 퓨징 → 부팅 환경 변수 확인·설정 → SD 부팅**입니다.
아래 Linux 명령은 같은 터미널에서 순서대로 실행합니다. `setenv`, `mmc`, `bootm` 명령은 보드의 U-Boot 콘솔에서 실행합니다.

## 1. U-Boot 빌드

AArch64 크로스 컴파일러와 호스트 빌드 도구가 필요합니다. GCC는 10 이상을 사용하며,
아래 예시는 현재 사용 중인 `aarch64-linux-gnu-gcc-10` 기준입니다.
빌드 중 `swig`를 찾을 수 없다는 오류가 나오면 호스트에 SWIG를 설치합니다.

```bash
project_dir="$HOME/project/u-boot-porting-lab"
cd "$project_dir/u-boot"

# 최초 빌드 또는 기본 설정을 다시 적용할 때 실행
make CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc-10 mj_defconfig

# 소스를 수정한 뒤에는 이 빌드 명령부터 실행
make CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc-10 -j"$(nproc)"
```

`mj_defconfig`를 다시 적용하면 현재 `.config`가 기본 설정으로 갱신됩니다.
빌드가 성공하면 `u-boot/u-boot.bin`이 생성됩니다.

| 파일 | 용도 |
| --- | --- |
| `u-boot/u-boot.bin` | U-Boot 본체와 제어용 DTB가 포함된 바이너리. 다음 단계의 헤더 생성 입력 |
| `u-boot/u-boot-nodtb.bin` | 제어용 DTB가 없는 중간 산출물 |
| `u-boot/u-boot-qspi-*.bin` | 다음 단계에서 생성하는 BL2 헤더 포함 이미지. QSPI에 기록할 파일 |
| `image.itb` | 기존 SDK에서 생성한 Linux 커널·Linux DTB·RAM rootfs의 FIT 이미지. SD 카드에 배치 |

이 프로젝트의 빌드는 U-Boot를 생성합니다. Linux 부팅에는 별도로 준비한 `image.itb`를 사용합니다.

## 2. BL2 헤더가 포함된 이미지 생성

이 보드의 BL2는 U-Boot 이미지 앞에 **64바이트 헤더**가 있다고 가정합니다.
따라서 빌드한 `u-boot.bin`을 [`scripts/make_bl2_image.py`](scripts/make_bl2_image.py)로 감싸서 퓨징할 파일을 만듭니다.

```text
QSPI에 기록할 이미지
┌────────────────────────────┬──────────────────────────┬─────────────┐
│ BL2 헤더: 64바이트          │ u-boot.bin: U-Boot + DTB  │ 0 패딩      │
│ 길이·체크섬·기존 부팅 설정  │                          │ 8바이트 정렬│
└────────────────────────────┴──────────────────────────┴─────────────┘
```

헤더 템플릿은 **이 보드에서 정상 부팅했던, BL2 헤더가 포함된 기존 U-Boot 이미지**입니다.
아래 `header_template` 값을 해당 파일의 실제 경로로 바꿉니다.
원본 이미지는 프로젝트 밖에 보관합니다.

```bash
cd "$project_dir"

header_template="/path/to/original-uboot-image.bin"
qspi_image="$project_dir/u-boot/u-boot-qspi-$(date +%Y%m%d-%H%M%S).bin"

python3 scripts/make_bl2_image.py \
    --template "$header_template" \
    --payload u-boot/u-boot.bin \
    --output "$qspi_image" \
    --max-size 0x80000
```

| 인자 | 의미 |
| --- | --- |
| `--template` | 기존 정상 U-Boot 이미지. 헤더와 본문의 체크섬을 검증한 뒤 부팅 설정을 가져옴 |
| `--payload` | 새로 빌드한 `u-boot.bin` |
| `--output` | 새로 생성할 파일 경로. 기존 파일을 덮어쓰지 않으므로 예시에서는 시각을 파일명에 추가 |
| `--max-size` | 헤더와 패딩을 포함한 출력 크기 제한. 예시는 보수적으로 512 KiB를 적용 |
| `--template-offset` | 템플릿이 전체 QSPI 덤프일 때 헤더가 시작하는 위치. 개별 이미지에서는 기본값 `0` 사용 |

스크립트는 기존 헤더의 부팅 설정을 보존하고, 새 본문을 8바이트 경계에 맞춘 뒤
본문 길이와 **리틀엔디언 32비트 워드의 합으로 계산한 체크섬**을 갱신합니다.
현재는 일반 부팅(normal boot) 형식만 지원합니다.

성공하면 `Output`, 본문·전체 크기, `Checksum`, `SHA256`이 출력됩니다.
`--max-size`는 BL2의 실제 슬롯 크기를 자동으로 알아내는 값이 아닙니다.
출력이 제한을 초과하면 현재 BL2와 QSPI 배치를 확인한 뒤 제한을 결정합니다.
스크립트는 파일 생성만 수행하며 QSPI에는 기록하지 않습니다.

## 3. 보드의 QSPI에 U-Boot 퓨징

헤더 생성이 성공한 뒤, 기존에 사용하던 `qspi-jig`로 **방금 생성한 `$qspi_image`**를 기록합니다.
다음은 현재 보드에서 사용한 U-Boot 시작 오프셋 **`0x851000`** 기준입니다.

보드를 기존 QSPI 기록 절차에 맞게 연결하고, Linux 호스트에서 실행합니다.

```bash
jig_dir="$HOME/project/jig"
cd "$jig_dir"

sudo ./qspi-jig write-single 0x851000 "$qspi_image"
```

이 명령은 QSPI의 기존 U-Boot 영역을 새 이미지로 교체합니다.
여기서 사용하는 `0x851000`은 **QSPI 저장 위치**이고,
`CONFIG_TEXT_BASE=0x80000000`은 BL2가 U-Boot를 올려 실행하는 **DDR 주소**입니다.

다른 터미널에서 퓨징한다면 `qspi_image`에 2단계에서 출력된 `Output`의 전체 경로를 다시 지정합니다.
기록이 완료되면 보드를 재시작하고 시리얼 콘솔(115200 baud)에서 새 U-Boot 배너를 확인합니다.

## 4. SD 카드와 파일 확인

현재 사용 중인 SD 카드는 MBR 파티션 구성이며 두 파티션 모두 FAT32입니다.

| 파티션 | 라벨 | 내용 |
| --- | --- | --- |
| 1 | `boot` | 루트 디렉터리에 `image.itb` 배치 |
| 2 | `user_data` | Linux에서 마운트할 앱·모듈 등 사용자 파일 |

```text
boot 파티션 루트/
└── image.itb

user_data 파티션 루트/
└── user_data/
    ├── start-app.sh
    ├── applications/
    └── modules/
```

기존 RAM rootfs의 시작 스크립트는 `user_data` 파티션을 찾아 `/mnt_sd`에 마운트하고
`/mnt_sd/user_data/start-app.sh`를 실행합니다. 이 동작은 Linux 이미지에 포함된 스크립트가 담당합니다.
U-Boot에서는 1번 파티션의 `image.itb`를 읽습니다.

보드에 SD 카드를 넣고 부팅한 뒤 `Hit any key to stop autoboot`에서 키를 눌러 자동 부팅을 멈춥니다.
U-Boot 프롬프트에서 아래 명령으로 카드와 파일을 확인합니다.

```text
mmc dev 0
mmc rescan
mmc info
mmc part
fatls mmc 0:1 /
```

`0:1`은 **U-Boot의 MMC 장치 0, 파티션 1**입니다.
`fatls` 결과에 `image.itb`가 보여야 합니다. FAT32 파티션에는 `fatls`와 `fatload`를 사용합니다.
Linux의 `/dev/mmcblk*` 번호는 Linux DT와 드라이버에 따라 달라질 수 있습니다.

## 5. U-Boot 환경 변수 설정과 SD 부팅

현재 빌드는 아래 부팅 설정을 기본값으로 포함합니다.
처음에는 `printenv`로 확인하고, 직접 설정하거나 기존 세션의 값을 바꿀 때는 다음 명령을 입력합니다.
모두 **보드의 U-Boot 프롬프트**에서 실행합니다.

```text
setenv mmcdev 0
setenv mmcpart 1
setenv fit_addr_r 0x90000000
setenv bootfile image.itb
setenv bootargs 'console=ttyS3,115200n8 earlycon loglevel=8 cma=512M rootwait'
setenv bootsd 'mmc dev ${mmcdev} && mmc rescan && fatload mmc ${mmcdev}:${mmcpart} ${fit_addr_r} ${bootfile} && bootm ${fit_addr_r}'
setenv bootcmd 'run bootsd'

printenv bootcmd bootsd bootargs mmcdev mmcpart fit_addr_r bootfile
run bootcmd
```

`bootsd`를 설정할 때 작은따옴표를 포함해서 입력합니다.
`${mmcdev}` 등의 변수는 저장한 명령을 실행할 때 확장됩니다.
각 단계가 `&&`로 연결되어 있어 SD 선택·스캔·파일 로드가 성공했을 때 `bootm`이 실행됩니다.

| 환경 변수 | 역할 |
| --- | --- |
| `bootcmd` | 자동 부팅 때 실행할 명령. 현재는 `run bootsd` |
| `bootsd` | SD 선택 → 카드 스캔 → FIT 로드 → Linux 부팅 |
| `mmcdev` / `mmcpart` | `image.itb`가 있는 장치와 파티션: `0` / `1` |
| `fit_addr_r` | SD에서 읽은 FIT를 임시로 보관할 DDR 주소: `0x90000000` |
| `bootfile` | 읽어올 파일 이름: `image.itb` |
| `bootargs` | Linux 커널에 전달할 콘솔·CMA 등의 부팅 인자 |

현재 FIT의 커널 로드·진입 주소는 `0x88080000`입니다.
FIT를 보관하는 `0x90000000`은 커널 압축 해제 영역과 겹치지 않도록 정한 값입니다.
메모리는 DDR1 1 GiB(`0x40000000`부터)와 DDR0 2 GiB(`0x80000000`부터), 총 3 GiB 구성입니다.

`image.itb`에 RAM rootfs가 포함되어 있으므로 `user_data` 파티션을 `root=/dev/mmcblk...`로 지정하지 않습니다.
`console=ttyS3`는 기존 Linux DT에 맞춘 값입니다.

환경 변수와 별개로 로드·부팅만 확인하려면 다음 한 줄을 사용할 수 있습니다.
이때도 Linux에 전달할 `bootargs`는 위와 같이 설정되어 있어야 합니다.

```text
fatload mmc 0:1 0x90000000 image.itb && bootm 0x90000000
```

### 재부팅 후에도 적용할 기본값 변경

현재 환경 변수 저장 방식은 **`CONFIG_ENV_IS_NOWHERE=y`**입니다.
`setenv` 변경은 현재 실행 중인 세션에만 적용되며, `saveenv`로 영구 저장할 수 없습니다.
재부팅하면 빌드에 포함된 기본 환경 변수로 돌아갑니다.

기본값을 바꾸려면 다음 파일을 수정하고 **빌드 → 헤더 생성 → QSPI 퓨징**을 다시 수행합니다.

| 파일 | 수정할 내용 |
| --- | --- |
| [`u-boot/board/mj/mj.env`](u-boot/board/mj/mj.env) | `bootsd`, `bootargs`, 파일 이름, 장치·파티션 번호, FIT 로드 주소 |
| [`u-boot/configs/mj_defconfig`](u-boot/configs/mj_defconfig) | `CONFIG_BOOTCOMMAND="run bootsd"` 등 빌드 설정 |

`mj_defconfig`를 수정했다면 1단계의 `make ... mj_defconfig`부터 다시 실행합니다.
현재 기본값으로 빌드·퓨징한 보드는 정상적인 SD 카드가 꽂혀 있으면
매번 `setenv`를 입력하지 않아도 카운트다운 뒤 `bootcmd`를 실행합니다.

## 자주 확인할 사항

| 증상 | 확인할 내용 |
| --- | --- |
| `Can't set block device` 또는 파일 로드 실패 | `mmc info`, `mmc part`, `fatls mmc 0:1 /`로 카드·파티션·파일 확인. 현재 FAT32 구성에는 `fatload` 사용 |
| `image.itb`를 찾지 못함 | 파일이 1번 파티션의 루트에 있는지, `bootfile` 값과 파일 이름이 같은지 확인 |
| 헤더 생성 시 출력 파일이 이미 존재한다는 오류 | 새 출력 파일 이름 지정. 스크립트는 기존 파일을 덮어쓰지 않음 |
| 재부팅 후 `setenv` 값이 사라짐 | 현재 저장 방식의 정상 동작. `mj.env`의 기본값을 수정하고 다시 빌드·퓨징 |
| `No ethernet found.` | 현재 네트워크 드라이버를 구성하지 않아 출력됨. SD 부팅은 MMC 경로로 진행 |
