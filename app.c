// app.c - simple test app for /dev/tankmotor (PWM+DIR version)
// no server needed
// 빌드 시 명령어
//$ gcc -O2 -Wall -o app app.c
//$ sudo ./app
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>

#include "tankmotor_drv.h"

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int apply(int fd, int m1, int m2) {
    struct tankmotor_set set;
    set.m1 = clamp(m1, -100, 100);
    set.m2 = clamp(m2, -100, 100);

    if (ioctl(fd, TANKMOTOR_IOCTL_SET, &set) < 0) {
        perror("ioctl(TANKMOTOR_IOCTL_SET)");
        return -1;
    }
    printf("APPLY: m1(MotorA)=%d  m2(MotorB)=%d\n", set.m1, set.m2);
    return 0;
}

int main(void) {
    int fd = open("/dev/tankmotor", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/tankmotor)");
        fprintf(stderr, "Did you insmod tankmotor_drv.ko and create /dev/tankmotor?\n");
        return 1;
    }

    int m1 = 0, m2 = 0;
    const int STEP = 10;
    const int STEER = 40;

    printf("=== tankmotor test app (no server) ===\n");
    printf("MotorA: PWMA(BCM12) + DIRA(BCM5)  -> m1\n");
    printf("MotorB: PWMB(BCM13) + DIRB(BCM6)  -> m2\n\n");

    printf("Commands:\n");
    printf("  u : m1 += %d\n", STEP);
    printf("  d : m1 -= %d\n", STEP);
    printf("  j : m2 += %d\n", STEP);
    printf("  k : m2 -= %d\n", STEP);
    printf("  l : m2 = +%d (steer/left press)\n", STEER);
    printf("  r : m2 = -%d (steer/right press)\n", STEER);
    printf("  c : m2 = 0 (steer release)\n");
    printf("  s : stop (m1=0,m2=0)\n");
    printf("  set <m1> <m2> : direct set (-100..100)\n");
    printf("  q : quit\n\n");

    apply(fd, m1, m2);

    char line[128];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0) continue;

        if (!strcmp(line, "q"))
            break;

        if (!strcmp(line, "u")) {
            m1 = clamp(m1 + STEP, -100, 100);
        } else if (!strcmp(line, "d")) {
            m1 = clamp(m1 - STEP, -100, 100);
        } else if (!strcmp(line, "j")) {
            m2 = clamp(m2 + STEP, -100, 100);
        } else if (!strcmp(line, "k")) {
            m2 = clamp(m2 - STEP, -100, 100);
        } else if (!strcmp(line, "l")) {
            m2 = +STEER;
        } else if (!strcmp(line, "r")) {
            m2 = -STEER;
        } else if (!strcmp(line, "c")) {
            m2 = 0;
        } else if (!strcmp(line, "s")) {
            m1 = 0; m2 = 0;
        } else if (!strncmp(line, "set ", 4)) {
            int a, b;
            if (sscanf(line + 4, "%d %d", &a, &b) == 2) {
                m1 = clamp(a, -100, 100);
                m2 = clamp(b, -100, 100);
            } else {
                printf("Usage: set <m1> <m2>\n");
                continue;
            }
        } else {
            printf("Unknown command\n");
            continue;
        }

        apply(fd, m1, m2);
    }

    apply(fd, 0, 0);
    close(fd);
    return 0;
}
