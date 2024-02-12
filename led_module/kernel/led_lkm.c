#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>
#include "../led_lkm.h"

#define MODNAME "led_test_lkm"
#define READ_LENGTH sizeof(int)

MODULE_AUTHOR("led experiment module");
MODULE_DESCRIPTION("Testing how to use GPIO to activate and deactivate a led");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
struct ledLkmCtx {
    struct device *dev;
    atomic_t powered;
};
static struct ledLkmCtx *gpriv; 

static int open_led_lkm(struct inode *inode, struct file *flip) {
    return nonseekable_open(inode, flip);
}
static ssize_t read_led_lkm(struct file *filp, char __user *ubuf, size_t count, loff_t *oof) {
    int ret, powered;
    ret = -EFAULT;
    powered = atomic_read(gpriv->powered);
    if (copy_to_user(ubuf, (void *)powered, READ_LENGTH)) {
        goto out;
    }
    ret = READ_LENGTH;
out:
    return ret;
}
static int close_led_lkm(struct inode *inode, struct file *flip) {
    return 0;
}
static long ioctl_led_lkm(struct file *filp, unsigned int cmd, unsigned long arg) {
    int retval = 0;
    pr_debug("In ioctl method, cmd=%d\n", _IOC_NR(cmd));
    // verify the message if for us
    if (_IOC_TYPE(cmd) != IOCTL_LED_LKM_MAGIC) {
        pr_info("ioctl fail; magin number mismatch\n");
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > IOCTL_MAXCMD_LED) {
        pr_info("ioctl failed; invalid cmd?\n");
        return -ENOTTY;
    }
    switch (cmd) {
        case IOCTL_POWER_ON:
            pr_debug("Power on\n");
            break;
        case IOCTL_POWER_OFF:
            pr_debug("Power off\n");
            break;
        case IOCTL_POWER_READ:
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
    printk(KERN_INFO "Entering LED LKM module");
    int ret;
    struct device* dev;

    ret = misc_register(&led_lkm_miscdev);
    if (ret != 0) {
        // Notice level = 5
        pr_notice("misc device registration failed, aborting\n");
        return ret;
    }
    dev = led_lkm_miscdev.this_device;
    // kzalloc sets memory to 0
    gpriv = devm_kzalloc(dev, sizeof(struct ledLkmCtx), GFP_KERNEL);
    gpriv->dev = dev;
    atomic_set(&gpriv->powered, 0);
    return 0;
}

static void __exit led_lkm_exit(void)
    {
    printk(KERN_INFO "Exiting LED LKM module");
}

module_init(led_lkm_init);
module_exit(led_lkm_exit);

