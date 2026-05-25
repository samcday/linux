// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_vblank.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "mdp4_kms.h"

static int mdp4_hw_init(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	struct drm_device *dev = mdp4_kms->dev;
	u32 dmap_cfg, vg_cfg;
	unsigned long clk;

	pm_runtime_get_sync(dev->dev);

	if (mdp4_kms->rev > 1) {
		mdp4_write(mdp4_kms, REG_MDP4_CS_CONTROLLER0, 0x0707ffff);
		mdp4_write(mdp4_kms, REG_MDP4_CS_CONTROLLER1, 0x03073f3f);
	}

	mdp4_write(mdp4_kms, REG_MDP4_PORTMAP_MODE, 0x3);

	/* max read pending cmd config, 3 pending requests: */
	mdp4_write(mdp4_kms, REG_MDP4_READ_CNFG, 0x02222);

	clk = clk_get_rate(mdp4_kms->clk);

	if ((mdp4_kms->rev >= 1) || (clk >= 90000000)) {
		dmap_cfg = 0x47;     /* 16 bytes-burst x 8 req */
		vg_cfg = 0x47;       /* 16 bytes-burs x 8 req */
	} else {
		dmap_cfg = 0x27;     /* 8 bytes-burst x 8 req */
		vg_cfg = 0x43;       /* 16 bytes-burst x 4 req */
	}

	DBG("fetch config: dmap=%02x, vg=%02x", dmap_cfg, vg_cfg);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_FETCH_CONFIG(DMA_P), dmap_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_FETCH_CONFIG(DMA_E), dmap_cfg);

	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(VG1), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(VG2), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(RGB1), vg_cfg);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_FETCH_CONFIG(RGB2), vg_cfg);

	if (mdp4_kms->rev >= 2)
		mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG_UPDATE_METHOD, 1);
	mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG, 0);

	/* disable CSC matrix / YUV by default: */
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_OP_MODE(VG1), 0);
	mdp4_write(mdp4_kms, REG_MDP4_PIPE_OP_MODE(VG2), 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_P_OP_MODE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_S_OP_MODE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CSC_CONFIG(1), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CSC_CONFIG(2), 0);

	if (mdp4_kms->rev > 1)
		mdp4_write(mdp4_kms, REG_MDP4_RESET_STATUS, 1);

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static void mdp4_enable_commit(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	mdp4_enable(mdp4_kms);
}

static void mdp4_disable_commit(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	mdp4_disable(mdp4_kms);
}

static void mdp4_flush_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	/* TODO */
}

static void mdp4_wait_flush(struct msm_kms *kms, unsigned crtc_mask)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	struct drm_crtc *crtc;

	for_each_crtc_mask(mdp4_kms->dev, crtc, crtc_mask)
		mdp4_crtc_wait_for_commit_done(crtc);
}

static void mdp4_complete_commit(struct msm_kms *kms, unsigned crtc_mask)
{
}

static long mdp4_round_pixclk(struct msm_kms *kms, unsigned long rate,
		struct drm_encoder *encoder)
{
	/* if we had >1 encoder, we'd need something more clever: */
	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_TMDS:
		return mdp4_dtv_round_pixclk(encoder, rate);
	case DRM_MODE_ENCODER_LVDS:
	case DRM_MODE_ENCODER_DSI:
	default:
		return rate;
	}
}

static void mdp4_disable_init_clocks(struct mdp4_kms *mdp4_kms);

