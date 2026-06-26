// SPDX-License-Identifier: GPL-2.0
#include "mini_pcie_telemetry.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

#define MINI_PCIE_CLASS_NAME "mini_bmc_pcie"
#define MINI_PCIE_DEVICE_NAME "mini_pcie0"
#define MINI_PCIE_LINE_SIZE 256

struct mini_pcie_file_context {
    unsigned long event_sequence;
};

struct mini_pcie_device {
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct mutex lock;
    wait_queue_head_t waitqueue;
    struct timer_list timer;
    struct mini_pcie_telemetry telemetry;
    enum mini_pcie_fault fault;
    unsigned long event_sequence;
};

static struct mini_pcie_device *mini_device;

static const char *fault_name(enum mini_pcie_fault fault)
{
    switch (fault) {
    case MINI_PCIE_FAULT_NONE:
        return "none";
    case MINI_PCIE_FAULT_LINK_DOWN:
        return "link_down";
    case MINI_PCIE_FAULT_LINK_DEGRADED:
        return "link_degraded";
    case MINI_PCIE_FAULT_CORRECTABLE_ERROR_SPIKE:
        return "correctable_error_spike";
    case MINI_PCIE_FAULT_NONFATAL_ERROR:
        return "nonfatal_error";
    case MINI_PCIE_FAULT_TELEMETRY_TIMEOUT:
        return "telemetry_timeout";
    case MINI_PCIE_FAULT_OVER_TEMPERATURE:
        return "over_temperature";
    case MINI_PCIE_FAULT_OVER_POWER:
        return "over_power";
    }
    return "none";
}

static int parse_fault(const char *value, enum mini_pcie_fault *fault)
{
    enum mini_pcie_fault candidate;

    for (candidate = MINI_PCIE_FAULT_NONE;
         candidate <= MINI_PCIE_FAULT_OVER_POWER; candidate++) {
        if (sysfs_streq(value, fault_name(candidate))) {
            *fault = candidate;
            return 0;
        }
    }
    return -EINVAL;
}

static void reset_telemetry(struct mini_pcie_device *device)
{
    strscpy(device->telemetry.device_id, "MiniGPU-0000",
            sizeof(device->telemetry.device_id));
    device->telemetry.link_width = 16;
    strscpy(device->telemetry.link_speed, "16.0GT/s",
            sizeof(device->telemetry.link_speed));
    strscpy(device->telemetry.link_state, "L0",
            sizeof(device->telemetry.link_state));
    device->telemetry.gpu_core_temp_millic = 65000;
    device->telemetry.gpu_power_milliwatt = 250000;
    device->telemetry.correctable_error_count = 0;
    device->telemetry.nonfatal_error_count = 0;
    strscpy(device->telemetry.health, "OK",
            sizeof(device->telemetry.health));
}

