// SPDX-License-Identifier: GPL-2.0-only
/*
 * Magnachip AMS452GP32 MIPI-DSI panel driver (Magna OLED WVGA 480x800)
 * Used on Samsung Express Att (SGH-I437) and similar.
 * Ported from downstream mipi_magna_oled_video_wvga_pt.c (SMD_AMS452GP32).
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct magnachip_ams452gp32 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *vddp;
	struct regulator *iovcc;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *dcdc_en_gpio;

	/* Read once during the first prepare() via DCS read of 0xDC. */
	u8	panel_id3;
	bool	panel_id3_valid;

	u8	mtp[21];          /* Per-panel MTP calibration data */
	bool	mtp_valid;
	u8	computed_gamma[24]; /* Smart-dimming gamma (no 0xF9 prefix) */
	bool	gamma_valid;
};

static int diag_late_on_phase;
module_param_named(diag_late_on_phase, diag_late_on_phase, int, 0644);
MODULE_PARM_DESC(diag_late_on_phase,
		 "Diagnostic late-on phase: 0 skip, 1 enable/HS, 2 enable/LP, 3 prepare/HS");

static bool diag_sleep_in_on_unprepare;
module_param_named(diag_sleep_in_on_unprepare, diag_sleep_in_on_unprepare,
		   bool, 0644);
MODULE_PARM_DESC(diag_sleep_in_on_unprepare,
		 "Send DCS sleep-in during unprepare instead of only powering off");

static bool diag_verbose = true;
module_param_named(diag_verbose, diag_verbose, bool, 0644);
MODULE_PARM_DESC(diag_verbose, "Log diagnostic panel command phases");

static inline struct magnachip_ams452gp32 *to_magnachip_ams452gp32(struct drm_panel *panel)
{
	return container_of(panel, struct magnachip_ams452gp32, panel);
}

static int magnachip_ams452gp32_late_on_phase(struct device *dev)
{
	if (diag_late_on_phase >= 0 && diag_late_on_phase <= 3)
		return diag_late_on_phase;

	dev_warn(dev, "diag: invalid late_on_phase=%d, treating as 0\n",
		 diag_late_on_phase);

	return 0;
}

static struct regulator *magnachip_ams452gp32_get_optional_supply(struct device *dev,
								 const char *id)
{
	struct regulator *supply;

	supply = devm_regulator_get_optional(dev, id);
	if (IS_ERR(supply) && PTR_ERR(supply) == -ENODEV)
		return NULL;

	return supply;
}

static int magnachip_ams452gp32_enable_supplies(struct magnachip_ams452gp32 *ctx)
{
	int ret;

	if (ctx->vddp) {
		ret = regulator_enable(ctx->vddp);
		if (ret)
			return ret;
	}

	if (ctx->iovcc) {
		ret = regulator_enable(ctx->iovcc);
		if (ret) {
			if (ctx->vddp)
				regulator_disable(ctx->vddp);
			return ret;
		}
	}

	return 0;
}

static void magnachip_ams452gp32_disable_supplies(struct magnachip_ams452gp32 *ctx)
{
	if (ctx->iovcc)
		regulator_disable(ctx->iovcc);

	if (ctx->vddp)
		regulator_disable(ctx->vddp);
}

/*
 * Force DCS Long Write (DT=0x39) for all commands.
 *
 * Downstream (mipi_magna_oled_video_wvga_pt.c) sends every command -
 * including 1-2 byte DCS commands like sleep_out (0x11) and display_on
 * (0x29) - as DTYPE_DCS_LWRITE.  The standard mipi_dsi_dcs_write_buffer()
 * helper automatically picks DCS Short Write (0x05/0x15) for payloads
 * <= 2 bytes, which this panel's firmware does not handle.
 */
static void dcs_long_write(struct mipi_dsi_multi_context *ctx, const char *label,
			   const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_DCS_LONG_WRITE,
		.tx_buf = data,
		.tx_len = len,
	};
	ssize_t ret;

	if (ctx->accum_err)
		return;

	if (diag_verbose)
		dev_info(&dsi->dev,
			 "diag: cmd=%s flags=0x%lx tx=%s data0=0x%02x len=%zu\n",
			 label, dsi->mode_flags,
			 dsi->mode_flags & MIPI_DSI_MODE_LPM ? "LP" : "HS",
			 len ? *(const u8 *)data : 0, len);

	if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
		msg.flags |= MIPI_DSI_MSG_USE_LPM;

	ret = dsi->host->ops->transfer(dsi->host, &msg);
	if (ret < 0) {
		if (diag_verbose)
			dev_err(&dsi->dev,
				"diag: cmd=%s failed: %zd\n", label, ret);
		ctx->accum_err = ret;
	}
}

/* Manufacturer command set (from downstream samsung_display_on_cmds) */
#define MCS_LEVEL2	0xF0
#define MCS_KEY		0xF1
#define MCS_LEVEL3	0xFC
#define MCS_DISPLAY_CTL	0xF2
#define MCS_GTCON	0xF7
#define MCS_LTPS	0xF8
#define MCS_GAMMA_FLAG	0xFB
#define MCS_GAMMA_SET	0xF9
#define MCS_ELVSS_ON	0xB1
#define MCS_ELVSS_COND	0xB2
#define MCS_POWER_CTL	0xF4
#define MCS_NVM_REF	0xB3
#define MCS_GLOBAL_PARAM	0xB0
#define MCS_EOT	0xE0
#define MCS_ACL		0xC0

/*
 * ===== EA8868 Smart Dimming ================================================
 * Ported from downstream smart_mtp_ea8868.c (Samsung Electronics, jb09.kim).
 *
 * Computes per-panel gamma correction from 21-byte MTP calibration data read
 * from the EA8868 AMOLED controller.  Without this, the static 300CD center
 * reference gamma produces visible red/green sub-pixel splatter because it
 * does not account for per-panel manufacturing variation.
 *
 * Algorithm:
 *  1. Parse MTP into signed per-color offsets for 7 reference gray levels
 *  2. Compute reference voltages at 8 points (V0..V255) using MTP + centers
 *  3. Interpolate a full 256-entry gray-scale table
 *  4. For target brightness, reverse-map through a 2.2-gamma curve to get
 *     the 24-byte register payload, then subtract MTP offsets
 * ==========================================================================
 */

/* Fixed-point: 14-bit shift, multiplier = 16384 */
#define SD_SHIFT	14
#define SD_VREG0	73728	/* 4.5 * (1 << 14) */
#define SD_GRAY_MAX	256
#define SD_NREF		8	/* V0, V1, V15, V35, V59, V87, V171, V255 */
#define SD_GAMMA_LEN	24

/* Gray-level indices of the 8 reference voltage points */
static const int sd_gray_pts[SD_NREF] = { 0, 1, 15, 35, 59, 87, 171, 255 };

