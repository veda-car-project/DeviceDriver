# Tankboy | Linux Tank Motor Device Driver

**C · Linux Kernel Module · Character Device · ioctl · GPIO · hrtimer · spinlock**

한화비전 VEDA 교육 과정의 **Tankboy RC Tank 팀 프로젝트**에서 제가 개발한 Raspberry Pi용 듀얼 모터 제어 드라이버입니다. 이 저장소에는 **모터 커널 드라이버 및 사용자 공간 테스트 앱**이 들어 있으며, 팀 프로젝트 전체의 Qt 원격 제어 화면·영상 스트리밍·네트워크 기능은 포함되지 않습니다.

> **구현 범위:** `/dev/tankmotor` Character Device Driver, `ioctl` 제어 인터페이스, `hrtimer` 기반 Software PWM, GPIO 모터 방향 제어, 테스트 앱 및 실물 하드웨어 연동. 팀의 다른 소프트웨어 기능을 이 저장소의 구현 성과로 표시하지 않습니다.

## 1. 동작 구조

```text
사용자 입력 (app.c)
   │
   ▼
open("/dev/tankmotor") → ioctl(TANKMOTOR_IOCTL_SET, { m1, m2 })
   │
   ▼
tank_ioctl() / copy_from_user()
   │
   ▼
motor_set_speed()
   ├─ 방향 GPIO (DIR) 설정
   └─ pwm_set_duty() → hrtimer 콜백 → PWM GPIO 출력
                                          │
                                          ▼
                              외부 모터 드라이버 → 모터 2개
```

- 커널 모듈은 `alloc_chrdev_region()` → `cdev_add()` → `class_create()` / `device_create()` 순서로 장치를 등록합니다.
- 사용자 프로그램은 `tankmotor_drv.h`에 정의된 `TANKMOTOR_IOCTL_SET`을 통해 두 모터의 명령을 한 번에 전달합니다.
- PWM 파형은 `hrtimer` 콜백이 GPIO를 High/Low로 전환하여 생성하며, 관련 공유 상태는 `spin_lock_irqsave()`로 보호합니다.
- 종료 시 타이머·Character Device·GPIO 자원을 정리합니다.

## 2. 실제 저장소 구성

```text
DeviceDriver/
├── tankmotor_drv.c    # 커널 모듈: GPIO, Software PWM, Character Device
├── tankmotor_drv.h    # ioctl 명령·모터 제어 데이터 구조체
├── app.c              # /dev/tankmotor를 사용하는 CLI 테스트 앱
├── Makefile           # Raspberry Pi 커널 모듈 빌드
├── app                # 과거에 빌드되어 커밋된 실행 파일
├── README.md
├── LICENSE
└── .gitignore
```

> 실제 저장소는 위와 같이 **최상위 디렉터리에 소스가 배치**되어 있습니다. 별도 `driver/`, `test_app/`, `scripts/` 디렉터리는 없습니다. 포함된 바이너리 `app`은 재사용하기보다 대상 장치에서 `app.c`를 다시 빌드하세요.

## 3. 드라이버 인터페이스

### ioctl 명령

```c
struct tankmotor_set {
    int m1;   // Motor A: -100 ~ 100
    int m2;   // Motor B: -100 ~ 100
};

#define TANKMOTOR_IOCTL_SET _IOW(TANKMOTOR_IOC_MAGIC, 0x01, struct tankmotor_set)
```

- 입력이 `0`이면 정지, 부호는 회전 방향, 절댓값은 PWM 듀티비를 결정합니다.
- 각 값은 드라이버에서 `-100 ~ 100`으로 제한합니다.
- 유효하지 않은 `ioctl` 명령에는 `-ENOTTY`, 사용자 메모리 복사 실패에는 `-EFAULT`를 반환합니다.

### 프로젝트 GPIO 매핑

| 신호 | BCM 핀(프로젝트 주석 기준) | 코드 기본 Linux GPIO 번호 |
| --- | ---: | ---: |
| Motor A PWM (`a_pwm`) | 12 | 524 |
| Motor A DIR (`a_dir`) | 5 | 517 |
| Motor B PWM (`b_pwm`) | 13 | 525 |
| Motor B DIR (`b_dir`) | 6 | 518 |

**주의:** BCM 번호와 Linux GPIO 번호는 다릅니다. 위 Linux GPIO 값은 당시 프로젝트 보드 환경에서 사용한 설정이며, 커널·보드 환경이 달라지면 그대로 적용되지 않을 수 있습니다. 장치의 실제 GPIO 매핑과 배선을 먼저 확인해야 합니다.

### 모듈 매개변수

| 매개변수 | 기본값 | 설명 |
| --- | ---: | --- |
| `a_pwm`, `b_pwm` | `524`, `525` | PWM용 출력 GPIO |
| `a_dir`, `b_dir` | `517`, `518` | 방향 제어 GPIO |
| `a_dir_inv`, `b_dir_inv` | `0`, `0` | 모터 방향 반전 (`0`/`1`) |
| `pwm_hz` | `1000` | Software PWM 초기 주파수(Hz) |

코드에는 `module_param()`이 정의되어 있습니다. 아래 예제에서는 **모듈을 로드할 때** 파라미터를 지정합니다. 런타임에서 파라미터를 변경해도 타이머 주기나 이미 요청한 GPIO 구성이 자동으로 재설정되는 것은 아닙니다.

## 4. Raspberry Pi에서 빌드 및 테스트

