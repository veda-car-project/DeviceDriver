#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>

#include "tankmotor_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LEEJEONGHAN");
MODULE_DESCRIPTION("Raspberry Pi PWM+DIR dual-motor driver (tankmotor)");
MODULE_VERSION("1.1");


//모터 1 변수 선언 (Motor A)
static int a_pwm = 12;   // PWMA = PWM 넣을 핀 번호 (BCM 12)
static int a_dir = 5;    // DIRA = 방향 결정 핀 (BCM 5)

//모터 2 변수 선언 (Motor B)
static int b_pwm = 13;   // PWMB = PWM 넣을 핀 번호 (BCM 13)
static int b_dir = 6;    // DIRB = 방향 결정 핀 (BCM 6)

//추가 설명 : DIR은 정방향/역방향 결정하는거, PWM = 속도(PWM) 넣을 핀 번호임.

//방향이 반대로 나오면 invert로 뒤집기
static int a_dir_inv = 0; // 1이면 Motor A 방향 반전
static int b_dir_inv = 0; // 1이면 Motor B 방향 반전

//속도 제어용 pwm hz, soft PWM frequency
static int pwm_hz = 1000;

//모듈 파라미터 선언
module_param(a_pwm, int, 0644);
module_param(a_dir, int, 0644);
module_param(b_pwm, int, 0644);
module_param(b_dir, int, 0644);
module_param(a_dir_inv, int, 0644);
module_param(b_dir_inv, int, 0644);
module_param(pwm_hz, int, 0644);

//모듈 파라미터 설명(DESCRIPTION임).
MODULE_PARM_DESC(a_pwm, "BCM GPIO for MotorA PWM (PWMA)");
MODULE_PARM_DESC(a_dir, "BCM GPIO for MotorA DIR (DIRA)");
MODULE_PARM_DESC(b_pwm, "BCM GPIO for MotorB PWM (PWMB)");
MODULE_PARM_DESC(b_dir, "BCM GPIO for MotorB DIR (DIRB)");
MODULE_PARM_DESC(a_dir_inv, "Invert MotorA direction (0/1)");
MODULE_PARM_DESC(b_dir_inv, "Invert MotorB direction (0/1)");
MODULE_PARM_DESC(pwm_hz, "Soft PWM frequency in Hz");


/* ---------- Soft PWM ---------- */
struct soft_pwm {
    int gpio_pwm;
    int duty_0_1000;     // 0..1000
    bool running;
    bool level_high;

    ktime_t period;
    ktime_t on_time;
    ktime_t off_time;

    struct hrtimer timer;
    spinlock_t lock;
};

static enum hrtimer_restart pwm_cb(struct hrtimer *t)
{
    struct soft_pwm *p = container_of(t, struct soft_pwm, timer);
    unsigned long flags;
    ktime_t next;

    spin_lock_irqsave(&p->lock, flags);

    if (!p->running) {
        spin_unlock_irqrestore(&p->lock, flags);
        return HRTIMER_NORESTART;
    }

    if (p->duty_0_1000 <= 0) {
        gpio_set_value(p->gpio_pwm, 0);
        p->running = false;
        spin_unlock_irqrestore(&p->lock, flags);
        return HRTIMER_NORESTART;
    }

    if (p->duty_0_1000 >= 1000) {
        gpio_set_value(p->gpio_pwm, 1);
        p->running = false;
        spin_unlock_irqrestore(&p->lock, flags);
        return HRTIMER_NORESTART;
    }

    if (p->level_high) {
        gpio_set_value(p->gpio_pwm, 0);
        p->level_high = false;
        next = p->off_time;
    } else {
        gpio_set_value(p->gpio_pwm, 1);
        p->level_high = true;
        next = p->on_time;
    }

    hrtimer_forward_now(&p->timer, next);
    spin_unlock_irqrestore(&p->lock, flags);
    return HRTIMER_RESTART;
}

static void pwm_recalc_times(struct soft_pwm *p)
{
    s64 period_ns = ktime_to_ns(p->period);
    s64 on_ns  = (period_ns * (s64)p->duty_0_1000) / 1000;
    s64 off_ns = period_ns - on_ns;

    if (on_ns < 0)  on_ns = 0;
    if (off_ns < 0) off_ns = 0;

    p->on_time  = ktime_set(0, (u32)on_ns);
    p->off_time = ktime_set(0, (u32)off_ns);
}

static void pwm_set_duty(struct soft_pwm *p, int duty_0_1000)
{
    unsigned long flags;

    if (duty_0_1000 < 0) duty_0_1000 = 0;
    if (duty_0_1000 > 1000) duty_0_1000 = 1000;

    spin_lock_irqsave(&p->lock, flags);
    p->duty_0_1000 = duty_0_1000;
    pwm_recalc_times(p);

    p->running = false;
    spin_unlock_irqrestore(&p->lock, flags);

    hrtimer_cancel(&p->timer);

    spin_lock_irqsave(&p->lock, flags);

    if (p->duty_0_1000 <= 0) {
        gpio_set_value(p->gpio_pwm, 0);
        p->level_high = false;
        p->running = false;
        spin_unlock_irqrestore(&p->lock, flags);
        return;
    }

    if (p->duty_0_1000 >= 1000) {
        gpio_set_value(p->gpio_pwm, 1);
        p->level_high = true;
        p->running = false;
        spin_unlock_irqrestore(&p->lock, flags);
        return;
    }

    gpio_set_value(p->gpio_pwm, 1);
    p->level_high = true;
    p->running = true;
    hrtimer_start(&p->timer, p->on_time, HRTIMER_MODE_REL);

    spin_unlock_irqrestore(&p->lock, flags);
}