/*
 * 300CD center references per color: {V255_LSB, V171, V87, V59, V35, V15, V1}
 * From smart_mtp_ea8868.h V255_300CD_*_LSB, V171_300CD_*, etc.
 */
static const u8 sd_center[3][7] = {
	{ 0xC0, 0xBA, 0xA5, 0xCF, 0xC0, 0xC8, 0x55 }, /* R */
	{ 0xE0, 0xB8, 0xA3, 0xCD, 0xBE, 0xC9, 0x55 }, /* G */
	{ 0xE7, 0xB8, 0xA3, 0xCD, 0xBE, 0xCF, 0x55 }, /* B */
};

/* Fallback gamma: center references, used when MTP hasn't been read yet */
static const u8 sd_gamma_fallback[SD_GAMMA_LEN] = {
	0x00, 0xC0, 0xBA, 0xA5, 0xCF, 0xC0, 0xC8, 0x55,
	0x00, 0xE0, 0xB8, 0xA3, 0xCD, 0xBE, 0xC9, 0x55,
	0x00, 0xE7, 0xB8, 0xA3, 0xCD, 0xBE, 0xCF, 0x55,
};

/* Non-linear interpolation: V1 -> V15 (13 steps, gray 2..14) */
static const int sd_nl_v1_v15[] = {
	49, 44, 39, 34, 29, 24, 20, 16, 12, 8, 6, 4, 2
};

/* Non-linear interpolation: V15 -> V35 (19 steps, gray 16..34) */
static const int sd_nl_v15_v35[] = {
	132, 124, 116, 108, 100, 92, 84, 76, 69, 62,
	55, 48, 42, 36, 30, 24, 18, 12, 6
};

/* ((i/255)^2.2) * 16384 - from smart_mtp_2p2_gamma.h candela_coeff_2p2[] */
static const int sd_coeff_2p2[SD_GRAY_MAX] = {
	    0,     0,     0,     1,     2,     3,     4,     6,
	    8,    10,    13,    16,    20,    23,    28,    32,
	   37,    42,    48,    54,    61,    67,    75,    82,
	   90,    99,   108,   117,   127,   137,   148,   159,
	  170,   182,   195,   207,   221,   234,   249,   263,
	  278,   294,   310,   326,   343,   361,   379,   397,
	  416,   435,   455,   475,   496,   517,   539,   561,
	  584,   607,   630,   654,   679,   704,   730,   756,
	  783,   810,   838,   866,   894,   924,   953,   984,
	 1014,  1046,  1077,  1110,  1142,  1176,  1210,  1244,
	 1279,  1314,  1350,  1387,  1424,  1461,  1499,  1538,
	 1577,  1617,  1657,  1698,  1739,  1781,  1824,  1866,
	 1910,  1954,  1999,  2044,  2089,  2136,  2182,  2230,
	 2278,  2326,  2375,  2425,  2475,  2526,  2577,  2629,
	 2681,  2734,  2788,  2842,  2896,  2951,  3007,  3064,
	 3121,  3178,  3236,  3295,  3354,  3414,  3474,  3535,
	 3597,  3659,  3721,  3785,  3849,  3913,  3978,  4044,
	 4110,  4177,  4244,  4312,  4380,  4450,  4519,  4590,
	 4660,  4732,  4804,  4877,  4950,  5024,  5098,  5173,
	 5249,  5325,  5402,  5480,  5558,  5637,  5716,  5796,
	 5876,  5957,  6039,  6121,  6204,  6288,  6372,  6457,
	 6542,  6628,  6715,  6802,  6890,  6978,  7067,  7157,
	 7247,  7338,  7429,  7522,  7614,  7708,  7802,  7896,
	 7992,  8087,  8184,  8281,  8379,  8477,  8576,  8676,
	 8776,  8877,  8978,  9080,  9183,  9287,  9391,  9495,
	 9601,  9707,  9813,  9920, 10028, 10137, 10246, 10355,
	10466, 10577, 10688, 10801, 10914, 11027, 11141, 11256,
	11372, 11488, 11605, 11722, 11840, 11959, 12078, 12198,
	12319, 12440, 12562, 12685, 12808, 12932, 13057, 13182,
	13308, 13434, 13561, 13689, 13818, 13947, 14077, 14207,
	14338, 14470, 14602, 14736, 14869, 15004, 15139, 15274,
	15411, 15548, 15686, 15824, 15963, 16103, 16243, 16384,
};

/* 300 * ((i/255)^2.2) * 16384 - from smart_mtp_2p2_gamma.h curve_2p2[] */
static const int sd_curve_2p2[SD_GRAY_MAX] = {
	      0,      25,     115,     280,     528,     862,
	   1287,    1807,    2424,    3141,    3960,    4884,
	   5915,    7054,    8303,    9663,   11138,   12727,
	  14432,   16255,   18197,   20259,   22442,   24748,
	  27177,   29730,   32410,   35215,   38149,   41210,
	  44402,   47723,   51175,   54760,   58477,   62328,
	  66313,   70433,   74689,   79081,   83611,   88279,
	  93085,   98031,  103116,  108343,  113710,  119219,
	 124871,  130666,  136605,  142687,  148915,  155288,
	 161807,  168473,  175285,  182245,  189353,  196610,
	 204016,  211571,  219277,  227133,  235140,  243299,
	 251610,  260073,  268690,  277459,  286383,  295461,
	 304693,  314081,  323624,  333324,  343180,  353192,
	 363362,  373690,  384175,  394819,  405622,  416585,
	 427707,  438988,  450431,  462034,  473798,  485724,
	 497812,  510061,  522474,  535049,  547788,  560691,
	 573757,  586988,  600384,  613944,  627670,  641562,
	 655619,  669844,  684234,  698792,  713517,  728410,
	 743470,  758699,  774097,  789663,  805399,  821304,
	 837379,  853624,  870040,  886626,  903383,  920311,
	 937411,  954683,  972127,  989744, 1007533, 1025495,
	1043630, 1061939, 1080422, 1099079, 1117910, 1136916,
	1156097, 1175452, 1194984, 1214691, 1234574, 1254633,
	1274869, 1295281, 1315870, 1336637, 1357581, 1378703,
	1400003, 1421481, 1443138, 1464973, 1486987, 1509181,
	1531554, 1554106, 1576839, 1599752, 1622845, 1646119,
	1669574, 1693210, 1717027, 1741026, 1765206, 1789569,
	1814114, 1838841, 1863751, 1888844, 1914120, 1939580,
	1965223, 1991050, 2017061, 2043257, 2069636, 2096201,
	2122950, 2149885, 2177005, 2204310, 2231801, 2259478,
	2287341, 2315391, 2343627, 2372050, 2400660, 2429457,
	2458442, 2487614, 2516974, 2546522, 2576258, 2606183,
	2636296, 2666598, 2697089, 2727769, 2758639, 2789698,
	2820947, 2852386, 2884015, 2915834, 2947844, 2980045,
	3012436, 3045019, 3077793, 3110758, 3143915, 3177264,
	3210805, 3244538, 3278464, 3312582, 3346893, 3381396,
	3416093, 3450984, 3486067, 3521345, 3556816, 3592481,
	3628340, 3664394, 3700642, 3737085, 3773723, 3810556,
	3847584, 3884808, 3922227, 3959842, 3997652, 4035659,
	4073862, 4112262, 4150858, 4189651, 4228641, 4267827,
	4307212, 4346793, 4386572, 4426549, 4466724, 4507096,
	4547668, 4588437, 4629405, 4670572, 4711937, 4753502,
	4795266, 4837229, 4879391, 4921754,
};