static void mdp4_destroy(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(kms));
	struct drm_device *drm = mdp4_kms->dev;
	struct msm_drm_private *priv = drm->dev_private;
	struct device *dev = drm->dev;

	if (mdp4_kms->blank_cursor_iova) {
		msm_gem_unpin_iova(mdp4_kms->blank_cursor_bo, kms->vm);
		mdp4_kms->blank_cursor_iova = 0;
	}
	drm_gem_object_put(mdp4_kms->blank_cursor_bo);
	mdp4_kms->blank_cursor_bo = NULL;

	if (kms->vm) {
		struct msm_mmu *mmu = to_msm_vm(kms->vm)->mmu;

		mmu->funcs->detach(mmu);
		drm_gpuvm_put(kms->vm);
		kms->vm = NULL;
	}

	if (mdp4_kms->rpm_enabled) {
		pm_runtime_disable(dev);
		mdp4_kms->rpm_enabled = false;
	}

	mdp4_disable_init_clocks(mdp4_kms);

	mdp_kms_destroy(&mdp4_kms->base);
	if (priv->kms == kms)
		priv->kms = NULL;
}

static const struct mdp_kms_funcs kms_funcs = {
	.base = {
		.hw_init         = mdp4_hw_init,
		.irq_preinstall  = mdp4_irq_preinstall,
		.irq_postinstall = mdp4_irq_postinstall,
		.irq_uninstall   = mdp4_irq_uninstall,
		.irq             = mdp4_irq,
		.enable_vblank   = mdp4_enable_vblank,
		.disable_vblank  = mdp4_disable_vblank,
		.enable_commit   = mdp4_enable_commit,
		.disable_commit  = mdp4_disable_commit,
		.flush_commit    = mdp4_flush_commit,
		.wait_flush      = mdp4_wait_flush,
		.complete_commit = mdp4_complete_commit,
		.round_pixclk    = mdp4_round_pixclk,
		.destroy         = mdp4_destroy,
	},
	.set_irqmask         = mdp4_set_irqmask,
};

int mdp4_disable(struct mdp4_kms *mdp4_kms)
{
	DBG("");

	clk_disable_unprepare(mdp4_kms->clk);
	clk_disable_unprepare(mdp4_kms->pclk);
	clk_disable_unprepare(mdp4_kms->lut_clk);
	clk_disable_unprepare(mdp4_kms->axi_clk);

	return 0;
}

int mdp4_enable(struct mdp4_kms *mdp4_kms)
{
	int ret;

	DBG("");

	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable before core_clk");
	ret = clk_prepare_enable(mdp4_kms->clk);
	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable after core_clk ret=%d", ret);
	if (ret)
		return ret;

	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable before iface_clk");
	ret = clk_prepare_enable(mdp4_kms->pclk);
	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable after iface_clk ret=%d", ret);
	if (ret)
		return ret;

	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable before lut_clk");
	ret = clk_prepare_enable(mdp4_kms->lut_clk);
	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable after lut_clk ret=%d", ret);
	if (ret)
		return ret;

	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable before bus_clk");
	ret = clk_prepare_enable(mdp4_kms->axi_clk);
	DRM_DEV_INFO(mdp4_kms->dev->dev, "HACK: mdp4_enable after bus_clk ret=%d", ret);
	if (ret)
		return ret;

	return 0;
}


