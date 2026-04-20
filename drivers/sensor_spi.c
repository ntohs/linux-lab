#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define MAX_TIMEOUT_MSEC 1000
#define BUF_SIZE 1024

#define DRIVER_NAME "k_spi_driver"
#define DRIVER_CLASS "k_spi_class"

static dev_t k_dev;
static struct class *k_class;
static struct cdev k_device;

static int dma_mode; /* 1: dma, 0: pio */

#define TOY_SPI_BUS_NUM 0
static struct spi_device *bmp280_dev;

struct spi_board_info spi_device_info = {
    .modalias = "bmp280",
    //.max_speed_hz = 1000000,
    .max_speed_hz = 10000000,
    .bus_num = TOY_SPI_BUS_NUM,
    .chip_select = 0,
    .mode = 3,
};

s32 dig_T1, dig_T2, dig_T3;
static struct hrtimer sensor_hrtimer;

static struct workqueue_struct *k_workqueue;
static void workqueue_fn(struct work_struct *work);

static DECLARE_WORK(work, workqueue_fn);

#define TIME_DEBUG

#ifdef TIME_DEBUG
#include <linux/ktime.h>

ktime_t start, end;
s64 delta_us;
#endif

static s32 read_temperature(void)
{
    int var1;
    int var2;
    s32 raw_temp;
    u8 raw_data[3] = {0};
    int target_byte = 512;
    int ret;

#ifdef TIME_DEBUG
    start = ktime_get();
#endif

#if 0
    u8 reg_addr = 0xFA;
    reg_addr = 0xFA | 0x80; /* bmp280 temperature read */
    ret = spi_write_then_read(bmp280_dev, &reg_addr, 1, raw_data, 3);
    if (ret < 0) {
        pr_err("sensor_spi: SPI read failed: %d\n", ret);
        return -EIO;
    }
#else
    u8 *tx_buf;
    u8 *rx_buf;
    struct spi_transfer txfers[14];
    struct spi_message msg;

    tx_buf = kzalloc(1, GFP_KERNEL);
    rx_buf = kzalloc(target_byte, GFP_KERNEL);

    if (!tx_buf || !rx_buf) {
        pr_err("sensor_spi: failed to alloc dma buffers\n");
        kfree(tx_buf);
        kfree(rx_buf);
        return -ENOMEM;
    }

    tx_buf[0] = 0xFA | 0x80; /* bmp280 temperature read */

    spi_message_init(&msg);
    memset(txfers, 0, sizeof(txfers));

    txfers[0].tx_buf = tx_buf;
    txfers[0].len = 1;
    spi_message_add_tail(&txfers[0], &msg);

    if (dma_mode) {
        /*
         * BCM2835 SPI 드라이버는 96바이트(BCM2835_SPI_DMA_MIN_LENGTH) 이상
         * 전송 시 자동으로 DMA를 사용한다. 수동 dma_map_single()은 불필요하며
         * spi_new_device()로 생성한 장치에 DMA mask가 없어 mapping error가 난다.
         * 따라서 가상 주소 버퍼만 넘기고 컨트롤러 드라이버에게 DMA 선택을 맡긴다.
         */

        /* target_byte >= 96이면 BCM2835 드라이버가 자동으로 DMA 사용 */
        txfers[1].rx_buf = rx_buf;
        txfers[1].len = target_byte;
        spi_message_add_tail(&txfers[1], &msg);
    } else {
        /* drivers/spi/spi-bcm2835.c
        * BCM2835_SPI_DMA_MIN_LENGTH(96) 바이트 이상을 전송하면
        * 자동으로 DMA로 변환해주고 있으므로, 95바이트로 나누어 전송
        */
        int remain = target_byte;
        int offset = 0;
        int i = 1;

        while (remain > 0) {
            int chunk = (remain > 95) ? 95 : remain;

            txfers[i].rx_buf = rx_buf + offset;
            txfers[i].len = chunk;

            spi_message_add_tail(&txfers[i], &msg);

            offset += chunk;
            remain -= chunk;
            i++;
        }
    }

    ret = spi_sync(bmp280_dev, &msg);
    if (ret == 0)
        memcpy(raw_data, rx_buf, 3);
    else
        pr_err("sensor_spi: forced pio read failed: %d\n", ret);


    kfree(tx_buf);
    kfree(rx_buf);

    if (ret < 0)
        return -EIO;
#endif

#ifdef TIME_DEBUG
    end = ktime_get();
    delta_us = ktime_to_us(ktime_sub(end, start));
    pr_info("sensor_spi: elapsed time: %lld us\n", delta_us);
#endif

    /* BMP280 compensation (reference code) */
    raw_temp = ((raw_data[0] << 16) | (raw_data[1] << 8) | raw_data[2]) >> 4;
    /* pr_info("sensor_spi: raw_temp : %d\n", raw_temp); */

    var1 = ((((raw_temp >> 3) - (dig_T1 << 1))) * (dig_T2)) >> 11;
    var2 = (((((raw_temp >> 4) - (dig_T1)) * ((raw_temp >> 4) - (dig_T1))) >> 12) * (dig_T3)) >> 14;

    return ((var1 + var2) * 5 + 128) >> 8;
}