/*
 * Compute the 8 reference voltage levels for one color from MTP offsets.
 * mtp[7] = {V255_MSB(unused), V255_LSB, V171, V87, V59, V35, V15}
 * center[7] = {V255_LSB, V171, V87, V59, V35, V15, V1}
 * voltage[8] = output {V0, V1, V15, V35, V59, V87, V171, V255}
 */
static void sd_compute_voltages(const u8 mtp[7], const u8 center[7],
				int voltage[8])
{
	u64 tmp;
	int add;

	/* V0 = VREG0 (constant) */
	voltage[0] = SD_VREG0;

	/* V255 = VREG0 - VREG0 * (100 + mtp_lsb + center_lsb) / 600 */
	add = (s8)mtp[1] + center[0];
	tmp = (u64)(100 + add) << SD_SHIFT;
	do_div(tmp, 600);
	voltage[7] = SD_VREG0 - (int)(((u64)SD_VREG0 * tmp) >> SD_SHIFT);

	/* V1 = VREG0 - VREG0 * (4 + center_v1) / 600  (MTP for V1 is 0) */
	add = center[6];
	tmp = (u64)(4 + add) << SD_SHIFT;
	do_div(tmp, 600);
	voltage[1] = SD_VREG0 - (int)(((u64)SD_VREG0 * tmp) >> SD_SHIFT);

	/* V171 = V1 - (V1 - V255) * (64 + mtp + center) / 320 */
	add = (s8)mtp[2] + center[1];
	tmp = (u64)(64 + add) << SD_SHIFT;
	do_div(tmp, 320);
	voltage[6] = voltage[1] -
		     (int)(((u64)(voltage[1] - voltage[7]) * tmp) >> SD_SHIFT);

	/* V87 = V1 - (V1 - V171) * (64 + mtp + center) / 320 */
	add = (s8)mtp[3] + center[2];
	tmp = (u64)(64 + add) << SD_SHIFT;
	do_div(tmp, 320);
	voltage[5] = voltage[1] -
		     (int)(((u64)(voltage[1] - voltage[6]) * tmp) >> SD_SHIFT);

	/* V59 = V1 - (V1 - V87) * (64 + mtp + center) / 320 */
	add = (s8)mtp[4] + center[3];
	tmp = (u64)(64 + add) << SD_SHIFT;
	do_div(tmp, 320);
	voltage[4] = voltage[1] -
		     (int)(((u64)(voltage[1] - voltage[5]) * tmp) >> SD_SHIFT);

	/* V35 = V1 - (V1 - V59) * (64 + mtp + center) / 320 */
	add = (s8)mtp[5] + center[4];
	tmp = (u64)(64 + add) << SD_SHIFT;
	do_div(tmp, 320);
	voltage[3] = voltage[1] -
		     (int)(((u64)(voltage[1] - voltage[4]) * tmp) >> SD_SHIFT);

	/* V15 = V1 - (V1 - V35) * (20 + mtp + center) / 320 */
	add = (s8)mtp[6] + center[5];
	tmp = (u64)(20 + add) << SD_SHIFT;
	do_div(tmp, 320);
	voltage[2] = voltage[1] -
		     (int)(((u64)(voltage[1] - voltage[3]) * tmp) >> SD_SHIFT);
}

/*
 * Interpolate the full 256-entry gray scale from 8 reference voltages.
 * Uses non-linear tables for V1->V15 and V15->V35, linear for the rest.
 */
static void sd_gen_gray(const int voltage[8], int *gray)
{
	int cnt, cal_cnt;
	u64 r;

	/* Plant the 8 reference voltages */
	for (cnt = 0; cnt < SD_NREF; cnt++)
		gray[sd_gray_pts[cnt]] = voltage[cnt];

	/* V1 -> V15: non-linear (gray 2..14) */
	for (cal_cnt = 0; cal_cnt < 13; cal_cnt++) {
		r = (u64)(gray[1] - gray[15]) * sd_nl_v1_v15[cal_cnt];
		r <<= SD_SHIFT;
		do_div(r, 54);
		gray[2 + cal_cnt] = gray[15] + (int)(r >> SD_SHIFT);
	}

	/* V15 -> V35: non-linear (gray 16..34) */
	for (cal_cnt = 0; cal_cnt < 19; cal_cnt++) {
		r = (u64)(gray[15] - gray[35]) * sd_nl_v15_v35[cal_cnt];
		r <<= SD_SHIFT;
		do_div(r, 140);
		gray[16 + cal_cnt] = gray[35] + (int)(r >> SD_SHIFT);
	}

	/* V35 -> V59: linear (gray 36..58), coeff=23, mul=1, denom=24 */
	for (cal_cnt = 0; cal_cnt < 23; cal_cnt++) {
		r = (u64)(gray[35] - gray[59]) * (23 - cal_cnt);
		r <<= SD_SHIFT;
		do_div(r, 24);
		gray[36 + cal_cnt] = gray[59] + (int)(r >> SD_SHIFT);
	}

	/* V59 -> V87: linear (gray 60..86), coeff=27, mul=1, denom=28 */
	for (cal_cnt = 0; cal_cnt < 27; cal_cnt++) {
		r = (u64)(gray[59] - gray[87]) * (27 - cal_cnt);
		r <<= SD_SHIFT;
		do_div(r, 28);
		gray[60 + cal_cnt] = gray[87] + (int)(r >> SD_SHIFT);
	}

	/* V87 -> V171: linear (gray 88..170), coeff=83, mul=1, denom=84 */
	for (cal_cnt = 0; cal_cnt < 83; cal_cnt++) {
		r = (u64)(gray[87] - gray[171]) * (83 - cal_cnt);
		r <<= SD_SHIFT;
		do_div(r, 84);
		gray[88 + cal_cnt] = gray[171] + (int)(r >> SD_SHIFT);
	}

	/* V171 -> V255: linear (gray 172..254), coeff=83, mul=1, denom=84 */
	for (cal_cnt = 0; cal_cnt < 83; cal_cnt++) {
		r = (u64)(gray[171] - gray[255]) * (83 - cal_cnt);
		r <<= SD_SHIFT;
		do_div(r, 84);
		gray[172 + cal_cnt] = gray[255] + (int)(r >> SD_SHIFT);
	}
}

