#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include "../led_lkm.h"

#define MODNAME "led_test_lkm"
#define READ_LENGTH sizeof(int)
// NOTE: The 0x7e200000 is the base address in the bus address space
#define PERIPH_BASE 0xFE000000
#define GPIO_BASE (PERIPH_BASE + 0x200000)
#define BASE_MEMORY_ADDRESS GPIO_BASE
#define RESOURCE_SIZE 232 // 58*4
#define GPIO_PIN_OFFSET 0x08
#define GPIO_SET_PIN_OFFSET 0x1c
#define GPIO_CLEAR_PIN_OFFSET 0x28
#define GPIO_READ_PIN_OFFSET 0x34
// It's not a register per pin! I'll have to write to multiple registers!
MODULE_AUTHOR("led experiment module");
MODULE_DESCRIPTION("Testing how to use GPIO to activate and deactivate a led");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
struct ledLkmCtx
{
    struct device *dev;
    atomic_t powered;
    void *base_io;
    struct mutex mutex_mmio;
};
static struct ledLkmCtx *gpriv;

static int open_led_lkm(struct inode *inode, struct file *flip)
{
    return nonseekable_open(inode, flip);
}
static ssize_t read_led_lkm(struct file *filp, char __user *ubuf, size_t count, loff_t *oof)
{
    int ret, powered;
    ret = -EFAULT;
    powered = atomic_read(&gpriv->powered);
    if (copy_to_user(ubuf, &powered, READ_LENGTH))
    {
        goto out;
    }
    ret = READ_LENGTH;
out:
    return ret;
}
static int close_led_lkm(struct inode *inode, struct file *flip)
{
    return 0;
}
static long ioctl_led_lkm(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    pr_debug("In ioctl method, cmd=%d\n", _IOC_NR(cmd));
    // verify the message if for us
    if (_IOC_TYPE(cmd) != IOCTL_LED_LKM_MAGIC)
    {
        pr_info("ioctl fail; magin number mismatch\n");
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > IOCTL_MAXCMD_LED)
    {
        pr_info("ioctl failed; invalid cmd?\n");
        return -ENOTTY;
    }
    u32 mask = 1 << 22;
    u32 gpset0;
    switch (cmd)
    {
    case IOCTL_POWER_ON:
        pr_debug("Power on\n");
        mutex_lock(&gpriv->mutex_mmio);
        gpset0 = ioread32(gpriv->base_io + GPIO_SET_PIN_OFFSET);
        gpset0 |= mask;
        iowrite32(gpset0, gpriv->base_io + GPIO_SET_PIN_OFFSET);
        mutex_unlock(&gpriv->mutex_mmio);
        break;
    case IOCTL_POWER_OFF:
        mutex_lock(&gpriv->mutex_mmio);
        gpset0 = ioread32(gpriv->base_io + GPIO_CLEAR_PIN_OFFSET);
        gpset0 |= mask;
        iowrite32(gpset0, gpriv->base_io + GPIO_CLEAR_PIN_OFFSET);
        mutex_unlock(&gpriv->mutex_mmio);
        pr_debug("Power off\n");
        break;
    case IOCTL_POWER_READ:
        mutex_lock(&gpriv->mutex_mmio);
        gpset0 = ioread32(gpriv->base_io + GPIO_READ_PIN_OFFSET);
        int value = gpset0 & mask;
        mutex_unlock(&gpriv->mutex_mmio);
        retval = __put_user(value > 0 ? 1 : 0, (int __user *)arg);
        pr_debug("Read power value\n");
        break;
    default:
        return -ENOTTY;
    }

    return retval;
}
static const struct file_operations led_lkm_fops = {
    .llseek = no_llseek,
    .open = open_led_lkm,
    .read = read_led_lkm,
    .release = close_led_lkm,
    .unlocked_ioctl = ioctl_led_lkm

};
static struct miscdevice led_lkm_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = MODNAME,
    .mode = 0666,
    .fops = &led_lkm_fops,
};

static int __init led_lkm_init(void)
{
    // Here I should mark the GPIO22 as output
    printk(KERN_INFO "Entering LED LKM module");
    int ret;
    struct device *dev;
    ret = misc_register(&led_lkm_miscdev);
    if (ret != 0)
    {
        // Notice level = 5
        pr_notice("misc device registration failed, aborting\n");
        return ret;
    }
    dev = led_lkm_miscdev.this_device;
    // kzalloc sets memory to 0
    gpriv = devm_kzalloc(dev, sizeof(struct ledLkmCtx), GFP_KERNEL);
    gpriv->dev = dev;
    // request memory: Note that this will always return NULL as someone is already using this memory.
    // We shouldn't map GPIOs! we're only doing it as a test (we should use /sys/class/gio)
    /*if (!request_mem_region(BASE_MEMORY_ADDRESS, RESOURCE_SIZE, MODNAME))
    {
        dev_warn(dev, "couldn't get region for mmio, aborting\n");
        return -EBUSY;
    }*/
    // map the memory
    gpriv->base_io = devm_ioremap(dev, BASE_MEMORY_ADDRESS, RESOURCE_SIZE);
    if (gpriv->base_io == NULL)
    {
        dev_err(dev, "register mapping failed\n");
        return -ENXIO;
    }
    mutex_init(&gpriv->mutex_mmio);
    atomic_set(&gpriv->powered, 0);
    // Mark GPIO22 as output
    u32 gpfsel2 = ioread32(gpriv->base_io + GPIO_PIN_OFFSET);
    u32 mask1 = ~(1 << 6 | 1 << 7 | 1 << 8);
    u32 mask2 = 1 << 6;
    gpfsel2 = (gpfsel2 & mask1) | mask2;
    iowrite32(gpfsel2, gpriv->base_io + GPIO_PIN_OFFSET);
    return 0;
}

static void __exit led_lkm_exit(void)
{
    // reset pin to input
    u32 gpfsel2 = ioread32(gpriv->base_io + GPIO_PIN_OFFSET);
    u32 mask1 = ~(1 << 6 | 1 << 7 | 1 << 8);
    gpfsel2 = (gpfsel2 & mask1);
    iowrite32(gpfsel2, gpriv->base_io + GPIO_PIN_OFFSET);
    misc_deregister(&led_lkm_miscdev);
    printk(KERN_INFO "Exiting LED LKM module");
}

module_init(led_lkm_init);
module_exit(led_lkm_exit);
