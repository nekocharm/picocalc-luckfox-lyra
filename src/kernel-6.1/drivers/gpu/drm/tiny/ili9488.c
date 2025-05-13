// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for Ilitek ILI9488 panels
 *
 * Copyright 2025 nekocharm <jumba.jookiba@outlook.com>
 *
 * Some code copied from ili9486.c
 * Copyright 2020 Kamlesh Gurudasani <kamlesh.gurudasani@gmail.com>
 */

 #include <linux/backlight.h>
 #include <linux/delay.h>
 #include <linux/gpio/consumer.h>
 #include <linux/module.h>
 #include <linux/property.h>
 #include <linux/spi/spi.h>
 
 #include <video/mipi_display.h>
 
 #include <drm/drm_atomic_helper.h>
 #include <drm/drm_damage_helper.h>
 #include <drm/drm_drv.h>
 #include <drm/drm_framebuffer.h>
 #include <drm/drm_fb_helper.h>
 #include <drm/drm_format_helper.h>
 #include <drm/drm_gem_framebuffer_helper.h>
 #include <drm/drm_gem_atomic_helper.h>
 #include <drm/drm_gem_dma_helper.h>
 #include <drm/drm_managed.h>
 #include <drm/drm_mipi_dbi.h>
 #include <drm/drm_modeset_helper.h>

#define ILI9488_POSITIVE_GAMMA_CTRL		0xE0
#define ILI9488_NEGATIVE_GAMMA_CTRL		0xE1
#define ILI9488_POWER_CTRL_1			0xC0
#define ILI9488_POWER_CTRL_2			0xC1
#define ILI9488_VCOM_CTRL				0xC5
#define ILI9488_MEMORY_ACCESS_CTRL		0x36
#define ILI9488_PIXEL_INTERFACE_FORMAT	0x3A
#define ILI9488_INTERFACE_MODE_CTRL		0xB0
#define ILI9488_FRAME_RATE_CTRL			0xB1
#define ILI9488_DISPLAY_INVERSION_ON	0x21
#define ILI9488_DISPLAY_INVERSION_CTRL	0xB4
#define ILI9488_DISPLAY_FUNCTION_CTRL	0xB6
#define ILI9488_ENTRY_MODE_SET			0xB7
#define ILI9488_ADJUST_CTRL_3			0xF7
#define ILI9488_SLEEP_OUT				0x11
#define ILI9488_DISPLAY_ON				0x29

static int dummy_backlight_update_status(struct backlight_device *bd)
{
	return 0;
}

static const struct backlight_ops dummy_backlight_ops = {
	.update_status = dummy_backlight_update_status,
};

static void ili9488_enable(struct drm_simple_display_pipe *pipe,
			     struct drm_crtc_state *crtc_state,
			     struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int ret, idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_conditional_reset(dbidev);
	if (ret < 0)
		goto out_exit;

	mipi_dbi_command(dbi, ILI9488_POSITIVE_GAMMA_CTRL,
					 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A,
					 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 
					 0x16, 0x1A, 0x0F);
	mipi_dbi_command(dbi, ILI9488_NEGATIVE_GAMMA_CTRL,
					 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05,
					 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D,
					 0x35, 0x37, 0x0F);
	mipi_dbi_command(dbi, ILI9488_POWER_CTRL_1, 0x17, 0x15);
	mipi_dbi_command(dbi, ILI9488_POWER_CTRL_2, 0x41);
	mipi_dbi_command(dbi, ILI9488_VCOM_CTRL, 0x00, 0x12, 0x80);
	mipi_dbi_command(dbi, ILI9488_MEMORY_ACCESS_CTRL, 0x48);
	mipi_dbi_command(dbi, ILI9488_PIXEL_INTERFACE_FORMAT, 0x55);
	mipi_dbi_command(dbi, ILI9488_INTERFACE_MODE_CTRL, 0x00);
	mipi_dbi_command(dbi, ILI9488_FRAME_RATE_CTRL, 0xA0);
	mipi_dbi_command(dbi, ILI9488_DISPLAY_INVERSION_ON);
	mipi_dbi_command(dbi, ILI9488_DISPLAY_INVERSION_CTRL, 0x02);
	mipi_dbi_command(dbi, ILI9488_DISPLAY_FUNCTION_CTRL, 0x02, 0x02, 0x3B);
	mipi_dbi_command(dbi, ILI9488_ENTRY_MODE_SET, 0xC6, 0xE9, 0x00);
	mipi_dbi_command(dbi, ILI9488_ADJUST_CTRL_3, 0xA9, 0x51, 0x2C, 0x82);
	mipi_dbi_command(dbi, ILI9488_SLEEP_OUT);
	msleep(100);
	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
	msleep(50);
	mipi_dbi_command(dbi, ILI9488_DISPLAY_ON);

out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs ili9488_pipe_funcs = {
	.mode_valid = mipi_dbi_pipe_mode_valid,
	.enable = ili9488_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update
};

static const struct drm_display_mode ili9488_mode = {
	DRM_SIMPLE_MODE(320, 320, 49, 49),
};

DEFINE_DRM_GEM_DMA_FOPS(ili9488_fops);

static const struct drm_driver ili9488_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9488_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9488",
	.desc			= "ilitek iLi9488",
	.date			= "20250501",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id ili9488_of_match[] = {
	{ .compatible = "ilitek,ili9488" },
	{ .compatible = "picocalc,spilcd" },
	{},
};
MODULE_DEVICE_TABLE(of, ili9488_of_match);

static const struct spi_device_id ili9488_id[] = {
	{ "ili9488", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9488_id);

static int ili9488_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	struct backlight_properties props;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &ili9488_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc), "Failed to get GPIO 'dc'\n");

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
	{
		memset(&props, 0, sizeof(props));
		props.type = BACKLIGHT_RAW;
		props.max_brightness = 1;
		dbidev->backlight = devm_backlight_device_register(dev, "dummy-backlight",
										dev, NULL, &dummy_backlight_ops, &props);
		if (IS_ERR(dbidev->backlight))
			return dev_err_probe(dev, PTR_ERR(dbidev->backlight), "Failed to register dummy-backlight\n");
	}

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init(dbidev, &ili9488_pipe_funcs,
		&ili9488_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);
	return 0;
}

static void ili9488_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void ili9488_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9488_spi_driver = {
	.driver = {
		.name = "ili9488",
		.of_match_table = ili9488_of_match,
	},
	.id_table = ili9488_id,
	.probe = ili9488_probe,
	.remove = ili9488_remove,
	.shutdown = ili9488_shutdown,
};
module_spi_driver(ili9488_spi_driver);

MODULE_DESCRIPTION("Ilitek ILI9488 DRM driver");
MODULE_AUTHOR("nekocharm <jumba.jookiba@outlook.com>");
MODULE_LICENSE("GPL");