static int mdp4_modeset_init_intf(struct mdp4_kms *mdp4_kms,
				  int intf_type)
{
	struct drm_device *dev = mdp4_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	int dsi_id;
	int ret;

	switch (intf_type) {
	case DRM_MODE_ENCODER_LVDS:
		/*
		 * bail out early if there is no panel node (no need to
		 * initialize LCDC encoder and LVDS connector)
		 */
		next_bridge = devm_drm_of_get_bridge(dev->dev, dev->dev->of_node, 0, 0);
		if (IS_ERR(next_bridge)) {
			ret = PTR_ERR(next_bridge);
			if (ret == -ENODEV)
				return 0;
			return ret;
		}

		encoder = mdp4_lcdc_encoder_init(dev);
		if (IS_ERR(encoder)) {
			DRM_DEV_ERROR(dev->dev, "failed to construct LCDC encoder\n");
			return PTR_ERR(encoder);
		}

		/* LCDC can be hooked to DMA_P (TODO: Add DMA_S later?) */
		encoder->possible_crtcs = 1 << DMA_P;

		ret = drm_bridge_attach(encoder, next_bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to attach LVDS panel/bridge: %d\n", ret);

			return ret;
		}

		connector = drm_bridge_connector_init(dev, encoder);
		if (IS_ERR(connector)) {
			DRM_DEV_ERROR(dev->dev, "failed to initialize LVDS connector\n");
			return PTR_ERR(connector);
		}

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to attach LVDS connector: %d\n", ret);

			return ret;
		}

		break;
	case DRM_MODE_ENCODER_TMDS:
		encoder = mdp4_dtv_encoder_init(dev);
		if (IS_ERR(encoder)) {
			DRM_DEV_ERROR(dev->dev, "failed to construct DTV encoder\n");
			return PTR_ERR(encoder);
		}

		/* DTV can be hooked to DMA_E: */
		encoder->possible_crtcs = 1 << 1;

		if (priv->kms->hdmi) {
			/* Construct bridge/connector for HDMI: */
			ret = msm_hdmi_modeset_init(priv->kms->hdmi, dev, encoder);
			if (ret) {
				DRM_DEV_ERROR(dev->dev, "failed to initialize HDMI: %d\n", ret);
				return ret;
			}
		}

		break;
	case DRM_MODE_ENCODER_DSI:
		/* only DSI1 supported for now */
		dsi_id = 0;

		if (!priv->kms->dsi[dsi_id])
			break;

		encoder = mdp4_dsi_encoder_init(dev);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			DRM_DEV_ERROR(dev->dev,
				"failed to construct DSI encoder: %d\n", ret);
			return ret;
		}

		/* TODO: Add DMA_S later? */
		encoder->possible_crtcs = 1 << DMA_P;

		ret = msm_dsi_modeset_init(priv->kms->dsi[dsi_id], dev, encoder);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to initialize DSI: %d\n",
				ret);
			return ret;
		}

		break;
	default:
		DRM_DEV_ERROR(dev->dev, "Invalid or unsupported interface\n");
		return -EINVAL;
	}

	return 0;
}

static int modeset_init(struct mdp4_kms *mdp4_kms)
{
	struct drm_device *dev = mdp4_kms->dev;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int i, ret;
	static const enum mdp4_pipe rgb_planes[] = {
		RGB1, RGB2,
	};
	static const enum mdp4_pipe vg_planes[] = {
		VG1, VG2,
	};
	static const enum mdp4_dma mdp4_crtcs[] = {
		DMA_P, DMA_E,
	};
	static const char * const mdp4_crtc_names[] = {
		"DMA_P", "DMA_E",
	};
	static const int mdp4_intfs[] = {
		DRM_MODE_ENCODER_LVDS,
		DRM_MODE_ENCODER_DSI,
		DRM_MODE_ENCODER_TMDS,
	};

	/* construct non-private planes: */
	for (i = 0; i < ARRAY_SIZE(vg_planes); i++) {
		plane = mdp4_plane_init(dev, vg_planes[i], false);
		if (IS_ERR(plane)) {
			DRM_DEV_ERROR(dev->dev,
				"failed to construct plane for VG%d\n", i + 1);
			ret = PTR_ERR(plane);
			goto fail;
		}
	}

	for (i = 0; i < ARRAY_SIZE(mdp4_crtcs); i++) {
		plane = mdp4_plane_init(dev, rgb_planes[i], true);
		if (IS_ERR(plane)) {
			DRM_DEV_ERROR(dev->dev,
				"failed to construct plane for RGB%d\n", i + 1);
			ret = PTR_ERR(plane);
			goto fail;
		}

		crtc  = mdp4_crtc_init(dev, plane, i,
				mdp4_crtcs[i]);
		if (IS_ERR(crtc)) {
			DRM_DEV_ERROR(dev->dev, "failed to construct crtc for %s\n",
				mdp4_crtc_names[i]);
			ret = PTR_ERR(crtc);
			goto fail;
		}
	}

	/*
	 * we currently set up two relatively fixed paths:
	 *
	 * LCDC/LVDS path: RGB1 -> DMA_P -> LCDC -> LVDS
	 *			or
	 * DSI path: RGB1 -> DMA_P -> DSI1 -> DSI Panel
	 *
	 * DTV/HDMI path: RGB2 -> DMA_E -> DTV -> HDMI
	 */

	for (i = 0; i < ARRAY_SIZE(mdp4_intfs); i++) {
		ret = mdp4_modeset_init_intf(mdp4_kms, mdp4_intfs[i]);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to initialize intf: %d, %d\n",
				i, ret);
			goto fail;
		}
	}

	return 0;