/* Search curve_2p2 for the gray index nearest to target candela */
static int sd_search(long long candela)
{
	long long d1, d2;
	int i;

	for (i = 0; i < SD_GRAY_MAX - 1; i++) {
		d1 = candela - sd_curve_2p2[i];
		d2 = candela - sd_curve_2p2[i + 1];

		if (d2 < 0)
			return (d1 + d2) <= 0 ? i : i + 1;
		if (d1 == 0)
			return i;
		if (d2 == 0)
			return i + 1;
	}
	return SD_GRAY_MAX - 1;
}

/* Subtract MTP offset: value - offset, clamped to [0, 255] */
static u8 sd_offset_cal(int offset, u8 value)
{
	int result = (int)value - offset;

	if (result < 0)
		return 0;
	if (result > 255)
		return 0xFF;
	return (u8)result;
}

/*
 * Generate the 24-byte gamma register payload for a given brightness.
 *
 * @mtp:        21-byte raw MTP from panel (3 colors x 7 bytes)
 * @brightness: target candela (e.g. 300)
 * @out:        24-byte output (gamma payload, no MCS_GAMMA_SET prefix)
 */
static void sd_generate_gamma(const u8 mtp[21], int brightness, u8 out[24])
{
	int bl_index[7]; /* V1, V15, V35, V59, V87, V171, V255 */
	int voltage[8];
	int *gray;
	int c, cnt;

	gray = kmalloc_array(SD_GRAY_MAX, sizeof(int), GFP_KERNEL);
	if (!gray) {
		memcpy(out, sd_gamma_fallback, SD_GAMMA_LEN);
		return;
	}

	/* Compute brightness-level gray indices (color-independent) */
	for (cnt = 0; cnt < 7; cnt++) {
		long long target = (long long)sd_coeff_2p2[sd_gray_pts[cnt + 1]]
				   * brightness;
		bl_index[cnt] = sd_search(target);
	}

	/* Process each color: R=0, G=1, B=2 */
	for (c = 0; c < 3; c++) {
		int base = c * 8;    /* offset into output */
		int mtp_off = c * 7; /* offset into raw MTP */
		u64 r;
		int val, denom;

		sd_compute_voltages(&mtp[mtp_off], sd_center[c], voltage);
		sd_gen_gray(voltage, gray);

		/*
		 * Reverse-map: convert gray table voltages back to the
		 * register values the panel expects.
		 */

		/* V255: 2-byte value (MSB, LSB) */
		r = (u64)(SD_VREG0 - gray[bl_index[6]]) * 600;
		do_div(r, (u32)SD_VREG0);
		val = (int)r - 100;
		out[base + 0] = (val >> 8) & 0xff;
		out[base + 1] = val & 0xff;

		/* V171: (V1 - V171_target) * 320 / (V1 - V255_target) - 64 */
		denom = gray[1] - gray[bl_index[6]];
		if (denom > 0) {
			r = (u64)(gray[1] - gray[bl_index[5]]) * 320;
			do_div(r, (u32)denom);
			out[base + 2] = ((int)r - 64) & 0xff;
		} else {
			out[base + 2] = sd_center[c][1];
		}

		/* V87: ref = V171 */
		denom = gray[1] - gray[bl_index[5]];
		if (denom > 0) {
			r = (u64)(gray[1] - gray[bl_index[4]]) * 320;
			do_div(r, (u32)denom);
			out[base + 3] = ((int)r - 64) & 0xff;
		} else {
			out[base + 3] = sd_center[c][2];
		}

		/* V59: ref = V87 */
		denom = gray[1] - gray[bl_index[4]];
		if (denom > 0) {
			r = (u64)(gray[1] - gray[bl_index[3]]) * 320;
			do_div(r, (u32)denom);
			out[base + 4] = ((int)r - 64) & 0xff;
		} else {
			out[base + 4] = sd_center[c][3];
		}

		/* V35: ref = V59 */
		denom = gray[1] - gray[bl_index[3]];
		if (denom > 0) {
			r = (u64)(gray[1] - gray[bl_index[2]]) * 320;
			do_div(r, (u32)denom);
			out[base + 5] = ((int)r - 64) & 0xff;
		} else {
			out[base + 5] = sd_center[c][4];
		}

		/* V15: ref = V35, coefficient = 20 (not 64) */
		denom = gray[1] - gray[bl_index[2]];
		if (denom > 0) {
			r = (u64)(gray[1] - gray[bl_index[1]]) * 320;
			do_div(r, (u32)denom);
			out[base + 6] = ((int)r - 20) & 0xff;
		} else {
			out[base + 6] = sd_center[c][5];
		}

		/* V1: always the 300CD center reference (no MTP adjust) */
		out[base + 7] = sd_center[c][6];

		/* Subtract MTP offsets (panel re-adds them internally) */
		out[base + 1] = sd_offset_cal((s8)mtp[mtp_off + 1],
					      out[base + 1]);
		out[base + 2] = sd_offset_cal((s8)mtp[mtp_off + 2],
					      out[base + 2]);
		out[base + 3] = sd_offset_cal((s8)mtp[mtp_off + 3],
					      out[base + 3]);
		out[base + 4] = sd_offset_cal((s8)mtp[mtp_off + 4],
					      out[base + 4]);
		out[base + 5] = sd_offset_cal((s8)mtp[mtp_off + 5],
					      out[base + 5]);
		out[base + 6] = sd_offset_cal((s8)mtp[mtp_off + 6],
					      out[base + 6]);
	}

	kfree(gray);
}
/* ===== End EA8868 Smart Dimming =========================================== */

static void magnachip_ams452gp32_power_on(struct magnachip_ams452gp32 *ctx)
{
	/* Power sequence: DCDC enable -> panel enable -> reset toggle.
	 * Downstream: gpio_set_value(GPIO_LCD_22V_EN, 1) before reset.
	 */
	if (ctx->dcdc_en_gpio) {
		gpiod_set_value_cansleep(ctx->dcdc_en_gpio, 1);
		usleep_range(5000, 10000);
	}

	if (ctx->enable_gpio) {
		gpiod_set_value_cansleep(ctx->enable_gpio, 1);
		usleep_range(10000, 15000);
	}

	/*
	 * 3-step reset matching downstream active_reset_ldi():
	 *  1. Deassert (pin HIGH) -> 500us
	 *  2. Assert  (pin LOW)  -> 5ms
	 *  3. Deassert (pin HIGH) -> wait 10ms for panel ready
	 *
	 * GPIO_ACTIVE_LOW: gpiod_set_value(0) = pin HIGH (deassert)
	 *                   gpiod_set_value(1) = pin LOW  (assert)
	 *
	 * Downstream also has 5ms LP11 before reset + 10ms after.
	 * The DSI PHY is already enabled (lanes in LP11) when prepare()
	 * runs, so LP11 context is satisfied.
	 */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);  /* deassert first */
	usleep_range(500, 1000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);  /* assert reset */
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);  /* deassert reset */
	msleep(10);  /* downstream: usleep(10000) after active_reset */
}

