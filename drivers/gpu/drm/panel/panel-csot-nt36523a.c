// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vddio",
	"vddenp",
	"vddenn",
};

static unsigned long const regulator_enable_loads[] = {
	62000,
	100000,
	100000,
};

static unsigned long const regulator_disable_loads[] = {
	80,
	100,
	100,
};

struct cmd_set {
	u8 commands[6];
	u8 size;
};

struct nt36523a_config {
	u32 width_mm;
	u32 height_mm;
	const char *panel_name;
	const struct cmd_set *panel_on_cmds;
	u32 num_on_cmds;
	const struct drm_display_mode *dm;
};

struct csot_nt36523a {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *reset_gpio;

	struct backlight_device *backlight;

	struct mipi_dsi_device *dsi;

	const struct nt36523a_config *config;
	bool prepared;
	bool enabled;
};

static inline struct csot_nt36523a *panel_to_ctx(struct drm_panel *panel)
{
	return container_of(panel, struct csot_nt36523a, panel);
}

static const struct cmd_set qcom_2k_panel_magic_cmds[] = {
	{ { 0xff, 0x10 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0xb9, 0x05 }, 2 },
	{ { 0xff, 0x20 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x18, 0x40 }, 2 },
	{ { 0xff, 0x10 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0xb9, 0x02 }, 2 },
	{ { 0xff, 0xd0 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x02, 0xaf }, 2 },
	{ { 0x00, 0x30 }, 2 },
	{ { 0x09, 0xee }, 2 },
	{ { 0x1c, 0x99 }, 2 },
	{ { 0x1d, 0x09 }, 2 },
	{ { 0xff, 0xf0 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x3a, 0x08 }, 2 },
	{ { 0xff, 0xe0 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x4f, 0x02 }, 2 },
	{ { 0xff, 0x20 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x58, 0x40 }, 2 },
	{ { 0xff, 0x10 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x35, 0x00 }, 2 },
	{ { 0xff, 0x23 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x00, 0x80 }, 2 },
	{ { 0x01, 0x84 }, 2 },
	{ { 0x05, 0x2d }, 2 },
	{ { 0x06, 0x00 }, 2 },
	{ { 0x07, 0x00 }, 2 },
	{ { 0x08, 0x01 }, 2 },
	{ { 0x09, 0x45 }, 2 },
	{ { 0x11, 0x02 }, 2 },
	{ { 0x12, 0x80 }, 2 },
	{ { 0x15, 0x83 }, 2 },
	{ { 0x16, 0x0c }, 2 },
	{ { 0x29, 0x0a }, 2 },
	{ { 0x30, 0xff }, 2 },
	{ { 0x31, 0xfe }, 2 },
	{ { 0x32, 0xfd }, 2 },
	{ { 0x33, 0xfb }, 2 },
	{ { 0x34, 0xf8 }, 2 },
	{ { 0x35, 0xf5 }, 2 },
	{ { 0x36, 0xf3 }, 2 },
	{ { 0x37, 0xf2 }, 2 },
	{ { 0x38, 0xf2 }, 2 },
	{ { 0x39, 0xf2 }, 2 },
	{ { 0x3a, 0xef }, 2 },
	{ { 0x3b, 0xec }, 2 },
	{ { 0x3d, 0xe9 }, 2 },
	{ { 0x3f, 0xe5 }, 2 },
	{ { 0x40, 0xe5 }, 2 },
	{ { 0x41, 0xe5 }, 2 },
	{ { 0x2a, 0x13 }, 2 },
	{ { 0x45, 0xff }, 2 },
	{ { 0x46, 0xf4 }, 2 },
	{ { 0x47, 0xe7 }, 2 },
	{ { 0x48, 0xda }, 2 },
	{ { 0x49, 0xcd }, 2 },
	{ { 0x4a, 0xc0 }, 2 },
	{ { 0x4b, 0xb3 }, 2 },
	{ { 0x4c, 0xb2 }, 2 },
	{ { 0x4d, 0xb2 }, 2 },
	{ { 0x4e, 0xb2 }, 2 },
	{ { 0x4f, 0x99 }, 2 },
	{ { 0x50, 0x80 }, 2 },
	{ { 0x51, 0x68 }, 2 },
	{ { 0x52, 0x66 }, 2 },
	{ { 0x53, 0x66 }, 2 },
	{ { 0x54, 0x66 }, 2 },
	{ { 0x2b, 0x0e }, 2 },
	{ { 0x58, 0xff }, 2 },
	{ { 0x59, 0xfb }, 2 },
	{ { 0x5a, 0xf7 }, 2 },
	{ { 0x5b, 0xf3 }, 2 },
	{ { 0x5c, 0xef }, 2 },
	{ { 0x5d, 0xe3 }, 2 },
	{ { 0x5e, 0xda }, 2 },
	{ { 0x5f, 0xd8 }, 2 },
	{ { 0x60, 0xd8 }, 2 },
	{ { 0x61, 0xd8 }, 2 },
	{ { 0x62, 0xcb }, 2 },
	{ { 0x63, 0xbf }, 2 },
	{ { 0x64, 0xb3 }, 2 },
	{ { 0x65, 0xb2 }, 2 },
	{ { 0x66, 0xb2 }, 2 },
	{ { 0x67, 0xb2 }, 2 },
	{ { 0xff, 0x10 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x51, 0x0f, 0xff }, 3 },
	{ { 0x53, 0x2c }, 2 },
	{ { 0x55, 0x00 }, 2 },
	{ { 0xbb, 0x13 }, 2 },
	{ { 0x3b, 0x03, 0xac, 0x1a, 0x04, 0x04 }, 6 },
	{ { 0xff, 0x2a }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x25, 0x46 }, 2 },
	{ { 0x30, 0x46 }, 2 },
	{ { 0x39, 0x46 }, 2 },
	{ { 0xff, 0x26 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x01, 0xb0 }, 2 },
	{ { 0x19, 0x10 }, 2 },
	{ { 0x1a, 0xe0 }, 2 },
	{ { 0x1b, 0x10 }, 2 },
	{ { 0x1c, 0x00 }, 2 },
	{ { 0x2a, 0x10 }, 2 },
	{ { 0x2b, 0xe0 }, 2 },
	{ { 0xff, 0xf0 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x84, 0x08 }, 2 },
	{ { 0x85, 0x0c }, 2 },
	{ { 0xff, 0x20 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x51, 0x00 }, 2 },
	{ { 0xff, 0x25 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x91, 0x1f }, 2 },
	{ { 0x92, 0x0f }, 2 },
	{ { 0x93, 0x01 }, 2 },
	{ { 0x94, 0x18 }, 2 },
	{ { 0x95, 0x03 }, 2 },
	{ { 0x96, 0x01 }, 2 },
	{ { 0xff, 0x10 }, 2 },
	{ { 0xb0, 0x01 }, 2 },
	{ { 0xff, 0x25 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x19, 0x1f }, 2 },
	{ { 0x1b, 0x1b }, 2 },
	{ { 0xff, 0x24 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0xb8, 0x28 }, 2 },
	{ { 0xff, 0x27 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0xd0, 0x31 }, 2 },
	{ { 0xd1, 0x20 }, 2 },
	{ { 0xd4, 0x08 }, 2 },
	{ { 0xde, 0x80 }, 2 },
	{ { 0xdf, 0x02 }, 2 },
	{ { 0xff, 0x26 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x00, 0x81 }, 2 },
	{ { 0x01, 0xb0 }, 2 },
	{ { 0xff, 0x22 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x6f, 0x01 }, 2 },
	{ { 0x70, 0x11 }, 2 },
	{ { 0x73, 0x01 }, 2 },
	{ { 0x74, 0x4d }, 2 },
	{ { 0xa0, 0x3f }, 2 },
	{ { 0xa9, 0x50 }, 2 },
	{ { 0xaa, 0x28 }, 2 },
	{ { 0xab, 0x28 }, 2 },
	{ { 0xad, 0x10 }, 2 },
	{ { 0xb8, 0x00 }, 2 },
	{ { 0xb9, 0x4b }, 2 },
	{ { 0xba, 0x96 }, 2 },
	{ { 0xbb, 0x4b }, 2 },
	{ { 0xbe, 0x07 }, 2 },
	{ { 0xbf, 0x4b }, 2 },
	{ { 0xc0, 0x07 }, 2 },
	{ { 0xc1, 0x5c }, 2 },
	{ { 0xc2, 0x00 }, 2 },
	{ { 0xc5, 0x00 }, 2 },
	{ { 0xc6, 0x3f }, 2 },
	{ { 0xc7, 0x00 }, 2 },
	{ { 0xca, 0x08 }, 2 },
	{ { 0xcb, 0x40 }, 2 },
	{ { 0xce, 0x00 }, 2 },
	{ { 0xcf, 0x08 }, 2 },
	{ { 0xd0, 0x40 }, 2 },
	{ { 0xd3, 0x08 }, 2 },
	{ { 0xd4, 0x40 }, 2 },
	{ { 0xff, 0x25 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0xbc, 0x01 }, 2 },
	{ { 0xbd, 0x1c }, 2 },
	{ { 0xff, 0x2a }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x9a, 0x03 }, 2 },
	{ { 0xff, 0x10 }, 2 },
};