fail:
	return ret;
}

/*
 * HACK: redo the MDP footswitch (GFS) power-up handshake with the MDP clocks
 * actually running, right before the MDP4 version read. Confirmed over UART
 * from U-Boot that the MDP register space only responds after this: SBL sets
 * the GFS enable bit (MDP_PD_CTL=0x31f) while mdp_clk/mdp_ahb are off, so the
 * power-on never finishes ramping and bus reads to 0x05100000 hang. With the
 * enable bit already set, no transition occurs, so the core stays unpowered.
 * mdp4_enable() has already enabled and rated the MDP clocks before we get
 * here, so cycling the GFS now (collapse -> assert resets -> enable ->
 * unclamp -> deassert resets) lets the power-on complete and the reset pulse
 * initialise the block, all with a live core clock.
 *
 * MMCC base/size from DT mmcc@4000000. GFS_CTL (mmcc-msm8960.c MDP_PD_CTL_REG)
 * = 0x0190: DELAY[4:0]=0x1f, CLAMP=BIT(5), ENABLE=BIT(8), RETENTION=BIT(9).
 */
static void mdp4_hack_dump_mmcc(struct drm_device *dev)
{
	void __iomem *mmcc;
	u32 gfs_before, gfs_after, reset_axi, reset_ahb, reset_core;
	u32 v_01d0, v_00c0, v_01dc, v_0008, v_01d8, v_0018, v_01e8, v_016c;

	mmcc = ioremap(0x04000000, 0x1000);
	if (!mmcc) {
		DRM_DEV_INFO(dev->dev, "HACK: mmcc ioremap failed");
		return;
	}

	gfs_before = readl(mmcc + 0x0190);
	reset_axi  = readl(mmcc + 0x0208);
	reset_ahb  = readl(mmcc + 0x020c);
	reset_core = readl(mmcc + 0x0210);
	v_01d0 = readl(mmcc + 0x01d0); v_00c0 = readl(mmcc + 0x00c0);
	v_01dc = readl(mmcc + 0x01dc); v_0008 = readl(mmcc + 0x0008);
	v_01d8 = readl(mmcc + 0x01d8); v_0018 = readl(mmcc + 0x0018);
	v_01e8 = readl(mmcc + 0x01e8); v_016c = readl(mmcc + 0x016c);

	/*
	 * Power-up handshake with clocks running, mirroring downstream
	 * footswitch-8x60.c: collapse -> assert MDP resets -> enable -> unclamp
	 * -> deassert MDP resets. The reset pulse (with the core clock live)
	 * initialises the freshly-powered block; U-Boot proved the GFS cycle
	 * alone (no reset, mdp_clk off) only floats garbage. Resets: CORE
	 * 0x0210 b21, AXI 0x0208 b13, AHB 0x020c b3.
	 */
	writel(0x1f | BIT(5), mmcc + 0x0190);		/* collapse: clamp on, enable off */
	readl(mmcc + 0x0190);
	udelay(20);
	writel(reset_core | BIT(21), mmcc + 0x0210);	/* assert MDP resets */
	writel(reset_axi  | BIT(13), mmcc + 0x0208);
	writel(reset_ahb  | BIT(3),  mmcc + 0x020c);
	readl(mmcc + 0x020c);
	udelay(20);
	writel(0x1f | BIT(5) | BIT(8), mmcc + 0x0190);	/* enable on, clamp on */
	readl(mmcc + 0x0190);
	udelay(20);
	writel(0x1f | BIT(8), mmcc + 0x0190);		/* release clamp */
	udelay(20);
	writel(reset_core & ~BIT(21), mmcc + 0x0210);	/* deassert MDP resets */
	writel(reset_axi  & ~BIT(13), mmcc + 0x0208);
	writel(reset_ahb  & ~BIT(3),  mmcc + 0x020c);
	gfs_after = readl(mmcc + 0x0190);
	udelay(50);

	iounmap(mmcc);

	DRM_DEV_INFO(dev->dev, "HACK: mmcc GFS_CTL before %#x after %#x", gfs_before, gfs_after);
	DRM_DEV_INFO(dev->dev, "HACK: mmcc resets AXI %#x(b13=%d) AHB %#x(b3=%d) CORE %#x(b21=%d)",
		     reset_axi, !!(reset_axi & BIT(13)),
		     reset_ahb, !!(reset_ahb & BIT(3)),
		     reset_core, !!(reset_core & BIT(21)));
	DRM_DEV_INFO(dev->dev, "HACK: mmcc clk en/halt: mdp %d/%d ahb %d/%d axi %d/%d lut %d/%d",
		     !!(v_00c0 & BIT(0)), !!(v_01d0 & BIT(10)),
		     !!(v_0008 & BIT(10)), !!(v_01dc & BIT(11)),
		     !!(v_0018 & BIT(23)), !!(v_01d8 & BIT(8)),
		     !!(v_016c & BIT(0)), !!(v_01e8 & BIT(13)));
}