static ssize_t k_driver_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
    char out_string[20];
    int temperature;

    pr_info("sensor_spi: read\n");
    temperature = read_temperature();
    snprintf(out_string, sizeof(out_string), "%d.%02d\n", temperature / 100, temperature % 100);

    pr_info("sensor_spi: out_string: %s\n", out_string);
    if (copy_to_user(buf, out_string, count)) {
        pr_err("sensor_spi: Failed to copy data to user space\n");
        return -EFAULT;
    }

    return count;
}

static ssize_t k_driver_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
    pr_info("sensor_spi: write\n");
    return count;
}

static int k_driver_open(struct inode *device_file, struct file *instance)
{
    pr_info("sensor_spi: open\n");
    return 0;
}

static int k_driver_close(struct inode *device_file, struct file *instance)
{
    pr_info("sensor_spi: close\n");
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = k_driver_open,
    .release = k_driver_close,
    .read = k_driver_read,
    .write = k_driver_write};

struct temp_attr {
    struct attribute attr;
    int value;
};

static struct temp_attr notify = {
    .attr.name = "notify",
    .attr.mode = 0644,
    .value = 0,
};

static struct temp_attr trigger = {
    .attr.name = "trigger",
    .attr.mode = 0644,
    .value = 0,
};

static struct temp_attr use_dma = {
    .attr.name = "use_dma",
    .attr.mode = 0644,
    .value = 0,
};

static struct attribute *temp_attrs[] = {
    &notify.attr,
    &trigger.attr,
    &use_dma.attr,
    NULL};

static struct attribute_group temp_attr_group = {
    .attrs = temp_attrs,
};

static const struct attribute_group *temp_attr_groups[] = {
    &temp_attr_group,
    NULL
};

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
    struct temp_attr *da = container_of(attr, struct temp_attr, attr);
    pr_info("sensor_spi: hello: show called (%s)\n", da->attr.name);
    return scnprintf(buf, PAGE_SIZE, "%d.%02d\n", da->value / 100, da->value % 100);
}

static struct kobject *toy_kobj;

static ssize_t store(struct kobject *kobj, struct attribute *attr,
                     const char *buf, size_t len)
{
    struct temp_attr *da = container_of(attr, struct temp_attr, attr);
    sscanf(buf, "%d", &(da->value));
    pr_info("sensor_spi: sysfs_notify store %s = %d\n", da->attr.name, da->value);

    if (strcmp(da->attr.name, "notify") == 0) {
        notify.value = da->value;
        sysfs_notify(toy_kobj, NULL, "notify");
    } else if (strcmp(da->attr.name, "trigger") == 0) {
        trigger.value = da->value;
        sysfs_notify(toy_kobj, NULL, "trigger");
    } else if (strcmp(da->attr.name, "use_dma") == 0) {
        use_dma.value = da->value;
        dma_mode = da->value;
        pr_info("sensor_spi: use_dma set to %d\n", use_dma.value);
    }

    return len;
}

