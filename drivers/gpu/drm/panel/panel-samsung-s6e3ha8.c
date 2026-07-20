// SPDX-License-Identifier: GPL-2.0-only
//
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//	Copyright (c) 2013, The Linux Foundation. All rights reserved.
// Copyright (c) 2024 Dzmitry Sankouski <dsankouski@gmail.com>

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_panel.h>

struct s6e3ha8_panel_desc {
	const struct drm_panel_funcs *panel_funcs;
	const struct regulator_bulk_data *supplies;
	unsigned int num_supplies;
	unsigned long mode_flags;
	u16 slice_width;
	u16 slice_height;
	u8 bits_per_component;
	u8 bits_per_pixel;
	bool has_backlight;
};

struct s6e3ha8 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data s6e3ha8_supplies[] = {
	{ .supply = "vdd3" },
	{ .supply = "vci" },
	{ .supply = "vddr" },
};

static const struct regulator_bulk_data s6e3ha8_crosshatch_supplies[] = {
	{ .supply = "vddi", .init_load_uA = 100000 },
	{ .supply = "vci", .init_load_uA = 100000 },
};

static inline struct s6e3ha8 *to_s6e3ha8(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3ha8, panel);
}

#define s6e3ha8_test_key_on_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a)
#define s6e3ha8_test_key_off_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5)
#define s6e3ha8_test_key_on_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x5a, 0x5a)
#define s6e3ha8_test_key_off_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0xa5, 0xa5)
#define s6e3ha8_test_key_on_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0xa5, 0xa5)
#define s6e3ha8_test_key_off_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0x5a, 0x5a)
#define s6e3ha8_afc_off(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe2, 0x00, 0x00)

static void s6e3ha8_amb577px01_wqhd_reset(struct s6e3ha8 *priv)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);
}

static int s6e3ha8_amb577px01_wqhd_on(struct s6e3ha8 *priv)
{
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	s6e3ha8_test_key_on_lvl1(&ctx);

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_compression_mode_multi(&ctx, true);
	s6e3ha8_test_key_off_lvl2(&ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	usleep_range(5000, 6000);

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x13);
	s6e3ha8_test_key_off_lvl2(&ctx);
	usleep_range(10000, 11000);

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x13);
	s6e3ha8_test_key_off_lvl2(&ctx);

	/* OMOK setting 1 (Initial setting) - Scaler Latch Setting Guide */
	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x07);
	/* latch setting 1 : Scaler on/off & address setting & PPS setting -> Image update latch */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x3c, 0x10);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x0b);
	/* latch setting 2 : Ratio change mode -> Image update latch */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x30);
	/* OMOK setting 2 - Seamless setting guide : WQHD */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2a, 0x00, 0x00, 0x05, 0x9f); /* CASET */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x2b, 0x00, 0x00, 0x0b, 0x8f); /* PASET */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xba, 0x01); /* scaler setup : scaler off */
	s6e3ha8_test_key_off_lvl2(&ctx);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x00); /* TE Vsync ON */

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xed, 0x4c); /* ERR_FG */
	s6e3ha8_test_key_off_lvl2(&ctx);

	s6e3ha8_test_key_on_lvl3(&ctx);
	/* FFC Setting 897.6Mbps */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x0d, 0x10, 0xb4, 0x3e, 0x01);
	s6e3ha8_test_key_off_lvl3(&ctx);

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9,
				     0x00, 0xb0, 0x81, 0x09, 0x00, 0x00, 0x00,
				     0x11, 0x03); /* TSP HSYNC Setting */
	s6e3ha8_test_key_off_lvl2(&ctx);

	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf6, 0x43);
	s6e3ha8_test_key_off_lvl2(&ctx);

	s6e3ha8_test_key_on_lvl2(&ctx);
	/* Brightness condition set */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xca,
				     0x07, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x00, 0x0c); /* AID Set : 0% */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb5,
				     0x19, 0xdc, 0x16, 0x01, 0x34, 0x67, 0x9a,
				     0xcd, 0x01, 0x22, 0x33, 0x44, 0x00, 0x00,
				     0x05, 0x55, 0xcc, 0x0c, 0x01, 0x11, 0x11,
				     0x10); /* MPS/ELVSS Setting */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf4, 0xeb, 0x28); /* VINT */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf7, 0x03); /* Gamma, LTPS(AID) update */
	s6e3ha8_test_key_off_lvl2(&ctx);

	s6e3ha8_test_key_off_lvl1(&ctx);

	return ctx.accum_err;
}