static void read_mdp_hw_revision(struct mdp4_kms *mdp4_kms,
				 u32 *major, u32 *minor)
{
	struct drm_device *dev = mdp4_kms->dev;
	u32 version;
	int ret;

	DRM_DEV_INFO(dev->dev, "HACK: read_mdp_hw_revision before mdp4_enable");
	ret = mdp4_enable(mdp4_kms);
	DRM_DEV_INFO(dev->dev, "HACK: read_mdp_hw_revision after mdp4_enable ret=%d", ret);
	if (ret) {
		*major = 0;
		*minor = 0;
		return;
	}

	DRM_DEV_INFO(dev->dev, "HACK: read_mdp_hw_revision before version read");
	mdp4_hack_dump_mmcc(dev);
	version = mdp4_read(mdp4_kms, REG_MDP4_VERSION);
	mdp4_disable(mdp4_kms);

	DRM_DEV_INFO(dev->dev, "HACK: raw MDP4 version %#x", version);

	*major = FIELD(version, MDP4_VERSION_MAJOR);
	*minor = FIELD(version, MDP4_VERSION_MINOR);

	DRM_DEV_INFO(dev->dev, "MDP4 version v%d.%d", *major, *minor);
}

static int mdp4_kms_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(to_mdp_kms(priv->kms));
	struct msm_kms *kms = NULL;
	struct drm_gpuvm *vm;
	int ret;
	u32 major, minor;
	unsigned long max_clk;

	/* TODO: Chips that aren't apq8064 have a 200 Mhz max_clk */
	max_clk = 200000000;
	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init entry");

	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init before mdp_kms_init");
	ret = mdp_kms_init(&mdp4_kms->base, &kms_funcs);
	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init after mdp_kms_init ret=%d", ret);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to init kms\n");
		goto fail;
	}

	kms = priv->kms;

	mdp4_kms->dev = dev;

	if (mdp4_kms->vdd) {
		ret = regulator_enable(mdp4_kms->vdd);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable regulator vdd: %d\n", ret);
			goto fail;
		}
	}

	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init before core clk_set_rate %lu", max_clk);
	ret = clk_set_rate(mdp4_kms->clk, max_clk);
	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init after core clk_set_rate ret=%d", ret);

	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init before read_mdp_hw_revision");
	read_mdp_hw_revision(mdp4_kms, &major, &minor);
	DRM_DEV_INFO(dev->dev, "HACK: mdp4_kms_init after read_mdp_hw_revision major=%u minor=%u",
		     major, minor);

	if (major != 4) {
		DRM_DEV_ERROR(dev->dev, "unexpected MDP version: v%d.%d\n",
			      major, minor);
		ret = -ENXIO;
		goto fail;
	}

	mdp4_kms->rev = minor;

	if (mdp4_kms->rev >= 2) {
		if (!mdp4_kms->lut_clk) {
			DRM_DEV_ERROR(dev->dev, "failed to get lut_clk\n");
			ret = -ENODEV;
			goto fail;
		}
		clk_set_rate(mdp4_kms->lut_clk, max_clk);
	}

	/* make sure things are off before attaching iommu (bootloader could
	 * have left things on, in which case we'll start getting faults if
	 * we don't disable):
	 */
	mdp4_enable(mdp4_kms);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_ENABLE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ENABLE, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DSI_ENABLE, 0);
	mdp4_disable(mdp4_kms);
	mdelay(16);

	vm = msm_kms_init_vm(mdp4_kms->dev, NULL);
	if (IS_ERR(vm)) {
		ret = PTR_ERR(vm);
		goto fail;
	}

	kms->vm = vm;

	ret = modeset_init(mdp4_kms);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "modeset_init failed: %d\n", ret);
		goto fail;
	}

	mdp4_kms->blank_cursor_bo = msm_gem_new(dev, SZ_16K, MSM_BO_WC | MSM_BO_SCANOUT);
	if (IS_ERR(mdp4_kms->blank_cursor_bo)) {
		ret = PTR_ERR(mdp4_kms->blank_cursor_bo);
		DRM_DEV_ERROR(dev->dev, "could not allocate blank-cursor bo: %d\n", ret);
		mdp4_kms->blank_cursor_bo = NULL;
		goto fail;
	}

	ret = msm_gem_get_and_pin_iova(mdp4_kms->blank_cursor_bo, kms->vm,
			&mdp4_kms->blank_cursor_iova);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "could not pin blank-cursor bo: %d\n", ret);
		goto fail;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	return 0;

