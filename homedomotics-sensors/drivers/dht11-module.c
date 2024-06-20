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
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/stat.h>
#include "../include/dht11-data.h"

#define DHT11_DEVICE_NAME "dht11_module"
#define HIGH_SIGNAL 1
#define LOW_SIGNAL 0
#define MIN_INTERVAL 1000
#define UINT32_MAX 0xFFFFFFFF
#define TIMEOUT UINT32_MAX
#define BITS_IN_SIGNAL 40
#define BITS_PER_VALUE 8
// We cannot sleep in the read since reading from the sensor is time sensitive
// Also, it is a bad idea to sleep with interrupts disabled!
struct dht11_module_data;
static ssize_t write_measurements_to_user(struct dht11_module_data *dht11_data, char __user *buf, size_t count);
static u32 count_cycles_in_pulse(struct dht11_module_data *dht11_data, int value);
static bool compute_values(struct dht11_module_data *dht11_data, u32 *low_values, u32 *high_values);
static u8 compute_single_value(u32 *low_values, u32 *high_values, int offset);
// we set the mode so that everyone can read from the device
static char *dht11_class_devnode(struct device *dev, umode_t *mode);

struct dht11_module_data
{
    unsigned int major;
    struct class *dht11_class;
    struct cdev dht11_cdev;
    spinlock_t gpio_spinlock;
    struct gpio_desc *gpio;
    spinlock_t read_jiffies_spinlock;
    uint64_t last_read_jiffies; // Module can be read once per second
    spinlock_t data_spinlock;
    u8 last_read_successful;
    u8 last_successful_humidity;
    u8 last_successful_humidity_decimal;
    u8 last_successful_temperature; // in celsius
    u8 last_successful_temperature_decimal;
    int max_cycles;
};