static int s6e3ha8_enable(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	s6e3ha8_test_key_on_lvl1(&ctx);
	mipi_dsi_dcs_set_display_on_multi(&ctx);
	s6e3ha8_test_key_off_lvl1(&ctx);

	return ctx.accum_err;
}

static int s6e3ha8_disable(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	s6e3ha8_test_key_on_lvl1(&ctx);
	mipi_dsi_dcs_set_display_off_multi(&ctx);
	s6e3ha8_test_key_off_lvl1(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	s6e3ha8_test_key_on_lvl2(&ctx);
	s6e3ha8_afc_off(&ctx);
	s6e3ha8_test_key_off_lvl2(&ctx);

	mipi_dsi_msleep(&ctx, 160);

	return ctx.accum_err;
}

static int s6e3ha8_amb577px01_wqhd_prepare(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3ha8_supplies), priv->supplies);
	if (ret < 0)
		return ret;
	mipi_dsi_msleep(&ctx, 120);
	s6e3ha8_amb577px01_wqhd_reset(priv);

	ret = s6e3ha8_amb577px01_wqhd_on(priv);
	if (ret < 0) {
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		goto err;
	}

	drm_dsc_pps_payload_pack(&pps, &priv->dsc);

	s6e3ha8_test_key_on_lvl1(&ctx);
	mipi_dsi_picture_parameter_set_multi(&ctx, &pps);
	s6e3ha8_test_key_off_lvl1(&ctx);

	mipi_dsi_msleep(&ctx, 28);

	return ctx.accum_err;
err:
	regulator_bulk_disable(ARRAY_SIZE(s6e3ha8_supplies), priv->supplies);
	return ret;
}

static int s6e3ha8_amb577px01_wqhd_unprepare(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);

	return regulator_bulk_disable(ARRAY_SIZE(s6e3ha8_supplies), priv->supplies);
}

static const struct drm_display_mode s6e3ha8_amb577px01_wqhd_mode = {
	.clock = (1440 + 116 + 44 + 120) * (2960 + 120 + 80 + 124) * 60 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 116,
	.hsync_end = 1440 + 116 + 44,
	.htotal = 1440 + 116 + 44 + 120,
	.vdisplay = 2960,
	.vsync_start = 2960 + 120,
	.vsync_end = 2960 + 120 + 80,
	.vtotal = 2960 + 120 + 80 + 124,
	.width_mm = 64,
	.height_mm = 132,
};

static int s6e3ha8_amb577px01_wqhd_get_modes(struct drm_panel *panel,
					     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e3ha8_amb577px01_wqhd_mode);
}

static const struct drm_panel_funcs s6e3ha8_amb577px01_wqhd_panel_funcs = {
	.prepare = s6e3ha8_amb577px01_wqhd_prepare,
	.unprepare = s6e3ha8_amb577px01_wqhd_unprepare,
	.get_modes = s6e3ha8_amb577px01_wqhd_get_modes,
	.enable = s6e3ha8_enable,
	.disable = s6e3ha8_disable,
};

