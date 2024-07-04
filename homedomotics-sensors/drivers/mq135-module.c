#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/fs.h>
#include "../include/mq135-data.h"

#define MODNAME "mq135_module"
struct mq135_module_data
{
    struct miscdevice *dev;
    struct mutex i2c_client_mutex;
    struct i2c_client *client;
};
static ssize_t mq135_read(struct file *flip, char __user *buf, size_t count, loff_t *off);
static const struct file_operations mq135_fops = {
    .llseek = no_llseek,
    .read = mq135_read,

};
static struct miscdevice mq135_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "mq135_device",
    .mode = 0444,
    .fops = &mq135_fops,
};
static int write_config(struct mq135_module_data *mq135_data)
{
    int ret;
    const int buffer_size = 3;
    char config_buffer[3];
    uint16_t config = CONFIG_COMP_QUE_1CONV | CONFIG_COMP_NOLAT | CONFIG_COMP_POL_LOW | CONFIG_COMP_MODE_TRAD |
                      CONFIG_DATA_RATE | CONFIG_MODE | CONFIG_PGA_DEFAULT | CONFIG_MUX_A0 | CONFIG_OS;
    // There's no need to send the address as a first parameter, since the i2c_client already has it
    config_buffer[0] = CONFIG_REGISTER;
    config_buffer[1] = config >> 8;
    config_buffer[2] = config & 0x00FF;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_send(mq135_data->client, config_buffer, buffer_size);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    return ret;
}
static int activate_alert_rdy(struct mq135_module_data *mq135_data)
{
    int ret;
    const int buffer_size = 2;
    char config[2];
    // HIGH THRESH
    config[0] = HI_THRESH_REGISTER;
    config[1] = 0x80;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_send(mq135_data->client, config, buffer_size);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    if (ret < 0)
        return ret;
    // LOW THRESH
    config[0] = LO_THRESH_RWGISTER;
    config[1] = 0x00;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_send(mq135_data->client, config, buffer_size);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    return ret;
}
static uint16_t conversion_running(struct mq135_module_data *mq135_data, int *err)
{
    int ret;
    uint16_t read_config = 0x0000;
    const int buffer_size = 2;
    char buffer[2];
    // Tell the device which register I want to read from
    buffer[0] = CONFIG_REGISTER;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_send(mq135_data->client, buffer, 1);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    if (ret < 0)
    {
        *err = ret;
        return 0;
    }
    buffer[0] = 0;
    buffer[1] = 0;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_recv(mq135_data->client, buffer, buffer_size);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    if (ret < 0)
    {
        *err = ret;
        return 0;
    }
    *err = 0;
    read_config |= (buffer[0] << 8);
    read_config |= buffer[1];
    return (read_config & 0x8000) != 0;
}
static int16_t read_converted_data(struct mq135_module_data *mq135_data, int *err)
{
    int ret, buffer_size = 2;
    int16_t read_data = 0;
    char buffer[2];
    buffer[0] = CONVERSION_REGISTER;
    // Read from conversion register
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_send(mq135_data->client, buffer, 1);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    if (ret < 0)
    {
        *err = ret;
        return read_data;
    }
    buffer[0] = 0;
    buffer[1] = 0;
    mutex_lock(&mq135_data->i2c_client_mutex);
    ret = i2c_master_recv(mq135_data->client, buffer, buffer_size);
    mutex_unlock(&mq135_data->i2c_client_mutex);
    if (ret < 0)
    {
        *err = ret;
        return read_data;
    }
    read_data = (int16_t)(buffer[0] << 8) | buffer[1];
    *err = 0;
    return read_data;
}
static ssize_t mq135_read(struct file *flip, char __user *buf, size_t count, loff_t *off)
{
    int quality, ret, err;
    struct mq135_measurement data = {.air_quality = 0, .read_data = 0};
    struct miscdevice *dev = flip->private_data;
    struct mq135_module_data *mq135_data = dev_get_drvdata(dev->this_device);

    // 1. Configure the device
    ret = write_config(mq135_data);
    if (ret < 0)
    {
        dev_err(&mq135_data->client->adapter->dev, "Error configuring device ADS1115\n");
        goto finally;
    }
    // 2. Activate alert
    ret = activate_alert_rdy(mq135_data);
    if (ret < 0)
    {
        dev_err(&mq135_data->client->adapter->dev, "Could not set alrt/rdy ADS1115\n");
        goto finally;
    }
    // 3. Wait until conversion is ready
    while (conversion_running(mq135_data, &err))
        ;
    if (err < 0)
    {
        ret = err;
        dev_err(&mq135_data->client->adapter->dev, "Error converting data ADS1115\n");
        goto finally;
    }
    // 4. Read output
    quality = (int)read_converted_data(mq135_data, &err);
    if (err)
    {
        ret = err;
        dev_err(&mq135_data->client->adapter->dev, "Could not read quality of air\n");
        goto finally;
    }
    dev_info(&mq135_data->client->adapter->dev, "Current air quality: %d\n", quality);
finally:
    if (ret > 0)
    {
        data.air_quality = quality;
    }
    // 5. Send data
    ret = copy_to_user(buf, &data, sizeof(struct mq135_measurement));
    return ret < 0 ? -EFAULT : 0;
}
// Using the old version since we work with a raspberry pi
static int mq135_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int err;
    struct mq135_module_data *mq135_data;
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EIO;
    err = misc_register(&mq135_device);
    if (err)
    {
        dev_err(&client->adapter->dev, "Could not register device\n");
        return -EIO;
    }
    mq135_data = (struct mq135_module_data *)kzalloc(sizeof(struct mq135_module_data), GFP_KERNEL);
    if (mq135_data == NULL)
    {
        dev_err(&client->adapter->dev, "Could not get memory fo MQ135\n");
        return -ENOMEM;
    }
    mutex_init(&mq135_data->i2c_client_mutex);
    mq135_data->dev = &mq135_device;
    mq135_data->client = client;
    dev_set_drvdata(mq135_device.this_device, mq135_data);
    i2c_set_clientdata(client, mq135_data);
    return 0;
}
static void mq135_remove(struct i2c_client *client)
{
    struct mq135_module_data *mq135_data;
    mq135_data = i2c_get_clientdata(client);
    misc_deregister(mq135_data->dev);
    kfree(mq135_data);
}
static const struct of_device_id mq135_dts_ids[] = {
    {.compatible = "calvarez,mq135"},
    {},
};
static struct i2c_driver mq135_driver = {
    .probe = mq135_probe,
    .remove = mq135_remove,
    .driver = {
        .name = "mq135-driver",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(mq135_dts_ids),
    },
};

MODULE_DEVICE_TABLE(of, mq135_dts_ids);
module_i2c_driver(mq135_driver);
MODULE_AUTHOR("Camila Alvarez<cam.alvarez.i@gmail.com>");
MODULE_LICENSE("GPL");
