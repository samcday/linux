// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 *
 * This driver is for the switch-mode battery charger and boost
 * hardware found in pmi8998 and related PMICs.
 */

#include <linux/bits.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#if IS_ENABLED(CONFIG_CHARGER_QCOM_SMB2_KUNIT_TEST)
#include <kunit/device.h>
#include <kunit/test.h>
#endif

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define BVR_INITIAL_RAMP_BIT				BIT(7)
#define CC_SOFT_TERMINATE_BIT				BIT(6)
#define STEP_CHARGING_STATUS_SHIFT			3
#define STEP_CHARGING_STATUS_MASK			GENMASK(5, 3)
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define INPUT_CURRENT_LIMITED_BIT			BIT(7)
#define CHARGER_ERROR_STATUS_SFT_EXPIRE_BIT		BIT(6)
#define CHARGER_ERROR_STATUS_BAT_OV_BIT			BIT(5)
#define CHARGER_ERROR_STATUS_BAT_TERM_MISSING_BIT	BIT(4)
#define BAT_TEMP_STATUS_MASK				GENMASK(3, 0)
#define BAT_TEMP_STATUS_SOFT_LIMIT_MASK			GENMASK(3, 2)
#define BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT		BIT(3)
#define BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT		BIT(2)
#define BAT_TEMP_STATUS_TOO_HOT_BIT			BIT(1)
#define BAT_TEMP_STATUS_TOO_COLD_BIT			BIT(0)

#define BATTERY_CHARGER_STATUS_4			0x0A
#define CHARGE_CURRENT_POST_JEITA_MASK			GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_7			0x0D
#define ENABLE_TRICKLE_BIT				BIT(7)
#define ENABLE_PRE_CHARGING_BIT				BIT(6)
#define ENABLE_FAST_CHARGING_BIT			BIT(5)
#define ENABLE_FULLON_MODE_BIT				BIT(4)
#define TOO_COLD_ADC_BIT				BIT(3)
#define TOO_HOT_ADC_BIT					BIT(2)
#define HOT_SL_ADC_BIT					BIT(1)
#define COLD_SL_ADC_BIT					BIT(0)

#define CHARGING_ENABLE_CMD				0x42
#define CHARGING_ENABLE_CMD_BIT				BIT(0)

#define CHGR_CFG2					0x51
#define CHG_EN_SRC_BIT					BIT(7)
#define CHG_EN_POLARITY_BIT				BIT(6)
#define PRETOFAST_TRANSITION_CFG_BIT			BIT(5)
#define BAT_OV_ECC_BIT					BIT(4)
#define I_TERM_BIT					BIT(3)
#define AUTO_RECHG_BIT					BIT(2)
#define EN_ANALOG_DROP_IN_VBATT_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT				BIT(0)

#define PRE_CHARGE_CURRENT_CFG				0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK			GENMASK(5, 0)

#define FAST_CHARGE_CURRENT_CFG				0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK		GENMASK(7, 0)

#define FLOAT_VOLTAGE_CFG				0x70
#define FLOAT_VOLTAGE_SETTING_MASK			GENMASK(7, 0)

#define FG_UPDATE_CFG_2_SEL				0x7D
#define SOC_LT_OTG_THRESH_SEL_BIT			BIT(3)
#define SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(2)
#define VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(1)
#define IBT_LT_CHG_TERM_THRESH_SEL_BIT			BIT(0)

#define JEITA_EN_CFG					0x90
#define JEITA_EN_HARDLIMIT_BIT				BIT(4)
#define JEITA_EN_HOT_SL_FCV_BIT				BIT(3)
#define JEITA_EN_COLD_SL_FCV_BIT			BIT(2)
#define JEITA_EN_HOT_SL_CCC_BIT				BIT(1)
#define JEITA_EN_COLD_SL_CCC_BIT			BIT(0)

#define INT_RT_STS					0x310
#define TYPE_C_CHANGE_RT_STS_BIT			BIT(7)
#define USBIN_ICL_CHANGE_RT_STS_BIT			BIT(6)
#define USBIN_SOURCE_CHANGE_RT_STS_BIT			BIT(5)
#define USBIN_PLUGIN_RT_STS_BIT				BIT(4)
#define USBIN_OV_RT_STS_BIT				BIT(3)
#define USBIN_UV_RT_STS_BIT				BIT(2)
#define USBIN_LT_3P6V_RT_STS_BIT			BIT(1)
#define USBIN_COLLAPSE_RT_STS_BIT			BIT(0)

#define OTG_CFG						0x153
#define OTG_RESERVED_MASK				GENMASK(7, 6)
#define DIS_OTG_ON_TLIM_BIT				BIT(5)
#define QUICKSTART_OTG_FASTROLESWAP_BIT			BIT(4)
#define INCREASE_DFP_TIME_BIT				BIT(3)
#define ENABLE_OTG_IN_DEBUG_MODE_BIT			BIT(2)
#define OTG_EN_SRC_CFG_BIT				BIT(1)
#define CONCURRENT_MODE_CFG_BIT				BIT(0)

#define OTG_ENG_OTG_CFG					0x1C0
#define ENG_BUCKBOOST_HALT1_8_MODE_BIT			BIT(0)

#define APSD_STATUS					0x307
#define APSD_STATUS_7_BIT				BIT(7)
#define HVDCP_CHECK_TIMEOUT_BIT				BIT(6)
#define SLOW_PLUGIN_TIMEOUT_BIT				BIT(5)
#define ENUMERATION_DONE_BIT				BIT(4)
#define VADP_CHANGE_DONE_AFTER_AUTH_BIT			BIT(3)
#define QC_AUTH_DONE_STATUS_BIT				BIT(2)
#define QC_CHARGER_BIT					BIT(1)
#define APSD_DTC_STATUS_DONE_BIT			BIT(0)

#define APSD_RESULT_STATUS				0x308
#define ICL_OVERRIDE_LATCH_BIT				BIT(7)
#define APSD_RESULT_STATUS_MASK				GENMASK(6, 0)
#define QC_3P0_BIT					BIT(6)
#define QC_2P0_BIT					BIT(5)
#define FLOAT_CHARGER_BIT				BIT(4)
#define DCP_CHARGER_BIT					BIT(3)
#define CDP_CHARGER_BIT					BIT(2)
#define OCP_CHARGER_BIT					BIT(1)
#define SDP_CHARGER_BIT					BIT(0)

#define USBIN_CMD_IL					0x340
#define USBIN_SUSPEND_BIT				BIT(0)

#define TYPE_C_STATUS_1					0x30B
#define UFP_TYPEC_MASK					GENMASK(7, 5)
#define UFP_TYPEC_RDSTD_BIT				BIT(7)
#define UFP_TYPEC_RD1P5_BIT				BIT(6)
#define UFP_TYPEC_RD3P0_BIT				BIT(5)
#define UFP_TYPEC_FMB_255K_BIT				BIT(4)
#define UFP_TYPEC_FMB_301K_BIT				BIT(3)
#define UFP_TYPEC_FMB_523K_BIT				BIT(2)
#define UFP_TYPEC_FMB_619K_BIT				BIT(1)
#define UFP_TYPEC_OPEN_OPEN_BIT				BIT(0)