static const struct s6e3ha8_panel_desc s6e3ha8_amb577px01_wqhd_desc = {
	.panel_funcs = &s6e3ha8_amb577px01_wqhd_panel_funcs,
	.supplies = s6e3ha8_supplies,
	.num_supplies = ARRAY_SIZE(s6e3ha8_supplies),
	.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS |
		MIPI_DSI_MODE_VIDEO_NO_HFP | MIPI_DSI_MODE_VIDEO_NO_HBP |
		MIPI_DSI_MODE_VIDEO_NO_HSA | MIPI_DSI_MODE_NO_EOT_PACKET,
	.slice_width = 720,
	.slice_height = 40,
	.bits_per_component = 8,
	.bits_per_pixel = 8,
};

static void s6e3ha8_crosshatch_reset(struct s6e3ha8 *priv)
{
	/* reset-gpios is active-low: logical 0/1/0 is physical high/low/high */
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int s6e3ha8_crosshatch_on(struct s6e3ha8 *priv)
{
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	priv->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_compression_mode_multi(&ctx, true);
	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x53, 0x20);
	mipi_dsi_dcs_set_tear_on_multi(&ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	s6e3ha8_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9,
				     0x01, 0xb0, 0x81, 0x09, 0x00, 0x00, 0x00,
				     0x11, 0x03);
	s6e3ha8_test_key_off_lvl2(&ctx);

	return ctx.accum_err;
}

static int s6e3ha8_crosshatch_prepare(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct device *dev = &priv->dsi->dev;
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3ha8_crosshatch_supplies),
				    priv->supplies);
	if (ret < 0) {
		dev_err(dev, "S6E3HA8_CROSSHATCH_PREPARE_FAILED stage=regulators error=%d\n",
			ret);
		return ret;
	}

	s6e3ha8_crosshatch_reset(priv);

	ret = s6e3ha8_crosshatch_on(priv);
	if (ret < 0) {
		dev_err(dev, "S6E3HA8_CROSSHATCH_PREPARE_FAILED stage=init error=%d\n",
			ret);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		goto err_disable_regulators;
	}

	drm_dsc_pps_payload_pack(&pps, &priv->dsc);
	mipi_dsi_picture_parameter_set_multi(&ctx, &pps);
	mipi_dsi_msleep(&ctx, 10);
	if (ctx.accum_err) {
		ret = ctx.accum_err;
		dev_err(dev, "S6E3HA8_CROSSHATCH_PREPARE_FAILED stage=pps error=%d\n",
			ret);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		goto err_disable_regulators;
	}

	dev_info(dev, "S6E3HA8_CROSSHATCH_PREPARED\n");

	return 0;

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(s6e3ha8_crosshatch_supplies),
			       priv->supplies);
	return ret;
}

static int s6e3ha8_crosshatch_enable(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct device *dev = &priv->dsi->dev;
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	if (!panel->prepared) {
		dev_err(dev, "S6E3HA8_CROSSHATCH_ENABLE_FAILED stage=not-prepared error=%d\n",
			-EIO);
		return -EIO;
	}

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	if (ctx.accum_err) {
		dev_err(dev, "S6E3HA8_CROSSHATCH_ENABLE_FAILED stage=display-on error=%d\n",
			ctx.accum_err);
		return ctx.accum_err;
	}

	dev_info(dev, "S6E3HA8_CROSSHATCH_ENABLED\n");

	return 0;
}

static int s6e3ha8_crosshatch_disable(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	/* Downstream sends the complete off sequence in HS mode. */
	priv->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 10);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	return ctx.accum_err;
}

static int s6e3ha8_crosshatch_unprepare(struct drm_panel *panel)
{
	struct s6e3ha8 *priv = to_s6e3ha8(panel);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);

	return regulator_bulk_disable(ARRAY_SIZE(s6e3ha8_crosshatch_supplies),
				      priv->supplies);
}

static const struct drm_display_mode s6e3ha8_crosshatch_mode = {
	.clock = (1440 + 116 + 44 + 116) * (2960 + 124 + 120 + 80) * 60 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 116,
	.hsync_end = 1440 + 116 + 44,
	.htotal = 1440 + 116 + 44 + 116,
	.vdisplay = 2960,
	.vsync_start = 2960 + 124,
	.vsync_end = 2960 + 124 + 120,
	.vtotal = 2960 + 124 + 120 + 80,
	.width_mm = 70,
	.height_mm = 144,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int s6e3ha8_crosshatch_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &s6e3ha8_crosshatch_mode);
}

