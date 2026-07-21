// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal Google Citadel SPI transport
 *
 * Copyright (C) 2017 Google Inc.
 * Copyright (C) 2026 Sam Day
 *
 * This is the TPM-style datagram portion of the downstream Citadel driver.
 * It intentionally does not expose raw read/write, reset, IRQ, or poll
 * interfaces.
 */

#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/unaligned.h>

#include <linux/citadel.h>

#define CITADEL_TPM_READ		0x80000000U
#define CITADEL_TPM_IDLE_RESPONSE	0xdfdfdfdeU
#define CITADEL_MAX_DATA_SIZE		2044
#define CITADEL_TPM_TIMEOUT_MS		10

struct citadel_data {
	struct spi_device *spi;
	struct miscdevice miscdev;
	/* Serializes access to the transfer buffers and Citadel. */
	struct mutex lock;
	u8 *command_buf;
	u8 *tx_buf;
	u8 *rx_buf;
};

static struct citadel_data *file_to_citadel(struct file *file)
{
	return container_of(file->private_data, struct citadel_data, miscdev);
}

static int citadel_spi_sync_locked(struct citadel_data *citadel,
				   struct spi_transfer *transfer)
{
	struct spi_message message;

	spi_message_init(&message);
	spi_message_add_tail(transfer, &message);

	return spi_sync_locked(citadel->spi, &message);
}

static int citadel_wait_ready_locked(struct citadel_data *citadel)
{
	struct spi_transfer transfer = {
		.rx_buf = citadel->rx_buf,
		.len = 1,
		.cs_change = true,
	};
	unsigned long deadline = jiffies + 1 +
		msecs_to_jiffies(CITADEL_TPM_TIMEOUT_MS);
	int ret;

	for (;;) {
		if (time_after(jiffies, deadline)) {
			dev_warn_ratelimited(&citadel->spi->dev,
					     "SPI ready timeout\n");
			return -EBUSY;
		}

		ret = citadel_spi_sync_locked(citadel, &transfer);
		if (ret)
			return ret;

		if (citadel->rx_buf[0])
			return citadel->rx_buf[0] & 0x01 ? 0 : -EAGAIN;
	}
}

/* Complete a transaction which still has chip select asserted. */
static int citadel_end_transaction_locked(struct citadel_data *citadel)
{
	struct spi_transfer transfer = {
		.rx_buf = citadel->rx_buf,
		.len = 1,
	};

	return citadel_spi_sync_locked(citadel, &transfer);
}

static int citadel_tpm_datagram(struct citadel_data *citadel,
				struct citadel_ioc_tpm_datagram *datagram)
{
	struct spi_device *spi = citadel->spi;
	struct spi_transfer transfer = {
		.tx_buf = citadel->command_buf,
		.rx_buf = citadel->rx_buf,
		.len = sizeof(datagram->command),
		.cs_change = true,
	};
	void __user *user_buf = u64_to_user_ptr(datagram->buf);
	bool is_read = datagram->command & CITADEL_TPM_READ;
	int cleanup_ret;
	int ret;

	if (!is_read && datagram->len &&
	    copy_from_user(citadel->tx_buf, user_buf, datagram->len))
		return -EFAULT;

	put_unaligned_be32(datagram->command, citadel->command_buf);

	spi_bus_lock(spi->controller);

	ret = citadel_spi_sync_locked(citadel, &transfer);
	if (ret)
		goto out_unlock;

	/*
	 * An idle Citadel returns df:df:df:de while the command header is
	 * shifted in. Anything else usually means it was asleep; terminating
	 * this transaction wakes it and userspace can retry after -EAGAIN.
	 */
	if (get_unaligned_be32(citadel->rx_buf) !=
	    CITADEL_TPM_IDLE_RESPONSE) {
		ret = -EAGAIN;
		goto out_end_transaction;
	}

	ret = citadel_wait_ready_locked(citadel);
	if (ret) {
		/* libnos retries only the downstream driver's -EAGAIN result. */
		ret = -EAGAIN;
		goto out_end_transaction;
	}

	if (!datagram->len)
		goto out_end_transaction;

	transfer = (struct spi_transfer) {
		.len = datagram->len,
	};
	if (is_read)
		transfer.rx_buf = citadel->rx_buf;
	else
		transfer.tx_buf = citadel->tx_buf;

	ret = citadel_spi_sync_locked(citadel, &transfer);
	if (ret)
		goto out_unlock;

	spi_bus_unlock(spi->controller);

	if (is_read && copy_to_user(user_buf, citadel->rx_buf, datagram->len))
		return -EFAULT;

	return 0;

out_end_transaction:
	cleanup_ret = citadel_end_transaction_locked(citadel);
	if (cleanup_ret)
		ret = cleanup_ret;
out_unlock:
	spi_bus_unlock(spi->controller);

	return ret;
}

static long citadel_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct citadel_data *citadel = file_to_citadel(file);
	struct citadel_ioc_tpm_datagram datagram;
	long ret;

	if (cmd != CITADEL_IOC_TPM_DATAGRAM)
		return -ENOTTY;

	if (copy_from_user(&datagram, (void __user *)arg, sizeof(datagram)))
		return -EFAULT;

	if (datagram.len > CITADEL_MAX_DATA_SIZE)
		return -E2BIG;

	ret = mutex_lock_interruptible(&citadel->lock);
	if (ret)
		return ret;

	ret = citadel_tpm_datagram(citadel, &datagram);
	mutex_unlock(&citadel->lock);

	return ret;
}

static const struct file_operations citadel_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = citadel_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int citadel_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct citadel_data *citadel;
	int ret;

	citadel = devm_kzalloc(dev, sizeof(*citadel), GFP_KERNEL);
	if (!citadel)
		return -ENOMEM;

	citadel->command_buf = devm_kmalloc(dev, sizeof(u32), GFP_KERNEL);
	citadel->tx_buf = devm_kmalloc(dev, CITADEL_MAX_DATA_SIZE, GFP_KERNEL);
	citadel->rx_buf = devm_kmalloc(dev, CITADEL_MAX_DATA_SIZE, GFP_KERNEL);
	if (!citadel->command_buf || !citadel->tx_buf || !citadel->rx_buf)
		return -ENOMEM;

	citadel->spi = spi;
	citadel->miscdev = (struct miscdevice) {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "citadel0",
		.fops = &citadel_fops,
		.parent = dev,
		.mode = 0600,
	};
	mutex_init(&citadel->lock);
	spi_set_drvdata(spi, citadel);

	ret = misc_register(&citadel->miscdev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register /dev/citadel0\n");

	dev_info(dev, "registered TPM datagram transport as /dev/citadel0\n");

	return 0;
}

static void citadel_remove(struct spi_device *spi)
{
	struct citadel_data *citadel = spi_get_drvdata(spi);

	misc_deregister(&citadel->miscdev);
}

static const struct of_device_id citadel_of_match[] = {
	{ .compatible = "google,citadel" },
	{}
};
MODULE_DEVICE_TABLE(of, citadel_of_match);

static struct spi_driver citadel_driver = {
	.probe = citadel_probe,
	.remove = citadel_remove,
	.driver = {
		.name = "citadel",
		.of_match_table = citadel_of_match,
		.suppress_bind_attrs = true,
	},
};
module_spi_driver(citadel_driver);

MODULE_AUTHOR("Fernando Lugo <flugo@google.com>");
MODULE_DESCRIPTION("Google Citadel TPM datagram transport");
MODULE_LICENSE("GPL");
