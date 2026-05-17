// SPDX-License-Identifier: GPL-2.0-only
/*
 * Direct SSBI regulators for Qualcomm PM8917.
 *
 * This intentionally covers only the PM8917 PLDOs needed by boards where these
 * rails are not safe to control through RPM votes.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define PM8917_LDO_ENABLE		BIT(7)
#define PM8917_LDO_PULL_DOWN		BIT(6)
#define PM8917_LDO_VPROG_MASK		GENMASK(4, 0)

#define PM8917_LDO_TEST_BANK_MASK	GENMASK(6, 4)
#define PM8917_LDO_TEST_BANK_WRITE	BIT(7)
#define PM8917_LDO_TEST_BANK(n)		((n) << 4)

#define PM8917_LDO_TEST2_VPROG_UPDATE	BIT(3)
#define PM8917_LDO_TEST2_RANGE_SEL	BIT(2)
#define PM8917_LDO_TEST2_FINE_STEP	BIT(1)

#define PM8917_LDO_TEST4_RANGE_EXT	BIT(0)

struct pm8917_ldo {
	struct regulator_desc desc;
	unsigned int ctrl_reg;
	unsigned int test_reg;
};

struct pm8917_regulators {
	struct regmap *regmap;
};

struct pm8917_ldo_data {
	struct pm8917_regulators *pm8917;
	const struct pm8917_ldo *ldo;
};

static const struct linear_range pm8917_pldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(750000, 0, 59, 12500),
	REGULATOR_LINEAR_RANGE(1500000, 60, 123, 25000),
	REGULATOR_LINEAR_RANGE(3100000, 124, 160, 50000),
};

static int pm8917_ldo_read_test(struct pm8917_regulators *pm8917,
				       const struct pm8917_ldo *ldo,
				       unsigned int bank, unsigned int *val)
{
	int ret;

	ret = regmap_write(pm8917->regmap, ldo->test_reg,
			   PM8917_LDO_TEST_BANK(bank));
	if (ret)
		return ret;

	return regmap_read(pm8917->regmap, ldo->test_reg, val);
}

static int pm8917_ldo_update_test(struct pm8917_regulators *pm8917,
					 const struct pm8917_ldo *ldo,
					 unsigned int bank, unsigned int mask,
					 unsigned int val)
{
	unsigned int reg;
	int ret;

	ret = pm8917_ldo_read_test(pm8917, ldo, bank, &reg);
	if (ret)
		return ret;

	reg &= ~(mask | PM8917_LDO_TEST_BANK_MASK);
	reg |= val | PM8917_LDO_TEST_BANK(bank) | PM8917_LDO_TEST_BANK_WRITE;

	return regmap_write(pm8917->regmap, ldo->test_reg, reg);
}

static int pm8917_ldo_get_voltage_sel(struct regulator_dev *rdev)
{
	struct pm8917_ldo_data *data = rdev_get_drvdata(rdev);
	struct pm8917_regulators *pm8917 = data->pm8917;
	const struct pm8917_ldo *ldo = data->ldo;
	unsigned int ctrl, test2, test4, vprog;
	int ret;

	ret = regmap_read(pm8917->regmap, ldo->ctrl_reg, &ctrl);
	if (ret)
		return ret;

	ret = pm8917_ldo_read_test(pm8917, ldo, 2, &test2);
	if (ret)
		return ret;

	ret = pm8917_ldo_read_test(pm8917, ldo, 4, &test4);
	if (ret)
		return ret;

	vprog = (ctrl & PM8917_LDO_VPROG_MASK) << 1;
	if (test2 & PM8917_LDO_TEST2_FINE_STEP)
		vprog++;

	if (test2 & PM8917_LDO_TEST2_RANGE_SEL)
		return vprog;

	if (!(test4 & PM8917_LDO_TEST4_RANGE_EXT))
		return vprog + 60;

	return vprog - 27 + 124;
}

static int pm8917_ldo_set_voltage_sel(struct regulator_dev *rdev,
					      unsigned int selector)
{
	struct pm8917_ldo_data *data = rdev_get_drvdata(rdev);
	struct pm8917_regulators *pm8917 = data->pm8917;
	const struct pm8917_ldo *ldo = data->ldo;
	unsigned int test2 = PM8917_LDO_TEST2_VPROG_UPDATE;
	unsigned int test4 = 0;
	unsigned int vprog;
	int ret;

	if (selector < 60) {
		vprog = selector;
		test2 |= PM8917_LDO_TEST2_RANGE_SEL;
	} else if (selector < 124) {
		vprog = selector - 60;
	} else {
		vprog = selector - 124 + 27;
		test4 = PM8917_LDO_TEST4_RANGE_EXT;
	}

	if (vprog & 1)
		test2 |= PM8917_LDO_TEST2_FINE_STEP;
	vprog >>= 1;

	ret = pm8917_ldo_update_test(pm8917, ldo, 2,
			PM8917_LDO_TEST2_VPROG_UPDATE |
			PM8917_LDO_TEST2_RANGE_SEL |
			PM8917_LDO_TEST2_FINE_STEP, test2);
	if (ret)
		return ret;

	ret = pm8917_ldo_update_test(pm8917, ldo, 4,
			PM8917_LDO_TEST4_RANGE_EXT, test4);
	if (ret)
		return ret;

	return regmap_update_bits(pm8917->regmap, ldo->ctrl_reg,
				  PM8917_LDO_VPROG_MASK, vprog);
}

static const struct regulator_ops pm8917_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.get_voltage_sel = pm8917_ldo_get_voltage_sel,
	.set_voltage_sel = pm8917_ldo_set_voltage_sel,
};

#define PM8917_PLDO(_name, _ctrl, _test) \
	{ \
		.desc = { \
			.name = #_name, \
			.of_match = #_name, \
			.ops = &pm8917_ldo_ops, \
			.type = REGULATOR_VOLTAGE, \
			.owner = THIS_MODULE, \
			.n_voltages = 161, \
			.linear_ranges = pm8917_pldo_ranges, \
			.n_linear_ranges = ARRAY_SIZE(pm8917_pldo_ranges), \
			.enable_reg = _ctrl, \
			.enable_mask = PM8917_LDO_ENABLE, \
			.vsel_reg = _ctrl, \
			.vsel_mask = PM8917_LDO_VPROG_MASK, \
		}, \
		.ctrl_reg = _ctrl, \
		.test_reg = _test, \
	}

static struct pm8917_ldo pm8917_ldos[] = {
	PM8917_PLDO(l30, 0x0a3, 0x0a4),
	PM8917_PLDO(l31, 0x0a5, 0x0a6),
	PM8917_PLDO(l33, 0x0c6, 0x0c7),
};

static int pm8917_regulator_probe(struct platform_device *pdev)
{
	struct pm8917_regulators *pm8917;
	struct regulator_config config = {};
	int i;

	pm8917 = devm_kzalloc(&pdev->dev, sizeof(*pm8917), GFP_KERNEL);
	if (!pm8917)
		return -ENOMEM;

	pm8917->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pm8917->regmap)
		return dev_err_probe(&pdev->dev, -ENODEV, "parent regmap not found\n");

	config.dev = &pdev->dev;
	config.regmap = pm8917->regmap;
	config.driver_data = pm8917;

	for (i = 0; i < ARRAY_SIZE(pm8917_ldos); i++) {
		struct pm8917_ldo_data *data;
		struct regulator_dev *rdev;
		struct device_node *np;

		np = of_get_child_by_name(pdev->dev.of_node,
					  pm8917_ldos[i].desc.of_match);
		if (!np)
			continue;

		data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
		if (!data) {
			of_node_put(np);
			return -ENOMEM;
		}

		data->pm8917 = pm8917;
		data->ldo = &pm8917_ldos[i];
		config.driver_data = data;
		config.of_node = np;

		if (of_property_read_bool(np, "bias-pull-down")) {
			int ret;

			ret = regmap_update_bits(pm8917->regmap,
						 pm8917_ldos[i].ctrl_reg,
						 PM8917_LDO_PULL_DOWN,
						 PM8917_LDO_PULL_DOWN);
			if (ret) {
				of_node_put(np);
				return ret;
			}
		}

		rdev = devm_regulator_register(&pdev->dev,
					      &pm8917_ldos[i].desc, &config);
		of_node_put(np);
		if (IS_ERR(rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rdev),
					     "failed to register %s\n",
					     pm8917_ldos[i].desc.name);
	}

	return 0;
}

static const struct of_device_id pm8917_regulator_match[] = {
	{ .compatible = "qcom,pm8917-direct-regulators" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8917_regulator_match);

static struct platform_driver pm8917_regulator_driver = {
	.probe = pm8917_regulator_probe,
	.driver = {
		.name = "qcom-pm8917-regulator",
		.of_match_table = pm8917_regulator_match,
	},
};
module_platform_driver(pm8917_regulator_driver);

MODULE_DESCRIPTION("Qualcomm PM8917 direct regulator driver");
MODULE_LICENSE("GPL");
