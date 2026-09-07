// SPDX-License-Identifier: GPL-2.0
/* Callback-level tests, included by qcom_pmic_typec_smb2_port.c. */

#include <kunit/device.h>
#include <kunit/test.h>

struct smb2_typec_test {
	struct pmic_typec tcpm;
	struct pmic_typec_port port;
	u8 regs[0x1700];
	unsigned int fail_write_reg;
	unsigned int status_reads;
	unsigned int charger_writes;
	int charger_error;
	int charger_status;
};

static int smb2_test_reg_read(void *context, unsigned int reg, unsigned int *value)
{
	struct smb2_typec_test *priv = context;

	if (reg >= ARRAY_SIZE(priv->regs))
		return -EINVAL;
	if (reg == priv->port.base + SMB2_TYPE_C_STATUS_4_REG)
		priv->status_reads++;
	*value = priv->regs[reg];
	return 0;
}

static int smb2_test_reg_write(void *context, unsigned int reg, unsigned int value)
{
	struct smb2_typec_test *priv = context;

	if (reg >= ARRAY_SIZE(priv->regs))
		return -EINVAL;
	if (reg == priv->fail_write_reg)
		return -EIO;
	priv->regs[reg] = value;
	return 0;
}

static const struct regmap_config smb2_test_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x16ff,
	.reg_read = smb2_test_reg_read,
	.reg_write = smb2_test_reg_write,
};

static int smb2_test_charger_get(struct power_supply *psy,
				 enum power_supply_property property,
				 union power_supply_propval *value)
{
	struct smb2_typec_test *priv = power_supply_get_drvdata(psy);

	if (property != POWER_SUPPLY_PROP_STATUS)
		return -EINVAL;
	value->intval = priv->charger_status;
	return 0;
}

static int smb2_test_charger_set(struct power_supply *psy,
				 enum power_supply_property property,
				 const union power_supply_propval *value)
{
	struct smb2_typec_test *priv = power_supply_get_drvdata(psy);

	if (property != POWER_SUPPLY_PROP_STATUS)
		return -EINVAL;
	priv->charger_writes++;
	if (priv->charger_error)
		return priv->charger_error;
	priv->charger_status = value->intval;
	return 0;
}

static const enum power_supply_property smb2_test_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
};

static const struct power_supply_desc smb2_test_charger_desc = {
	.name = "smb2-typec-kunit-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = smb2_test_charger_properties,
	.num_properties = ARRAY_SIZE(smb2_test_charger_properties),
	.get_property = smb2_test_charger_get,
	.set_property = smb2_test_charger_set,
};

static int smb2_typec_test_init(struct kunit *test)
{
	struct power_supply_config charger_config = {};
	struct smb2_typec_test *priv;
	struct pmic_typec_port *port;
	struct device *dev;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	test->priv = priv;
	priv->fail_write_reg = UINT_MAX;
	port = &priv->port;
	port->base = 0x1300;
	port->cc = TYPEC_CC_OPEN;
	port->try_role = TYPEC_NO_PREFERRED_ROLE;
	mutex_init(&port->lock);
	INIT_DELAYED_WORK(&port->cc_debounce_work, smb2_cc_debounce_work);
	INIT_WORK(&port->cc2_detach_work, smb2_cc2_detach_work);
	INIT_WORK(&port->vconn_oc_work, smb2_vconn_oc_work);
	priv->regs[port->base + SMB2_TYPE_C_SW_CTRL_REG] =
		SMB2_TYPEC_DISABLE_CMD_BIT;

	dev = kunit_device_register(test, "smb2-typec-test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	port->dev = dev;
	port->regmap = devm_regmap_init(dev, NULL, priv, &smb2_test_regmap_config);
	if (IS_ERR(port->regmap))
		return PTR_ERR(port->regmap);

	charger_config.drv_data = priv;
	priv->tcpm.charger = devm_power_supply_register(dev,
							&smb2_test_charger_desc, &charger_config);
	if (IS_ERR(priv->tcpm.charger))
		return PTR_ERR(priv->tcpm.charger);
	priv->tcpm.dev = dev;
	priv->tcpm.pmic_typec_port = port;
	return 0;
}

static void smb2_typec_test_exit(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	cancel_delayed_work_sync(&priv->port.cc_debounce_work);
	cancel_work_sync(&priv->port.cc2_detach_work);
	cancel_work_sync(&priv->port.vconn_oc_work);
}

static void smb2_typec_drp_from_rd_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;
	u32 base = priv->port.base;

	/* Rd gives no Rp hint: reset a stale 1.5 A setting to default Rp. */
	priv->regs[base + SMB2_TYPE_C_CFG_2_REG] =
		SMB2_EN_80UA_180UA_CUR_SOURCE_BIT | SMB2_EN_TRY_SOURCE_MODE_BIT;
	KUNIT_ASSERT_EQ(test, smb2_typec_try_role(&priv->tcpm.tcpc, TYPEC_SINK), 0);
	KUNIT_ASSERT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_DRP, TYPEC_CC_RD), 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_2_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_3_REG],
			SMB2_EN_TRYSINK_MODE_BIT);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_SW_CTRL_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->port.cc, TYPEC_CC_RD);
}

