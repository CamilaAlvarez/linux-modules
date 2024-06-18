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
#include <linux/types.h>
#include <linux/jiffies.h>
// non-blocking and cannot sleep
#include <linux/delay.h>

#define DHT11_DEVICE_NAME "dht11_module"
#define HIGH_SIGNAL 1
#define LOW_SIGNAL 0
#define MIN_INTERVAL 1000
#define UINT32_MAX 0xFFFFFFFF
#define TIMEOUT UINT32_MAX
#define BITS_IN_SIGNAL 40
#define VALUES_TO_WRITE 5

static ssize_t write_measurements_to_user(char __user *buf, size_t count);
static u32 count_cycles_in_pulse(int value);
static bool compute_values(char *low_values, char *high_values);

// We cannot sleep in the read since reading from the sensor is time sensitive
// Also, it is a bad idea to sleep with interrupts disabled!

struct dht11_data
{
    unsigned int major;
    struct class *dht11_class;
    struct cdev dht11_cdev;
    struct spinlock_t gpio_spinlock;
    static struct gpio_desc *gpio;
    struct spinlock_t read_jiffies_spinlock;
    uint64_t last_read_jiffies; // Module can be read once per second
    struct spinlock_t data_spinlock;
    char last_read_successful;
    char last_successful_humidity;
    char last_successful_humidity_decimal;
    char last_successful_temperature;
    char last_successful_temperature_decimal;
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
static ssize_t read_sensors(struct file *flip, char __user *buf, size_t count, loff_t *off)
{
    unsigned long irq_flags;
    u64 jiffies_start, jiffies_end;
    u32 low_count[BITS_IN_SIGNAL], high_count[BITS_IN_SIGNAL];
    // 1. Verify that the last request was over a second ago
    uint64_t current_jiffies = get_jiffies_64();
    if ((current_jiffies - data->last_read_jiffies) <= (uint64_t)msecs_to_jiffies(MIN_INTERVAL))
    {
        goto writebuf;
    }
    spin_lock(&data->read_jiffies_spinlock);
    data->last_read_jiffies = current_jiffies;
    spin_unlock(&data->read_jiffies_spinlock);

    // 2. Send start signal
    spin_lock(&data->gpio_spinlock);
    // 2.1 pin is supposed to be in high, we force that
    gpiod_direction_output(data->gpio, HIGH_SIGNAL);
    spin_unlock(&data->gpio_spinlock);
    mdelay(1);
    // 2.2 set pin to low
    // IMPORTANT NOTE: In this case the controller DOES NOT sit on a slow bus, meaning we do not check if
    // we will sleep (we don't use gpiod_cansleep)
    spin_lock(&data->gpio_spinlock);
    gpiod_set_value(data->gpio, LOW_SIGNAL);
    spin_unlock(&data->gpio_spinlock);
    // 2.3 wait for at least 18ms (20ms to make sure)
    mdelay(20);
    // 3. the time sensitive process starts, we need to disable irqs
    spin_lock_irqsave(&data->gpio_spinlock, flags);
    // 4. set pin as input
    gpiod_direction_input(data->gpio);
    // 5. wait for 20-40us
    ndelay(40);
    // 6. expect low pulse for 80us
    jiffies_start = jiffies_get_64();
    if (count_cycles_in_pulse(LOW_SIGNAL) == TIMEOUT)
    {
        pr_debug("Timeout while reading low signal from dht11\n");
        data->last_read_successful = 0;
        spin_unlock_irqrestore(&data->gpio_spinlock, flags);
        goto writebuf;
    }
    jiffies_end = jiffies_get_64();
    // 7. expect high pulse for 80us
    jiffies_start = jiffies_get_64();
    if (count_cycles_in_pulse(HIGH_SIGNAL) == TIMEOUT)
    {
        pr_debug("Timeout while reading high signal from dht11\n");
        data->last_read_successful = 0;
        spin_unlock_irqrestore(&data->gpio_spinlock, flags);
        goto writebuf;
    }
    jiffies_end = jiffies_get_64();
    // 8. Read the data, each bit is represented by one low-high cycle
    for (size_t i = 0; i < BITS_IN_SIGNAL; i++)
    {
        low_count[i] = count_cycles_in_pulse(LOW_SIGNAL);
        high_count[i] = count_cycles_in_pulse(HIGH_SIGNAL);
    }
    // 9. We finished the time-sensitive process, we can now re-enable interrupts
    spin_unlock_irqrestore(&data->gpio_spinlock, flags);
    // 10. we compute the values: integral and decimal humity, integral and decimal temperature and checksum
    // low cycles > high cycles => 0
    // low cycles < high cycles => 1
    if (!compute_values(low_count, high_count))
    {
        pr_info("Invalid data read from DHT11\n");
        goto writebuf;
    }
writebuf:
    // 11. write data to user space
    return write_measurements_to_user(buf, count);
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
    data->gpio = devm_gpiod_get(dev, "temperature", GPIOD_OUT_HIGH);
    spin_lock_init(&data->gpio_spinlock);
    spin_lock_init(&data->read_jiffies_spinlock);
    spin_lock_init(&data->data_spinlock);

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
    // To allow the device to be read instantly after it has been initialized
    data->last_read_jiffies = get_jiffies_64() - (u64)msecs_to_jiffies(MIN_INTERVAL);
    // we haven't read anything
    data->last_read_successful = 0;
    data->last_successful_humidity = 0;
    data->last_successful_humidity_decimal = 0;
    data->last_successful_temperature = 0;
    data->last_successful_temperature_decimal = 0;
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