static struct sysfs_ops s_ops = {
    .show = show,
    .store = store,
};

static struct kobj_type k_type = {
    .sysfs_ops = &s_ops,
    .default_groups = temp_attr_groups,
};

static void workqueue_fn(struct work_struct *work)
{
    char out_string[20];
    int temperature;

    temperature = read_temperature();

    snprintf(out_string, sizeof(out_string), "%d.%02d\n",
             temperature / 100, temperature % 100);

    /* vervose */
    /* pr_info("sensor_spi: %s\n", out_string); */

    notify.value = temperature;

    /* pr_info("sensor_spi: Calling sysfs_notify()\n"); */
    sysfs_notify(toy_kobj, NULL, "notify");
    /* pr_info("sensor_spi: sysfs_notify() called\n"); */
}

static enum hrtimer_restart sensor_timer_callback(struct hrtimer *timer)
{
    queue_work(k_workqueue, &work);
    hrtimer_forward_now(timer, ms_to_ktime(MAX_TIMEOUT_MSEC));
    return HRTIMER_RESTART;
}

static int bmp280_spi_write_reg(struct spi_device *spi, u8 reg, u8 val)
{
    u8 buf[2];
    int ret;

    buf[0] = reg & 0x7F;
    buf[1] = val;

    ret = spi_write(spi, buf, 2);
    if (ret < 0) {
        pr_err("sensor_spi: SPI write error to reg 0x%x\n", reg);
    }
    return ret;
}

