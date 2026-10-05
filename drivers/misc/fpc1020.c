// SPDX-License-Identifier: GPL-2.0-only
/*
 * Electrical companion for the Google Sargo fingerprint trusted application.
 * Reset timing and IRQ wiring follow Google's Android fpc1020_platform_tee
 * driver, Copyright (c) 2015 Fingerprint Cards AB. Sensor SPI and biometric
 * operations belong to the trusted application, not this driver.
 */

#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/poll.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fpc1020.h>

struct fpc1020 {
	struct miscdevice misc;
	struct device *dev;
	struct gpio_desc *reset;
	struct gpio_desc *irq_gpio;
	struct mutex lock; /* Serializes removal and hardware access. */
	struct kref ref;
	wait_queue_head_t wait;
	atomic64_t sequence;
	const char *firmware_name;
	bool opened;
	bool removed;
};

struct fpc1020_file {
	struct fpc1020 *fpc;
	struct mutex read_lock; /* Serializes reads on a shared file descriptor. */
	u64 seen;
};

static void fpc1020_free(struct kref *ref)
{
	struct fpc1020 *fpc = container_of(ref, struct fpc1020, ref);

	mutex_destroy(&fpc->lock);
	kfree(fpc);
}

static void fpc1020_put(void *data)
{
	struct fpc1020 *fpc = data;

	kref_put(&fpc->ref, fpc1020_free);
}

static void fpc1020_reset(struct fpc1020 *fpc)
{
	lockdep_assert_held(&fpc->lock);
	/* reset-gpios is active low: logical 1 asserts the reset line. */
	gpiod_set_value_cansleep(fpc->reset, 0);
	usleep_range(100, 200);
	gpiod_set_value_cansleep(fpc->reset, 1);
	usleep_range(5000, 5100);
	gpiod_set_value_cansleep(fpc->reset, 0);
	usleep_range(5000, 5100);
}

static irqreturn_t fpc1020_irq(int irq, void *data)
{
	struct fpc1020 *fpc = data;

	atomic64_inc(&fpc->sequence);
	if (device_may_wakeup(fpc->dev))
		pm_wakeup_event(fpc->dev, 1000);
	wake_up_interruptible(&fpc->wait);
	return IRQ_HANDLED;
}

static int fpc1020_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct fpc1020 *fpc = container_of(misc, struct fpc1020, misc);
	struct fpc1020_file *ctx;
	int ret = 0;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&fpc->lock);
	if (fpc->removed) {
		ret = -ENODEV;
	} else if (fpc->opened) {
		ret = -EBUSY;
	} else {
		fpc->opened = true;
		kref_get(&fpc->ref);
		ctx->fpc = fpc;
		ctx->seen = atomic64_read(&fpc->sequence);
		mutex_init(&ctx->read_lock);
		file->private_data = ctx;
	}
	mutex_unlock(&fpc->lock);
	if (ret)
		kfree(ctx);
	else
		nonseekable_open(inode, file);
	return ret;
}

static int fpc1020_release(struct inode *inode, struct file *file)
{
	struct fpc1020_file *ctx = file->private_data;
	struct fpc1020 *fpc = ctx->fpc;

	mutex_lock(&fpc->lock);
	if (!fpc->removed)
		device_set_wakeup_enable(fpc->dev, false);
	fpc->opened = false;
	mutex_unlock(&fpc->lock);
	kref_put(&fpc->ref, fpc1020_free);
	mutex_destroy(&ctx->read_lock);
	kfree(ctx);
	return 0;
}

static bool fpc1020_ready(struct fpc1020_file *ctx)
{
	return READ_ONCE(ctx->fpc->removed) ||
	       atomic64_read(&ctx->fpc->sequence) != READ_ONCE(ctx->seen);
}

static ssize_t fpc1020_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct fpc1020_file *ctx = file->private_data;
	struct fpc1020 *fpc = ctx->fpc;
	struct fpc1020_irq_event event = {};
	int ret;

	if (count < sizeof(event))
		return -EINVAL;
	if (mutex_lock_interruptible(&ctx->read_lock))
		return -ERESTARTSYS;

	if (!fpc1020_ready(ctx)) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}
		ret = wait_event_interruptible(fpc->wait, fpc1020_ready(ctx));
		if (ret)
			goto out;
	}

	mutex_lock(&fpc->lock);
	if (fpc->removed) {
		ret = -ENODEV;
		goto out_unlock;
	}
	event.sequence = atomic64_read(&fpc->sequence);
	ret = gpiod_get_value_cansleep(fpc->irq_gpio);
	if (ret < 0)
		goto out_unlock;
	event.level = ret;
	ret = copy_to_user(buf, &event, sizeof(event)) ? -EFAULT : sizeof(event);
	if (ret > 0)
		WRITE_ONCE(ctx->seen, event.sequence);
out_unlock:
	mutex_unlock(&fpc->lock);
out:
	mutex_unlock(&ctx->read_lock);
	return ret;
}

