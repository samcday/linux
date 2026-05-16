// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal Qualcomm PM8921/PM8917 charger support.
 *
 * This intentionally covers only basic limit programming and power-supply
 * reporting. The PM8921 BMS/coulomb-counter block is separate hardware and is
 * not implemented here.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define PBL_ACCESS2		0x005
#define CHG_CNTRL		0x204
#define CHG_IBAT_MAX		0x205
#define CHG_TEST		0x206
#define CHG_IBAT_SAFE		0x210
#define CHG_ITERM		0x215
#define CHG_CNTRL_3		0x216
#define CHG_VDD_MAX		0x220
#define CHG_VDD_SAFE		0x221
#define IUSB_FINE_RES		0x2b6

#define CHG_CHARGE_DIS_BIT	BIT(1)
#define CHG_EN_BIT		BIT(7)
#define CHG_USB_SUSPEND_BIT	BIT(2)

#define PM8921_CHG_V_MIN_MV	3240
#define PM8921_CHG_V_STEP_MV	20
#define PM8921_CHG_V_OFF_10MV	BIT(7)
#define PM8921_CHG_V_MASK	0x7f

#define PM8921_CHG_I_MIN_MA	225
#define PM8921_CHG_I_STEP_MA	50
#define PM8921_CHG_I_MASK	0x3f

#define PM8921_CHG_ITERM_MIN_MA	50
#define PM8921_CHG_ITERM_STEP_MA 10
#define PM8921_CHG_ITERM_MASK	0x0f

#define PM8921_CHG_IUSB_MASK	0x1c
#define PM8921_CHG_IUSB_SHIFT	2
#define PM8917_IUSB_FINE_RES	BIT(0)

#define CAPTURE_FSM_STATE_CMD	0xc2
#define READ_BANK_7		0x70
#define READ_BANK_4		0x40

enum pm8921_fsm_state {
	FSM_STATE_OFF_0 = 0,
	FSM_STATE_ON_CHG_HIGHI_1 = 1,
	FSM_STATE_ATC_2A = 2,
	FSM_STATE_ON_BAT_3 = 3,
	FSM_STATE_ATC_FAIL_4 = 4,
	FSM_STATE_DELAY_5 = 5,
	FSM_STATE_ON_CHG_AND_BAT_6 = 6,
	FSM_STATE_FAST_CHG_7 = 7,
	FSM_STATE_TRKL_CHG_8 = 8,
	FSM_STATE_CHG_FAIL_9 = 9,
	FSM_STATE_EOC_10 = 10,
	FSM_STATE_ON_CHG_VREGOK_11 = 11,
	FSM_STATE_BATFETDET_START_12 = 12,
	FSM_STATE_ATC_PAUSE_13 = 13,
	FSM_STATE_FAST_CHG_PAUSE_14 = 14,
	FSM_STATE_TRKL_CHG_PAUSE_15 = 15,
	FSM_STATE_BATFETDET_END_16 = 16,
	FSM_STATE_ATC_2B = 18,
	FSM_STATE_START_BOOT = 20,
	FSM_STATE_FLCB_VREGOK = 21,
	FSM_STATE_FLCB = 22,
};

struct pm8921_usb_limit {
	unsigned int ua;
	u8 value;
};

static const struct pm8921_usb_limit pm8921_usb_limits[] = {
	{ 100000, 0x0 },
	{ 200000, 0x1 },
	{ 500000, 0x2 },
	{ 600000, 0x3 },
	{ 700000, 0x4 },
	{ 800000, 0x5 },
	{ 850000, 0x6 },
	{ 900000, 0x8 },
	{ 950000, 0x7 },
	{ 1000000, 0x9 },
	{ 1100000, 0xa },
	{ 1200000, 0xb },
	{ 1300000, 0xc },
	{ 1400000, 0xd },
	{ 1500000, 0xe },
	{ 1600000, 0xf },
};

struct pm8921_charger {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *usb;
	struct power_supply *battery;
	struct power_supply_battery_info *info;
	int irq_usb_valid;
	int irq_charge_done;
	int irq_fast_charge;
	int irq_trickle_charge;
	int irq_battery_present;
	unsigned int charge_current_ua;
	unsigned int safe_current_ua;
	unsigned int charge_voltage_ua;
	unsigned int safe_voltage_ua;
	unsigned int term_current_ua;
	unsigned int usb_current_ua;
};