static void smb2_typec_drp_try_role_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;
	u32 base = priv->port.base;

	priv->regs[base + SMB2_TYPE_C_CFG_3_REG] = SMB2_EN_TRYSINK_MODE_BIT;
	KUNIT_ASSERT_EQ(test, smb2_typec_try_role(&priv->tcpm.tcpc, TYPEC_SOURCE), 0);
	KUNIT_ASSERT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_DRP, TYPEC_CC_RP_1_5), 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_2_REG],
			SMB2_EN_80UA_180UA_CUR_SOURCE_BIT | SMB2_EN_TRY_SOURCE_MODE_BIT);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_3_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_SW_CTRL_REG], 0);

	KUNIT_ASSERT_EQ(test, smb2_typec_try_role(&priv->tcpm.tcpc,
						  TYPEC_NO_PREFERRED_ROLE), 0);
	KUNIT_ASSERT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_DRP, TYPEC_CC_RP_DEF), 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_2_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_3_REG], 0);
}

static void smb2_typec_single_role_pbs_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;
	u32 base = priv->port.base;

	priv->port.pbs_wa = true;
	priv->regs[SMB2_TM_IO_DTEST4_SEL_REG] = SMB2_PBS_ENABLED_VALUE;
	priv->regs[base + SMB2_TYPE_C_CFG_2_REG] = SMB2_EN_TRY_SOURCE_MODE_BIT;
	priv->regs[base + SMB2_TYPE_C_CFG_3_REG] = SMB2_EN_TRYSINK_MODE_BIT;
	KUNIT_ASSERT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_SNK, TYPEC_CC_RD), 0);
	KUNIT_EXPECT_EQ(test, priv->regs[SMB2_TM_IO_DTEST4_SEL_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_SW_CTRL_REG],
			SMB2_UFP_EN_CMD_BIT);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_2_REG], 0);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_3_REG], 0);

	KUNIT_ASSERT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_SRC, TYPEC_CC_RP_1_5), 0);
	KUNIT_EXPECT_EQ(test, priv->regs[SMB2_TM_IO_DTEST4_SEL_REG],
			SMB2_PBS_ENABLED_VALUE);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_SW_CTRL_REG],
			SMB2_DFP_EN_CMD_BIT);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_2_REG],
			SMB2_EN_80UA_180UA_CUR_SOURCE_BIT);
	KUNIT_EXPECT_EQ(test, priv->regs[base + SMB2_TYPE_C_CFG_3_REG], 0);
}

static void smb2_typec_invalid_rp_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	KUNIT_EXPECT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_DRP, TYPEC_CC_RP_3_0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, priv->regs[priv->port.base + SMB2_TYPE_C_SW_CTRL_REG],
			SMB2_TYPEC_DISABLE_CMD_BIT);
	KUNIT_EXPECT_EQ(test, priv->port.cc, TYPEC_CC_OPEN);
}