static void magnachip_ams452gp32_power_off(struct magnachip_ams452gp32 *ctx)
{
	/* Assert reset before cutting power to avoid leakage */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);

	if (ctx->dcdc_en_gpio)
		gpiod_set_value_cansleep(ctx->dcdc_en_gpio, 0);
}

static int magnachip_ams452gp32_on(struct magnachip_ams452gp32 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/*
	 * All command buffers below match the exact downstream arrays from
	 * mipi_magna_oled_video_wvga_pt.c.  Every command - including 1-2
	 * byte DCS commands - is sent via dcs_long_write() to produce
	 * DT=0x39 (DCS Long Write) on the wire, matching downstream's
	 * DTYPE_DCS_LWRITE for all entries in samsung_display_on_cmds[].
	 */
	static const u8 level2_cmd[]    = { MCS_LEVEL2, 0x5a, 0x5a };
	static const u8 sleep_out[]     = { MIPI_DCS_EXIT_SLEEP_MODE, 0x00 };
	static const u8 key_cmd[]       = { MCS_KEY, 0x5a, 0x5a };
	static const u8 level3_cmd[]    = { MCS_LEVEL3, 0x5a, 0x5a };
	static const u8 display_ctl[]   = { MCS_DISPLAY_CTL,
					    0x02, 0x03, 0x69, 0x10, 0x10 };
	static const u8 gtcon[]         = { MCS_GTCON, 0x09 };
	static const u8 ltps_buf[]      = { MCS_LTPS,
		0x05, 0x71, 0xac, 0x80, 0x8f, 0x0f, 0x48, 0x00, 0x00, 0x3a,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x06, 0x24, 0x24,
		0x24, 0x00, 0x00 };
	static const u8 gamma_flag[]    = { MCS_GAMMA_FLAG, 0x00, 0x5a };
	/*
	 * GAMMA_SET (0xF9) - 24-byte gamma table { V255_MSB, V255_LSB,
	 * V171, V87, V59, V35, V15, V1 } per color (R, G, B).
	 *
	 * If smart-dimming has already computed a per-panel gamma from
	 * MTP (ctx->gamma_valid), use that.  Otherwise fall back to the
	 * 300CD center references - these are what generate_gamma()
	 * would emit for a panel whose MTP offsets are all zero.
	 */
	u8 gamma_buf[25]; /* MCS_GAMMA_SET + 24-byte payload */

	gamma_buf[0] = MCS_GAMMA_SET;
	if (ctx->gamma_valid)
		memcpy(&gamma_buf[1], ctx->computed_gamma, SD_GAMMA_LEN);
	else
		memcpy(&gamma_buf[1], sd_gamma_fallback, SD_GAMMA_LEN);
	static const u8 gamma_flag2[]   = { MCS_GAMMA_FLAG, 0x00, 0xa5 };
	static const u8 elvss_on[]      = { MCS_ELVSS_ON, 0x0b, 0x16 };
	/*
	 * ELVSS_COND (0xB2) - AMOLED ELVSS supply level for the four
	 * sub-blocks. Downstream computes this per-cd via
	 * `set_elvss_level(bl)`:
	 *
	 *   if (id2 == 0x4A)  elvss = id3 + GET_ELVSS_ID[cd];
	 *   else              elvss = GET_DEFAULT_ELVSS_ID[cd];
	 *
	 * with cap LCD_ELVSS_RESULT_LIMIT (0x1F).
	 *
	 * For 180cd (cd-index 15): GET_ELVSS_ID[15] = LCD_ELVSS_DELTA_200CD = 0x06.
	 * For our panel (id2=0x4A, id3=0x15): 0x15 + 0x06 = 0x1B.
	 *
	 * Mainline previously sent the absolute fallback (0x0B) without
	 * adding id3 - wrong absolute ELVSS made the panel's internal
	 * regulator slowly drift, producing the gradual brightening.
	 *
	 * Buffer is non-const so we can patch in the runtime value below.
	 */
	u8 elvss_cond[] = { MCS_ELVSS_COND, 0x0B, 0x0B, 0x0B, 0x0B };
	static const u8 power_ctl[]     = { MCS_POWER_CTL,
					    0xab, 0x6a, 0x00, 0x55, 0x03 };
	static const u8 nvm_ref[]       = { MCS_NVM_REF, 0x00 };
	static const u8 global_param[]  = { MCS_GLOBAL_PARAM, 0x06 };
	static const u8 eot_cmd[]       = { MCS_EOT, 0x41 };
	/*
	 * ACL_OFF (0xC0, 0x00) - disable Adaptive Content Luminance.
	 * Downstream sends acl_off via samsung_panel_acl_update_cmds[]
	 * whenever the per-cd ACL table entry is empty (every entry is
	 * empty for this panel by default). Without it, the panel keeps
	 * ACL active, which dynamically alters luminance based on frame
	 * content - producing the slow brightness ramp on a static image.
	 */
	static const u8 acl_off[]       = { MCS_ACL, 0x00 };

	/*
	 * Downstream sends init commands in HS mode for this panel
	 * (DSI_COMMAND_MODE_DMA_CTRL = 0x10000000 = HS, not LP).
	 */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	/* Downstream: samsung_display_on_cmds[] sequence, all DCS Long Write */
	dcs_long_write(&dsi_ctx, "ready_to_on:level2", level2_cmd,
		       sizeof(level2_cmd));
	dcs_long_write(&dsi_ctx, "ready_to_on:sleep_out", sleep_out,
		       sizeof(sleep_out));
	/*
	 * MIPI DCS spec: Sleep_Out (0x11) requires the DDIC's internal
	 * regulator + RAM to power up before any further commands are
	 * accepted.  Datasheets typically specify ~120 ms.  Downstream
	 * sends only a 10 ms wait field on dsi_cmd_desc here, but in
	 * downstream the mipi_dsi_cmds_tx() path serialises a long
	 * sequence of LP cmd packets afterwards (level2/level3 unlock,
	 * gtcon, ltps, gamma, elvss...) -- their cumulative LP transfer
	 * time at ~10 Mbps adds up to >100 ms before display_on, giving
	 * the panel time to wake.  Mainline sends those same cmds in HS
	 * mode (microseconds each), which arrives WAY before the panel's
	 * internal MCU is ready -- the panel silently drops them and the
	 * subsequent display_on lights an unconfigured RAM (black).
	 * Honour the MIPI DCS spec explicitly.
	 */
	mipi_dsi_msleep(&dsi_ctx, 120);

	dcs_long_write(&dsi_ctx, "ready_to_on:key", key_cmd, sizeof(key_cmd));
	dcs_long_write(&dsi_ctx, "ready_to_on:level2", level2_cmd,
		       sizeof(level2_cmd));
	dcs_long_write(&dsi_ctx, "ready_to_on:level3", level3_cmd,
		       sizeof(level3_cmd));

	dcs_long_write(&dsi_ctx, "ready_to_on:display_ctl", display_ctl,
		       sizeof(display_ctl));
	mipi_dsi_msleep(&dsi_ctx, 10);  /* downstream: 10ms wait */
	dcs_long_write(&dsi_ctx, "ready_to_on:gtcon", gtcon, sizeof(gtcon));

	dcs_long_write(&dsi_ctx, "ready_to_on:ltps", ltps_buf,
		       sizeof(ltps_buf));

	dcs_long_write(&dsi_ctx, "ready_to_on:gamma_flag", gamma_flag,
		       sizeof(gamma_flag));
	dcs_long_write(&dsi_ctx, "ready_to_on:gamma", gamma_buf,
		       sizeof(gamma_buf));
	dcs_long_write(&dsi_ctx, "ready_to_on:gamma_flag2", gamma_flag2,
		       sizeof(gamma_flag2));

	dcs_long_write(&dsi_ctx, "ready_to_on:elvss_on", elvss_on,
		       sizeof(elvss_on));

	/*
	 * Patch ELVSS_COND with runtime value: id3 + 0x06 (delta_200cd
	 * for our 180cd default brightness), capped at 0x1F. id3 comes
	 * from the manufacture-id read on a previous prepare() pass; on
	 * the very first boot it isn't known yet, so we fall back to the
	 * 0x4A-blessed default 0x1B (= 0x15 + 0x06) until the panel ID can
	 * be read.
	 */
	{
		u8 elvss = ctx->panel_id3_valid ? ctx->panel_id3 : 0x15;

		elvss += 0x06;
		if (elvss > 0x1F)
			elvss = 0x1F;
		elvss_cond[1] = elvss;
		elvss_cond[2] = elvss;
		elvss_cond[3] = elvss;
		elvss_cond[4] = elvss;
		dev_info(&dsi->dev, "_on: ELVSS_COND = 0x%02x (id3=0x%02x %svalid)\n",
			 elvss, ctx->panel_id3,
			 ctx->panel_id3_valid ? "" : "in");
	}
	dcs_long_write(&dsi_ctx, "ready_to_on:elvss_cond", elvss_cond,
		       sizeof(elvss_cond));

	dcs_long_write(&dsi_ctx, "ready_to_on:acl_off", acl_off,
		       sizeof(acl_off));

	dcs_long_write(&dsi_ctx, "ready_to_on:power_ctl", power_ctl,
		       sizeof(power_ctl));

	dcs_long_write(&dsi_ctx, "ready_to_on:nvm_ref", nvm_ref,
		       sizeof(nvm_ref));
	dcs_long_write(&dsi_ctx, "ready_to_on:global_param", global_param,
		       sizeof(global_param));
	dcs_long_write(&dsi_ctx, "ready_to_on:eot", eot_cmd,
		       sizeof(eot_cmd));

	/*
	 * Downstream samsung_display_on_cmds[] ends with display_on (0x29)
	 * before video mode starts.  It is re-sent later in late_on (enable())
	 * together with enter_normal_mode after video data is streaming.
	 */
	{
		static const u8 disp_on[] = { MIPI_DCS_SET_DISPLAY_ON, 0x00 };

		dcs_long_write(&dsi_ctx, "ready_to_on:display_on",
			       disp_on, sizeof(disp_on));
	}

	return dsi_ctx.accum_err;
}