#define TYPE_C_STATUS_2					0x30C
#define DFP_RA_OPEN_BIT					BIT(7)
#define TIMER_STAGE_BIT					BIT(6)
#define EXIT_UFP_MODE_BIT				BIT(5)
#define EXIT_DFP_MODE_BIT				BIT(4)
#define DFP_TYPEC_MASK					GENMASK(3, 0)
#define DFP_RD_OPEN_BIT					BIT(3)
#define DFP_RD_RA_VCONN_BIT				BIT(2)
#define DFP_RD_RD_BIT					BIT(1)
#define DFP_RA_RA_BIT					BIT(0)

#define TYPE_C_STATUS_3					0x30D
#define ENABLE_BANDGAP_BIT				BIT(7)
#define U_USB_GND_NOVBUS_BIT				BIT(6)
#define U_USB_FLOAT_NOVBUS_BIT				BIT(5)
#define U_USB_GND_BIT					BIT(4)
#define U_USB_FMB1_BIT					BIT(3)
#define U_USB_FLOAT1_BIT				BIT(2)
#define U_USB_FMB2_BIT					BIT(1)
#define U_USB_FLOAT2_BIT				BIT(0)

#define TYPE_C_STATUS_4					0x30E
#define UFP_DFP_MODE_STATUS_BIT				BIT(7)
#define TYPEC_VBUS_STATUS_BIT				BIT(6)
#define TYPEC_VBUS_ERROR_STATUS_BIT			BIT(5)
#define TYPEC_DEBOUNCE_DONE_STATUS_BIT			BIT(4)
#define TYPEC_UFP_AUDIO_ADAPT_STATUS_BIT		BIT(3)
#define TYPEC_VCONN_OVERCURR_STATUS_BIT			BIT(2)
#define CC_ORIENTATION_BIT				BIT(1)
#define CC_ATTACHED_BIT					BIT(0)

#define TYPE_C_STATUS_5					0x30F
#define TRY_SOURCE_FAILED_BIT				BIT(6)
#define TRY_SINK_FAILED_BIT				BIT(5)
#define TIMER_STAGE_2_BIT				BIT(4)
#define TYPEC_LEGACY_CABLE_STATUS_BIT			BIT(3)
#define TYPEC_NONCOMP_LEGACY_CABLE_STATUS_BIT		BIT(2)
#define TYPEC_TRYSOURCE_DETECT_STATUS_BIT		BIT(1)
#define TYPEC_TRYSINK_DETECT_STATUS_BIT			BIT(0)

#define CMD_APSD					0x341
#define ICL_OVERRIDE_BIT				BIT(1)
#define APSD_RERUN_BIT					BIT(0)

#define TYPE_C_CFG					0x358
#define APSD_START_ON_CC_BIT				BIT(7)
#define WAIT_FOR_APSD_BIT				BIT(6)
#define FACTORY_MODE_DETECTION_EN_BIT			BIT(5)
#define FACTORY_MODE_ICL_3A_4A_BIT			BIT(4)
#define FACTORY_MODE_DIS_CHGING_CFG_BIT			BIT(3)
#define SUSPEND_NON_COMPLIANT_CFG_BIT			BIT(2)
#define VCONN_OC_CFG_BIT				BIT(1)
#define TYPE_C_OR_U_USB_BIT				BIT(0)

#define TYPE_C_CFG_2					0x359
#define TYPE_C_DFP_CURRSRC_MODE_BIT			BIT(7)
#define DFP_CC_1P4V_OR_1P6V_BIT				BIT(6)
#define VCONN_SOFTSTART_CFG_MASK			GENMASK(5, 4)
#define EN_TRY_SOURCE_MODE_BIT				BIT(3)
#define USB_FACTORY_MODE_ENABLE_BIT			BIT(2)
#define TYPE_C_UFP_MODE_BIT				BIT(1)
#define EN_80UA_180UA_CUR_SOURCE_BIT			BIT(0)

#define TYPE_C_CFG_3					0x35A
#define TVBUS_DEBOUNCE_BIT				BIT(7)
#define TYPEC_LEGACY_CABLE_INT_EN_BIT			BIT(6)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_B	BIT(5)
#define TYPEC_TRYSOURCE_DETECT_INT_EN_BIT		BIT(4)
#define TYPEC_TRYSINK_DETECT_INT_EN_BIT			BIT(3)
#define EN_TRYSINK_MODE_BIT				BIT(2)
#define EN_LEGACY_CABLE_DETECTION_BIT			BIT(1)
#define ALLOW_PD_DRING_UFP_TCCDB_BIT			BIT(0)

#define USBIN_OPTIONS_1_CFG				0x362
#define CABLE_R_SEL_BIT					BIT(7)
#define HVDCP_AUTH_ALG_EN_CFG_BIT			BIT(6)
#define HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT		BIT(5)
#define INPUT_PRIORITY_BIT				BIT(4)
#define AUTO_SRC_DETECT_BIT				BIT(3)
#define HVDCP_EN_BIT					BIT(2)
#define VADP_INCREMENT_VOLTAGE_LIMIT_BIT		BIT(1)
#define VADP_TAPER_TIMER_EN_BIT				BIT(0)

#define USBIN_OPTIONS_2_CFG				0x363
#define WIPWR_RST_EUD_CFG_BIT				BIT(7)
#define SWITCHER_START_CFG_BIT				BIT(6)
#define DCD_TIMEOUT_SEL_BIT				BIT(5)
#define OCD_CURRENT_SEL_BIT				BIT(4)
#define SLOW_PLUGIN_TIMER_EN_CFG_BIT			BIT(3)
#define FLOAT_OPTIONS_MASK				GENMASK(2, 0)
#define FLOAT_DIS_CHGING_CFG_BIT			BIT(2)
#define SUSPEND_FLOAT_CFG_BIT				BIT(1)
#define FORCE_FLOAT_SDP_CFG_BIT				BIT(0)

#define TAPER_TIMER_SEL_CFG				0x364
#define TYPEC_SPARE_CFG_BIT				BIT(7)
#define TYPEC_DRP_DFP_TIME_CFG_BIT			BIT(5)
#define TAPER_TIMER_SEL_MASK				GENMASK(1, 0)

#define USBIN_LOAD_CFG					0x365
#define USBIN_OV_CH_LOAD_OPTION_BIT			BIT(7)
#define ICL_OVERRIDE_AFTER_APSD_BIT			BIT(4)

#define USBIN_ICL_OPTIONS				0x366
#define CFG_USB3P0_SEL_BIT				BIT(2)
#define USB51_MODE_BIT					BIT(1)
#define USBIN_MODE_CHG_BIT				BIT(0)

#define TYPE_C_INTRPT_ENB_SOFTWARE_CTRL			0x368
#define EXIT_SNK_BASED_ON_CC_BIT			BIT(7)
#define VCONN_EN_ORIENTATION_BIT			BIT(6)
#define TYPEC_VCONN_OVERCURR_INT_EN_BIT			BIT(5)
#define VCONN_EN_SRC_BIT				BIT(4)
#define VCONN_EN_VALUE_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define UFP_EN_CMD_BIT					BIT(2)
#define DFP_EN_CMD_BIT					BIT(1)
#define TYPEC_DISABLE_CMD_BIT				BIT(0)

