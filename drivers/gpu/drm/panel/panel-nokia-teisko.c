// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nokia "Teisko" MIPI-DSI video panel (Orise controller, FWVGA 480x800).
 * Used on the Nokia Lumia 520 (fame, MSM8227).
 *
 * No dedicated panel datasheet is available; "Teisko" is the Nokia/FFU
 * panel codename. The mode and init sequence are ported from the
 * Android4Lumia downstream drivers (mipi_orise.c +
 * mipi_orise_video_fwvga_pt.c). Panel power (vdda/avdd/vddio) is supplied
 * by the DSI host; this driver only sequences reset and the DCS init.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define TEISKO_DEFAULT_BRIGHTNESS	0x80
#define TEISKO_MAX_BRIGHTNESS		0xff
#define TEISKO_CONTROL_DISPLAY_ON	0x24

struct teisko {
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
};

static inline struct teisko *to_teisko(struct drm_panel *panel)
{
	return container_of(panel, struct teisko, panel);
}

static void teisko_reset(struct teisko *ctx)
{
	/*
	 * reset-gpios is active low (gpiod logical 1 = pin low = asserted).
	 * Mirror the downstream board-8930-display.c release pulse:
	 * deassert, assert, deassert with short settling delays.
	 */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(2);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(2);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int teisko_log_ret(struct teisko *ctx, const char *name, ssize_t ret)
{
	struct device *dev = &ctx->dsi->dev;

	if (ret < 0) {
		dev_err(dev, "%s failed: %zd\n", name, ret);
		return ret;
	}

	dev_info(dev, "%s ok: %zd\n", name, ret);

	return 0;
}

static int teisko_write_brightness(struct teisko *ctx, u8 brightness)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	u8 control_display = brightness ? TEISKO_CONTROL_DISPLAY_ON : 0;
	int ret, ret2;

	dev_info(&dsi->dev, "set DSI LPM brightness: 0x%02x\n", brightness);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				 &brightness, sizeof(brightness));
	ret = teisko_log_ret(ctx, "dcs set display brightness", ret);

	ret2 = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				  &control_display, sizeof(control_display));
	ret2 = teisko_log_ret(ctx, "dcs write control display", ret2);

	return ret ?: ret2;
}

static int teisko_backlight_update_status(struct backlight_device *bl)
{
	struct teisko *ctx = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);

	if (!ctx->prepared) {
		dev_info(&ctx->dsi->dev,
			 "defer DSI brightness 0x%02x until panel is prepared\n",
			 brightness);
		return 0;
	}

	if (ctx->enabled) {
		dev_info(&ctx->dsi->dev,
			 "skip DSI brightness 0x%02x after display-on\n",
			 brightness);
		return 0;
	}

	return teisko_write_brightness(ctx, brightness);
}

static int teisko_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static const struct backlight_ops teisko_backlight_ops = {
	.update_status = teisko_backlight_update_status,
	.get_brightness = teisko_backlight_get_brightness,
};

static struct backlight_device *teisko_create_backlight(struct teisko *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = TEISKO_DEFAULT_BRIGHTNESS,
		.max_brightness = TEISKO_MAX_BRIGHTNESS,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
					      &teisko_backlight_ops, &props);
}

static u8 teisko_current_brightness(struct teisko *ctx)
{
	u32 brightness = TEISKO_DEFAULT_BRIGHTNESS;

	if (ctx->backlight)
		brightness = ctx->backlight->props.brightness;

	return min_t(u32, brightness, TEISKO_MAX_BRIGHTNESS);
}

static int teisko_prepare(struct drm_panel *panel)
{
	struct teisko *ctx = to_teisko(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	/*
	 * Vendor command from the downstream on-sequence; sent there as a
	 * generic long write. If init proves unreliable, forcing a generic
	 * long write (DT 0x29) for this command is the first thing to try.
	 */
	static const u8 vendor_cmd[] = { 0xff, 0x78 };
	static const u8 address_mode[] = { 0x00 };
	static const u8 control_display[] = { TEISKO_CONTROL_DISPLAY_ON };
	int ret;

	if (ctx->prepared)
		return 0;

	dev_info(dev, "prepare\n");

	teisko_reset(ctx);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	ret = teisko_log_ret(ctx, "dcs exit sleep mode", ret);
	if (ret < 0)
		return ret;
	msleep(120);

	ret = mipi_dsi_generic_write(dsi, vendor_cmd, sizeof(vendor_cmd));
	ret = teisko_log_ret(ctx, "generic vendor command ff 78", ret);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_ADDRESS_MODE,
				 address_mode, sizeof(address_mode));
	ret = teisko_log_ret(ctx, "dcs set address mode", ret);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				 control_display, sizeof(control_display));
	ret = teisko_log_ret(ctx, "dcs write control display", ret);
	if (ret < 0)
		return ret;

	ret = teisko_write_brightness(ctx, teisko_current_brightness(ctx));
	if (ret < 0)
		return ret;

	ctx->prepared = true;

	return 0;
}

