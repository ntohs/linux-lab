#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define SIGNR 10
#define BUF_SIZE 1024

static char kernel_write_buffer[BUF_SIZE];
static dev_t kdt_dev;
static struct class *kdt_class;
static struct cdev kdt_device;
static struct task_struct *wait_thread;
int wait_queue_flag = 0;
unsigned int button_irq;
static struct task_struct *user_space_task = NULL;

DECLARE_WAIT_QUEUE_HEAD(wait_queue);

#define DRIVER_NAME "kdt_interrupt_driver"
#define DRIVER_CLASS "kdt_interrupt_class"
#define GPIO_BASE 512
#define BCM_GPIO(n) (GPIO_BASE + (n))

#define KDT_GPIO_OUTPUT BCM_GPIO(17)
#define KDT_GPIO_INPUT BCM_GPIO(16)

/* 리눅스 6.12부터 struct pt_regs *regs 인자는 받지 않는다. */
static irqreturn_t kdt_gpio_irq_signal_handler(int irq, void *dev_id /*, struct pt_regs *regs*/)
{
    struct kernel_siginfo info;

    pr_info("kdt_gpio_irq_signal_handler: interrupt triggered!!!\n");

    if (user_space_task != NULL) {
        memset(&info, 0, sizeof(info));
        info.si_signo = SIGNR;
        info.si_code = SI_QUEUE;
        info.si_int = 1;

        /* 시그널을 linux-lab 프로세스에게 보내자!! */
        if (send_sig_info(SIGNR, &info, user_space_task) < 0)
            pr_err("gpio_irq_signal: Error sending signal\n");
        else
            pr_info("signal %d sent to pid %d\n", SIGNR, user_space_task->pid);
    }

    /* kthread도 깨우자!! */
    wait_queue_flag = 1;
    wake_up_interruptible(&wait_queue);

    return (irqreturn_t)IRQ_HANDLED;
}

static ssize_t kdt_driver_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
    if (count == 0)
        return 0;

    if (copy_to_user(buf, "t", 1)) {
        pr_err("read: error!\n");
        return -EFAULT;
    }

    user_space_task = current;
    pr_info("Userspace PID: %d is registered\n", user_space_task->pid);

    wait_queue_flag = 1;
    wake_up_interruptible(&wait_queue);

    pr_info("Value of button: %d\n", gpio_get_value(KDT_GPIO_INPUT));

    return 1;
}

static ssize_t kdt_driver_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
    if (copy_from_user(kernel_write_buffer, buf, count)) {
        pr_err("write: error\n");
    }

    switch (kernel_write_buffer[0]) {
    case 0:
        pr_info("gpio %d: low\n", KDT_GPIO_OUTPUT);
        gpio_set_value(KDT_GPIO_OUTPUT, 0);
        break;
    case 1:
        pr_info("gpio %d: high\n", KDT_GPIO_OUTPUT);
        gpio_set_value(KDT_GPIO_OUTPUT, 1);
        break;
    default:
        pr_info("Invalid Input!\n");
        break;
    }

    pr_info("write: done\n");

    return count;
}

static int kdt_driver_open(struct inode *device_file, struct file *instance)
{
    pr_info("open\n");
    return 0;
}

static int kdt_driver_close(struct inode *device_file, struct file *instance)
{
    pr_info("close\n");
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = kdt_driver_open,
    .release = kdt_driver_close,
    .read = kdt_driver_read,
    .write = kdt_driver_write};

static int kdt_wait_kthread(void *unused)
{
    while (1) {
        pr_info("waiting for event\n");
        wait_event_interruptible(wait_queue, wait_queue_flag != 0);
        if (wait_queue_flag == 2) {
            pr_info("exit\n");
            return 0;
        }
        pr_info("wake-up!!!\n");
        wait_queue_flag = 0;
    }
    do_exit(0);
    return 0;
}

