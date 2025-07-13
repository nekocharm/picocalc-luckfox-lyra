/*
 * Copyright (C) 2017 starterkit.ru
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/hrtimer.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

/* 
 * DEFAULT_PERIOD/DEFAULT_PWM_PERIOD
 * The MCU Timers' frequency is 24 MHz
 * The PWM's frequency is 64 kHz
 * The Sample rate is 8 kHz, so each sample has 8 cycles
 * DEFAULT_PERIOD = 1 / 8 kHz = 125000 ns
 * DEFAULT_PWM_PERIOD = 24 MHz / 64 kHz = 375
 */
#define DEFAULT_PERIOD (125000)
#define DEFAULT_PWM_PERIOD (375)

struct softpwm_config {
	u32 left_duty; // 0~374, 0 and 375 for stop, use 0 to implement mute
	u32 right_duty;
};

struct softpwm_sound {
    struct platform_device *pdev;
	struct snd_card *snd_card;
	struct snd_pcm_substream *substream;
	struct hrtimer timer;
	spinlock_t lock;
	u32 data_ptr;
	u32 period_ptr;
	u32 channels;
	u32 is_on;
    struct softpwm_config *softpwm;
};

enum hrtimer_restart softpwm_hrtimer_callback(struct hrtimer *t)
{
	struct softpwm_sound *softpwm_snd = container_of(t, struct softpwm_sound, timer);
	u32 buffer_size, period_size;
	u32 period_elapsed = 0;
	u8 *data;

	

	if (!softpwm_snd->is_on)
		return HRTIMER_NORESTART;

	data = softpwm_snd->substream->runtime->dma_area;
	buffer_size = softpwm_snd->substream->runtime->buffer_size;
	period_size = softpwm_snd->substream->runtime->period_size;
	if (++softpwm_snd->data_ptr >= buffer_size)
		softpwm_snd->data_ptr = 0;

	// The audio bit width is 8 bit (0~255)
	// The range of left_duty and right_duty is 0~374
	// We need map 0~255 to 0~374
	// TODO: Stereo channel can't work
	switch (softpwm_snd->channels) {
	case 1: //Mono
		softpwm_snd->softpwm->left_duty = (u32)data[softpwm_snd->data_ptr] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
		softpwm_snd->softpwm->right_duty = (u32)data[softpwm_snd->data_ptr] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
		break;
	case 2: //Stereo
		softpwm_snd->softpwm->left_duty = (u32)data[softpwm_snd->data_ptr] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
		softpwm_snd->softpwm->right_duty = (u32)data[softpwm_snd->data_ptr++] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
		break;
	}

	if (++softpwm_snd->period_ptr >= period_size) {
		softpwm_snd->period_ptr = 0;
		period_elapsed = 1;
	}

	if (period_elapsed)
		snd_pcm_period_elapsed(softpwm_snd->substream);

	hrtimer_forward_now(t, ktime_set(0, DEFAULT_PERIOD));

	return HRTIMER_RESTART;
}

static int softpwm_enable(struct softpwm_sound *softpwm_snd)
{
    softpwm_snd->timer.function = &softpwm_hrtimer_callback;
    hrtimer_start(&softpwm_snd->timer, ktime_set(0, DEFAULT_PERIOD), HRTIMER_MODE_REL);

    return 0;
}

static void softpwm_disable(struct softpwm_sound *softpwm_snd)
{
    softpwm_snd->softpwm->left_duty = 0;
	softpwm_snd->softpwm->right_duty = 0;
}

static int softpwm_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct softpwm_sound *softpwm_snd = snd_pcm_substream_chip(substream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		softpwm_snd->channels = params_channels(hw_params);
		return 0;
	}
    return -EINVAL;
}

static int softpwm_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

static const struct snd_pcm_hardware softpwm_pcm_playback_hw = {
	.info				= (SNDRV_PCM_INFO_MMAP |
						   SNDRV_PCM_INFO_MMAP_VALID |
						   SNDRV_PCM_INFO_INTERLEAVED |
						   SNDRV_PCM_INFO_HALF_DUPLEX),
	.formats			= SNDRV_PCM_FMTBIT_U8,
	.rates				= SNDRV_PCM_RATE_8000,
	.rate_min			= 8000,
	.rate_max			= 8000,
	.channels_min		= 1,
	.channels_max		= 1, //TODO Stereo channel can't work
	.buffer_bytes_max	= 8 * 1024,
	.period_bytes_min	= 4,
	.period_bytes_max	= 4 * 1024,
	.periods_min		= 4,
	.periods_max		= 1024,
};

static int softpwm_pcm_open(struct snd_pcm_substream *substream)
{
	struct softpwm_sound *softpwm_snd = snd_pcm_substream_chip(substream);

	substream->runtime->hw = softpwm_pcm_playback_hw;
	softpwm_snd->substream = substream;
	return 0;
}