fail:
	if (kms)
		mdp4_destroy(kms);

	return ret;
}

static const struct dev_pm_ops mdp4_pm_ops = {
	.prepare = msm_kms_pm_prepare,
	.complete = msm_kms_pm_complete,
};

static int mdp4_enable_init_clk(struct device *dev, const char *name,
				       struct clk *clk)
{
	int ret;

	if (!clk)
		return 0;

	ret = clk_prepare_enable(clk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable %s for MDP4 init\n", name);

	return 0;
}

static void mdp4_disable_init_clk(struct clk *clk)
{
	if (clk)
		clk_disable_unprepare(clk);
}

static void mdp4_disable_init_clocks(struct mdp4_kms *mdp4_kms)
{
	if (!mdp4_kms->init_clocks_enabled)
		return;

	mdp4_disable_init_clk(mdp4_kms->lut_clk);
	mdp4_disable_init_clk(mdp4_kms->axi_clk);
	mdp4_disable_init_clk(mdp4_kms->pclk);
	mdp4_disable_init_clk(mdp4_kms->clk);
	mdp4_kms->init_clocks_enabled = false;
}

static int mdp4_enable_init_clocks(struct platform_device *pdev,
				   struct mdp4_kms *mdp4_kms)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = mdp4_enable_init_clk(dev, "core_clk", mdp4_kms->clk);
	if (ret)
		return ret;

	ret = mdp4_enable_init_clk(dev, "iface_clk", mdp4_kms->pclk);
	if (ret)
		goto disable_core;