#define USBIN_CURRENT_LIMIT_CFG				0x370
#define USBIN_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG				0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT			BIT(7)
#define USBIN_AICL_HDC_EN_BIT				BIT(6)
#define USBIN_AICL_START_AT_MAX_BIT			BIT(5)
#define USBIN_AICL_RERUN_EN_BIT				BIT(4)
#define USBIN_AICL_ADC_EN_BIT				BIT(3)
#define USBIN_AICL_EN_BIT				BIT(2)
#define USBIN_HV_COLLAPSE_RESPONSE_BIT			BIT(1)
#define USBIN_LV_COLLAPSE_RESPONSE_BIT			BIT(0)

#define USBIN_5V_AICL_THRESHOLD_CFG			0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK		GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG			0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define DC_ENG_SSUPPLY_CFG2				0x4C1
#define ENG_SSUPPLY_IVREF_OTG_SS_MASK			GENMASK(2, 0)
#define OTG_SS_SLOW					0x3

#define DCIN_AICL_REF_SEL_CFG				0x481
#define DCIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define WI_PWR_OPTIONS					0x495
#define CHG_OK_BIT					BIT(7)
#define WIPWR_UVLO_IRQ_OPT_BIT				BIT(6)
#define BUCK_HOLDOFF_ENABLE_BIT				BIT(5)
#define CHG_OK_HW_SW_SELECT_BIT				BIT(4)
#define WIPWR_RST_ENABLE_BIT				BIT(3)
#define DCIN_WIPWR_IRQ_SELECT_BIT			BIT(2)
#define AICL_SWITCH_ENABLE_BIT				BIT(1)
#define ZIN_ICL_ENABLE_BIT				BIT(0)

#define ICL_STATUS					0x607
#define INPUT_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define POWER_PATH_STATUS				0x60B
#define P_PATH_INPUT_SS_DONE_BIT			BIT(7)
#define P_PATH_USBIN_SUSPEND_STS_BIT			BIT(6)
#define P_PATH_DCIN_SUSPEND_STS_BIT			BIT(5)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_USE_DCIN_BIT				BIT(3)
#define P_PATH_POWER_PATH_MASK				GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

#define BARK_BITE_WDOG_PET				0x643
#define BARK_BITE_WDOG_PET_BIT				BIT(0)

#define WD_CFG						0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT			BIT(7)
#define BARK_WDOG_INT_EN_BIT				BIT(6)
#define BITE_WDOG_INT_EN_BIT				BIT(5)
#define SFT_AFTER_WDOG_IRQ_MASK				GENMASK(4, 3)
#define WDOG_IRQ_SFT_BIT				BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT			BIT(1)
#define WDOG_TIMER_EN_BIT				BIT(0)

#define SNARL_BARK_BITE_WD_CFG				0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT		BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK				GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK				GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK				GENMASK(1, 0)

#define AICL_RERUN_TIME_CFG				0x661
#define AICL_RERUN_TIME_MASK				GENMASK(1, 0)

#define STAT_CFG					0x690
#define STAT_SW_OVERRIDE_VALUE_BIT			BIT(7)
#define STAT_SW_OVERRIDE_CFG_BIT			BIT(6)
#define STAT_PARALLEL_OFF_DG_CFG_MASK			GENMASK(5, 4)
#define STAT_POLARITY_CFG_BIT				BIT(3)
#define STAT_PARALLEL_CFG_BIT				BIT(2)
#define STAT_FUNCTION_CFG_BIT				BIT(1)
#define STAT_IRQ_PULSING_EN_BIT				BIT(0)

#define SDP_CURRENT_UA					500000
#define CDP_CURRENT_UA					1500000
#define DCP_CURRENT_UA					1500000
#define CURRENT_MAX_UA					DCP_CURRENT_UA
#define SMB2_ICL_MAX_UA					4800000
#define SMB2_APSD_DELAY_MS				1500
#define SMB2_APSD_RETRY_MS				1000

/* pmi8998 registers represent current in increments of 1/40th of an amp */
#define CURRENT_SCALE_FACTOR				25000
/* clang-format on */

#define THERMAL_FCC_MAX					3000000
#define THERMAL_FCC_STEP				500000

enum charger_status {
	TRICKLE_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	INHIBIT_CHARGE,
	DISABLE_CHARGE,
};

struct smb_init_register {
	u16 addr;
	u8 mask;
	u8 val;
	bool typec;
};

/**
 * struct smb_icl_policy - input-current ownership and cached policy
 * @apsd_ua: last current limit derived from a current-generation APSD result
 * @tcpm_ua: quantized current limit requested by TCPM
 * @programmed_ua: last limit successfully written to the charger
 * @generation: invalidates APSD work across cable and TCPM policy changes
 * @apsd_valid: whether @apsd_ua belongs to the current generation
 * @tcpm_active: whether TCPM currently owns the input-current limit
 * @sink_enabled: whether TCPM permits the physical USB input path
 * @dirty: whether the effective policy still needs to reach hardware
 */
struct smb_icl_policy {
	u32 apsd_ua;
	u32 tcpm_ua;
	u32 programmed_ua;
	u64 generation;
	bool apsd_valid;
	bool tcpm_active;
	bool sink_enabled;
	bool dirty;
};

/**
 * struct smb_chip - smb chip structure
 * @dev:		Device reference for power_supply
 * @name:		The platform device name
 * @base:		Base address for smb registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @status_change_work: Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_i_chan:	USB_IN current measurement channel
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 * @cdev:		Cooling device
 * @cdev_fcc_max:	Maximum fast-charge current for thermal cooling
 * @cdev_fcc_step:	Fast-charge current reduction per cooling state
 * @cooling_state:	Current thermal cooling state
 * @typec_tcpm_present: A TCPM child owns the charger block's Type-C registers
 * @icl_lock:		Serializes TCPM/APSD input-current arbitration
 * @icl:		Input-current ownership and cached policy
 * @plugin_check_pending: Retry a failed physical cable-status read
 */
struct smb_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;

	struct delayed_work status_change_work;
	int cable_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;

	struct thermal_cooling_device *cdev;
	u32 cdev_fcc_max;
	u32 cdev_fcc_step;
	int cooling_state;
	bool typec_tcpm_present;

	/* Protect provider ownership and all ICL policy state. */
	struct mutex icl_lock;
	struct smb_icl_policy icl;
	bool plugin_check_pending;
};

static bool smb_typec_tcpm_present(struct device *dev)
{
	struct device_node *child;

	if (!dev->parent || !dev->parent->of_node)
		return false;

	for_each_available_child_of_node(dev->parent->of_node, child) {
		if (of_device_is_compatible(child, "qcom,pm660-typec") ||
		    of_device_is_compatible(child, "qcom,pmi8998-typec")) {
			of_node_put(child);
			return true;
		}
	}

	return false;
}

static enum power_supply_property smb_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int smb_get_prop_usb_online(struct smb_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + POWER_PATH_STATUS, &stat);
	if (rc < 0) {
		dev_err_ratelimited(chip->dev,
				    "Couldn't read power path status: %d\n", rc);
		return rc;
	}

	*val = (stat & P_PATH_USE_USBIN_BIT) &&
	       (stat & P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT);
	return 0;
}

static int smb_get_usb_plugin(struct smb_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + INT_RT_STS, &stat);
	if (rc < 0) {
		dev_err_ratelimited(chip->dev,
				    "Couldn't read USB plugin status: %d\n", rc);
		return rc;
	}

	*val = !!(stat & USBIN_PLUGIN_RT_STS_BIT);
	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