static int pm8921_masked_write(struct pm8921_charger *chg, unsigned int reg,
				       unsigned int mask, unsigned int val)
{
	return regmap_update_bits(chg->regmap, reg, mask, val);
}

static int pm8921_set_charge_voltage(struct pm8921_charger *chg,
				     unsigned int reg, unsigned int uv)
{
	unsigned int mv = uv / 1000;
	unsigned int val;

	val = (mv - PM8921_CHG_V_MIN_MV) / PM8921_CHG_V_STEP_MV;
	if (mv % PM8921_CHG_V_STEP_MV >= 10)
		val |= PM8921_CHG_V_OFF_10MV;

	return pm8921_masked_write(chg, reg, PM8921_CHG_V_MASK | PM8921_CHG_V_OFF_10MV, val);
}

static int pm8921_set_charge_current(struct pm8921_charger *chg,
				     unsigned int reg, unsigned int ua)
{
	unsigned int ma = ua / 1000;
	unsigned int val;

	val = (ma - PM8921_CHG_I_MIN_MA) / PM8921_CHG_I_STEP_MA;

	return pm8921_masked_write(chg, reg, PM8921_CHG_I_MASK, val);
}

static int pm8921_set_term_current(struct pm8921_charger *chg, unsigned int ua)
{
	unsigned int ma = ua / 1000;
	unsigned int val;

	val = (ma - PM8921_CHG_ITERM_MIN_MA) / PM8921_CHG_ITERM_STEP_MA;

	return pm8921_masked_write(chg, CHG_ITERM, PM8921_CHG_ITERM_MASK, val);
}

static int pm8921_set_usb_current(struct pm8921_charger *chg, unsigned int ua)
{
	const struct pm8921_usb_limit *limit = &pm8921_usb_limits[0];
	unsigned int val;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pm8921_usb_limits); i++) {
		if (pm8921_usb_limits[i].ua <= ua)
			limit = &pm8921_usb_limits[i];
	}

	val = (limit->value >> 1) << PM8921_CHG_IUSB_SHIFT;
	ret = pm8921_masked_write(chg, PBL_ACCESS2, PM8921_CHG_IUSB_MASK, val);
	if (ret)
		return ret;

	ret = pm8921_masked_write(chg, IUSB_FINE_RES, PM8917_IUSB_FINE_RES,
				  limit->value & PM8917_IUSB_FINE_RES);
	if (ret)
		return ret;

	chg->usb_current_ua = limit->ua;

	return 0;
}

static int pm8921_get_irq_state(int irq)
{
	bool state = false;

	if (irq < 0)
		return 0;
	if (irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &state))
		return 0;

	return state;
}

static int pm8921_get_fsm_state(struct pm8921_charger *chg)
{
	unsigned int val;
	int ret, state;

	ret = regmap_write(chg->regmap, CHG_TEST, CAPTURE_FSM_STATE_CMD);
	if (ret)
		return ret;

	ret = regmap_write(chg->regmap, CHG_TEST, READ_BANK_7);
	if (ret)
		return ret;

	ret = regmap_read(chg->regmap, CHG_TEST, &val);
	if (ret)
		return ret;
	state = val & 0x0f;

	ret = regmap_write(chg->regmap, CHG_TEST, READ_BANK_4);
	if (ret)
		return ret;

	ret = regmap_read(chg->regmap, CHG_TEST, &val);
	if (ret)
		return ret;

	return state | ((val & 0x01) << 4);
}

