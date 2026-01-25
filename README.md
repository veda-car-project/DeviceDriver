Tank Motor Device Driver 개발환경 구축 가이드

이 문서는 Tank Motor 커널 드라이버 개발 및 테스트를 위한 전체 개발환경 구축 방법을 처음부터 끝까지 정리한 README입니다.
라즈베리파이에서 커널 모듈(.ko) 빌드 → 로드 → 테스트 앱 실행 → 부팅 시 자동 로드까지의 전 과정을 포함합니다.
프로젝트 보드에서는 systemd를 이용해 모듈이 자동 적재되게 처리했지만, 그 외의 보드를 위한 README 입니다.


---

1. 개발 환경 개요

1.1 구성 요약

타겟 보드: Raspberry Pi (64bit, aarch64)

OS: Raspberry Pi OS (64-bit)

커널 드라이버: tankmotor_drv.ko

제어 방식:

PWM 제어: PWMA, PWMB

방향 제어: A_DIR, B_DIR (GPIO)


테스트 앱: 사용자 공간 테스트 프로그램 (char device 기반)



---

2. 라즈베리파이 기본 환경 준비

2.1 OS 확인

uname -a

예시:

Linux raspberrypi 6.12.62+rpt-rpi-v8 aarch64

> 커널 버전이 변경되면 반드시 커널 헤더 재설치 + 모듈 재빌드 필요




---

2.2 필수 패키지 설치

sudo apt update
sudo apt install -y \
    build-essential \
    raspberrypi-kernel-headers \
    git \
    make

설치 확인:

ls /lib/modules/$(uname -r)/build


---

3. 프로젝트 디렉토리 구조

DeviceDriver/
├── driver/
│ ├── tankmotor_drv.c
│ ├── tankmotor_drv.h
│ └── Makefile
├── test_app/
│ ├── tankmotor_test.c
│ └── Makefile
├── scripts/
│ └── tankmotor.sh
└── README.md


---

4. GPIO / PWM 핀 설정

4.1 사용 핀 정의

기능 핀 이름 GPIO 번호

모터 A PWM PWMA GPIO 12
모터 B PWM PWMB GPIO 13
모터 A 방향 A_DIR GPIO 5
모터 B 방향 B_DIR GPIO 6


---

12. 참고

Linux Device Driver (LDD)

Raspberry Pi GPIO / PWM Documentation

systemd 서비스 매뉴얼



---

13. 작성자

Jeong Han Lee

Tank Motor Device Driver Project