static int smb_apsd_get_charger_type(struct smb_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err_ratelimited(chip->dev,
				    "Failed to read APSD status: %d\n", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_dbg(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err_ratelimited(chip->dev,
				    "Failed to read APSD result: %d\n", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	else if (stat & (DCP_CHARGER_BIT | OCP_CHARGER_BIT | FLOAT_CHARGER_BIT))
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	else /* SDP_CHARGER_BIT (or others) */
		*val = POWER_SUPPLY_USB_TYPE_SDP;

	return 0;
}

static int smb_get_prop_status(struct smb_chip *chip, int *val)
{
	unsigned char stat[2];
	int usb_online = 0;
	int rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	if (stat[1] & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat[0] = stat[0] & BATTERY_CHARGER_STATUS_MASK;

	switch (stat[0]) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FAST_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
	case INHIBIT_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

static inline int smb_get_current_limit(struct smb_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + ICL_STATUS, val);

	if (rc >= 0)
		*val *= CURRENT_SCALE_FACTOR;
	return rc;
}

static u32 smb_icl_quantize(u32 ua)
{
	return ua / CURRENT_SCALE_FACTOR * CURRENT_SCALE_FACTOR;
}

static u32 smb_icl_effective(const struct smb_icl_policy *icl)
{
	/* Type-C/PD is authoritative; hardware AICL remains the final clamp. */
	if (icl->tcpm_active)
		return icl->tcpm_ua;

	if (icl->apsd_valid)
		return icl->apsd_ua;

	return SDP_CURRENT_UA;
}

static const char *smb_icl_owner(const struct smb_icl_policy *icl)
{
	if (icl->tcpm_active)
		return "TCPM";

	return icl->apsd_valid ? "APSD" : "fallback";
}

static void smb_icl_request_tcpm(struct smb_icl_policy *icl, u32 ua)
{
	icl->generation++;
	icl->tcpm_active = ua != 0;
	icl->tcpm_ua = smb_icl_quantize(ua);
	icl->apsd_valid = false;
	icl->dirty = true;
}

static void smb_icl_set_sink(struct smb_icl_policy *icl, bool enabled)
{
	if (!enabled)
		smb_icl_request_tcpm(icl, 0);
	if (icl->sink_enabled != enabled)
		icl->dirty = true;
	icl->sink_enabled = enabled;
}

static void smb_icl_cable_event(struct smb_icl_policy *icl, bool present)
{
	icl->generation++;
	icl->apsd_valid = false;
	icl->dirty = true;

	/* A VBUS loss is an independent backstop for a missed TCPM reset. */
	if (!present) {
		icl->tcpm_active = false;
		icl->tcpm_ua = 0;
		icl->sink_enabled = false;
	}
}

static bool smb_icl_set_apsd(struct smb_icl_policy *icl, u64 generation,
			     u32 ua)
{
	if (icl->generation != generation || icl->tcpm_active)
		return false;

	icl->apsd_ua = ua;
	icl->apsd_valid = true;
	icl->dirty = true;
	return true;
}

static int smb_write_current_limit(struct smb_chip *chip, unsigned int val)
{
	unsigned char val_raw;

	if (val > SMB2_ICL_MAX_UA) {
		dev_err(chip->dev,
			"Can't set current limit higher than %uuA\n",
			SMB2_ICL_MAX_UA);
		return -EINVAL;
	}
	val_raw = val / CURRENT_SCALE_FACTOR;

	return regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
			    val_raw);
}

static int smb_apply_icl_locked(struct smb_chip *chip)
{
	u32 current_ua = smb_icl_effective(&chip->icl);
	bool override = chip->icl.tcpm_active ||
			(chip->icl.apsd_valid && current_ua > SDP_CURRENT_UA);
	/* Qualcomm's SMB2 policy suspends USB input at 25 mA or below. */
	bool sink = chip->icl.sink_enabled && current_ua > CURRENT_SCALE_FACTOR;
	bool apsd = chip->icl.sink_enabled && !chip->icl.tcpm_active;
	int rc;

	if (!chip->icl.dirty && chip->icl.programmed_ua == current_ua)
		return 0;

	/*
	 * Never draw with an old limit or partly programmed override. In
	 * particular, a failed decrease from a previous PD contract must leave
	 * USB input suspended. A retry can resume it only if TCPM still sinks.
	 */
	chip->icl.dirty = true;
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
				USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
	if (rc)
		return rc;

	rc = smb_write_current_limit(chip, current_ua);
	if (rc)
		return rc;

	/*
	 * SMB2 selects a separate SDP limit unless both high-current mode and
	 * the post-APSD override are enabled. Follow the downstream ICL policy:
	 * a TCPM request overrides BC1.2. A valid CDP/DCP result also uses its
	 * programmed ceiling; absent either, restore USB2 SDP defaults.
	 * Hardware AICL remains enabled and may further reduce the input draw.
	 */
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_ICL_OPTIONS,
				CFG_USB3P0_SEL_BIT | USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
				override ? USBIN_MODE_CHG_BIT : USB51_MODE_BIT);
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_LOAD_CFG,
				ICL_OVERRIDE_AFTER_APSD_BIT,
				override ? ICL_OVERRIDE_AFTER_APSD_BIT : 0);
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_OPTIONS_1_CFG,
				AUTO_SRC_DETECT_BIT, apsd ? AUTO_SRC_DETECT_BIT : 0);
	if (rc)
		return rc;

	if (sink) {
		rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT, 0);
		if (rc)
			return rc;
	}

	chip->icl.programmed_ua = current_ua;
	chip->icl.dirty = false;
	dev_dbg(chip->dev,
		"ICL applied: owner=%s effective=%uuA sink=%u apsd=%uuA valid=%u generation=%llu\n",
		smb_icl_owner(&chip->icl),
		current_ua, sink, chip->icl.apsd_ua, chip->icl.apsd_valid,
		(unsigned long long)chip->icl.generation);
	return 0;
}

static int smb_set_current_limit(struct smb_chip *chip, int val)
{
	bool active;
	int rc;

	if (val < 0 || val > SMB2_ICL_MAX_UA)
		return -EINVAL;

	/* Preserve the original direct APSD/userspace policy without TCPM. */
	if (!chip->typec_tcpm_present)
		return smb_write_current_limit(chip, val);

	active = val != 0;

	mutex_lock(&chip->icl_lock);
	smb_icl_request_tcpm(&chip->icl, val);
	dev_dbg(chip->dev,
		"ICL TCPM request: requested=%duA quantized=%uuA active=%u generation=%llu\n",
		val, chip->icl.tcpm_ua, chip->icl.tcpm_active,
		(unsigned long long)chip->icl.generation);
	rc = smb_apply_icl_locked(chip);
	mutex_unlock(&chip->icl_lock);

	/* TCPM uses zero for both detach and Rp-default APSD fallback. */
	if (!active || rc)
		mod_delayed_work(system_wq, &chip->status_change_work,
				 msecs_to_jiffies(rc ? SMB2_APSD_RETRY_MS :
						    SMB2_APSD_DELAY_MS));

	return rc;
}