static void apply_fault(struct mini_pcie_device *device,
                        enum mini_pcie_fault fault)
{
    reset_telemetry(device);
    device->fault = fault;
    switch (fault) {
    case MINI_PCIE_FAULT_LINK_DOWN:
        strscpy(device->telemetry.link_state, "Down",
                sizeof(device->telemetry.link_state));
        strscpy(device->telemetry.health, "Critical",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_LINK_DEGRADED:
        device->telemetry.link_width = 8;
        strscpy(device->telemetry.health, "Warning",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_CORRECTABLE_ERROR_SPIKE:
        device->telemetry.correctable_error_count += 100;
        strscpy(device->telemetry.health, "Warning",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_NONFATAL_ERROR:
        device->telemetry.nonfatal_error_count++;
        strscpy(device->telemetry.health, "Critical",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_OVER_TEMPERATURE:
        device->telemetry.gpu_core_temp_millic = 95000;
        strscpy(device->telemetry.health, "Critical",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_OVER_POWER:
        device->telemetry.gpu_power_milliwatt = 350000;
        strscpy(device->telemetry.health, "Critical",
                sizeof(device->telemetry.health));
        break;
    case MINI_PCIE_FAULT_NONE:
    case MINI_PCIE_FAULT_TELEMETRY_TIMEOUT:
        break;
    }
    device->event_sequence++;
    wake_up_interruptible(&device->waitqueue);
}

static void mini_pcie_timer(struct timer_list *timer)
{
    struct mini_pcie_device *device =
        from_timer(device, timer, timer);

    mutex_lock(&device->lock);
    if (device->fault == MINI_PCIE_FAULT_CORRECTABLE_ERROR_SPIKE) {
        device->telemetry.correctable_error_count += 100;
        device->event_sequence++;
        wake_up_interruptible(&device->waitqueue);
    }
    mutex_unlock(&device->lock);
    mod_timer(&device->timer, jiffies + HZ);
}

#define DEFINE_TEXT_ATTRIBUTE(name, expression)                              \
    static ssize_t name##_show(struct device *dev,                           \
                               struct device_attribute *attr, char *buf)      \
    {                                                                         \
        struct mini_pcie_device *device = dev_get_drvdata(dev);              \
        ssize_t count;                                                        \
        (void)attr;                                                           \
        mutex_lock(&device->lock);                                            \
        count = sysfs_emit(buf, "%s\n", expression);                         \
        mutex_unlock(&device->lock);                                          \
        return count;                                                         \
    }                                                                         \
    static DEVICE_ATTR_RO(name)

#define DEFINE_U32_ATTRIBUTE(name, expression)                               \
    static ssize_t name##_show(struct device *dev,                           \
                               struct device_attribute *attr, char *buf)      \
    {                                                                         \
        struct mini_pcie_device *device = dev_get_drvdata(dev);              \
        ssize_t count;                                                        \
        (void)attr;                                                           \
        mutex_lock(&device->lock);                                            \
        count = sysfs_emit(buf, "%u\n", expression);                         \
        mutex_unlock(&device->lock);                                          \
        return count;                                                         \
    }                                                                         \
    static DEVICE_ATTR_RO(name)

#define DEFINE_U64_ATTRIBUTE(name, expression)                               \
    static ssize_t name##_show(struct device *dev,                           \
                               struct device_attribute *attr, char *buf)      \
    {                                                                         \
        struct mini_pcie_device *device = dev_get_drvdata(dev);              \
        ssize_t count;                                                        \
        (void)attr;                                                           \
        mutex_lock(&device->lock);                                            \
        count = sysfs_emit(buf, "%llu\n",                                    \
                           (unsigned long long)(expression));                  \
        mutex_unlock(&device->lock);                                          \
        return count;                                                         \
    }                                                                         \
    static DEVICE_ATTR_RO(name)

DEFINE_TEXT_ATTRIBUTE(device_id, device->telemetry.device_id);
DEFINE_U32_ATTRIBUTE(link_width, device->telemetry.link_width);
DEFINE_TEXT_ATTRIBUTE(link_speed, device->telemetry.link_speed);
DEFINE_TEXT_ATTRIBUTE(link_state, device->telemetry.link_state);
DEFINE_U32_ATTRIBUTE(gpu_core_temp_millic,
                     device->telemetry.gpu_core_temp_millic);
DEFINE_U32_ATTRIBUTE(gpu_power_milliwatt,
                     device->telemetry.gpu_power_milliwatt);
DEFINE_U64_ATTRIBUTE(correctable_error_count,
                     device->telemetry.correctable_error_count);
DEFINE_U64_ATTRIBUTE(nonfatal_error_count,
                     device->telemetry.nonfatal_error_count);
DEFINE_TEXT_ATTRIBUTE(health, device->telemetry.health);

static ssize_t fault_mode_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct mini_pcie_device *device = dev_get_drvdata(dev);
    ssize_t count;

    (void)attr;
    mutex_lock(&device->lock);
    count = sysfs_emit(buf, "%s\n", fault_name(device->fault));
    mutex_unlock(&device->lock);
    return count;
}

static ssize_t fault_mode_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct mini_pcie_device *device = dev_get_drvdata(dev);
    enum mini_pcie_fault fault;
    int ret;

    (void)attr;
    if (!count || count > 64)
        return -EINVAL;
    ret = parse_fault(buf, &fault);
    if (ret)
        return ret;
    mutex_lock(&device->lock);
    apply_fault(device, fault);
    mutex_unlock(&device->lock);
    return count;
}
static DEVICE_ATTR_RW(fault_mode);

static struct attribute *mini_pcie_attrs[] = {
    &dev_attr_device_id.attr,
    &dev_attr_link_width.attr,
    &dev_attr_link_speed.attr,
    &dev_attr_link_state.attr,
    &dev_attr_gpu_core_temp_millic.attr,
    &dev_attr_gpu_power_milliwatt.attr,
    &dev_attr_correctable_error_count.attr,
    &dev_attr_nonfatal_error_count.attr,
    &dev_attr_health.attr,
    &dev_attr_fault_mode.attr,
    NULL,
};
ATTRIBUTE_GROUPS(mini_pcie);

static int mini_pcie_open(struct inode *inode, struct file *file)
{
    struct mini_pcie_device *device =
        container_of(inode->i_cdev, struct mini_pcie_device, cdev);
    struct mini_pcie_file_context *context;

    context = kzalloc(sizeof(*context), GFP_KERNEL);
    if (!context)
        return -ENOMEM;
    context->event_sequence = READ_ONCE(device->event_sequence);
    file->private_data = context;
    return 0;
}

static int mini_pcie_release(struct inode *inode, struct file *file)
{
    (void)inode;
    kfree(file->private_data);
    return 0;
}

static ssize_t mini_pcie_read(struct file *file, char __user *buf,
                              size_t count, loff_t *position)
{
    char line[MINI_PCIE_LINE_SIZE];
    int length;

    (void)file;
    mutex_lock(&mini_device->lock);
    if (mini_device->fault == MINI_PCIE_FAULT_TELEMETRY_TIMEOUT) {
        mutex_unlock(&mini_device->lock);
        return -ETIMEDOUT;
    }
    length = scnprintf(
        line, sizeof(line),
        "device=%s temp_millic=%u power_mw=%u link_width=%u "
        "link_speed=%s state=%s health=%s\n",
        mini_device->telemetry.device_id,
        mini_device->telemetry.gpu_core_temp_millic,
        mini_device->telemetry.gpu_power_milliwatt,
        mini_device->telemetry.link_width,
        mini_device->telemetry.link_speed,
        mini_device->telemetry.link_state,
        mini_device->telemetry.health);
    mutex_unlock(&mini_device->lock);
    return simple_read_from_buffer(buf, count, position, line, length);
}