static int pm8921_battery_status(struct pm8921_charger *chg)
{
	int state;

	if (!pm8921_get_irq_state(chg->irq_battery_present))
		return POWER_SUPPLY_STATUS_UNKNOWN;

	state = pm8921_get_fsm_state(chg);
	if (state < 0) {
		if (pm8921_get_irq_state(chg->irq_fast_charge) ||
		    pm8921_get_irq_state(chg->irq_trickle_charge))
			return POWER_SUPPLY_STATUS_CHARGING;
		if (!pm8921_get_irq_state(chg->irq_usb_valid))
			return POWER_SUPPLY_STATUS_DISCHARGING;
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}

	switch (state) {
	case FSM_STATE_ON_CHG_HIGHI_1:
	case FSM_STATE_EOC_10:
		return POWER_SUPPLY_STATUS_FULL;
	case FSM_STATE_ATC_2A:
	case FSM_STATE_ATC_2B:
	case FSM_STATE_ON_CHG_AND_BAT_6:
	case FSM_STATE_FAST_CHG_7:
	case FSM_STATE_TRKL_CHG_8:
		return POWER_SUPPLY_STATUS_CHARGING;
	case FSM_STATE_ON_BAT_3:
	case FSM_STATE_ATC_FAIL_4:
	case FSM_STATE_CHG_FAIL_9:
		return POWER_SUPPLY_STATUS_DISCHARGING;
	case FSM_STATE_ON_CHG_VREGOK_11:
	case FSM_STATE_ATC_PAUSE_13:
	case FSM_STATE_FAST_CHG_PAUSE_14:
	case FSM_STATE_TRKL_CHG_PAUSE_15:
	case FSM_STATE_START_BOOT:
	case FSM_STATE_FLCB_VREGOK:
	case FSM_STATE_FLCB:
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
	default:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
}

static int pm8921_usb_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct pm8921_charger *chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = pm8921_get_irq_state(chg->irq_usb_valid);
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chg->usb_current_ua;
		return 0;
	default:
		return -EINVAL;
	}
}

static int pm8921_usb_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct pm8921_charger *chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return pm8921_set_usb_current(chg, val->intval);
	default:
		return -EINVAL;
	}
}

static int pm8921_usb_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_CURRENT_MAX;
}

static enum power_supply_property pm8921_usb_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc pm8921_usb_desc = {
	.name = "pm8921-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = pm8921_usb_properties,
	.num_properties = ARRAY_SIZE(pm8921_usb_properties),
	.get_property = pm8921_usb_get_property,
	.set_property = pm8921_usb_set_property,
	.property_is_writeable = pm8921_usb_property_is_writeable,
};

static int pm8921_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct pm8921_charger *chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = pm8921_battery_status(chg);
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = pm8921_get_irq_state(chg->irq_battery_present);
		return 0;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = chg->charge_current_ua;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = chg->charge_voltage_ua;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		val->intval = chg->term_current_ua;
		return 0;
	default:
		return -EINVAL;
	}
}

static int pm8921_battery_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct pm8921_charger *chg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = pm8921_set_charge_current(chg, CHG_IBAT_MAX, val->intval);
		if (!ret)
			chg->charge_current_ua = val->intval;
		return ret;
	default:
		return -EINVAL;
	}
}

static int pm8921_battery_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT;
}

static enum power_supply_property pm8921_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
};

static char *pm8921_battery_supplied_by[] = {
	"pm8921-usb",
};

static const struct power_supply_desc pm8921_battery_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = pm8921_battery_properties,
	.num_properties = ARRAY_SIZE(pm8921_battery_properties),
	.get_property = pm8921_battery_get_property,
	.set_property = pm8921_battery_set_property,
	.property_is_writeable = pm8921_battery_property_is_writeable,
};

static irqreturn_t pm8921_charger_irq(int irq, void *data)
{
	struct pm8921_charger *chg = data;

	power_supply_changed(chg->usb);
	power_supply_changed(chg->battery);

	return IRQ_HANDLED;
}

static int pm8921_request_irq(struct pm8921_charger *chg, struct platform_device *pdev,
			      const char *name, int *irq)
{
	int ret;

	*irq = platform_get_irq_byname(pdev, name);
	if (*irq < 0)
		return *irq;

	ret = devm_request_threaded_irq(chg->dev, *irq, NULL, pm8921_charger_irq,
					IRQF_ONESHOT, name, chg);
	if (ret)
		return dev_err_probe(chg->dev, ret, "failed to request %s IRQ\n", name);

	return 0;
}

static int pm8921_hw_init(struct pm8921_charger *chg)
{
	int ret;

	ret = pm8921_set_charge_voltage(chg, CHG_VDD_SAFE, chg->safe_voltage_ua);
	if (ret)
		return ret;

	ret = pm8921_set_charge_voltage(chg, CHG_VDD_MAX, chg->charge_voltage_ua);
	if (ret)
		return ret;

	ret = pm8921_set_charge_current(chg, CHG_IBAT_SAFE, chg->safe_current_ua);
	if (ret)
		return ret;

	ret = pm8921_set_charge_current(chg, CHG_IBAT_MAX, chg->charge_current_ua);
	if (ret)
		return ret;

	ret = pm8921_set_term_current(chg, chg->term_current_ua);
	if (ret)
		return ret;

	ret = pm8921_set_usb_current(chg, chg->usb_current_ua);
	if (ret)
		return ret;

	ret = pm8921_masked_write(chg, CHG_CNTRL_3, CHG_USB_SUSPEND_BIT, 0);
	if (ret)
		return ret;

	ret = pm8921_masked_write(chg, CHG_CNTRL, CHG_CHARGE_DIS_BIT, 0);
	if (ret)
		return ret;

	return pm8921_masked_write(chg, CHG_CNTRL_3, CHG_EN_BIT, CHG_EN_BIT);
}

