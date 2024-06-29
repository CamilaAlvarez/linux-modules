#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/timekeeping.h>

#define DEVICE_NAME "ky004"
#define DEBOUNCE_NANO 200000000
static irqreturn_t button_interrupt_handler(int irq, void *dev_id);
static unsigned int ky004_poll(struct file *flip, poll_table *wait);

static DECLARE_WAIT_QUEUE_HEAD(onq);
struct ky004_data
{
    spinlock_t data_spinlock;
    bool on;
    struct gpio_desc *button_gpio;
    spinlock_t led_spinlock;
    struct gpio_desc *led_gpio;
    int button_irq;
    struct miscdevice *dev;
    spinlock_t time_spinlock;
    u64 last_button_press;
};
static struct file_operations ky004_fops = {
    .llseek = no_llseek,
    .poll = ky004_poll};
static struct miscdevice ky004_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .mode = 0400,
    .fops = &ky004_fops,
    .name = DEVICE_NAME};
static int ky004_probe(struct platform_device *device)
{
    struct gpio_desc *button, *led;
    struct ky004_data *data;
    int irq_button, error;
    long irq_flags;
    button = devm_gpiod_get(&device->dev, "button", GPIOD_IN);
    if (IS_ERR(button))
    {
        dev_err(&device->dev, "Could not get gpio descriptor for the button\n");
        return PTR_ERR(button);
    }
    irq_button = gpiod_to_irq(button);
    if (gpiod_is_active_low(button))
        irq_flags = IRQF_TRIGGER_FALLING;
    else
        irq_flags = IRQF_TRIGGER_RISING;
    led = devm_gpiod_get(&device->dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led))
    {
        dev_err(&device->dev, "Could not get gpio descriptor for the led\n");
        return PTR_ERR(led);
    }
    data = kzalloc(sizeof(struct ky004_data), GFP_KERNEL);
    if (data == NULL)
    {
        dev_err(&device->dev, "Could not get memory for KY-004\n");
        return -ENOMEM;
    }
    data->on = false;
    data->button_gpio = button;
    data->led_gpio = led;
    data->button_irq = irq_button;
    data->last_button_press = ktime_get_ns() - DEBOUNCE_NANO;
    spin_lock_init(&data->data_spinlock);
    spin_lock_init(&data->led_spinlock);
    spin_lock_init(&data->time_spinlock);
    error = devm_request_any_context_irq(&device->dev, irq_button, button_interrupt_handler,
                                         irq_flags, DEVICE_NAME, data);
    if (error)
    {
        dev_err(&device->dev, "irq %d request failed: %d\n", irq_button, error);
        kfree(data);
        return error;
    }
    error = misc_register(&ky004_device);
    if (error)
    {
        dev_err(&device->dev, "Could not register device\n");
        kfree(data);
        return error;
    }
    data->dev = &ky004_device;
    platform_set_drvdata(device, (void *)data);
    dev_info(&device->dev, "Loaded KY-004 module\n");
    return 0;
}
static int ky004_remove(struct platform_device *device)
{
    struct ky004_data *data = (struct ky004_data *)platform_get_drvdata(device);
    kfree(data);
    misc_deregister(&ky004_device);
    return 0;
}
static irqreturn_t button_interrupt_handler(int irq, void *dev_id)
{
    struct ky004_data *data;
    bool device_status;
    u64 last_press, now;
    now = ktime_get_ns();
    data = dev_id;
    spin_lock(&data->time_spinlock);
    last_press = data->last_button_press;
    spin_unlock(&data->time_spinlock);
    if (data->button_irq == irq && (now - last_press) > DEBOUNCE_NANO)
    {
        spin_lock(&data->data_spinlock);
        device_status = !data->on;
        data->on = device_status;
        spin_unlock(&data->data_spinlock);
        spin_lock(&data->led_spinlock);
        gpiod_set_value(data->led_gpio, device_status ? 1 : 0);
        spin_unlock(&data->led_spinlock);
        if (device_status)
            wake_up_interruptible(&onq);
    }
    return IRQ_HANDLED;
}
static unsigned int ky004_poll(struct file *flip, poll_table *wait)
{
    unsigned int reval_mask = 0;
    struct miscdevice *dev = flip->private_data;
    struct ky004_data *data = container_of(&dev, struct ky004_data, dev);
    if (data->on)
        reval_mask = POLLIN | POLLRDNORM;
    else
        poll_wait(flip, &onq, wait);
    return reval_mask;
}
static const struct of_device_id ky004_device_ids[] = {
    {.compatible = "calvarez,ky004"},
    {},
};
static struct platform_driver ky004_driver = {
    .probe = ky004_probe,
    .remove = ky004_remove,
    .driver = {
        .name = "ky004-driver",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(ky004_device_ids)}};
MODULE_DEVICE_TABLE(of, ky004_device_ids);
module_platform_driver(ky004_driver);
MODULE_AUTHOR("Camila Alvarez<cam.alvarez.i@gmail.com>");
MODULE_LICENSE("GPL");