static long mini_pcie_ioctl(struct file *file, unsigned int command,
                            unsigned long argument)
{
    enum mini_pcie_fault fault;
    int ret = 0;

    (void)file;
    mutex_lock(&mini_device->lock);
    switch (command) {
    case MINI_PCIE_GET_TELEMETRY:
        if (mini_device->fault == MINI_PCIE_FAULT_TELEMETRY_TIMEOUT) {
            ret = -ETIMEDOUT;
            break;
        }
        if (copy_to_user((void __user *)argument, &mini_device->telemetry,
                         sizeof(mini_device->telemetry)))
            ret = -EFAULT;
        break;
    case MINI_PCIE_SET_FAULT:
        if (copy_from_user(&fault, (void __user *)argument, sizeof(fault))) {
            ret = -EFAULT;
            break;
        }
        if (fault < MINI_PCIE_FAULT_NONE ||
            fault > MINI_PCIE_FAULT_OVER_POWER) {
            ret = -EINVAL;
            break;
        }
        apply_fault(mini_device, fault);
        break;
    case MINI_PCIE_CLEAR_FAULT:
        apply_fault(mini_device, MINI_PCIE_FAULT_NONE);
        break;
    case MINI_PCIE_INJECT_EVENT:
        mini_device->event_sequence++;
        wake_up_interruptible(&mini_device->waitqueue);
        break;
    default:
        ret = -ENOTTY;
    }
    mutex_unlock(&mini_device->lock);
    return ret;
}

static __poll_t mini_pcie_poll(struct file *file, poll_table *wait)
{
    struct mini_pcie_file_context *context = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &mini_device->waitqueue, wait);
    if (context->event_sequence != READ_ONCE(mini_device->event_sequence)) {
        context->event_sequence = READ_ONCE(mini_device->event_sequence);
        mask |= EPOLLIN | EPOLLRDNORM | EPOLLPRI;
    }
    return mask;
}

static const struct file_operations mini_pcie_fops = {
    .owner = THIS_MODULE,
    .open = mini_pcie_open,
    .release = mini_pcie_release,
    .read = mini_pcie_read,
    .unlocked_ioctl = mini_pcie_ioctl,
    .poll = mini_pcie_poll,
    .llseek = no_llseek,
};

static int __init mini_pcie_init(void)
{
    int ret;

    mini_device = kzalloc(sizeof(*mini_device), GFP_KERNEL);
    if (!mini_device)
        return -ENOMEM;
    mutex_init(&mini_device->lock);
    init_waitqueue_head(&mini_device->waitqueue);
    reset_telemetry(mini_device);

    ret = alloc_chrdev_region(&mini_device->devt, 0, 1,
                              MINI_PCIE_DEVICE_NAME);
    if (ret)
        goto free_device;
    cdev_init(&mini_device->cdev, &mini_pcie_fops);
    mini_device->cdev.owner = THIS_MODULE;
    ret = cdev_add(&mini_device->cdev, mini_device->devt, 1);
    if (ret)
        goto unregister_region;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    mini_device->class = class_create(MINI_PCIE_CLASS_NAME);
#else
    mini_device->class = class_create(THIS_MODULE, MINI_PCIE_CLASS_NAME);
#endif
    if (IS_ERR(mini_device->class)) {
        ret = PTR_ERR(mini_device->class);
        goto delete_cdev;
    }
    mini_device->device = device_create_with_groups(
        mini_device->class, NULL, mini_device->devt, mini_device,
        mini_pcie_groups, MINI_PCIE_DEVICE_NAME);
    if (IS_ERR(mini_device->device)) {
        ret = PTR_ERR(mini_device->device);
        goto destroy_class;
    }
    timer_setup(&mini_device->timer, mini_pcie_timer, 0);
    mod_timer(&mini_device->timer, jiffies + HZ);
    pr_info("mini_pcie_telemetry: loaded\n");
    return 0;

destroy_class:
    class_destroy(mini_device->class);
delete_cdev:
    cdev_del(&mini_device->cdev);
unregister_region:
    unregister_chrdev_region(mini_device->devt, 1);
free_device:
    kfree(mini_device);
    mini_device = NULL;
    return ret;
}

static void __exit mini_pcie_exit(void)
{
    del_timer_sync(&mini_device->timer);
    device_destroy(mini_device->class, mini_device->devt);
    class_destroy(mini_device->class);
    cdev_del(&mini_device->cdev);
    unregister_chrdev_region(mini_device->devt, 1);
    kfree(mini_device);
    mini_device = NULL;
    pr_info("mini_pcie_telemetry: unloaded\n");
}

module_init(mini_pcie_init);
module_exit(mini_pcie_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mini OpenBMC Platform Service");
MODULE_DESCRIPTION("Synthetic PCIe telemetry provider for Mini OpenBMC");