static int smb_set_sink_enabled(struct smb_chip *chip, int enabled)
{
	bool apsd;
	int rc;

	/* Preserve the existing boolean STATUS ABI for non-TCPM devices. */
	if (!chip->typec_tcpm_present)
		return regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					  USBIN_SUSPEND_BIT, !enabled);
	if (enabled != 0 && enabled != 1)
		return -EINVAL;

	mutex_lock(&chip->icl_lock);
	smb_icl_set_sink(&chip->icl, enabled);
	rc = smb_apply_icl_locked(chip);
	apsd = chip->icl.sink_enabled && !chip->icl.tcpm_active;
	mutex_unlock(&chip->icl_lock);

	power_supply_changed(chip->chg_psy);
	if (rc || apsd)
		mod_delayed_work(system_wq, &chip->status_change_work,
				 msecs_to_jiffies(rc ? SMB2_APSD_RETRY_MS :
						    SMB2_APSD_DELAY_MS));
	return rc;
}

static void smb_status_change_work(struct work_struct *work)
{
	unsigned int charger_type, current_ua;
	u64 generation = 0;
	bool notify = false;
	bool retry = false;
	int usb_online = 0;
	int usb_present = 0;
	int apply_rc;
	int count, rc;
	struct smb_chip *chip;

	chip = container_of(work, struct smb_chip, status_change_work.work);

	if (chip->typec_tcpm_present) {
		mutex_lock(&chip->icl_lock);
		if (chip->plugin_check_pending) {
			rc = smb_get_usb_plugin(chip, &usb_present);
			if (rc) {
				if (chip->icl.dirty) {
					apply_rc = smb_apply_icl_locked(chip);
					if (!apply_rc)
						notify = true;
					else
						dev_err_ratelimited(chip->dev,
								    "failed to retry ICL: %d\n",
								    apply_rc);
				}
				retry = true;
				mutex_unlock(&chip->icl_lock);
				goto out;
			}

			chip->plugin_check_pending = false;
			smb_icl_cable_event(&chip->icl, usb_present);
			rc = smb_apply_icl_locked(chip);
			if (rc) {
				retry = true;
				mutex_unlock(&chip->icl_lock);
				goto out;
			}
			notify = true;
		}
		if (chip->icl.dirty) {
			rc = smb_apply_icl_locked(chip);
			if (rc) {
				retry = true;
				mutex_unlock(&chip->icl_lock);
				goto out;
			}
			notify = true;
		}
		if (chip->icl.tcpm_active || !chip->icl.sink_enabled) {
			mutex_unlock(&chip->icl_lock);
			goto out;
		}
		generation = chip->icl.generation;
		mutex_unlock(&chip->icl_lock);
	}

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (rc) {
		retry = chip->typec_tcpm_present;
		goto out;
	}

	if (!usb_online)
		goto out;

	for (count = 0; count < 3; count++) {
		dev_dbg(chip->dev, "get charger type retry %d\n", count);
		rc = smb_apsd_get_charger_type(chip, &charger_type);
		if (rc != -EAGAIN)
			break;
		msleep(100);
	}

	if (rc < 0 && rc != -EAGAIN) {
		dev_err_ratelimited(chip->dev,
				    "get charger type failed: %d\n", rc);
		retry = chip->typec_tcpm_present;
		goto out;
	}

	if (rc < 0) {
		if (chip->typec_tcpm_present) {
			/* Do not start APSD after a newer cable or TCPM event. */
			mutex_lock(&chip->icl_lock);
			if (chip->icl.generation != generation ||
			    chip->icl.tcpm_active) {
				mutex_unlock(&chip->icl_lock);
				goto out;
			}

			rc = regmap_update_bits(chip->regmap,
						chip->base + CMD_APSD,
						APSD_RERUN_BIT,
						APSD_RERUN_BIT);
			mutex_unlock(&chip->icl_lock);
			retry = true;
			if (rc)
				dev_err(chip->dev,
					"failed to rerun APSD: %d\n", rc);
			else
				dev_dbg(chip->dev,
					"charger type unavailable, reran APSD\n");
			goto out;
		}

		rc = regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
					APSD_RERUN_BIT, APSD_RERUN_BIT);
		schedule_delayed_work(&chip->status_change_work,
				      msecs_to_jiffies(SMB2_APSD_RETRY_MS));
		dev_dbg(chip->dev, "get charger type failed, rerun apsd\n");
		return;
	}

	switch (charger_type) {
	case POWER_SUPPLY_USB_TYPE_CDP:
		current_ua = CDP_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		current_ua = DCP_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
	default:
		current_ua = SDP_CURRENT_UA;
		break;
	}

	if (!chip->typec_tcpm_present) {
		smb_write_current_limit(chip, current_ua);
		power_supply_changed(chip->chg_psy);
		return;
	}

	mutex_lock(&chip->icl_lock);
	if (smb_icl_set_apsd(&chip->icl, generation, current_ua)) {
		dev_dbg(chip->dev,
			"ICL APSD result: requested=%uuA generation=%llu\n",
			current_ua, (unsigned long long)generation);
		rc = smb_apply_icl_locked(chip);
		notify = true;
		retry = rc != 0;
		if (rc)
			dev_err(chip->dev,
				"failed to apply APSD current limit: %d\n", rc);
	}
	mutex_unlock(&chip->icl_lock);

out:
	if (notify)
		power_supply_changed(chip->chg_psy);
	if (retry)
		mod_delayed_work(system_wq, &chip->status_change_work,
				 msecs_to_jiffies(SMB2_APSD_RETRY_MS));
}

static void smb_cancel_status_change_work(void *data)
{
	struct smb_chip *chip = data;

	cancel_delayed_work_sync(&chip->status_change_work);
}

static int smb_get_iio_chan(struct smb_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	union power_supply_propval status;

	rc = power_supply_get_property(chip->chg_psy, POWER_SUPPLY_PROP_STATUS,
				       &status);
	if (rc < 0 || status.intval != POWER_SUPPLY_STATUS_CHARGING) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to chan, err = %li", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	return iio_read_channel_processed(chan, val);
}

static int smb_get_prop_health(struct smb_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	switch (stat) {
	case CHARGER_ERROR_STATUS_BAT_OV_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	case BAT_TEMP_STATUS_TOO_COLD_BIT:
		*val = POWER_SUPPLY_HEALTH_COLD;
		return 0;
	case BAT_TEMP_STATUS_TOO_HOT_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	case BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_COOL;
		return 0;
	case BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_WARM;
		return 0;
	default:
		*val = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}
}

static int smb_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return smb_get_iio_chan(chip, chip->usb_in_i_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb_apsd_get_charger_type(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return smb_set_sink_enabled(chip, val->intval);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb_set_current_limit(chip, val->intval);
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		/* Keep TCPM ownership distinct from arbitrary sysfs writes. */
		return !chip->typec_tcpm_present;
	default:
		return 0;
	}
}

