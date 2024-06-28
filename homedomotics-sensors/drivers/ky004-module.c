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

#define DEVICE_NAME "ky004"
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
    button = devm_gpio_get(&device->dev, "button", GPIOD_IN);
    if (button == -ENOENT)
    {
        dev_err(&device->dev, "Could not get gpio descriptor for the button\n");
        return button;
    }
    irq_button = gpiod_to_irq(button);
    if (gpiod_is_active_low(button))
        irq_flags = IRQF_TRIGGER_FALLING;
    else
        irq_flags = IRQF_TRIGGER_RISING;
    led = devm_gpio_get(&device->dev, "led", GPIOD_OUT_LOW);
    if (led == -ENOENT)
    {
        dev_err(&device->dev, "Could not get gpio descriptor for the led\n");
        return led;
    }
    data = kzalloc(sizeof(struct ky004_data), GFP_KERNEL);
    if (data == NULL)
    {
        dev_error(&device->dev, "Could not get memory for KY-004\n");
        return -ENOMEM;
    }
    data->on = false;
    data->button_gpio = button;
    data->led_gpio = led;
    data->button_irq = irq_button;
    spin_lock_init(&data->data_spinlock);
    spin_lock_init(&data->led_spinlock);
    error = devm_request_any_context_irq(&device->dev, irq_button, button_interrupt_handler,
                                         DEVICE_NAME, data);
    if (error)
    {
        dev_err(&device->dev, "irq %d request failed: %d\n", irq_button, error);
        kfree(data);
        return error;
    }
    error = misc_register(ky004_device);
    if (error)
    {
        dev_err(&device->dev, "Could not register device\n");
        kfree(data);
        return error;
    }
    data->dev = &ky004_device;
    platform_set_drvdata(device, (void *)data);
    return 0;
}
static void ky004_remove(struct platform_device *device)
{
    struct ky004_data *data = (struct ky004_data *)platform_get_drvdata(device);
    kfree(data);
    misc_unregister(ky004_device);
}
static irqreturn_t button_interrupt_handler(int irq, void *dev_id)
{
    bool device_status;
    struct ky004_data *data = dev_id;
    if (data->button_irq == irq)
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
    struct miscdevice *dev = file->private_data;
    struct ky004_data *data = container_of(&dev, struct ky004_dev, dev);
    if (data->on)
        retval_mask = POLLIN | POLLRDNORM;
    else
        poll_wait(file, &onq, wait);
    return reval_mask;
}
static const struct of_device_id ky004_device_ids = {
    {.compatible = "calvarez,ky004"},
    {}};
static struct platform_driver ky004_driver = {
    .probe = ky004_probe,
    .remove_new = ky004_remove,
    .drive = {
        .name = "ky004-driver",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(ky004_device_ids)}};
MODULE_DEVICE_TABLE(of, ky004_device_ids);
module_platform_driver(ky004_driver);
MODULE_AUTHOR("Camila Alvarez<cam.alvarez.i@gmail.com>");
MODULE_LICENSE("GPL");
