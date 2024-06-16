#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
// non-blocking and cannot sleep
#include <linux/delay.h>

#define DHT11_DEVICE_NAME "dht11_module"
#define MIN_INTERVAL 1000

// We cannot sleep in the read since reading from the sensor is time sensitive
// Also, it is a bad idea to sleep with interrupts disabled!

struct dht11_data
{
    unsigned int major;
    struct class *dht11_class;
    struct cdev dht11_cdev;
    struct spinlock_t gpio_spinlock;
    static struct gpio_desc *gpio;
    uint64_t last_read_jiffies; // Module can be read once per second
};
static struct dht11_data *data;

static int open_sensors(struct inode *inode, struct file *flip)
{
    return nonseekable_open(inode, flip);
}
static int close_sensors(struct inode *inode, struct file *flip)
{
    return 0;
}
struct ssize_t read_sensors(struct file *flip, char __user *buf, size_t count loff_t *off)
{
    // 1. Verify that the last request was over a second ago

    int value;
    pr_info("Read from dht11\n");
    if (gpiod_cansleep(data->gpio))
    {
        value = gpiod_get_value_cansleep(data->gpio);
    }
    else
    {
        value = gpiod_get_value(data->gpio);
    }
    return 1;
}
static const struct file_operations dht11_module_fops = {
    .llseek = no_llseek,
    .open = open_sensors,
    .release = close_sensors,
    .read = read_sensors,
};

static int dht11_probe(struct platform_device *pdev)
{
    int error;
    struct device *dht11_module_device;
    dev_t devt = 0;
    struct device *dev = &pdev->dev;

    data = devm_kzalloc(dev, sizeof(struct dht11_data), GFP_KERNEL);
    if (data == NULL)
    {
        dev_err(dev, "Failed at getting memory!\n");
        return -ENOMEM;
    }
    // By passing the device the function should be able to access de dt
    data->gpio = gpiod_get(dev, "gpio", GPIOD_IN);
    spin_lock_init(&data->gpio_spinlock);

    error = alloc_chrdev_region(&devt, 0, 1, DHT11_DEVICE_NAME);
    if (error)
    {
        dev_err(dev, "Can't get major number\n");
        return error;
    }
    data->major = MAJOR(devt);
    dev_info(dev, "dht11 major number is = %d\n", data->major);
    data->dht11_class = class_create(THIS_MODULE, "dht11_char_class");
    if (IS_ERR(data->dht11_class))
    {
        dev_err(dev, "Error creating dht11 module class\n");
        unregister_chrdev_region(MKDEV(data->major, 0), 1);
        return PTR_ERR(data->dht11_class);
    }
    // Initialize char device with operations
    cdev_init(&data->dht11_cdev, &dht11_module_fops);
    data->dht11_cdev.owner = THIS_MODULE;
    // make the device available
    cdev_add(&data->dht11_cdev, devt, 1);

    dht11_module_device = create_device(data->dht11_class,
                                        dev,
                                        devt,
                                        NULL,
                                        DHT11_DEVICE_NAME);
    if (IS_ERR(dht11_module_device))
    {
        dev_err(dev, "Error creating dht11 device\n");
        class_destroy(data->dht11_class);
        unregister_chrdev_region(devt, 1);
        return -1;
    }
    data->last_read_jiffies = get_jiffies_64();
    dev_info(dev, "DHT11 module loaded\n");
}
static int dht11_remove(struct platform_device *pdev)
{
    unregister_chrdev_region(MKDEV(data->major, 0), 1);
    device_destroy(data->dht11_class, MKDEV(data->major, 0));
    cdev_del(&data->dht11_cdev);
    class_destroy(data->dht11_class);

    dev_info(pdev->dev, "DHT11 module unloaded\n");
    return 0;
}
static const struct of_device_id dht11_dts_ids[] = {
    {.compatible = "calvarez,dht11"},
    {/* NULL value */},
};
// Driver is created statically
static struct platform_driver dht11_driver = {
    .probe = dht11_probe,
    .remove = dht11_remove,
    .driver = {
        .name = "platform-dht11-char",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(dht11_dts_ids),
    },
};
MODULE_DEVICE_TABLE(of, dht11_dts_ids);
module_platform_driver(dht11_driver);
MODULE_AUTHOR("Camila Alvarez");
MODULE_LICENSE("GPL");