static irqreturn_t smb_handle_batt_overvoltage(int irq, void *data)
{
	struct smb_chip *chip = data;
	unsigned int status;

	regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
		    &status);

	if (status & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		/* The hardware stops charging automatically */
		dev_err(chip->dev, "battery overvoltage detected\n");
		power_supply_changed(chip->chg_psy);
	}

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_usb_plugin(int irq, void *data)
{
	struct smb_chip *chip = data;
	int usb_present = 0;
	int apply_rc;
	int rc;

	if (!chip->typec_tcpm_present) {
		power_supply_changed(chip->chg_psy);

		schedule_delayed_work(&chip->status_change_work,
				      msecs_to_jiffies(SMB2_APSD_DELAY_MS));
		return IRQ_HANDLED;
	}

	mutex_lock(&chip->icl_lock);
	rc = smb_get_usb_plugin(chip, &usb_present);
	if (rc) {
		/* Preserve a possible owner, but retry the physical status. */
		chip->plugin_check_pending = true;
		chip->icl.generation++;
		chip->icl.apsd_valid = false;
	} else {
		chip->plugin_check_pending = false;
		smb_icl_cable_event(&chip->icl, usb_present);
		dev_dbg(chip->dev,
			"ICL cable event: present=%u owner=%s generation=%llu\n",
			usb_present, smb_icl_owner(&chip->icl),
			(unsigned long long)chip->icl.generation);
	}
	apply_rc = smb_apply_icl_locked(chip);
	mutex_unlock(&chip->icl_lock);

	if (apply_rc)
		dev_err(chip->dev, "failed to apply cable-event ICL: %d\n",
			apply_rc);

	power_supply_changed(chip->chg_psy);

	if (rc || usb_present || apply_rc)
		mod_delayed_work(system_wq, &chip->status_change_work,
				 msecs_to_jiffies(rc || apply_rc ?
						    SMB2_APSD_RETRY_MS :
						    SMB2_APSD_DELAY_MS));

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_usb_icl_change(int irq, void *data)
{
	struct smb_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_wdog_bark(int irq, void *data)
{
	struct smb_chip *chip = data;
	int rc;

	power_supply_changed(chip->chg_psy);

	rc = regmap_write(chip->regmap, BARK_BITE_WDOG_PET,
			  BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

static const struct power_supply_desc smb_psy_desc = {
	.name = "pmi8998_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb_properties,
	.num_properties = ARRAY_SIZE(smb_properties),
	.get_property = smb_get_property,
	.set_property = smb_set_property,
	.property_is_writeable = smb_property_is_writable,
};

/* Init sequence derived from vendor downstream driver */
static const struct smb_init_register smb_init_seq[] = {
	{ .addr = AICL_RERUN_TIME_CFG, .mask = AICL_RERUN_TIME_MASK, .val = 0 },
	/*
	 * By default configure us as an upstream facing port
	 * FIXME: This will be handled by the type-c driver
	 */
	{ .addr = TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
	  .mask = TYPEC_POWER_ROLE_CMD_MASK | VCONN_EN_SRC_BIT |
		  VCONN_EN_VALUE_BIT,
	  .val = VCONN_EN_SRC_BIT,
	  .typec = true },
	/*
	 * Disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	{ .addr = TYPE_C_CFG,
	  .mask = FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT,
	  .val = 0,
	  .typec = true },
	/* Configure VBUS for software control */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Use VBAT to determine the recharge threshold when battery is full
	 * rather than the state of charge.
	 */
	{ .addr = FG_UPDATE_CFG_2_SEL,
	  .mask = SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
		  VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
	  .val = VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT },
	/* Enable charging */
	{ .addr = USBIN_OPTIONS_1_CFG, .mask = HVDCP_EN_BIT, .val = 0 },
	{ .addr = CHARGING_ENABLE_CMD,
	  .mask = CHARGING_ENABLE_CMD_BIT,
	  .val = CHARGING_ENABLE_CMD_BIT },
	/*
	 * Match downstream defaults
	 * CHG_EN_SRC_BIT - charger enable is controlled by software
	 * CHG_EN_POLARITY_BIT - polarity of charge enable pin when in HW control
	 *                       pulled low on OnePlus 6 and SHIFT6mq
	 * PRETOFAST_TRANSITION_CFG_BIT -
	 * BAT_OV_ECC_BIT -
	 * I_TERM_BIT - Current termination ?? 0 = enabled
	 * AUTO_RECHG_BIT - Enable automatic recharge when battery is full
	 *                  0 = enabled
	 * EN_ANALOG_DROP_IN_VBATT_BIT
	 * CHARGER_INHIBIT_BIT - Inhibit charging based on battery voltage
	 *                       instead of ??
	 */
	{ .addr = CHGR_CFG2,
	  .mask = CHG_EN_SRC_BIT | CHG_EN_POLARITY_BIT |
		  PRETOFAST_TRANSITION_CFG_BIT | BAT_OV_ECC_BIT | I_TERM_BIT |
		  AUTO_RECHG_BIT | EN_ANALOG_DROP_IN_VBATT_BIT |
		  CHARGER_INHIBIT_BIT,
	  .val = CHARGER_INHIBIT_BIT },
	/* STAT pin software override, match downstream. Parallel charging? */
	{ .addr = STAT_CFG,
	  .mask = STAT_SW_OVERRIDE_CFG_BIT,
	  .val = STAT_SW_OVERRIDE_CFG_BIT },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
	  .val = USB51_MODE_BIT },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/* These bits aren't documented anywhere */
	{ .addr = USBIN_5V_AICL_THRESHOLD_CFG,
	  .mask = USBIN_5V_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	{ .addr = USBIN_CONT_AICL_THRESHOLD_CFG,
	  .mask = USBIN_CONT_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_ADC_EN_BIT |
		  USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT |
		  USBIN_HV_COLLAPSE_RESPONSE_BIT |
		  USBIN_LV_COLLAPSE_RESPONSE_BIT,
	  .val = USBIN_HV_COLLAPSE_RESPONSE_BIT |
		 USBIN_LV_COLLAPSE_RESPONSE_BIT | USBIN_AICL_EN_BIT },
	/*
	 * Set pre charge current to default, the OnePlus 6 bootloader
	 * sets this very conservatively.
	 */
	{ .addr = PRE_CHARGE_CURRENT_CFG,
	  .mask = PRE_CHARGE_CURRENT_SETTING_MASK,
	  .val = 500000 / CURRENT_SCALE_FACTOR },
	/*
	 * This overrides all of the current limit options exposed to userspace
	 * and prevents the device from pulling more than ~1A. This is done
	 * to minimise potential fire hazard risks.
	 */
	{ .addr = FAST_CHARGE_CURRENT_CFG,
	  .mask = FAST_CHARGE_CURRENT_SETTING_MASK,
	  .val = THERMAL_FCC_MAX / CURRENT_SCALE_FACTOR },
};

static int smb_thermal_get_max_state(struct thermal_cooling_device *cdev, unsigned long *max_state)
{
	struct smb_chip *chip = cdev->devdata;

	*max_state = (chip->cdev_fcc_max / chip->cdev_fcc_step) - 1;

	return 0;
}

static int smb_thermal_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct smb_chip *chip = cdev->devdata;

	*state = chip->cooling_state;

	return 0;
}

static int smb_thermal_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct smb_chip *chip = cdev->devdata;
	u32 current_ua = chip->cdev_fcc_max - state * chip->cdev_fcc_step;

	regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
		     current_ua / CURRENT_SCALE_FACTOR);
	chip->cooling_state = state;

	return 0;
}

static int smb_thermal_get_requested_power(struct thermal_cooling_device *cdev, u32 *power)
{
	struct smb_chip *chip = cdev->devdata;
	u32 current_ua = chip->cdev_fcc_max - chip->cooling_state * chip->cdev_fcc_step;

	*power = current_ua / 1000;

	return 0;
}

static int smb_thermal_state2power(struct thermal_cooling_device *cdev,
				    unsigned long state, u32 *power)
{
	struct smb_chip *chip = cdev->devdata;
	u32 current_ua = chip->cdev_fcc_max - state * chip->cdev_fcc_step;

	*power = current_ua / 1000;

	return 0;
}

static int smb_thermal_power2state(struct thermal_cooling_device *cdev,
				    u32 power, unsigned long *state)
{
	struct smb_chip *chip = cdev->devdata;

	*state = (chip->cdev_fcc_max - power * 1000) / chip->cdev_fcc_step;

	return 0;
}

static const struct thermal_cooling_device_ops smb_cooling_ops = {
	.get_max_state = smb_thermal_get_max_state,
	.get_cur_state = smb_thermal_get_cur_state,
	.set_cur_state = smb_thermal_set_cur_state,
	.get_requested_power = smb_thermal_get_requested_power,
	.state2power = smb_thermal_state2power,
	.power2state = smb_thermal_power2state,
};

static int smb_init_hw(struct smb_chip *chip)
{
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(smb_init_seq); i++) {
		if (chip->typec_tcpm_present && smb_init_seq[i].typec)
			continue;

		dev_dbg(chip->dev, "%d: Writing 0x%02x to 0x%02x\n", i,
			smb_init_seq[i].val, smb_init_seq[i].addr);
		rc = regmap_update_bits(chip->regmap,
					chip->base + smb_init_seq[i].addr,
					smb_init_seq[i].mask,
					smb_init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "%s: init command %d failed\n",
					     __func__, i);
	}

	return 0;
}

