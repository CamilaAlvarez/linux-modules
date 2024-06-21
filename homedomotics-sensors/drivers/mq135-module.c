#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#define MODNAME "mq135_module"

struct mq135_module_data
{
    struct miscdevice *dev;
    spinlock_t i2c_client_spinlock;
    struct i2c_client *client;
}

static ssize_t mq135_read(struct file *flip, char __user *buf, size_t count, loff_t *off)
{
    int quality, ret;
    struct miscdevice *dev = file->private_data;
    struct mq135_module_data *mq135_data = container_of(dev, struct mq135_module_data, dev);
    spin_lock(mq135_data->i2c_client_spinlock);
    ret = i2c_master_recv(mq135->client, &quality, sizeof(int));
    spin_unlock(mq135_data->i2c_client_spinlock);
    if (ret < 0)
    {
        dev_err(&mq135_data->client->adapter->dev, "Could not read quality of air\n"); 
    } else 
    {
        dev_info(&mq135_data->client->adapter->dev, "Current air quality: %d\n", quality);
        ret = copy_to_user(buf, &quality, sizeof(int));
    }
    return ret ? -EFAULT : 0; 
}

// Open will set file->private_data to the misc device
static const struct file_operations mq135_ops = {
    .llseek = no_llseek,
    .read = mq135_read,
};
static struct miscdevice mq135_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = MODNAME,
    .mode = 0444,
    .fops = &mq135_ops,
};
static int mq135_probe(struct i2c_client *client)
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
    spin_lock_init(&mq135_data->i2c_client_spinlock);
    mq135_data->dev = &mq135_device;
    mq135_data->client = client;
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
static struct ic2_driver mq135_driver = {
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