static int pm8921_charger_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct pm8921_charger *chg;
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = &pdev->dev;
	chg->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chg->regmap)
		return -ENODEV;

	chg->safe_voltage_ua = 4350000;
	chg->charge_voltage_ua = 4350000;
	chg->safe_current_ua = 2000000;
	chg->charge_current_ua = 1500000;
	chg->term_current_ua = 60000;
	chg->usb_current_ua = 500000;

	device_property_read_u32(&pdev->dev, "qcom,fast-charge-safe-voltage",
				 &chg->safe_voltage_ua);
	device_property_read_u32(&pdev->dev, "qcom,fast-charge-high-threshold-voltage",
				 &chg->charge_voltage_ua);
	device_property_read_u32(&pdev->dev, "qcom,fast-charge-safe-current",
				 &chg->safe_current_ua);
	device_property_read_u32(&pdev->dev, "qcom,fast-charge-current-limit",
				 &chg->charge_current_ua);
	device_property_read_u32(&pdev->dev, "qcom,term-current", &chg->term_current_ua);
	device_property_read_u32(&pdev->dev, "usb-charge-current-limit", &chg->usb_current_ua);

	ret = pm8921_request_irq(chg, pdev, "usb-valid", &chg->irq_usb_valid);
	if (ret)
		return ret;
	ret = pm8921_request_irq(chg, pdev, "charge-done", &chg->irq_charge_done);
	if (ret)
		return ret;
	ret = pm8921_request_irq(chg, pdev, "fast-charge", &chg->irq_fast_charge);
	if (ret)
		return ret;
	ret = pm8921_request_irq(chg, pdev, "trickle-charge", &chg->irq_trickle_charge);
	if (ret)
		return ret;
	ret = pm8921_request_irq(chg, pdev, "battery-present", &chg->irq_battery_present);
	if (ret)
		return ret;

	ret = pm8921_hw_init(chg);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to initialize charger\n");

	psy_cfg.drv_data = chg;
	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	psy_cfg.supplied_to = pm8921_battery_supplied_by;
	psy_cfg.num_supplicants = ARRAY_SIZE(pm8921_battery_supplied_by);
	chg->usb = devm_power_supply_register(&pdev->dev, &pm8921_usb_desc, &psy_cfg);
	if (IS_ERR(chg->usb))
		return dev_err_probe(&pdev->dev, PTR_ERR(chg->usb),
				     "failed to register USB power supply\n");

	psy_cfg.supplied_to = NULL;
	psy_cfg.num_supplicants = 0;
	chg->battery = devm_power_supply_register(&pdev->dev, &pm8921_battery_desc, &psy_cfg);
	if (IS_ERR(chg->battery))
		return dev_err_probe(&pdev->dev, PTR_ERR(chg->battery),
				     "failed to register battery power supply\n");

	ret = power_supply_get_battery_info(chg->battery, &chg->info);
	if (ret)
		chg->info = NULL;

	platform_set_drvdata(pdev, chg);

	return 0;
}

static void pm8921_charger_remove(struct platform_device *pdev)
{
	struct pm8921_charger *chg = platform_get_drvdata(pdev);

	if (chg->info)
		power_supply_put_battery_info(chg->battery, chg->info);
}

static const struct of_device_id pm8921_charger_of_match[] = {
	{ .compatible = "qcom,pm8917-charger" },
	{ .compatible = "qcom,pm8921-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8921_charger_of_match);

static struct platform_driver pm8921_charger_driver = {
	.probe = pm8921_charger_probe,
	.remove = pm8921_charger_remove,
	.driver = {
		.name = "pm8921-charger",
		.of_match_table = pm8921_charger_of_match,
	},
};
module_platform_driver(pm8921_charger_driver);

MODULE_DESCRIPTION("Qualcomm PM8921/PM8917 charger driver");
MODULE_LICENSE("GPL");