static int smb_init_irq(struct smb_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static int smb_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb_chip *chip;
	int rc, irq;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->name = pdev->name;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usbin_v");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usbin_v IIO channel\n");

	chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usbin_i");
	if (IS_ERR(chip->usb_in_i_chan)) {
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_i_chan),
				     "Couldn't get usbin_i IIO channel\n");
	}

	chip->cooling_state = 0;
	chip->cdev_fcc_max = THERMAL_FCC_MAX;
	chip->cdev_fcc_step = THERMAL_FCC_STEP;
	chip->typec_tcpm_present = smb_typec_tcpm_present(chip->dev);
	mutex_init(&chip->icl_lock);
	chip->icl.dirty = true;
	INIT_DELAYED_WORK(&chip->status_change_work, smb_status_change_work);

	chip->cdev = devm_thermal_of_cooling_device_register(chip->dev,
							     chip->dev->of_node,
							     "qcom-smb-charger",
							     chip,
							     &smb_cooling_ops);
	if (IS_ERR(chip->cdev))
		return PTR_ERR(chip->cdev);

	rc = smb_init_hw(chip);
	if (rc < 0)
		return rc;

	if (chip->typec_tcpm_present) {
		mutex_lock(&chip->icl_lock);
		rc = smb_apply_icl_locked(chip);
		mutex_unlock(&chip->icl_lock);
		if (rc)
			return dev_err_probe(chip->dev, rc,
					     "Failed to set safe input current\n");
	}

	supply_config.drv_data = chip;
	supply_config.fwnode = dev_fwnode(&pdev->dev);

	desc = devm_kzalloc(chip->dev, sizeof(smb_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb_psy_desc, sizeof(smb_psy_desc));
	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
			       (const char *)device_get_match_data(chip->dev));
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = devm_add_action_or_reset(chip->dev,
				      smb_cancel_status_change_work, chip);
	if (rc)
		return rc;

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");

	rc = (chip->batt_info->voltage_max_design_uv - 3487500) / 7500 + 1;
	rc = regmap_update_bits(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
				FLOAT_VOLTAGE_SETTING_MASK, rc);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set vbat max\n");

	rc = smb_init_irq(chip, &irq, "bat-ov", smb_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &chip->cable_irq, "usb-plugin",
			   smb_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &irq, "usbin-icl-change",
			   smb_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	rc = smb_init_irq(chip, &irq, "wdog-bark", smb_handle_wdog_bark);
	if (rc < 0)
		return rc;

	devm_device_init_wakeup(chip->dev);

	rc = devm_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);

	/* Initialise charger state */
	schedule_delayed_work(&chip->status_change_work, 0);

	return 0;
}

#if IS_ENABLED(CONFIG_CHARGER_QCOM_SMB2_KUNIT_TEST)
struct smb_icl_test_context {
	struct smb_chip chip;
	u8 regs[0x700];
	unsigned int fail_reg;
	bool fail_write;
};

static int smb_icl_test_read(void *context, unsigned int reg, unsigned int *val)
{
	struct smb_icl_test_context *ctx = context;

	*val = ctx->regs[reg];
	return 0;
}

static int smb_icl_test_write(void *context, unsigned int reg, unsigned int val)
{
	struct smb_icl_test_context *ctx = context;

	if (ctx->fail_write && reg == ctx->fail_reg)
		return -EIO;
	ctx->regs[reg] = val;
	return 0;
}

static const struct regmap_bus smb_icl_test_bus = {
	.reg_read = smb_icl_test_read,
	.reg_write = smb_icl_test_write,
};

static const struct regmap_config smb_icl_test_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x6ff,
	.cache_type = REGCACHE_NONE,
};

static int smb_icl_test_init(struct kunit *test)
{
	struct smb_icl_test_context *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	dev = kunit_device_register(test, "smb2-icl");
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ctx->chip.dev = dev;
	ctx->chip.regmap = devm_regmap_init(dev, &smb_icl_test_bus, ctx,
					   &smb_icl_test_regmap_config);
	if (IS_ERR(ctx->chip.regmap))
		return PTR_ERR(ctx->chip.regmap);
	ctx->chip.typec_tcpm_present = true;
	ctx->chip.icl.dirty = true;
	mutex_init(&ctx->chip.icl_lock);
	ctx->regs[USBIN_ICL_OPTIONS] = USB51_MODE_BIT;
	ctx->regs[USBIN_OPTIONS_1_CFG] = AUTO_SRC_DETECT_BIT;
	ctx->regs[USBIN_AICL_OPTIONS_CFG] = USBIN_AICL_EN_BIT;
	test->priv = ctx;
	return 0;
}

static void smb_icl_program_contract_test(struct kunit *test)
{
	struct smb_icl_test_context *ctx = test->priv;
	struct smb_icl_policy *icl = &ctx->chip.icl;

	/* Setting a limit alone must not open the physical sink path. */
	smb_icl_request_tcpm(icl, 3000000);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
	smb_icl_set_sink(icl, true);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], (u8)120);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_ICL_OPTIONS], (u8)USBIN_MODE_CHG_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_LOAD_CFG], (u8)ICL_OVERRIDE_AFTER_APSD_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_OPTIONS_1_CFG], (u8)0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_AICL_OPTIONS_CFG], (u8)USBIN_AICL_EN_BIT);
	/* A repeated sink-enable must not discard the negotiated limit. */
	smb_icl_set_sink(icl, true);
	KUNIT_EXPECT_TRUE(test, icl->tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(icl), (u32)3000000);
}