	ret = mdp4_enable_init_clk(dev, "bus_clk", mdp4_kms->axi_clk);
	if (ret)
		goto disable_iface;

	ret = mdp4_enable_init_clk(dev, "lut_clk", mdp4_kms->lut_clk);
	if (ret)
		goto disable_bus;

	mdp4_kms->init_clocks_enabled = true;

	return 0;

disable_bus:
	mdp4_disable_init_clk(mdp4_kms->axi_clk);
disable_iface:
	mdp4_disable_init_clk(mdp4_kms->pclk);
disable_core:
	mdp4_disable_init_clk(mdp4_kms->clk);
	return ret;
}

static int mdp4_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mdp4_kms *mdp4_kms;
	int ret;
	int irq;

	mdp4_kms = devm_kzalloc(dev, sizeof(*mdp4_kms), GFP_KERNEL);
	if (!mdp4_kms)
		return -ENOMEM;

	mdp4_kms->mmio = msm_ioremap(pdev, NULL);
	if (IS_ERR(mdp4_kms->mmio))
		return PTR_ERR(mdp4_kms->mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to get irq\n");

	mdp4_kms->base.base.irq = irq;

	/*
	 * Legacy MDP4 DTs model the block power rail as a regulator. If DT
	 * provides a power domain instead, genpd owns that sequencing and the
	 * regulator fallback would only trigger a dummy-regulator warning.
	 */
	if (!pdev->dev.of_node ||
	    !of_property_present(pdev->dev.of_node, "power-domains")) {
		mdp4_kms->vdd = devm_regulator_get_exclusive(&pdev->dev, "vdd");
		if (IS_ERR(mdp4_kms->vdd))
			mdp4_kms->vdd = NULL;
	}

	mdp4_kms->clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(mdp4_kms->clk))
		return dev_err_probe(dev, PTR_ERR(mdp4_kms->clk), "failed to get core_clk\n");

	mdp4_kms->pclk = devm_clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(mdp4_kms->pclk))
		mdp4_kms->pclk = NULL;

	mdp4_kms->axi_clk = devm_clk_get(&pdev->dev, "bus_clk");
	if (IS_ERR(mdp4_kms->axi_clk))
		return dev_err_probe(dev, PTR_ERR(mdp4_kms->axi_clk), "failed to get axi_clk\n");

	/*
	 * This is required for revn >= 2. Handle errors here and let the kms
	 * init bail out if the clock is not provided.
	 */
	mdp4_kms->lut_clk = devm_clk_get_optional(&pdev->dev, "lut_clk");
	if (IS_ERR(mdp4_kms->lut_clk))
		return dev_err_probe(dev, PTR_ERR(mdp4_kms->lut_clk), "failed to get lut_clk\n");

	/* Keep MDP clocks owned before msm_drv_probe() removes firmware fbdevs. */
	ret = mdp4_enable_init_clocks(pdev, mdp4_kms);
	if (ret)
		return ret;

	ret = msm_drv_probe(&pdev->dev, mdp4_kms_init, &mdp4_kms->base.base);
	if (ret)
		mdp4_disable_init_clocks(mdp4_kms);

	return ret;
}

static void mdp4_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &msm_drm_ops);
}

static const struct of_device_id mdp4_dt_match[] = {
	{ .compatible = "qcom,mdp4" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mdp4_dt_match);

static struct platform_driver mdp4_platform_driver = {
	.probe      = mdp4_probe,
	.remove     = mdp4_remove,
	.shutdown   = msm_kms_shutdown,
	.driver_managed_dma = true,
	.driver     = {
		.name   = "mdp4",
		.of_match_table = mdp4_dt_match,
		.pm     = &mdp4_pm_ops,
	},
};

void __init msm_mdp4_register(void)
{
	platform_driver_register(&mdp4_platform_driver);
}

void __exit msm_mdp4_unregister(void)
{
	platform_driver_unregister(&mdp4_platform_driver);
}