static __poll_t fpc1020_poll(struct file *file, poll_table *wait)
{
	struct fpc1020_file *ctx = file->private_data;

	poll_wait(file, &ctx->fpc->wait, wait);
	if (READ_ONCE(ctx->fpc->removed))
		return EPOLLERR | EPOLLHUP;
	if (fpc1020_ready(ctx))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static long fpc1020_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fpc1020_file *ctx = file->private_data;
	struct fpc1020 *fpc = ctx->fpc;
	struct fpc1020_irq_event event = {};
	u32 enabled;
	int ret = 0;

	mutex_lock(&fpc->lock);
	if (fpc->removed) {
		ret = -ENODEV;
		goto out;
	}
	switch (cmd) {
	case FPC1020_IOC_GET_IRQ:
		event.sequence = atomic64_read(&fpc->sequence);
		ret = gpiod_get_value_cansleep(fpc->irq_gpio);
		if (ret < 0)
			break;
		event.level = ret;
		ret = copy_to_user((void __user *)arg, &event, sizeof(event)) ?
		      -EFAULT : 0;
		break;
	case FPC1020_IOC_RESET:
		fpc1020_reset(fpc);
		break;
	case FPC1020_IOC_SET_WAKEUP:
		if (copy_from_user(&enabled, (void __user *)arg, sizeof(enabled)))
			ret = -EFAULT;
		else if (enabled > 1)
			ret = -EINVAL;
		else
			ret = device_set_wakeup_enable(fpc->dev, enabled);
		break;
	default:
		ret = -ENOTTY;
	}
out:
	mutex_unlock(&fpc->lock);
	return ret;
}

static const struct file_operations fpc1020_fops = {
	.owner = THIS_MODULE,
	.open = fpc1020_open,
	.release = fpc1020_release,
	.read = fpc1020_read,
	.poll = fpc1020_poll,
	.unlocked_ioctl = fpc1020_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static ssize_t firmware_name_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct fpc1020 *fpc = container_of(misc, struct fpc1020, misc);

	return sysfs_emit(buf, "%s\n", fpc->firmware_name);
}
static DEVICE_ATTR_RO(firmware_name);

static struct attribute *fpc1020_attrs[] = {
	&dev_attr_firmware_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpc1020);

static void fpc1020_clear_wakeup(void *data)
{
	struct device *dev = data;

	dev_pm_clear_wake_irq(dev);
	device_init_wakeup(dev, false);
}

static int fpc1020_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fpc1020 *fpc;
	int irq, ret;

	fpc = kzalloc_obj(*fpc);
	if (!fpc)
		return -ENOMEM;
	mutex_init(&fpc->lock);
	kref_init(&fpc->ref);
	/* Registered first so GPIO/IRQ resources are released before this ref. */
	ret = devm_add_action_or_reset(dev, fpc1020_put, fpc);
	if (ret)
		return ret;
	fpc->dev = dev;
	init_waitqueue_head(&fpc->wait);
	atomic64_set(&fpc->sequence, 0);
	fpc->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(fpc->reset))
		return dev_err_probe(dev, PTR_ERR(fpc->reset), "reset GPIO\n");
	fpc->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(fpc->irq_gpio))
		return dev_err_probe(dev, PTR_ERR(fpc->irq_gpio), "IRQ GPIO\n");
	ret = device_property_read_string(dev, "firmware-name", &fpc->firmware_name);
	if (ret)
		return ret;
	irq = gpiod_to_irq(fpc->irq_gpio);
	if (irq < 0)
		return irq;
	ret = devm_request_threaded_irq(dev, irq, NULL, fpc1020_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(dev), fpc);
	if (ret)
		return dev_err_probe(dev, ret, "request IRQ\n");
	device_set_wakeup_capable(dev, true);
	ret = dev_pm_set_wake_irq(dev, irq);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, fpc1020_clear_wakeup, dev);
	if (ret)
		return ret;
	/* Stay reset until userspace deliberately starts the trusted app. */
	fpc->misc.minor = MISC_DYNAMIC_MINOR;
	fpc->misc.name = "fpc1020";
	fpc->misc.fops = &fpc1020_fops;
	fpc->misc.parent = dev;
	fpc->misc.mode = 0600;
	fpc->misc.groups = fpc1020_groups;
	platform_set_drvdata(pdev, fpc);
	return misc_register(&fpc->misc);
}

static void fpc1020_remove(struct platform_device *pdev)
{
	struct fpc1020 *fpc = platform_get_drvdata(pdev);

	misc_deregister(&fpc->misc);
	mutex_lock(&fpc->lock);
	WRITE_ONCE(fpc->removed, true);
	device_set_wakeup_enable(fpc->dev, false);
	gpiod_set_value_cansleep(fpc->reset, 1);
	mutex_unlock(&fpc->lock);
	wake_up_interruptible(&fpc->wait);
}

static const struct of_device_id fpc1020_of_match[] = {
	{ .compatible = "google,sargo-fingerprint" },
	{}
};
MODULE_DEVICE_TABLE(of, fpc1020_of_match);

static struct platform_driver fpc1020_driver = {
	.probe = fpc1020_probe,
	.remove = fpc1020_remove,
	.driver = {
		.name = "fpc1020",
		.of_match_table = fpc1020_of_match,
	},
};
module_platform_driver(fpc1020_driver);

MODULE_DESCRIPTION("Google Sargo fingerprint reset and IRQ companion");
MODULE_LICENSE("GPL");
