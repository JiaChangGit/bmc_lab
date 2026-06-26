// SPDX-License-Identifier: GPL-2.0
#include "mini_i2c_hwmon.h"

#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/string.h>

struct mini_i2c_data {
    struct mutex lock;
    enum mini_i2c_fault fault;
    long temperature;
    long voltage;
    long fan;
};

static struct platform_device *mini_i2c_platform_device;

static const char *fault_name(enum mini_i2c_fault fault)
{
    switch (fault) {
    case MINI_I2C_FAULT_NONE:
        return "none";
    case MINI_I2C_FAULT_READ_TIMEOUT:
        return "read_timeout";
    case MINI_I2C_FAULT_DEVICE_DISAPPEARED:
        return "device_disappeared";
    case MINI_I2C_FAULT_STUCK_VALUE:
        return "stuck_value";
    case MINI_I2C_FAULT_OUT_OF_RANGE:
        return "out_of_range";
    case MINI_I2C_FAULT_INVALID_READING:
        return "invalid_reading";
    }
    return "none";
}

static int parse_fault(const char *value, enum mini_i2c_fault *fault)
{
    enum mini_i2c_fault candidate;

    for (candidate = MINI_I2C_FAULT_NONE;
         candidate <= MINI_I2C_FAULT_INVALID_READING; candidate++) {
        if (sysfs_streq(value, fault_name(candidate))) {
            *fault = candidate;
            return 0;
        }
    }
    return -EINVAL;
}

static int fault_error(enum mini_i2c_fault fault)
{
    if (fault == MINI_I2C_FAULT_READ_TIMEOUT)
        return -ETIMEDOUT;
    if (fault == MINI_I2C_FAULT_DEVICE_DISAPPEARED)
        return -ENODEV;
    return 0;
}

static long reading_value(struct mini_i2c_data *data, long normal,
                          long out_of_range, long invalid)
{
    if (data->fault == MINI_I2C_FAULT_OUT_OF_RANGE)
        return out_of_range;
    if (data->fault == MINI_I2C_FAULT_INVALID_READING)
        return invalid;
    return normal;
}

#define DEFINE_READING_ATTRIBUTE(name, normal_field, out_value, invalid_value) \
    static ssize_t name##_show(struct device *dev,                            \
                               struct device_attribute *attr, char *buf)       \
    {                                                                          \
        struct mini_i2c_data *data = dev_get_drvdata(dev);                     \
        long value;                                                            \
        int error;                                                             \
        (void)attr;                                                            \
        mutex_lock(&data->lock);                                               \
        error = fault_error(data->fault);                                      \
        value = reading_value(data, data->normal_field, out_value,             \
                              invalid_value);                                  \
        mutex_unlock(&data->lock);                                             \
        if (error)                                                             \
            return error;                                                      \
        return sysfs_emit(buf, "%ld\n", value);                               \
    }                                                                          \
    static DEVICE_ATTR_RO(name)

DEFINE_READING_ATTRIBUTE(temp1_input, temperature, 200000, -999999);
DEFINE_READING_ATTRIBUTE(in1_input, voltage, 50000, -1);
DEFINE_READING_ATTRIBUTE(fan1_input, fan, 2000000, -1);

static ssize_t temp1_label_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    (void)dev;
    (void)attr;
    return sysfs_emit(buf, "CPU Board Temp\n");
}
static DEVICE_ATTR_RO(temp1_label);

static ssize_t in1_label_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    (void)dev;
    (void)attr;
    return sysfs_emit(buf, "Board Voltage\n");
}
static DEVICE_ATTR_RO(in1_label);

static ssize_t fan1_label_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    (void)dev;
    (void)attr;
    return sysfs_emit(buf, "Fan0 Tach\n");
}
static DEVICE_ATTR_RO(fan1_label);

static ssize_t fault_mode_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct mini_i2c_data *data = dev_get_drvdata(dev);
    ssize_t count;

    (void)attr;
    mutex_lock(&data->lock);
    count = sysfs_emit(buf, "%s\n", fault_name(data->fault));
    mutex_unlock(&data->lock);
    return count;
}

static ssize_t fault_mode_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct mini_i2c_data *data = dev_get_drvdata(dev);
    enum mini_i2c_fault fault;
    int ret;

    (void)attr;
    if (!count || count > 64)
        return -EINVAL;
    ret = parse_fault(buf, &fault);
    if (ret)
        return ret;
    mutex_lock(&data->lock);
    data->fault = fault;
    mutex_unlock(&data->lock);
    return count;
}
static DEVICE_ATTR_RW(fault_mode);

static struct attribute *mini_i2c_attributes[] = {
    &dev_attr_temp1_input.attr,
    &dev_attr_temp1_label.attr,
    &dev_attr_in1_input.attr,
    &dev_attr_in1_label.attr,
    &dev_attr_fan1_input.attr,
    &dev_attr_fan1_label.attr,
    &dev_attr_fault_mode.attr,
    NULL,
};

static const struct attribute_group mini_i2c_group = {
    .attrs = mini_i2c_attributes,
};

static const struct attribute_group *mini_i2c_groups[] = {
    &mini_i2c_group,
    NULL,
};

static int mini_i2c_probe(struct platform_device *platform)
{
    struct mini_i2c_data *data;
    struct device *hwmon;

    data = devm_kzalloc(&platform->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    mutex_init(&data->lock);
    data->temperature = 42000;
    data->voltage = 12000;
    data->fan = 8000;
    platform_set_drvdata(platform, data);

    hwmon = devm_hwmon_device_register_with_groups(
        &platform->dev, "mini_i2c_hwmon", data, mini_i2c_groups);
    return PTR_ERR_OR_ZERO(hwmon);
}

static struct platform_driver mini_i2c_driver = {
    .probe = mini_i2c_probe,
    .driver = {
        .name = "mini_i2c_hwmon",
    },
};

static int __init mini_i2c_init(void)
{
    int ret;

    ret = platform_driver_register(&mini_i2c_driver);
    if (ret)
        return ret;
    mini_i2c_platform_device =
        platform_device_register_simple("mini_i2c_hwmon", -1, NULL, 0);
    if (IS_ERR(mini_i2c_platform_device)) {
        ret = PTR_ERR(mini_i2c_platform_device);
        platform_driver_unregister(&mini_i2c_driver);
        return ret;
    }
    pr_info("mini_i2c_hwmon: loaded\n");
    return 0;
}

static void __exit mini_i2c_exit(void)
{
    platform_device_unregister(mini_i2c_platform_device);
    platform_driver_unregister(&mini_i2c_driver);
    pr_info("mini_i2c_hwmon: unloaded\n");
}

module_init(mini_i2c_init);
module_exit(mini_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mini OpenBMC Platform Service");
MODULE_DESCRIPTION("Synthetic hwmon telemetry provider for Mini OpenBMC");