static const struct drm_panel_funcs s6e3ha8_crosshatch_panel_funcs = {
	.prepare = s6e3ha8_crosshatch_prepare,
	.unprepare = s6e3ha8_crosshatch_unprepare,
	.get_modes = s6e3ha8_crosshatch_get_modes,
	.enable = s6e3ha8_crosshatch_enable,
	.disable = s6e3ha8_crosshatch_disable,
};

static const struct s6e3ha8_panel_desc s6e3ha8_crosshatch_desc = {
	.panel_funcs = &s6e3ha8_crosshatch_panel_funcs,
	.supplies = s6e3ha8_crosshatch_supplies,
	.num_supplies = ARRAY_SIZE(s6e3ha8_crosshatch_supplies),
	.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS |
		MIPI_DSI_MODE_NO_EOT_PACKET,
	.slice_width = 720,
	.slice_height = 40,
	.bits_per_component = 8,
	.bits_per_pixel = 8,
	.has_backlight = true,
};

static int s6e3ha8_crosshatch_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	if (brightness && brightness < 5)
		brightness = 5;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
}

static const struct backlight_ops s6e3ha8_crosshatch_bl_ops = {
	.update_status = s6e3ha8_crosshatch_bl_update_status,
};

static struct backlight_device *
s6e3ha8_crosshatch_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3ha8_crosshatch_bl_ops, &props);
}

static int s6e3ha8_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct s6e3ha8_panel_desc *desc;
	struct s6e3ha8 *priv;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return dev_err_probe(dev, -EINVAL, "missing panel description\n");

	priv = devm_drm_panel_alloc(dev, struct s6e3ha8, panel,
				    desc->panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	ret = devm_regulator_bulk_get_const(dev, desc->num_supplies,
					    desc->supplies,
					    &priv->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "Failed to get reset-gpios\n");

	priv->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, priv);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = desc->mode_flags;

	priv->panel.prepare_prev_first = true;

	if (desc->has_backlight) {
		priv->panel.backlight = s6e3ha8_crosshatch_create_backlight(dsi);
		if (IS_ERR(priv->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(priv->panel.backlight),
					     "Failed to create backlight\n");
	}

	drm_panel_add(&priv->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &priv->dsc;

	priv->dsc.dsc_version_major = 1;
	priv->dsc.dsc_version_minor = 1;

	priv->dsc.slice_height = desc->slice_height;
	priv->dsc.slice_width = desc->slice_width;
	WARN_ON(1440 % priv->dsc.slice_width);
	priv->dsc.slice_count = 1440 / priv->dsc.slice_width;
	priv->dsc.bits_per_component = desc->bits_per_component;
	priv->dsc.bits_per_pixel = desc->bits_per_pixel << 4; /* 4 fractional bits */
	priv->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&priv->panel);
		return ret;
	}

	return 0;
}

static void s6e3ha8_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3ha8 *priv = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&priv->panel);
}

static const struct of_device_id s6e3ha8_of_match[] = {
	{
		.compatible = "samsung,s6e3ha8",
		.data = &s6e3ha8_amb577px01_wqhd_desc,
	},
	{
		.compatible = "samsung,s6e3ha8-crosshatch",
		.data = &s6e3ha8_crosshatch_desc,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3ha8_of_match);

static struct mipi_dsi_driver s6e3ha8_driver = {
	.probe = s6e3ha8_probe,
	.remove = s6e3ha8_remove,
	.driver = {
		.name = "panel-s6e3ha8",
		.of_match_table = s6e3ha8_of_match,
	},
};
module_mipi_dsi_driver(s6e3ha8_driver);

MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_DESCRIPTION("DRM driver for S6E3HA8 panel");
MODULE_LICENSE("GPL");