/* ---------- Motor (PWM+DIR) ---------- */
struct motor {
    int gpio_dir;
    int dir_invert;     // 0/1
    struct soft_pwm pwm;
};

static struct motor motorA, motorB;

static void motor_set_speed(struct motor *m, int speed)
{
    int duty;

    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;

    if (speed == 0) {
        pwm_set_duty(&m->pwm, 0);
        return;
    }

    duty = (speed > 0) ? speed : -speed;
    /* DIR: forward(0) / reverse(1) 정의 (필요하면 invert로 뒤집기) */
    {
        int dir = (speed < 0) ? 1 : 0;
        if (m->dir_invert) dir ^= 1;
        gpio_set_value(m->gpio_dir, dir);
    }

    pwm_set_duty(&m->pwm, duty * 10); // 0..100 -> 0..1000
}

/* ---------- Char device ---------- */
static dev_t devno;
static struct cdev tank_cdev;
static struct class *tank_class;

static long tank_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct tankmotor_set set;

    if (cmd != TANKMOTOR_IOCTL_SET)
        return -ENOTTY;

    if (copy_from_user(&set, (void __user *)arg, sizeof(set)))
        return -EFAULT;

    motor_set_speed(&motorA, set.m1);
    motor_set_speed(&motorB, set.m2);
    return 0;
}

static const struct file_operations tank_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = tank_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = tank_ioctl,
#endif
};

static int req_out_gpio(int gpio, const char *name)
{
    int ret;
    if (!gpio_is_valid(gpio)) {
        pr_err("TANKMOTOR: invalid gpio %d (%s)\n", gpio, name);
        return -EINVAL;
    }
    ret = gpio_request(gpio, name);
    if (ret) {
        pr_err("TANKMOTOR: gpio_request failed %d (%s)\n", ret, name);
        return ret;
    }
    ret = gpio_direction_output(gpio, 0);
    if (ret) {
        pr_err("TANKMOTOR: gpio_direction_output failed %d (%s)\n", ret, name);
        gpio_free(gpio);
        return ret;
    }
    gpio_set_value(gpio, 0);
    return 0;
}

static void free_gpio_if_valid(int gpio)
{
    if (gpio_is_valid(gpio))
        gpio_free(gpio);
}

static int soft_pwm_init(struct soft_pwm *p, int gpio_pwm)
{
    if (pwm_hz <= 0) pwm_hz = 1000;

    p->gpio_pwm = gpio_pwm;
    p->duty_0_1000 = 0;
    p->running = false;
    p->level_high = false;

    p->period  = ktime_set(0, (u32)(1000000000ULL / (u32)pwm_hz));
    p->on_time = ktime_set(0, 0);
    p->off_time = p->period;

    spin_lock_init(&p->lock);
    hrtimer_init(&p->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    p->timer.function = pwm_cb;
    return 0;
}

static int __init tankmotor_init(void)
{
    int ret;
    struct device *dev;

    pr_info("TANKMOTOR: init (pwm_hz=%d)\n", pwm_hz);

    /* request gpios (4 pins) */
    ret = req_out_gpio(a_dir, "tank_a_dir"); if (ret) return ret;
    ret = req_out_gpio(a_pwm, "tank_a_pwm"); if (ret) goto err1;

    ret = req_out_gpio(b_dir, "tank_b_dir"); if (ret) goto err2;
    ret = req_out_gpio(b_pwm, "tank_b_pwm"); if (ret) goto err3;

    motorA.gpio_dir = a_dir;
    motorA.dir_invert = (a_dir_inv != 0);
    soft_pwm_init(&motorA.pwm, a_pwm);

    motorB.gpio_dir = b_dir;
    motorB.dir_invert = (b_dir_inv != 0);
    soft_pwm_init(&motorB.pwm, b_pwm);

    ret = alloc_chrdev_region(&devno, 0, 1, "tankmotor");
    if (ret) goto err4;

    cdev_init(&tank_cdev, &tank_fops);
    ret = cdev_add(&tank_cdev, devno, 1);
    if (ret) goto err5;

    tank_class = class_create("tankmotor");
    if (IS_ERR(tank_class)) {
        ret = PTR_ERR(tank_class);
        tank_class = NULL;
        goto err6;
    }

    dev = device_create(tank_class, NULL, devno, NULL, "tankmotor");
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err7;
    }

    pr_info("TANKMOTOR: created /dev/tankmotor major=%d\n", MAJOR(devno));
    return 0;

err7:
    class_destroy(tank_class);
err6:
    cdev_del(&tank_cdev);
err5:
    unregister_chrdev_region(devno, 1);
err4:
    free_gpio_if_valid(b_pwm);
err3:
    free_gpio_if_valid(b_dir);
err2:
    free_gpio_if_valid(a_pwm);
err1:
    free_gpio_if_valid(a_dir);
    return ret;
}

static void __exit tankmotor_exit(void)
{
    pwm_set_duty(&motorA.pwm, 0);
    pwm_set_duty(&motorB.pwm, 0);
    hrtimer_cancel(&motorA.pwm.timer);
    hrtimer_cancel(&motorB.pwm.timer);

    device_destroy(tank_class, devno);
    class_destroy(tank_class);
    cdev_del(&tank_cdev);
    unregister_chrdev_region(devno, 1);

    free_gpio_if_valid(a_dir);
    free_gpio_if_valid(a_pwm);
    free_gpio_if_valid(b_dir);
    free_gpio_if_valid(b_pwm);

    pr_info("TANKMOTOR: exit\n");
}

module_init(tankmotor_init);
module_exit(tankmotor_exit);