/* Removed magnachip_ams452gp32_off(); display-off in disable(), sleep-in in unprepare() */

/*
 * Read manufacture id (3 bytes via DCS reads 0xDA/0xDB/0xDC). These are
 * standard MIPI DCS commands and work without the Magnachip unlock keys
 * (Level2/Level3), so they can be issued before _on(). id3 (the byte
 * read at 0xDC) is the per-panel ELVSS calibration value we need to
 * compute the correct ELVSS_COND payload during init.
 */
static void magnachip_ams452gp32_read_id(struct magnachip_ams452gp32 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	u8 id[3] = { 0, 0, 0 };
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_read(dsi, 0xDA, &id[0], 1);
	if (ret < 0) {
		dev_warn(dev, "ID: read 0xDA failed: %d (BTA broken?)\n", ret);
		goto out;
	}
	mipi_dsi_dcs_read(dsi, 0xDB, &id[1], 1);
	mipi_dsi_dcs_read(dsi, 0xDC, &id[2], 1);

	dev_info(dev, "ID: manufacture_id = %02x %02x %02x\n",
		 id[0], id[1], id[2]);

	ctx->panel_id3 = id[2];
	ctx->panel_id3_valid = true;

	if (id[1] != 0x4A)
		dev_warn(dev, "ID: id2 != 0x4A - not a Magnachip-blessed panel; ELVSS path may be wrong\n");

out:
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
}

/*
 * Read EA8868 MTP for diagnostic logging. Requires Level2/Level3 keys
 * to be unlocked, so this runs AFTER _on().
 */
static void magnachip_ams452gp32_read_diag(struct magnachip_ams452gp32 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	u8 mtp[21] = { 0 };
	int ret;
	int bank;
	static const u8 mtp_addrs[3] = { 0xD3, 0xD4, 0xE0 };
	char buf[3 * sizeof(mtp) + 16];
	int i, n;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_set_maximum_return_packet_size(dsi, 7);
	if (ret < 0) {
		dev_warn(dev, "DIAG: set_max_return_packet_size failed: %d\n", ret);
		goto out;
	}

	for (bank = 0; bank < 3; bank++) {
		u8 set_addr[2] = { 0xFD, mtp_addrs[bank] };

		ret = mipi_dsi_dcs_write_buffer(dsi, set_addr, sizeof(set_addr));
		if (ret < 0) {
			dev_warn(dev, "DIAG: bank %d set_addr failed: %d\n", bank, ret);
			goto out;
		}

		ret = mipi_dsi_dcs_read(dsi, 0xFE, &mtp[bank * 7], 7);
		if (ret < 0) {
			dev_warn(dev, "DIAG: bank %d (0x%02x) read failed: %d\n",
				 bank, mtp_addrs[bank], ret);
			goto out;
		}
	}

	n = 0;
	for (i = 0; i < (int)sizeof(mtp); i++)
		n += scnprintf(buf + n, sizeof(buf) - n, " %02x", mtp[i]);
	dev_info(dev, "DIAG: MTP[21] =%s\n", buf);

	/* Store MTP for smart-dimming gamma computation */
	memcpy(ctx->mtp, mtp, sizeof(mtp));
	ctx->mtp_valid = true;

out:
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
}