static void smb_icl_same_current_release_mode_test(struct kunit *test)
{
	struct smb_icl_test_context *ctx = test->priv;
	struct smb_icl_policy *icl = &ctx->chip.icl;

	smb_icl_set_sink(icl, true);
	smb_icl_request_tcpm(icl, SDP_CURRENT_UA);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_ASSERT_EQ(test, ctx->regs[USBIN_ICL_OPTIONS], (u8)USBIN_MODE_CHG_BIT);

	/* Same numeric ceiling, different owner: restore the hardware SDP mode. */
	smb_icl_request_tcpm(icl, 0);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], (u8)20);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_ICL_OPTIONS], (u8)USB51_MODE_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_LOAD_CFG], (u8)0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_OPTIONS_1_CFG], (u8)AUTO_SRC_DETECT_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)0);

	KUNIT_ASSERT_TRUE(test, smb_icl_set_apsd(icl, icl->generation, DCP_CURRENT_UA));
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], (u8)60);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_ICL_OPTIONS], (u8)USBIN_MODE_CHG_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_LOAD_CFG], (u8)ICL_OVERRIDE_AFTER_APSD_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_OPTIONS_1_CFG], (u8)AUTO_SRC_DETECT_BIT);
}

static void smb_icl_stop_sink_test(struct kunit *test)
{
	struct smb_icl_test_context *ctx = test->priv;
	struct smb_icl_policy *icl = &ctx->chip.icl;
	u64 generation;

	smb_icl_set_sink(icl, true);
	smb_icl_request_tcpm(icl, 3000000);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	generation = icl->generation;
	smb_icl_set_sink(icl, false);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_FALSE(test, icl->tcpm_active);
	KUNIT_EXPECT_FALSE(test, smb_icl_set_apsd(icl, generation, DCP_CURRENT_UA));
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], (u8)20);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_LOAD_CFG], (u8)0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_OPTIONS_1_CFG], (u8)0);

	/* USB-plugin IRQs caused by source VBUS must not resume sinking. */
	smb_icl_cable_event(icl, true);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
}

static void smb_icl_program_error_stays_suspended_test(struct kunit *test)
{
	static const unsigned int fail_regs[] = {
		USBIN_CURRENT_LIMIT_CFG, USBIN_ICL_OPTIONS,
		USBIN_LOAD_CFG, USBIN_OPTIONS_1_CFG,
	};
	struct smb_icl_test_context *ctx = test->priv;
	struct smb_icl_policy *icl = &ctx->chip.icl;
	int i;

	for (i = 0; i < ARRAY_SIZE(fail_regs); i++) {
		smb_icl_set_sink(icl, true);
		smb_icl_request_tcpm(icl, 3000000);
		KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
		smb_icl_request_tcpm(icl, 0);
		ctx->fail_reg = fail_regs[i];
		ctx->fail_write = true;
		KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), -EIO);
		KUNIT_EXPECT_TRUE(test, icl->dirty);
		KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);

		/* A source transition before the retry must keep the input off. */
		smb_icl_set_sink(icl, false);
		ctx->fail_write = false;
		KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
		KUNIT_EXPECT_FALSE(test, icl->dirty);
		KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
	}
}

static void smb_icl_substep_keeps_input_off_test(struct kunit *test)
{
	struct smb_icl_test_context *ctx = test->priv;

	smb_icl_set_sink(&ctx->chip.icl, true);
	smb_icl_request_tcpm(&ctx->chip.icl, 10000);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_TRUE(test, ctx->chip.icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
	smb_icl_request_tcpm(&ctx->chip.icl, 25000);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)USBIN_SUSPEND_BIT);
	smb_icl_request_tcpm(&ctx->chip.icl, 50000);
	KUNIT_ASSERT_EQ(test, smb_apply_icl_locked(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], (u8)2);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], (u8)0);
}

static void smb_icl_default_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};

	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)SDP_CURRENT_UA);
}

static void smb_icl_tcpm_overrides_stale_apsd_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};
	u64 generation = icl.generation;

	smb_icl_request_tcpm(&icl, 3000000);

	KUNIT_EXPECT_FALSE(test,
			   smb_icl_set_apsd(&icl, generation, SDP_CURRENT_UA));
	KUNIT_EXPECT_TRUE(test, icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)3000000);
}

static void smb_icl_tcpm_release_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};
	u64 generation = icl.generation;

	KUNIT_ASSERT_TRUE(test,
			  smb_icl_set_apsd(&icl, generation, DCP_CURRENT_UA));
	smb_icl_request_tcpm(&icl, 3000000);
	smb_icl_request_tcpm(&icl, 0);

	KUNIT_EXPECT_FALSE(test, icl.tcpm_active);
	KUNIT_EXPECT_FALSE(test, icl.apsd_valid);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)SDP_CURRENT_UA);
}

static void smb_icl_cable_lifecycle_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};

	smb_icl_request_tcpm(&icl, 3000000);
	smb_icl_cable_event(&icl, true);
	KUNIT_EXPECT_TRUE(test, icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)3000000);

	smb_icl_cable_event(&icl, false);
	KUNIT_EXPECT_FALSE(test, icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)SDP_CURRENT_UA);
}

static void smb_icl_substep_request_stays_owned_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};

	/* A non-zero request below the 25mA register step quantizes to zero. */
	smb_icl_request_tcpm(&icl, 10000);
	KUNIT_EXPECT_TRUE(test, icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)0);

	/* Only an original zero-current request releases ownership. */
	smb_icl_request_tcpm(&icl, 0);
	KUNIT_EXPECT_FALSE(test, icl.tcpm_active);
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)SDP_CURRENT_UA);
}

static void smb_icl_stale_generation_test(struct kunit *test)
{
	struct smb_icl_policy icl = {};
	u64 generation = icl.generation;

	smb_icl_cable_event(&icl, false);
	smb_icl_cable_event(&icl, true);

	KUNIT_EXPECT_FALSE(test,
			   smb_icl_set_apsd(&icl, generation, DCP_CURRENT_UA));
	KUNIT_EXPECT_EQ(test, smb_icl_effective(&icl), (u32)SDP_CURRENT_UA);
}

static struct kunit_case smb_icl_test_cases[] = {
	KUNIT_CASE(smb_icl_program_contract_test),
	KUNIT_CASE(smb_icl_same_current_release_mode_test),
	KUNIT_CASE(smb_icl_stop_sink_test),
	KUNIT_CASE(smb_icl_program_error_stays_suspended_test),
	KUNIT_CASE(smb_icl_substep_keeps_input_off_test),
	KUNIT_CASE(smb_icl_default_test),
	KUNIT_CASE(smb_icl_tcpm_overrides_stale_apsd_test),
	KUNIT_CASE(smb_icl_tcpm_release_test),
	KUNIT_CASE(smb_icl_cable_lifecycle_test),
	KUNIT_CASE(smb_icl_substep_request_stays_owned_test),
	KUNIT_CASE(smb_icl_stale_generation_test),
	{}
};

static struct kunit_suite smb_icl_test_suite = {
	.name = "qcom-smbx-icl",
	.init = smb_icl_test_init,
	.test_cases = smb_icl_test_cases,
};

kunit_test_suite(smb_icl_test_suite);
#endif

static const struct of_device_id smb_match_id_table[] = {
	{ .compatible = "qcom,pmi8998-charger", .data = "pmi8998" },
	{ .compatible = "qcom,pm660-charger", .data = "pm660" },
	{ /* sentinal */ }
};
MODULE_DEVICE_TABLE(of, smb_match_id_table);

static struct platform_driver qcom_spmi_smb = {
	.probe = smb_probe,
	.driver = {
		.name = "qcom-smbx-charger",
		.of_match_table = smb_match_id_table,
		},
};

module_platform_driver(qcom_spmi_smb);

MODULE_AUTHOR("Casey Connolly <casey.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB2 Charger Driver");
MODULE_LICENSE("GPL");