static int __init kdt_module_init(void)
{

    /* 여기서 노드를 할당 받는다. */
    if (alloc_chrdev_region(&kdt_dev, 0, 1, DRIVER_NAME) < 0) {
        pr_info("Device Nr. could not be allocated!\n");
        return -1;
    }

    pr_info("할당 받은 Major = %d Minor = %d \n", MAJOR(kdt_dev), MINOR(kdt_dev));

    /* device class 생성 */
    if ((kdt_class = class_create(DRIVER_CLASS)) == NULL) {
        pr_info("Device class can not be created!\n");
        goto cerror;
    }

    /* device file 생성 */
    if (device_create(kdt_class, NULL, kdt_dev, NULL, DRIVER_NAME) == NULL) {
        pr_info("Can not create device file!\n");
        goto device_error;
    }

    /* character device file 초기화 */
    cdev_init(&kdt_device, &fops);

    /* 커널에 등록 */
    if (cdev_add(&kdt_device, kdt_dev, 1) == -1) {
        pr_info("Registering of device to kernel failed!\n");
        goto reg_error;
    }

    if (gpio_request(KDT_GPIO_OUTPUT, "kdt-gpio-output")) {
        pr_info("Can not allocate GPIO %d\n", KDT_GPIO_OUTPUT);
        goto reg_error;
    }

    if (gpio_direction_output(KDT_GPIO_OUTPUT, 0)) {
        pr_info("Can not set GPIO %d to output!\n", KDT_GPIO_OUTPUT);
        goto gpio_15_error;
    }

    if (gpio_request(KDT_GPIO_INPUT, "kdt-gpio-input")) {
        pr_info("Can not allocate GPIO %d\n", KDT_GPIO_INPUT);
        goto gpio_14_error;
    }

    if (gpio_direction_input(KDT_GPIO_INPUT)) {
        pr_info("Can not set GPIO %d to input!\n", KDT_GPIO_INPUT);
        goto gpio_14_error;
    }

    wait_thread = kthread_create(kdt_wait_kthread, NULL, "kdt wait thread");
    if (wait_thread) {
        pr_info("Thread created successfully\n");
        wake_up_process(wait_thread);
    } else
        pr_info("Thread creation failed\n");

    button_irq = gpio_to_irq(KDT_GPIO_INPUT);

    /* 채터링도 방지하자!!
     * 리눅스 6.12부터 정의되어있지 않아 일단 주석처리
     */
    // gpio_set_debounce(16, 300);

    if (request_irq(button_irq, (irq_handler_t)kdt_gpio_irq_signal_handler, IRQF_TRIGGER_RISING, "kdt_gpio_irq_signal", NULL) != 0) {
        pr_err("Error!\n Can not request interrupt nr: %d\n", button_irq);
        gpio_free(KDT_GPIO_OUTPUT);
        return -1;
    }

    return 0;

gpio_14_error:
    gpio_free(KDT_GPIO_INPUT);
gpio_15_error:
    gpio_free(KDT_GPIO_OUTPUT);
reg_error:
    device_destroy(kdt_class, kdt_dev);
device_error:
    class_destroy(kdt_class);
cerror:
    unregister_chrdev_region(kdt_dev, 1);
    return -1;
}

static void __exit kdt_module_exit(void)
{
    free_irq(button_irq, NULL);
    wait_queue_flag = 2;
    wake_up_interruptible(&wait_queue);
    gpio_set_value(KDT_GPIO_OUTPUT, 0);
    gpio_free(KDT_GPIO_INPUT);
    gpio_free(KDT_GPIO_OUTPUT);
    cdev_del(&kdt_device);
    device_destroy(kdt_class, kdt_dev);
    class_destroy(kdt_class);
    unregister_chrdev_region(kdt_dev, 1);
    pr_info("exit\n");
}

module_init(kdt_module_init);
module_exit(kdt_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KTD <ktd@kdt.com>");
MODULE_DESCRIPTION("kdt character");
MODULE_VERSION("1.0.0");