static int magnachip_ams452gp32_late_on(struct magnachip_ams452gp32 *ctx,
					const char *phase, bool lpm)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	static const u8 display_on[] = { MIPI_DCS_SET_DISPLAY_ON, 0x00 };
	static const u8 normal_mode[] = { MIPI_DCS_ENTER_NORMAL_MODE, 0x00 };
	unsigned long old_mode_flags = dsi->mode_flags;

	if (lpm)
		dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	else
		dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (diag_verbose)
		dev_info(&dsi->dev, "diag: late_on phase=%s tx=%s\n", phase,
			 lpm ? "LP" : "HS");

	/*
	 * Downstream mipi_magna_late_on() sends enter_normal_mode (0x13)
	 * first, then display_on (0x29) with a 5ms delay.
	 */
	dcs_long_write(&dsi_ctx, "late_on:normal_mode", normal_mode,
		       sizeof(normal_mode));
	dcs_long_write(&dsi_ctx, "late_on:display_on", display_on,
		       sizeof(display_on));
	mipi_dsi_msleep(&dsi_ctx, 5);

	dsi->mode_flags = old_mode_flags;

	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "late_on: command send failed: %d\n",
			dsi_ctx.accum_err);

	return dsi_ctx.accum_err;
}

static int magnachip_ams452gp32_prepare(struct drm_panel *panel)
{
	struct magnachip_ams452gp32 *ctx = to_magnachip_ams452gp32(panel);
	struct device *dev = &ctx->dsi->dev;
	int late_on_phase;
	int ret;

	dev_info(dev, "prepare: starting panel init\n");
	dev_info(dev, "prepare: dcdc_en_gpio=%s enable_gpio=%s\n",
		ctx->dcdc_en_gpio ? "present" : "MISSING",
		ctx->enable_gpio ? "present" : "MISSING");
	late_on_phase = magnachip_ams452gp32_late_on_phase(dev);
	if (diag_verbose)
		dev_info(dev, "diag: late_on_phase=%d sleep_in_on_unprepare=%u verbose=%u\n",
			 late_on_phase, diag_sleep_in_on_unprepare, diag_verbose);

	ret = magnachip_ams452gp32_enable_supplies(ctx);
	if (ret < 0) {
		dev_err(dev, "prepare: enabling supplies failed: %d\n", ret);
		return ret;
	}

	dev_info(dev, "prepare: regulators enabled, waiting 20ms\n");

	/* Let panel power (vddp/iovcc) stabilize before reset */
	msleep(20);

	magnachip_ams452gp32_power_on(ctx);
	dev_info(dev, "prepare: power_on done (DCDC+reset toggled)\n");

	/*
	 * Read manufacture ID first (no unlock keys required) so _on()
	 * can compute correct ELVSS_COND from the per-panel id3 byte.
	 */
	magnachip_ams452gp32_read_id(ctx);

	ret = magnachip_ams452gp32_on(ctx);
	if (ret < 0) {
		dev_err(dev, "prepare: panel _on() failed: %d\n", ret);
		magnachip_ams452gp32_power_off(ctx);
		magnachip_ams452gp32_disable_supplies(ctx);
		return ret;
	}

	dev_info(dev, "prepare: panel init complete (DSI cmds sent OK)\n");

	/*
	 * One-shot diagnostic read of panel ID + MTP. Logs to dmesg under
	 * "DIAG:" prefix. We don't fail prepare() on read errors - the
	 * panel might still be usable even if BTA is broken.
	 */
	magnachip_ams452gp32_read_diag(ctx);

	/*
	 * Smart-dimming: compute per-panel gamma from MTP.  On the very
	 * first boot _on() already sent the static fallback gamma; now
	 * we have MTP data and can compute the correct table and re-send.
	 * On subsequent prepare() calls (resume), _on() will use the
	 * stored computed_gamma directly.
	 */
	if (ctx->mtp_valid && !ctx->gamma_valid) {
		struct mipi_dsi_multi_context gctx = { .dsi = ctx->dsi };
		static const u8 gf1[] = { MCS_GAMMA_FLAG, 0x00, 0x5a };
		static const u8 gf2[] = { MCS_GAMMA_FLAG, 0x00, 0xa5 };
		u8 gcmd[25];
		int i, n;
		char hex[3 * SD_GAMMA_LEN + 1];

		sd_generate_gamma(ctx->mtp, 300, ctx->computed_gamma);
		ctx->gamma_valid = true;

		/* Re-send gamma: flag_unlock, gamma_set, flag_lock */
		gcmd[0] = MCS_GAMMA_SET;
		memcpy(&gcmd[1], ctx->computed_gamma, SD_GAMMA_LEN);

		ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
		dcs_long_write(&gctx, "smart_gamma:flag", gf1, sizeof(gf1));
		dcs_long_write(&gctx, "smart_gamma:gamma", gcmd, sizeof(gcmd));
		dcs_long_write(&gctx, "smart_gamma:flag2", gf2, sizeof(gf2));

		n = 0;
		for (i = 0; i < SD_GAMMA_LEN; i++)
			n += scnprintf(hex + n, sizeof(hex) - n, " %02x",
				       ctx->computed_gamma[i]);
		dev_info(dev, "prepare: smart-dimming gamma[24] =%s\n", hex);
	}

	if (late_on_phase == 3) {
		ret = magnachip_ams452gp32_late_on(ctx, "prepare-pre-video",
						   false);
		if (ret)
			return ret;
	}

	return 0;
}