이 문서의 명령은 현재 저장소의 `Makefile` 및 `app.c`를 기준으로 작성한 **재현 절차**입니다. 이 문서 작성 과정에서 실제 Raspberry Pi 하드웨어 재실행·재빌드를 수행한 것은 아닙니다.

### 4.1 환경 확인

프로젝트 개발 환경: **Raspberry Pi (aarch64) / 64-bit Raspberry Pi OS**.

```bash
uname -m
uname -r
ls -ld /lib/modules/$(uname -r)/build
```

Raspberry Pi OS에서 의존 도구를 설치하는 예시:

```bash
sudo apt update
sudo apt install -y build-essential make raspberrypi-kernel-headers
```

설치 패키지와 커널 헤더 제공 방식은 OS·커널 버전에 따라 다릅니다. **실행 중인 커널과 빌드 헤더가 일치**해야 합니다.

### 4.2 커널 모듈 빌드

저장소 최상위 디렉터리에서 실행합니다.

```bash
make
ls -l tankmotor_drv.ko
```

`Makefile`은 `/lib/modules/$(uname -r)/build`를 사용하므로 **현재 실행 중인 커널 대상의 로컬 빌드**가 기본입니다. 파일에 남아 있는 ARM64 크로스컴파일 명령은 주석 처리되어 있어, 현재 `make` 명령만으로 크로스컴파일이 자동 구성되지는 않습니다.

### 4.3 모듈 로드

GPIO 배선·외부 모터 전원·핀 점유 상태를 확인한 다음:

```bash
sudo insmod tankmotor_drv.ko
ls -l /dev/tankmotor
lsmod | grep tankmotor
sudo dmesg | tail -n 30
```

모듈 로드 시 GPIO 번호와 PWM 초기 주파수를 지정할 수도 있습니다.

```bash
sudo insmod tankmotor_drv.ko a_pwm=524 a_dir=517 b_pwm=525 b_dir=518 pwm_hz=1000
```

> 두 `insmod` 예시는 **각각 독립적인 로드 방법**입니다. 한 번 로드한 모듈을 중복 로드하면 안 됩니다. `/dev/tankmotor` 노드는 `device_create()`와 장치 노드 관리 환경에 의해 생성됩니다.

### 4.4 테스트 앱 실행

```bash
gcc -O2 -Wall -o app app.c
sudo ./app
```

테스트 앱은 별도 서버 없이 `/dev/tankmotor`를 직접 열어 `ioctl`을 전송합니다.

| 입력 | 동작 (`app.c` 기준) |
| --- | --- |
| `u` / `d` | Motor A 값을 10씩 증가 / 감소 |
| `j` / `k` | Motor B 값을 10씩 증가 / 감소 |
| `l` / `r` | Motor B 값을 각각 `+40` / `-40`으로 설정 |
| `c` | Motor B 값 `0`으로 설정 |
| `set 30 -30` | Motor A `30`, Motor B `-30` 입력 |
| `s` | 두 모터 정지 |
| `q` | 앱 종료 전 정지 명령 전송 |

> 실물 모터 테스트는 안전한 환경에서 낮은 속도부터 진행하고, 모터 전원은 사양에 맞는 외부 전원을 사용하세요. GPIO에 모터를 직접 연결하지 마세요.

### 4.5 모듈 종료 및 빌드 파일 정리

테스트 앱을 종료한 다음:

```bash
sudo rmmod tankmotor_drv
make clean
```

## 5. 주요 확인 포인트 및 문제 해결

| 현상 | 확인할 내용 |
| --- | --- |
| `make` 실패 / 커널 헤더 없음 | `uname -r`과 `/lib/modules/$(uname -r)/build` 확인 |
| `insmod` 실패 | 커널 빌드 버전·아키텍처·`dmesg` 로그 확인 |
| `gpio_request` 실패 | GPIO 번호, 핀 점유 상태 및 보드 매핑 확인 |
| `/dev/tankmotor`가 없음 | 모듈 로드 성공 여부·`device_create()` 로그·장치 노드 관리 환경 확인 |
| `open(/dev/tankmotor)` 실패 | 장치 생성 여부 및 접근 권한 확인 |
| 모터가 움직이지 않거나 방향이 반대 | 모터 전원·결선·GPIO 번호·방향 반전 파라미터 확인 |

프로젝트 과정에서는 커널 모듈 및 모터 구동 관련 오류를 디버깅했습니다. 구체적인 변경 이력은 저장소의 [커밋 기록](../../commits/main)을 확인할 수 있습니다.

## 6. 저장소 범위 및 참고사항

- 본 저장소는 **Tankboy의 Linux 모터 디바이스 드라이버와 독립 테스트 앱**만 포함합니다. 전체 RC Tank 시스템의 네트워크·영상·UI 기능은 다른 구성 요소입니다.
- 프로젝트 통합 시 자동 실행을 위한 systemd 구성도 사용했지만, **현재 이 저장소 자체에는 서비스 파일이나 자동 로드 스크립트가 없습니다**.
- 소스의 GPIO 인터페이스 및 번호 매핑은 개발 당시 환경을 전제로 합니다. 다른 커널 버전·보드에서의 호환성을 보장하지 않습니다.
- 라이선스는 [LICENSE](LICENSE) 파일을 참고하세요.

## 작성

**Jeonghan Lee (이정한)** — Tankboy Motor Device Driver 개발 및 하드웨어 연동