static int teisko_enable(struct drm_panel *panel)
{
	struct teisko *ctx = to_teisko(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->enabled)
		return 0;

	dev_info(dev, "enable\n");

	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	ret = teisko_log_ret(ctx, "dcs set display on", ret);
	if (ret < 0)
		return ret;

	msleep(20);

	ctx->enabled = true;

	return 0;
}

static int teisko_disable(struct drm_panel *panel)
{
	struct teisko *ctx = to_teisko(panel);
	int ret = 0, ret2;

	if (!ctx->enabled)
		return 0;

	dev_info(&ctx->dsi->dev, "disable\n");

	if (ctx->prepared)
		ret = teisko_write_brightness(ctx, 0);

	ret2 = mipi_dsi_dcs_set_display_off(ctx->dsi);
	ret2 = teisko_log_ret(ctx, "dcs set display off", ret2);
	ctx->enabled = false;

	return ret ?: ret2;
}

static int teisko_unprepare(struct drm_panel *panel)
{
	struct teisko *ctx = to_teisko(panel);

	if (!ctx->prepared)
		return 0;

	dev_info(&ctx->dsi->dev, "unprepare\n");
	dev_info(&ctx->dsi->dev, "skip DCS enter sleep mode\n");

	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	ctx->prepared = false;

	return 0;
}

/*
 * Timing and 343.85 MHz DSI bit clock from the downstream
 * mipi_orise_video_fwvga_pt.c (clk_rate=343848960, 60 Hz). The msm DSI v2
 * host derives the bit clock as clock * bpp / lanes, so
 * clock = 343848960 * 2 / 24 = 28654 kHz. The stock-FFU PCFG reports
 * different porches (see notes/display.md); the self-consistent downstream
 * values are used here.
 */
static const struct drm_display_mode teisko_mode = {
	.clock = 28654,
	.hdisplay = 480,
	.hsync_start = 480 + 45,
	.hsync_end = 480 + 45 + 4,
	.htotal = 480 + 45 + 4 + 44,
	.vdisplay = 800,
	.vsync_start = 800 + 14,
	.vsync_end = 800 + 14 + 1,
	.vtotal = 800 + 14 + 1 + 14,
	.width_mm = 47,
	.height_mm = 79,
};

static int teisko_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &teisko_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs teisko_panel_funcs = {
	.prepare = teisko_prepare,
	.enable = teisko_enable,
	.disable = teisko_disable,
	.unprepare = teisko_unprepare,
	.get_modes = teisko_get_modes,
};

static int teisko_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct teisko *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset GPIO\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &teisko_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->backlight = teisko_create_backlight(ctx);
	if (IS_ERR(ctx->backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->backlight),
				     "failed to register backlight\n");

	dev_info(dev, "probed lanes=%u format=0x%x mode_flags=0x%lx\n",
		 dsi->lanes, dsi->format, dsi->mode_flags);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "failed to attach DSI device\n");
	}

	return 0;
}

static void teisko_remove(struct mipi_dsi_device *dsi)
{
	struct teisko *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach DSI device: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id teisko_of_match[] = {
	{ .compatible = "nokia,teisko" },
	{ }
};
MODULE_DEVICE_TABLE(of, teisko_of_match);

static struct mipi_dsi_driver teisko_driver = {
	.probe = teisko_probe,
	.remove = teisko_remove,
	.driver = {
		.name = "panel-nokia-teisko",
		.of_match_table = teisko_of_match,
	},
};
module_mipi_dsi_driver(teisko_driver);

MODULE_DESCRIPTION("Nokia Teisko (Orise FWVGA 480x800) DSI panel driver");
MODULE_LICENSE("GPL");