static void smb2_typec_toggle_write_error_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	priv->fail_write_reg = priv->port.base + SMB2_TYPE_C_CFG_3_REG;
	KUNIT_ASSERT_EQ(test, smb2_typec_try_role(&priv->tcpm.tcpc, TYPEC_SINK), 0);
	KUNIT_EXPECT_EQ(test, smb2_typec_start_toggling(&priv->tcpm.tcpc,
							TYPEC_PORT_DRP, TYPEC_CC_RD), -EIO);
	KUNIT_EXPECT_EQ(test, priv->regs[priv->port.base + SMB2_TYPE_C_SW_CTRL_REG],
			SMB2_TYPEC_DISABLE_CMD_BIT);
	KUNIT_EXPECT_EQ(test, priv->port.cc, TYPEC_CC_OPEN);
}

static void smb2_typec_sink_change_without_source_change_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	KUNIT_ASSERT_EQ(test, smb2_typec_set_vbus(&priv->tcpm.tcpc, false, true), 0);
	KUNIT_EXPECT_EQ(test, priv->charger_status, 1);
	KUNIT_EXPECT_EQ(test, priv->charger_writes, 1);
	KUNIT_ASSERT_EQ(test, smb2_typec_set_vbus(&priv->tcpm.tcpc, false, false), 0);
	KUNIT_EXPECT_EQ(test, priv->charger_status, 0);
	KUNIT_EXPECT_EQ(test, priv->charger_writes, 2);
	KUNIT_EXPECT_FALSE(test, priv->port.vbus_enabled);
}

static void smb2_typec_reject_source_and_sink_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	KUNIT_EXPECT_EQ(test, smb2_typec_set_vbus(&priv->tcpm.tcpc, true, true), -EINVAL);
	KUNIT_EXPECT_EQ(test, priv->charger_writes, 0);
	KUNIT_EXPECT_EQ(test, priv->status_reads, 0);
	KUNIT_EXPECT_FALSE(test, priv->port.vbus_enabled);
}

static void smb2_typec_source_requires_charger_suspend_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	priv->charger_status = 1;
	priv->charger_error = -EIO;
	KUNIT_EXPECT_EQ(test, smb2_typec_set_vbus(&priv->tcpm.tcpc, true, false), -EIO);
	KUNIT_EXPECT_EQ(test, priv->charger_writes, 1);
	KUNIT_EXPECT_EQ(test, priv->charger_status, 1);
	/* Abort before even sensing VBUS, which precedes enabling its regulator. */
	KUNIT_EXPECT_EQ(test, priv->status_reads, 0);
	KUNIT_EXPECT_FALSE(test, priv->port.vbus_enabled);
}

static void smb2_typec_source_rejects_external_vbus_test(struct kunit *test)
{
	struct smb2_typec_test *priv = test->priv;

	priv->charger_status = 1;
	priv->regs[priv->port.base + SMB2_TYPE_C_STATUS_4_REG] =
		SMB2_TYPEC_VBUS_STATUS_BIT;
	KUNIT_EXPECT_EQ(test, smb2_typec_set_vbus(&priv->tcpm.tcpc, true, false), -EBUSY);
	KUNIT_EXPECT_EQ(test, priv->charger_status, 0);
	KUNIT_EXPECT_EQ(test, priv->charger_writes, 1);
	KUNIT_EXPECT_TRUE(test, priv->port.vbus_high);
	KUNIT_EXPECT_FALSE(test, priv->port.vbus_enabled);
}

static struct kunit_case smb2_typec_test_cases[] = {
	KUNIT_CASE(smb2_typec_drp_from_rd_test),
	KUNIT_CASE(smb2_typec_drp_try_role_test),
	KUNIT_CASE(smb2_typec_single_role_pbs_test),
	KUNIT_CASE(smb2_typec_invalid_rp_test),
	KUNIT_CASE(smb2_typec_toggle_write_error_test),
	KUNIT_CASE(smb2_typec_sink_change_without_source_change_test),
	KUNIT_CASE(smb2_typec_reject_source_and_sink_test),
	KUNIT_CASE(smb2_typec_source_requires_charger_suspend_test),
	KUNIT_CASE(smb2_typec_source_rejects_external_vbus_test),
	{}
};

static struct kunit_suite smb2_typec_test_suite = {
	.name = "qcom-smb2-typec",
	.init = smb2_typec_test_init,
	.exit = smb2_typec_test_exit,
	.test_cases = smb2_typec_test_cases,
};

kunit_test_suite(smb2_typec_test_suite);
