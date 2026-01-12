#ifndef _TANKMOTOR_DRV_H_
#define _TANKMOTOR_DRV_H_
#include <linux/ioctl.h>
#define TANKMOTOR_IOC_MAGIC 'T'

struct tankmotor_set {
    int m1;
    int m2;
};

#define TANKMOTOR_IOCTL_SET _IOW(TANKMOTOR_IOC_MAGIC, 0x01, struct tankmotor_set)
#endif