static int __init k_module_init(void)
{
    u8 id;
    u8 reg_addr;
    int ret;

    if (alloc_chrdev_region(&k_dev, 0, 1, DRIVER_NAME) < 0) {
        pr_err("sensor_spi: Device Nr. could not be allocated!\n");
        return -1;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    if ((k_class = class_create(THIS_MODULE, DRIVER_CLASS)) == NULL) {
#else
    if ((k_class = class_create(DRIVER_CLASS)) == NULL) {
#endif
        pr_err("sensor_spi: Device class can not be created!\n");
        goto cerror;
    }

    if (device_create(k_class, NULL, k_dev, NULL, DRIVER_NAME) == NULL) {
        pr_err("sensor_spi: Can not create device file!\n");
        goto device_error;
    }

    cdev_init(&k_device, &fops);

    if (cdev_add(&k_device, k_dev, 1) == -1) {
        pr_err("sensor_spi: Registering of device to kernel failed!\n");
        goto reg_error;
    }

    /* spi_busnum_to_master */
    {
        struct device_node *np;
        struct platform_device *pdev;
        struct device *ctlr_dev;
        struct spi_controller *master;

        np = of_find_compatible_node(NULL, NULL, "brcm,bcm2835-spi");
        if (!np) {
            pr_err("sensor_spi: Could not find BCM2835 SPI DT node\n");
            goto reg_error;
        }
        pdev = of_find_device_by_node(np);
        of_node_put(np);
        if (!pdev) {
            pr_err("sensor_spi: Could not find SPI platform device\n");
            goto reg_error;
        }
        ctlr_dev = device_find_child_by_name(&pdev->dev, "spi0");
        put_device(&pdev->dev);
        if (!ctlr_dev) {
            pr_err("sensor_spi: Could not find SPI controller\n");
            goto reg_error;
        }
        master = container_of(ctlr_dev, struct spi_controller, dev);

        bmp280_dev = spi_new_device(master, &spi_device_info);
        put_device(ctlr_dev);
    }
    if (!bmp280_dev) {
        pr_err("sensor_spi: Could not create SPI device!\n");
        goto reg_error;
    }

    bmp280_dev->bits_per_word = 8;

    if (spi_setup(bmp280_dev) != 0) {
        pr_err("sensor_spi: Could not change bus setup!\n");
        goto spi_error;
    }

    id = spi_w8r8(bmp280_dev, 0xD0);
    pr_info("sensor_spi: ID: 0x%x\n", id);

    {
        u8 calib[2];

        reg_addr = 0x88;  /* dig_T1: u16, little-endian */
        ret = spi_write_then_read(bmp280_dev, &reg_addr, 1, calib, 2);
        if (ret < 0) { pr_err("sensor_spi: SPI read failed: %d\n", ret); goto spi_error; }
        dig_T1 = (u16)(calib[0] | (calib[1] << 8));

        reg_addr = 0x8A;  /* dig_T2: s16, little-endian */
        ret = spi_write_then_read(bmp280_dev, &reg_addr, 1, calib, 2);
        if (ret < 0) { pr_err("sensor_spi: SPI read failed: %d\n", ret); goto spi_error; }
        dig_T2 = (s16)(calib[0] | (calib[1] << 8));

        reg_addr = 0x8C;  /* dig_T3: s16, little-endian */
        ret = spi_write_then_read(bmp280_dev, &reg_addr, 1, calib, 2);
        if (ret < 0) { pr_err("sensor_spi: SPI read failed: %d\n", ret); goto spi_error; }
        dig_T3 = (s16)(calib[0] | (calib[1] << 8));
    }

    pr_info("sensor_spi: dig_T1: %d, dig_T2: %d, dig_T3: %d\n", dig_T1, dig_T2, dig_T3);

    bmp280_spi_write_reg(bmp280_dev, 0xF5, 5 << 5);
    bmp280_spi_write_reg(bmp280_dev, 0xF4, ((5 << 5) | (5 << 2) | (3 << 0)));

    pr_info("sensor_spi: sysfs init\n");
    toy_kobj = kzalloc(sizeof(*toy_kobj), GFP_KERNEL);
    if (toy_kobj) {
        kobject_init(toy_kobj, &k_type);
        if (kobject_add(toy_kobj, NULL, "%s", "sensor_spi")) {
            pr_err("sensor_spi: kobject_add() failed\n");
            kobject_put(toy_kobj);
            toy_kobj = NULL;
            goto spi_error;
        }
    }

    hrtimer_init(&sensor_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    sensor_hrtimer.function = &sensor_timer_callback;

    k_workqueue = create_workqueue("k_wq");
    if (!k_workqueue) {
        pr_err("sensor_spi: Failed to create workqueue\n");
        goto kobject_error;
    }

    hrtimer_start(&sensor_hrtimer, ms_to_ktime(MAX_TIMEOUT_MSEC), HRTIMER_MODE_REL);

    return 0;

kobject_error:
    kobject_put(toy_kobj);
spi_error:
    spi_unregister_device(bmp280_dev);
reg_error:
    device_destroy(k_class, k_dev);
device_error:
    class_destroy(k_class);
cerror:
    unregister_chrdev_region(k_dev, 1);
    return -1;
}

static void __exit k_module_exit(void)
{
    int retval;

    destroy_workqueue(k_workqueue);

    retval = hrtimer_cancel(&sensor_hrtimer);

    if (retval) {
        pr_info("sensor_spi: timer del...\n");
    }

    if (toy_kobj) {
        kobject_put(toy_kobj);
        kfree(toy_kobj);
    }

    if (bmp280_dev)
        spi_unregister_device(bmp280_dev);
    cdev_del(&k_device);
    device_destroy(k_class, k_dev);
    class_destroy(k_class);
    unregister_chrdev_region(k_dev, 1);
    pr_info("sensor_spi: exit\n");
}

module_init(k_module_init);
module_exit(k_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("K <k@k.com>");
MODULE_DESCRIPTION("k spi");
MODULE_VERSION("1.0.0");