static int magnachip_ams452gp32_enable(struct drm_panel *panel)
{
	struct magnachip_ams452gp32 *ctx = to_magnachip_ams452gp32(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int late_on_phase = magnachip_ams452gp32_late_on_phase(&dsi->dev);

	switch (late_on_phase) {
	case 0:
		if (diag_verbose)
			dev_info(&dsi->dev,
				 "diag: enable skipping post-video late_on\n");
		return 0;
	case 1:
		return magnachip_ams452gp32_late_on(ctx, "enable-post-video",
						    false);
	case 2:
		return magnachip_ams452gp32_late_on(ctx, "enable-post-video",
						    true);
	case 3:
		if (diag_verbose)
			dev_info(&dsi->dev,
				 "diag: enable late_on already sent in prepare\n");
		return 0;
	default:
		return 0;
	}
}

static int magnachip_ams452gp32_disable(struct drm_panel *panel)
{
	struct magnachip_ams452gp32 *ctx = to_magnachip_ams452gp32(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	static const u8 display_off[] = { MIPI_DCS_SET_DISPLAY_OFF, 0x00 };

	if (diag_verbose)
		dev_info(&dsi->dev, "diag: disable display_off\n");
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	dcs_long_write(&dsi_ctx, "disable:display_off", display_off,
		       sizeof(display_off));
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int magnachip_ams452gp32_unprepare(struct drm_panel *panel)
{
	struct magnachip_ams452gp32 *ctx = to_magnachip_ams452gp32(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	static const u8 sleep_in[] = { MIPI_DCS_ENTER_SLEEP_MODE, 0x00 };

	if (diag_sleep_in_on_unprepare) {
		if (diag_verbose)
			dev_info(&dsi->dev, "diag: unprepare sending sleep_in\n");
		dsi->mode_flags |= MIPI_DSI_MODE_LPM;
		dcs_long_write(&dsi_ctx, "unprepare:sleep_in", sleep_in,
			       sizeof(sleep_in));
		mipi_dsi_msleep(&dsi_ctx, 120);
	} else {
		if (diag_verbose)
			dev_info(&dsi->dev, "diag: unprepare skipping sleep_in\n");
	}

	magnachip_ams452gp32_power_off(ctx);
	magnachip_ams452gp32_disable_supplies(ctx);

	return 0;
}

/* Downstream Express LTE: 480x800, hsync 4/16/80, vsync 2/4/10, 60Hz. */
static const struct drm_display_mode magnachip_ams452gp32_mode = {
	/*
	 * Pixel clock is intentionally bumped to 28625 kHz (= downstream
	 * Samsung BSP's link rate of 343.5 Mbps/lane * 2 lanes / bpp 24)
	 * rather than the geometric htotal*vtotal*60 = 28397 kHz.
	 *
	 * Reason: with the geometric pixel rate the DSI byte_clk computed
	 * by mainline (pclk*bpp/(8*lanes) = 42.306 MHz) is the *exact*
	 * minimum needed to drain hdisplay bytes per htotal pixel-times,
	 * leaving zero headroom for HSS/HSE/HSA/HBP/HFP packet headers
	 * and EOT/BLLP LP transitions in burst video mode.  MDP4 then
	 * underruns on every vsync (PRIMARY_INTF_UDERRUN), filling each
	 * frame with the UNDERFLOW_CLR colour and producing the bright
	 * pulsing R/G splatter / 4-5 white bar artefacts.
	 *
	 * A ~1.5% bump on mode->clock scales pixel_clk AND byte_clk
	 * together (they share the DSI PHY PLL on V2 / 28nm-8960) so the
	 * PLL VCO ratio stays valid (factor=16, VCO ~ 687 MHz; well within
	 * 600..1200 MHz min/max).  Bumping byte_clk independently in the
	 * host driver does NOT work - it desynchronises pixel_clk from
	 * the new VCO and clk_set_rate(pixel_clk) returns -EINVAL on every
	 * DSI command transfer.
	 *
	 * 28625 kHz exactly matches Samsung's downstream
	 * mipi_magna_oled_video_wvga_pt.c pinfo.clk_rate=343500000.
	 */
	.clock = 28625,
	.hdisplay = 480,
	.hsync_start = 480 + 80,
	.hsync_end = 480 + 80 + 4,
	.htotal = 480 + 80 + 4 + 16,
	.vdisplay = 800,
	.vsync_start = 800 + 10,
	.vsync_end = 800 + 10 + 2,
	.vtotal = 800 + 10 + 2 + 4,
	/*
	 * No NHSYNC/NVSYNC flags: DSI video mode has no physical H/V sync
	 * pins.  These flags would cause the MDP4 DSI timing generator to
	 * invert its internal polarity (CTRL_POLARITY register), swapping
	 * the active display and blanking intervals.  That makes DMA_P
	 * fetch during blanking and idle during active -> FIFO starves ->
	 * PRIMARY_INTF_UDERRUN every vsync.  Downstream (Samsung BSP)
	 * unconditionally uses polarity=0 for all DSI video panels.
	 */
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int magnachip_ams452gp32_get_modes(struct drm_panel *panel,
					  struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &magnachip_ams452gp32_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	connector->display_info.width_mm = 52;
	connector->display_info.height_mm = 87;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs magnachip_ams452gp32_panel_funcs = {
	.prepare = magnachip_ams452gp32_prepare,
	.enable = magnachip_ams452gp32_enable,
	.disable = magnachip_ams452gp32_disable,
	.unprepare = magnachip_ams452gp32_unprepare,
	.get_modes = magnachip_ams452gp32_get_modes,
};

static int magnachip_ams452gp32_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct magnachip_ams452gp32 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct magnachip_ams452gp32, panel,
				   &magnachip_ams452gp32_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->vddp = magnachip_ams452gp32_get_optional_supply(dev, "vddp");
	if (IS_ERR(ctx->vddp))
		return dev_err_probe(dev, PTR_ERR(ctx->vddp),
				     "Failed to get vddp regulator\n");

	ctx->iovcc = magnachip_ams452gp32_get_optional_supply(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to get iovcc regulator\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset GPIO\n");

	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get enable GPIO\n");

	ctx->dcdc_en_gpio = devm_gpiod_get_optional(dev, "dcdc-en", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->dcdc_en_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->dcdc_en_gpio),
				     "Failed to get dcdc-en GPIO\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/*
	 * Match Samsung downstream BSP (mipi_magna_oled_video_wvga_pt.c)
	 * for this msm8960 AMOLED: video burst + HSE, no EOT.
	 */
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	/*
	 * The panel's prepare() does DCS reads (panel ID, MTP) which need
	 * the DSI host clocks already running.  Tell the DRM bridge layer
	 * to enable the upstream DSI bridge before our prepare() runs;
	 * otherwise the host may still be raising HS while we transmit
	 * and we get spurious dsi_link_clk_set_rate_v2 storms.
	 */
	ctx->panel.prepare_prev_first = true;

	dev_dbg(dev, "magnachip: probe lanes=%u flags=%lx prepare_prev_first=1\n",
		dsi->lanes, dsi->mode_flags);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void magnachip_ams452gp32_remove(struct mipi_dsi_device *dsi)
{
	struct magnachip_ams452gp32 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id magnachip_ams452gp32_of_match[] = {
	{ .compatible = "magnachip,ams452gp32" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, magnachip_ams452gp32_of_match);

static struct mipi_dsi_driver magnachip_ams452gp32_driver = {
	.probe = magnachip_ams452gp32_probe,
	.remove = magnachip_ams452gp32_remove,
	.driver = {
		.name = "panel-magnachip-ams452gp32",
		.of_match_table = magnachip_ams452gp32_of_match,
	},
};
module_mipi_dsi_driver(magnachip_ams452gp32_driver);

MODULE_AUTHOR("Ported from downstream Samsung Express Att (mipi_magna_oled_video_wvga_pt)");
MODULE_DESCRIPTION("Magnachip AMS452GP32 480x800 WVGA OLED panel");
MODULE_LICENSE("GPL");