static int truly_dcs_write(struct drm_panel *panel, u32 command)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret;

	ret = mipi_dsi_dcs_write(ctx->dsi, command, NULL, 0);
	if (ret < 0) {
		dev_err(ctx->dev, "cmd 0x%x failed for dsi\n", command);
	}

	return ret;
}

static int truly_dcs_write_buf(struct drm_panel *panel,
	u32 size, const u8 *buf)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret = 0;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, buf, size);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to tx cmd, err: %d\n", ret);
		return ret;
	}

	return ret;
}

static int csot_nt36523a_power_on(struct csot_nt36523a *ctx)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
					regulator_enable_loads[i]);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	/*
	 * Reset sequence of truly panel requires the panel to be
	 * out of reset for 10ms, followed by being held in reset
	 * for 10ms and then out again
	 */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);

	return 0;
}

static int csot_nt36523a_power_off(struct csot_nt36523a *ctx)
{
	int ret = 0;
	int i;

	gpiod_set_value(ctx->reset_gpio, 1);

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
				regulator_disable_loads[i]);
		if (ret) {
			dev_err(ctx->dev, "regulator_set_load failed %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret) {
		dev_err(ctx->dev, "regulator_bulk_disable failed %d\n", ret);
	}
	return ret;
}

static int csot_nt36523a_disable(struct drm_panel *panel)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret;

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_disable(ctx->backlight);
		if (ret < 0)
			dev_err(ctx->dev, "backlight disable failed %d\n", ret);
	}

	ctx->enabled = false;
	return 0;
}

