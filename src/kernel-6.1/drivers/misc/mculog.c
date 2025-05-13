// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for mculog
 *
 * Copyright 2025 nekocharm <jumba.jookiba@outlook.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/io.h>


struct mculog_buffer {
	u32 init_flag;
	u32 max_size;
	size_t head;
    size_t tail;
	u8 buf[0];
};
struct mculog_buffer *logbuf;

static int mculog_open(struct inode *inode, struct file *file)
{
	return 0;
}
 
static int mculog_release(struct inode *inode, struct file *file)
{
	return 0;
}
 
static ssize_t mculog_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	DEFINE_WAIT(wait);
    size_t avail, head;
	size_t len, to_end;
	int ret = 0;

    while (1) {
		head = logbuf->head; // ensure head atomicity
        avail = (head >= logbuf->tail) ? (head - logbuf->tail) : (logbuf->max_size - logbuf->tail + head);
        if (avail > 0) {
            break;
        }
		return 0;
    }

    len = min(count, avail);
    to_end = logbuf->max_size - logbuf->tail;

    if (len > to_end) {
        if (copy_to_user(buf, logbuf->buf + logbuf->tail, to_end)) {
            ret = -EFAULT;
            goto out;
        }
        if (copy_to_user(buf + to_end, logbuf->buf, len - to_end)) {
            ret = -EFAULT;
            goto out;
        }
    } else {
        if (copy_to_user(buf, logbuf->buf + logbuf->tail, len)) {
            ret = -EFAULT;
            goto out;
        }
    }

    logbuf->tail = (logbuf->tail + len) % logbuf->max_size;
    ret = len;
out:
    return ret;
}
 
static const struct file_operations mculog_fops = {
	.owner          = THIS_MODULE,
	.open           = mculog_open,
	.release        = mculog_release,
	.read           = mculog_read,
};
 
static struct miscdevice mculog_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "log_mcu",
	.fops = &mculog_fops,
	.mode = 0444,
};

static const struct of_device_id mculog_of_match[] = {
	{ .compatible = "picocalc,mculog" },
	{},
};
MODULE_DEVICE_TABLE(of, log_mcu_of_match);

static int __init mculog_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reserved_mem *rmem;
	struct device_node *np = dev->of_node;
	struct device_node *shmem_np;
	u32 shmem_offset, shmem_length;	
	volatile void __iomem *shmem_addr;
	int ret, timeout;

	if (!np) {
        dev_err(dev, "No device tree node found\n");
        return -ENODEV;
    }

	shmem_np = of_parse_phandle(np, "memory-region", 0);
	if (!shmem_np)
		return -EINVAL;
	rmem = of_reserved_mem_lookup(shmem_np);
	of_node_put(shmem_np);
	if (!rmem) {
		dev_err(dev, "Unable to acquire memory-region\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "shmem-offset", &shmem_offset)) {
    	dev_err(dev, "Failed to read 'shmem-offset' property\n");
    	return -EINVAL;
	}
	if (of_property_read_u32(np, "shmem-length", &shmem_length)) {
    	dev_err(dev, "Failed to read 'shmem-length' property\n");
    	return -EINVAL;
	}
	if (shmem_length > rmem->size - shmem_offset) {
		dev_err(dev, "Share memory is too small\n");
    	return -EINVAL;
	}

	shmem_addr = devm_memremap(dev, rmem->base, rmem->size, MEMREMAP_WC);
	logbuf = (struct mculog_buffer*)(shmem_addr + shmem_offset);
	//logbuf = (struct mculog_buffer*)(shmem_addr + 32);

	logbuf->max_size = shmem_length - sizeof(struct mculog_buffer);
	logbuf->head = 0;
	logbuf->tail = 0;

	logbuf->init_flag = 0x4D43554C;	// MCUL(MCULOG)
	wmb();
	while(logbuf->init_flag != 0x554C4F47){	// ULOG(MCULOG)
		msleep(1);
		if(timeout++ > 100) {
			dev_err(dev, "Failed to handshake with the M0 core\n");
			return -ENODEV;
		}
	}

	ret = misc_register(&mculog_misc_device);
    if (ret) {
        dev_err(dev, "Failed to register misc device\n");
        return ret;
    }

	return 0;
}

static int mculog_remove(struct platform_device *pdev)
{
	misc_deregister(&mculog_misc_device);
    return 0;
}

static struct platform_driver mculog_driver = {
    .probe = mculog_probe,
    .remove = mculog_remove,
    .driver = {
        .name = "mculog_driver",
        .of_match_table = mculog_of_match,
        .owner = THIS_MODULE,
    },
};
module_platform_driver(mculog_driver);

MODULE_DESCRIPTION("mculog driver");
MODULE_AUTHOR("nekocharm <jumba.jookiba@outlook.com>");
MODULE_LICENSE("GPL");