static int softpwm_pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int softpwm_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int softpwm_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct softpwm_sound *softpwm_snd = snd_pcm_substream_chip(substream);
	unsigned long flags;
	unsigned char *data;
	int ret = 0;

	spin_lock_irqsave(&softpwm_snd->lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		data = softpwm_snd->substream->runtime->dma_area;

		switch (softpwm_snd->channels) {
		case 1: //Mono
			softpwm_snd->softpwm->left_duty = (u32)data[0] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
			softpwm_snd->softpwm->right_duty = (u32)data[0] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
			softpwm_snd->data_ptr = 0;
			break;
		case 2: //Stereo
			softpwm_snd->softpwm->left_duty = (u32)data[0] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
			softpwm_snd->softpwm->right_duty = (u32)data[1] * (u32)(DEFAULT_PWM_PERIOD - 1) / 255;
			softpwm_snd->data_ptr = 1;
			break;
		}
		softpwm_snd->period_ptr = 0;
		if (softpwm_enable(softpwm_snd) == 0)
			softpwm_snd->is_on = 1;
		else
			ret = -EIO;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (!softpwm_snd->is_on)
			break;
		softpwm_snd->is_on = 0;
		softpwm_snd->softpwm->left_duty = 0;
		softpwm_snd->softpwm->right_duty = 0;
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&softpwm_snd->lock, flags);

	return ret;
}

static snd_pcm_uframes_t softpwm_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct softpwm_sound *softpwm_snd = snd_pcm_substream_chip(substream);
	return softpwm_snd->data_ptr;
}

static struct snd_pcm_ops softpwm_pcm_playback_ops = {
	.open = softpwm_pcm_open,
	.close = softpwm_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = softpwm_pcm_hw_params,
	.hw_free = softpwm_pcm_hw_free,
	.prepare = softpwm_pcm_prepare,
	.trigger = softpwm_pcm_trigger,
	.pointer = softpwm_pcm_pointer,
};

static int softpwm_sound_dev_init(struct softpwm_sound *softpwm_snd)
{
	static const struct snd_device_ops ops = { NULL };
	struct snd_card *card;
	struct snd_pcm *pcm;
	int ret;

	ret = snd_card_new(&softpwm_snd->pdev->dev, SNDRV_DEFAULT_IDX1,
		SNDRV_DEFAULT_STR1, THIS_MODULE, 0, &card);
	if (ret)
		return ret;

    softpwm_snd->snd_card = card;
	strlcpy(card->driver, "softpwm", sizeof(card->driver));
	strlcpy(card->shortname, "softpwm-sound", sizeof(card->shortname));
	strlcpy(card->longname, "Soft PWM Audio", sizeof(card->longname));

	ret = snd_device_new(card, SNDRV_DEV_LOWLEVEL, softpwm_snd, &ops);
	if (ret)
		goto error;

	ret = snd_pcm_new(card, card->driver, 0, 1, 0, &pcm);
	if (ret)
		goto error;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &softpwm_pcm_playback_ops);
	snd_pcm_chip(pcm) = softpwm_snd;
	pcm->info_flags = 0;

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
				&softpwm_snd->pdev->dev, 8 * 1024, 8 * 1024);

	ret = snd_card_register(card);
	if (ret == 0)
		return 0;

error:
	snd_device_free(card, softpwm_snd);
	snd_card_free(card);
	softpwm_snd->snd_card = NULL;
	return ret;
}

static const struct of_device_id softpwm_sound_of_match[] = {
	{ .compatible = "picocalc,softpwm-sound", },
	{},
};
MODULE_DEVICE_TABLE(of, softpwm_sound_of_match);

static int softpwm_sound_probe(struct platform_device *pdev)
{
	struct softpwm_sound *softpwm_snd;
    struct device *dev = &pdev->dev;
	struct reserved_mem *rmem;
	struct device_node *np = dev->of_node;
	struct device_node *shmem_np;
	u32 shmem_offset, shmem_length;	
	volatile void __iomem *shmem_addr;
	int ret;

    softpwm_snd = devm_kzalloc(dev, sizeof(*softpwm_snd), GFP_KERNEL);
	if (softpwm_snd == NULL)
		return -ENOMEM;

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
	softpwm_snd->pdev = pdev;
	softpwm_snd->softpwm = (struct softpwm_config*)(shmem_addr + shmem_offset);

	spin_lock_init(&softpwm_snd->lock);

	ret = softpwm_sound_dev_init(softpwm_snd);
	if (ret)
	{
        dev_err(dev, "softpwm_sound_dev_init failed!\n");
		return ret;
	}

	hrtimer_init(&softpwm_snd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	platform_set_drvdata(pdev, softpwm_snd);
	return 0;
}

static int softpwm_sound_remove(struct platform_device *pdev)
{
	struct softpwm_sound *softpwm_snd = platform_get_drvdata(pdev);
    softpwm_disable(softpwm_snd);
	snd_card_free(softpwm_snd->snd_card);
	softpwm_snd->snd_card = NULL;
	return 0;
}

static struct platform_driver picocalc_snd_pwm_driver = {
	.driver		= {
		.name	= "picocalc-softpwm",
		.of_match_table = softpwm_sound_of_match,
	},
	.probe		= softpwm_sound_probe,
	.remove		= softpwm_sound_remove,
};

module_platform_driver(picocalc_snd_pwm_driver);

MODULE_AUTHOR("nekocharm <jumba.jookiba@outlook.com>");
MODULE_DESCRIPTION("Sound driver for Soft PWM Sound");
MODULE_LICENSE("GPL");