static int csot_nt36523a_unprepare(struct drm_panel *panel)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret = 0;

	if (!ctx->prepared)
		return 0;

	ctx->dsi->mode_flags = 0;

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_OFF);
	if (ret < 0) {
		dev_err(ctx->dev, "set_display_off cmd failed ret = %d\n", ret);
	}

	/* 120ms delay required here as per DCS spec */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_ENTER_SLEEP_MODE);
	if (ret < 0) {
		dev_err(ctx->dev, "enter_sleep cmd failed ret = %d\n", ret);
	}

	ret = csot_nt36523a_power_off(ctx);
	if (ret < 0)
		dev_err(ctx->dev, "power_off failed ret = %d\n", ret);

	ctx->prepared = false;
	return ret;
}

static int csot_nt36523a_prepare(struct drm_panel *panel)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret;
	int i;
	const struct cmd_set *panel_on_cmds;
	const struct nt36523a_config *config;
	u32 num_cmds;

	if (ctx->prepared)
		return 0;

	ret = csot_nt36523a_power_on(ctx);
	if (ret < 0)
		return ret;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	config = ctx->config;
	panel_on_cmds = config->panel_on_cmds;
	num_cmds = config->num_on_cmds;

	for (i = 0; i < num_cmds; i++) {
		ret = truly_dcs_write_buf(panel,
				panel_on_cmds[i].size,
					panel_on_cmds[i].commands);
		if (ret < 0) {
			dev_err(ctx->dev, "cmd set tx failed i = %d ret = %d\n", i, ret);
			goto power_off;
		}
	}

	ret = truly_dcs_write(panel, MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret < 0) {
		dev_err(ctx->dev, "exit_sleep_mode cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending exit sleep DCS command */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_ON);
	if (ret < 0) {
		dev_err(ctx->dev, "set_display_on cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending set_display_on DCS command */
	msleep(120);

	ctx->prepared = true;

	return 0;

power_off:
	if (csot_nt36523a_power_off(ctx))
		dev_err(ctx->dev, "power_off failed\n");
	return ret;
}

static int csot_nt36523a_enable(struct drm_panel *panel)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	int ret;

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_enable(ctx->backlight);
		if (ret < 0)
			dev_err(ctx->dev, "backlight enable failed %d\n", ret);
	}

	ctx->enabled = true;

	return 0;
}

static int csot_nt36523a_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct csot_nt36523a *ctx = panel_to_ctx(panel);
	struct drm_display_mode *mode;
	const struct nt36523a_config *config;

	config = ctx->config;
	mode = drm_mode_create(connector->dev);
	if (!mode) {
		dev_err(ctx->dev, "failed to create a new display mode\n");
		return 0;
	}

	connector->display_info.width_mm = config->width_mm;
	connector->display_info.height_mm = config->height_mm;
	drm_mode_copy(mode, config->dm);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs csot_nt36523a_drm_funcs = {
	.disable = csot_nt36523a_disable,
	.unprepare = csot_nt36523a_unprepare,
	.prepare = csot_nt36523a_prepare,
	.enable = csot_nt36523a_enable,
	.get_modes = csot_nt36523a_get_modes,
};

static int csot_nt36523a_panel_add(struct csot_nt36523a *ctx)
{
	struct device *dev = ctx->dev;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio %ld\n", PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	drm_panel_init(&ctx->panel, dev, &csot_nt36523a_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	return 0;
}

static const struct drm_display_mode qcom_sm8250_mtp_2k_mode = {
	.name = "16000x2560csot_nt36523a",
	.clock = (800 + 100 + 20 + 26) * (2560 + 26 + 4 + 168) * 120 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 100,
	.hsync_end = 800 + 100 + 20,
	.htotal = 800 + 100 + 20 + 26,
	.vdisplay = 2560,
	.vsync_start = 2560 + 26,
	.vsync_end = 2560 + 26 + 4,
	.vtotal = 2560 + 26 + 4 + 168,
	.flags = 0,
};

static const struct nt36523a_config nt36523a_dir = {
	.width_mm = 1474,
	.height_mm = 2359,
	.panel_name = "qcom_sm8250_mtp_2k_panel",
	.dm = &qcom_sm8250_mtp_2k_mode,
	.panel_on_cmds = qcom_2k_panel_magic_cmds,
	.num_on_cmds = ARRAY_SIZE(qcom_2k_panel_magic_cmds),
};

static int csot_nt36523a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct csot_nt36523a *ctx;
	struct mipi_dsi_device *dsi_dev;
	int ret = 0;

	/*
	const struct mipi_dsi_device_info info = {
		.type = "csotnt36523a",
		.channel = 0,
		.node = NULL,
	};
	*/

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;

	/*
	 * This device represents itself as one with two input ports which are
	 * fed by the output ports of the two DSI controllers . The DSI0 is
	 * the master controller and has most of the panel related info in its
	 * child node.
	 */

	ctx->config = of_device_get_match_data(dev);

	if (!ctx->config) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->dsi = dsi;

	ret = csot_nt36523a_panel_add(ctx);
	if (ret) {
		dev_err(dev, "failed to add panel\n");
		goto err_panel_add;
	}

	dsi_dev = ctx->dsi;
	dsi_dev->lanes = 3;
	dsi_dev->format = MIPI_DSI_FMT_RGB888;
	dsi_dev->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM |
		MIPI_DSI_CLOCK_NON_CONTINUOUS;
	ret = mipi_dsi_attach(dsi_dev);
	if (ret < 0) {
		dev_err(dev, "dsi attach failed \n");
		goto err_dsi_attach;
	}

	return 0;

err_dsi_attach:
	drm_panel_remove(&ctx->panel);
err_panel_add:
	return ret;
}

static int csot_nt36523a_remove(struct mipi_dsi_device *dsi)
{
	struct csot_nt36523a *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->dsi)
		mipi_dsi_detach(ctx->dsi);

	drm_panel_remove(&ctx->panel);
	return 0;
}

static const struct of_device_id csot_nt36523a_of_match[] = {
	{
		.compatible = "csot,nt36523a-2K-display",
		.data = &nt36523a_dir,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, csot_nt36523a_of_match);

static struct mipi_dsi_driver csot_nt36523a_driver = {
	.driver = {
		.name = "panel-csot-nt36523a",
		.of_match_table = csot_nt36523a_of_match,
	},
	.probe = csot_nt36523a_probe,
	.remove = csot_nt36523a_remove,
};
module_mipi_dsi_driver(csot_nt36523a_driver);

MODULE_DESCRIPTION("Csot NT356523A DSI Panel Driver");
MODULE_LICENSE("GPL v2");