static int open_sensors(struct inode *inode, struct file *flip)
{
    struct dht11_module_data *dht11_data = container_of(inode->i_cdev, struct dht11_module_data, dht11_cdev);
    flip->private_data = dht11_data;
    return nonseekable_open(inode, flip);
}
static int close_sensors(struct inode *inode, struct file *flip)
{
    return 0;
}
static ssize_t read_sensors(struct file *flip, char __user *buf, size_t count, loff_t *off)
{
    struct dht11_module_data *dht11_data;
    unsigned long irq_flags;
    uint64_t current_jiffies;
    u32 low_count[BITS_IN_SIGNAL],
        high_count[BITS_IN_SIGNAL];
    dht11_data = (struct dht11_module_data *)flip->private_data;
    // 1. Verify that the last request was over a second ago
    current_jiffies = get_jiffies_64();
    if ((current_jiffies - dht11_data->last_read_jiffies) <= (uint64_t)msecs_to_jiffies(MIN_INTERVAL))
    {
        goto writebuf;
    }
    spin_lock(&dht11_data->read_jiffies_spinlock);
    dht11_data->last_read_jiffies = current_jiffies;
    spin_unlock(&dht11_data->read_jiffies_spinlock);

    spin_lock(&dht11_data->data_spinlock);
    dht11_data->max_cycles = cpufreq_get(smp_processor_id()); // Frequency on KHZ (for 1ms = kHZ*1000/1000)
    spin_unlock(&dht11_data->data_spinlock);

    // 2. Send start signal
    spin_lock(&dht11_data->gpio_spinlock);
    // 2.1 pin is supposed to be in high, we force that
    gpiod_direction_output(dht11_data->gpio, HIGH_SIGNAL);
    spin_unlock(&dht11_data->gpio_spinlock);
    mdelay(1);
    // 2.2 set pin to low
    // IMPORTANT NOTE: In this case the controller DOES NOT sit on a slow bus, meaning we do not check if
    // we will sleep (we don't use gpiod_cansleep)
    spin_lock(&dht11_data->gpio_spinlock);
    gpiod_set_value(dht11_data->gpio, LOW_SIGNAL);
    spin_unlock(&dht11_data->gpio_spinlock);
    // 2.3 wait for at least 18ms (20ms to make sure)
    mdelay(20);
    // 3. the time sensitive process starts, we need to disable irqs
    spin_lock_irqsave(&dht11_data->gpio_spinlock, irq_flags);
    // 4. pull up and wait for 20-40us
    gpiod_set_value(dht11_data->gpio, HIGH_SIGNAL);
    udelay(40);
    // 5. set pin as input
    gpiod_direction_input(dht11_data->gpio);
    // 6. expect low pulse for 80us
    if (count_cycles_in_pulse(dht11_data, LOW_SIGNAL) == TIMEOUT)
    {
        pr_debug("Timeout while reading low signal from dht11\n");
        dht11_data->last_read_successful = 0;
        spin_unlock_irqrestore(&dht11_data->gpio_spinlock, irq_flags);
        goto writebuf;
    }
    // 7. expect high pulse for 80us
    if (count_cycles_in_pulse(dht11_data, HIGH_SIGNAL) == TIMEOUT)
    {
        pr_debug("Timeout while reading high signal from dht11\n");
        dht11_data->last_read_successful = 0;
        spin_unlock_irqrestore(&dht11_data->gpio_spinlock, irq_flags);
        goto writebuf;
    }
    // 8. Read the data, each bit is represented by one low-high cycle
    for (size_t i = 0; i < BITS_IN_SIGNAL; i++)
    {
        low_count[i] = count_cycles_in_pulse(dht11_data, LOW_SIGNAL);
        high_count[i] = count_cycles_in_pulse(dht11_data, HIGH_SIGNAL);
    }
    // 9. We finished the time-sensitive process, we can now re-enable interrupts
    spin_unlock_irqrestore(&dht11_data->gpio_spinlock, irq_flags);
    // 10. we compute the values: integral and decimal humity, integral and decimal temperature and checksum
    if (!compute_values(dht11_data, low_count, high_count))
    {
        pr_info("Invalid data read from DHT11\n");
        goto writebuf;
    }
writebuf:
    // 11. Move pin to output high
    spin_lock(&dht11_data->gpio_spinlock);
    gpiod_direction_output(dht11_data->gpio, HIGH_SIGNAL);
    spin_unlock(&dht11_data->gpio_spinlock);
    // 12. write data to user space
    return write_measurements_to_user(dht11_data, buf, count) ? -EFAULT : 0;
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
    struct dht11_module_data *dht11_data;
    struct device *dht11_module_device;
    struct device *dev = &pdev->dev;
    dev_t devt = 0;

    dht11_data = devm_kzalloc(dev, sizeof(struct dht11_module_data), GFP_KERNEL);
    if (dht11_data == NULL)
    {
        dev_err(dev, "Failed at getting memory!\n");
        return -ENOMEM;
    }
    // By passing the device the function should be able to access de dt
    dht11_data->gpio = devm_gpiod_get(dev, "temperature", GPIOD_OUT_HIGH);
    spin_lock_init(&dht11_data->gpio_spinlock);
    spin_lock_init(&dht11_data->read_jiffies_spinlock);
    spin_lock_init(&dht11_data->data_spinlock);

    // we'll register one device with a minor of 0
    error = alloc_chrdev_region(&devt, 0, 1, DHT11_DEVICE_NAME);
    if (error)
    {
        dev_err(dev, "Can't get major number\n");
        return error;
    }
    dht11_data->major = MAJOR(devt);
    dev_info(dev, "dht11 major number is = %d\n", dht11_data->major);
    dht11_data->dht11_class = class_create(THIS_MODULE, "dht11_char_class");
    dht11_data->dht11_class->devnode = dht11_class_devnode;
    if (IS_ERR(dht11_data->dht11_class))
    {
        dev_err(dev, "Error creating dht11 module class\n");
        unregister_chrdev_region(MKDEV(dht11_data->major, 0), 1);
        return PTR_ERR(dht11_data->dht11_class);
    }
    // Initialize char device with operations
    cdev_init(&dht11_data->dht11_cdev, &dht11_module_fops);
    dht11_data->dht11_cdev.owner = THIS_MODULE;
    // make the device available
    cdev_add(&dht11_data->dht11_cdev, devt, 1);

    dht11_module_device = device_create(dht11_data->dht11_class,
                                        dev,
                                        devt,
                                        NULL,
                                        DHT11_DEVICE_NAME);
    if (IS_ERR(dht11_module_device))
    {
        dev_err(dev, "Error creating dht11 device\n");
        class_destroy(dht11_data->dht11_class);
        unregister_chrdev_region(devt, 1);
        return -1;
    }
    // To allow the device to be read instantly after it has been initialized
    dht11_data->last_read_jiffies = get_jiffies_64() - (u64)msecs_to_jiffies(MIN_INTERVAL);
    // we haven't read anything
    dht11_data->last_read_successful = 0;
    dht11_data->last_successful_humidity = 0;
    dht11_data->last_successful_humidity_decimal = 0;
    dht11_data->last_successful_temperature = 0;
    dht11_data->last_successful_temperature_decimal = 0;
    dev_info(dev, "DHT11 module loaded\n");
    platform_set_drvdata(pdev, dht11_data);
    return 0;
}
static int dht11_remove(struct platform_device *pdev)
{
    struct dht11_module_data *dht11_data = platform_get_drvdata(pdev);
    unregister_chrdev_region(MKDEV(dht11_data->major, 0), 1);
    device_destroy(dht11_data->dht11_class, MKDEV(dht11_data->major, 0));
    cdev_del(&dht11_data->dht11_cdev);
    class_destroy(dht11_data->dht11_class);

    dev_info(&pdev->dev, "DHT11 module unloaded\n");
    return 0;
}
static ssize_t write_measurements_to_user(struct dht11_module_data *dht11_data, char __user *buf, size_t count)
{
    struct dht11_measurement measurement;
    if (count < sizeof(struct dht11_measurement))
    {
        pr_err("Requesting less that necessary. Requires %d vs %zu", sizeof(struct dht11_measurement), count);
        return sizeof(struct dht11_measurement);
    }
    spin_lock(&dht11_data->data_spinlock);
    measurement.successful = dht11_data->last_read_successful;
    measurement.humidity = dht11_data->last_successful_humidity;
    measurement.humidity_decimal = dht11_data->last_successful_humidity_decimal;
    measurement.temperature = dht11_data->last_successful_temperature;
    measurement.temperature_decimal = dht11_data->last_successful_temperature_decimal;
    spin_unlock(&dht11_data->data_spinlock);
    // return the number of bytes that could not be copied
    return copy_to_user(buf, &measurement, sizeof(struct dht11_measurement)) ? sizeof(struct dht11_measurement) : 0;
}
// This function is called with irqs disabled
static u32 count_cycles_in_pulse(struct dht11_module_data *dht11_data, int value)
{
    u32 count = 0;
    while (gpiod_get_value(dht11_data->gpio) == value)
    {
        if (count++ >= dht11_data->max_cycles)
        {
            return TIMEOUT;
        }
    }
    pr_debug("Took %u cycles", count);
    return count;
}
static bool compute_values(struct dht11_module_data *dht11_data, u32 *low_values, u32 *high_values)
{
    char checksum, humidity, humidity_decimal, temperature, temperature_decimal;
    humidity = compute_single_value(low_values, high_values, 0);
    humidity_decimal = compute_single_value(low_values, high_values, BITS_PER_VALUE);
    temperature = compute_single_value(low_values, high_values, BITS_PER_VALUE * 2);
    temperature_decimal = compute_single_value(low_values, high_values, BITS_PER_VALUE * 3);
    checksum = compute_single_value(low_values, high_values, BITS_PER_VALUE * 4);

    if (checksum != (humidity + humidity_decimal + temperature + temperature_decimal))
    {
        pr_info("Invalid value obtained from DHT11. Checksum doesn't match: expected %c got %c\n",
                checksum,
                humidity + humidity_decimal + temperature + temperature_decimal);
        spin_lock(&dht11_data->data_spinlock);
        dht11_data->last_read_successful = 0;
        spin_unlock(&dht11_data->data_spinlock);
        return false;
    }
    spin_lock(&dht11_data->data_spinlock);
    dht11_data->last_read_successful = 1;
    dht11_data->last_successful_humidity = humidity;
    dht11_data->last_successful_humidity_decimal = humidity_decimal;
    dht11_data->last_successful_temperature = temperature;
    dht11_data->last_successful_temperature_decimal = temperature_decimal;
    spin_unlock(&dht11_data->data_spinlock);
    return true;
}
static u8 compute_single_value(u32 *low_values, u32 *high_values, int offset)
{
    u8 value = 0;
    // MSB comes first
    for (size_t i = 0; i < BITS_PER_VALUE; i++)
    {
        // low cycles > high cycles => 0
        // low cycles < high cycles => 1
        if (low_values[i + offset] < high_values[i + offset])
        {
            value |= 1 << (BITS_PER_VALUE - 1 - i);
        }
    }
    return value;
}
static char *dht11_class_devnode(struct device *dev, umode_t *mode)
{
    if (mode != NULL)
        *mode = S_IRUGO;
    return NULL;
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