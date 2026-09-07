// SPDX-License-Identifier: GPL-2.0
/*
 * Standalone Qualcomm SMB2-generation PMIC USB Type-C driver.
 *
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 * Copyright (c) 2026, Sam Day
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>
#include <linux/usb/tcpm.h>
#include <soc/qcom/qcom-spmi-pmic.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_pdphy.h"
#include "qcom_pmic_typec_smb2_port.h"

struct smb2_typec_resources {
	const struct pmic_typec_pdphy_resources *pdphy_res;
	const struct pmic_typec_port_resources *port_res;
	const char *charger_compatible;
	unsigned int pmic_subtype;
};

#define SMB2_MAX_SINK_CURRENT_MA		3000
#define SMB2_MAX_SOURCE_CURRENT_MA	1500

static int smb2_validate_pdo_property(struct device *dev,
				      struct fwnode_handle *fwnode,
				      const char *property,
				      unsigned int max_current_ma)
{
	u32 pdos[PDO_MAX_OBJECTS];
	int count;
	int ret;
	int i;

	if (!fwnode_property_present(fwnode, property))
		return 0;

	count = fwnode_property_count_u32(fwnode, property);
	if (count < 1 || count > PDO_MAX_OBJECTS)
		return dev_err_probe(dev, -EINVAL,
				     "%s must contain 1..%d PDOs\n",
				     property, PDO_MAX_OBJECTS);

	ret = fwnode_property_read_u32_array(fwnode, property, pdos, count);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read %s\n", property);

	for (i = 0; i < count; i++) {
		unsigned int current_ma;
		unsigned int voltage_mv;

		if (pdo_type(pdos[i]) != PDO_TYPE_FIXED)
			return dev_err_probe(dev, -EOPNOTSUPP,
					     "%s[%d] is not a fixed PDO\n",
					     property, i);

		voltage_mv = pdo_fixed_voltage(pdos[i]);
		/* Fixed and variable PDOs use the same 10 mA current field. */
		current_ma = pdo_max_current(pdos[i]);
		if (voltage_mv != VSAFE5V)
			return dev_err_probe(dev, -EOPNOTSUPP,
					     "%s[%d] advertises %umV; only 5V is supported\n",
					     property, i, voltage_mv);
		if (!current_ma || current_ma > max_current_ma)
			return dev_err_probe(dev, -ERANGE,
					     "%s[%d] advertises unsupported %umA\n",
					     property, i, current_ma);
	}

	return 0;
}

static int smb2_validate_pd_caps_node(struct device *dev,
				      struct fwnode_handle *fwnode)
{
	int ret;

	ret = smb2_validate_pdo_property(dev, fwnode, "source-pdos",
					 SMB2_MAX_SOURCE_CURRENT_MA);
	if (ret)
		return ret;

	return smb2_validate_pdo_property(dev, fwnode, "sink-pdos",
					  SMB2_MAX_SINK_CURRENT_MA);
}

static int smb2_validate_pd_caps(struct device *dev,
				 struct fwnode_handle *connector)
{
	struct fwnode_handle *capabilities;
	struct fwnode_handle *caps = NULL;
	u8 revision[4];
	int count;
	int ret = 0;

	count = fwnode_property_count_u8(connector, "pd-revision");
	if (count != (int)ARRAY_SIZE(revision))
		return dev_err_probe(dev, -EINVAL,
				     "PD requires a four-byte pd-revision property\n");

	ret = fwnode_property_read_u8_array(connector, "pd-revision",
					    revision, ARRAY_SIZE(revision));
	if (ret)
		return dev_err_probe(dev, ret, "failed to read pd-revision\n");
	if (revision[0] != 2 || revision[1] != 0)
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "only USB PD revision 2.0 is supported\n");

	capabilities = fwnode_get_named_child_node(connector, "capabilities");
	if (!capabilities)
		return smb2_validate_pd_caps_node(dev, connector);

	while ((caps = fwnode_get_next_child_node(capabilities, caps))) {
		ret = smb2_validate_pd_caps_node(dev, caps);
		if (ret) {
			fwnode_handle_put(caps);
			break;
		}
	}

	fwnode_handle_put(capabilities);
	return ret;
}

static int smb2_typec_set_current_limit(struct tcpc_dev *tcpc, u32 max_ma,
					u32 mv)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	union power_supply_propval value;
	int ret;

	/* TCPM also reports Rp-default fallback as (0, VSAFE5V). */
	if (!max_ma && mv && mv != VSAFE5V)
		return -EINVAL;
	if (max_ma && mv != VSAFE5V) {
		dev_err(tcpm->dev, "refusing unsupported %umV contract\n", mv);
		return -EOPNOTSUPP;
	}
	if (max_ma > SMB2_MAX_SINK_CURRENT_MA) {
		dev_err(tcpm->dev, "refusing unsupported %umA contract\n", max_ma);
		return -ERANGE;
	}

	value.intval = max_ma * 1000;
	ret = power_supply_set_property(tcpm->charger,
					POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	if (ret) {
		dev_err(tcpm->dev, "failed to set charger limit to %umA: %d\n",
			max_ma, ret);
		return ret;
	}

	dev_dbg(tcpm->dev, "charger limit set to %umA at %umV\n", max_ma, mv);
	return 0;
}

static int smb2_typec_init(struct tcpc_dev *tcpc)
{
	return 0;
}

static int smb2_typec_probe(struct platform_device *pdev)
{
	const struct smb2_typec_resources *res;
	const struct qcom_spmi_pmic *pmic;
	struct device *dev = &pdev->dev;
	struct device_link *charger_link;
	struct pmic_typec *tcpm;
	struct tcpm_port *tcpm_port;
	struct regmap *regmap;
	bool pd_disabled;
	u32 base;
	int ret;

	res = device_get_match_data(dev);
	if (!res)
		return -ENODEV;

	tcpm = devm_kzalloc(dev, sizeof(*tcpm), GFP_KERNEL);
	if (!tcpm)
		return -ENOMEM;

	tcpm->dev = dev;
	tcpm->tcpc.init = smb2_typec_init;
	tcpm->tcpc.set_current_limit = smb2_typec_set_current_limit;
	tcpm->tcpc.fwnode = device_get_named_child_node(dev, "connector");
	if (!tcpm->tcpc.fwnode)
		return dev_err_probe(dev, -EINVAL, "missing connector node\n");

	pd_disabled = fwnode_property_read_bool(tcpm->tcpc.fwnode,
						"pd-disable");
	tcpm->pd_disabled = pd_disabled;

	pmic = qcom_pmic_get(dev);
	if (IS_ERR(pmic)) {
		ret = dev_err_probe(dev, PTR_ERR(pmic),
				    "failed to read PMIC revision\n");
		goto put_fwnode;
	}
	if (!pmic || pmic->subtype != res->pmic_subtype) {
		ret = dev_err_probe(dev, -ENODEV,
				    "compatible does not match PMIC subtype\n");
		goto put_fwnode;
	}

	tcpm->pbs_wa = true;
	tcpm->cc2_detach_wa = pmic->subtype == PMI8998_SUBTYPE &&
				 pmic->major == 2;

	if (!pd_disabled) {
		ret = smb2_validate_pd_caps(dev, tcpm->tcpc.fwnode);
		if (ret)
			goto put_fwnode;
	}

	/*
	 * qcom_smbx registers this power supply only after its one-time
	 * hardware initialization. Requiring the reference therefore orders
	 * all of our Type-C register programming after the charger writes.
	 */
	tcpm->charger = devm_power_supply_get_by_reference(dev,
							   "charger-power-supply");
	if (IS_ERR(tcpm->charger)) {
		ret = dev_err_probe(dev, PTR_ERR(tcpm->charger),
				    "invalid charger-power-supply\n");
		goto put_fwnode;
	}
	if (!tcpm->charger) {
		ret = dev_err_probe(dev, -EPROBE_DEFER,
				    "waiting for charger power supply\n");
		goto put_fwnode;
	}
	if (!tcpm->charger->dev.parent) {
		ret = dev_err_probe(dev, -ENODEV,
				    "charger has no supplier device\n");
		goto put_fwnode;
	}
	if (!device_is_compatible(tcpm->charger->dev.parent,
				  res->charger_compatible)) {
		ret = dev_err_probe(dev, -EINVAL,
				    "charger-power-supply is not the matching qcom_smbx device\n");
		goto put_fwnode;
	}

	charger_link = device_link_add(dev, tcpm->charger->dev.parent,
				       DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!charger_link) {
		ret = dev_err_probe(dev, -EINVAL,
				    "failed to link charger supplier\n");
		goto put_fwnode;
	}

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		ret = dev_err_probe(dev, -ENODEV, "failed to get parent regmap\n");
		goto put_fwnode;
	}

	ret = of_property_read_u32_index(dev->of_node, "reg", 0, &base);
	if (ret)
		goto put_fwnode;

	ret = qcom_pmic_typec_smb2_port_probe(pdev, tcpm, res->port_res,
					      regmap, base);
	if (ret)
		goto put_fwnode;

	if (!pd_disabled) {
		ret = of_property_read_u32_index(dev->of_node, "reg", 1, &base);
		if (ret) {
			ret = dev_err_probe(dev, ret,
					    "PD enabled without a PD PHY resource\n");
			goto put_fwnode;
		}

		ret = qcom_pmic_typec_smb2_pdphy_probe(pdev, tcpm,
						       res->pdphy_res, regmap, base);
	} else {
		ret = qcom_pmic_typec_smb2_pdphy_stub_probe(pdev, tcpm);
	}
	if (ret)
		goto put_fwnode;

	platform_set_drvdata(pdev, tcpm);

	/*
	 * TCPM synchronously invokes the tcpc callbacks while registering the
	 * port. Prepare both hardware blocks first, but leave their IRQ callback
	 * targets unbound until tcpm_register_port() returns a valid port.
	 */
	ret = tcpm->pdphy_start(tcpm, NULL);
	if (ret)
		goto put_fwnode;

	ret = tcpm->port_start(tcpm, NULL);
	if (ret)
		goto stop_pdphy;

	tcpm_port = tcpm_register_port(dev, &tcpm->tcpc);
	if (IS_ERR(tcpm_port)) {
		ret = PTR_ERR(tcpm_port);
		goto stop_port;
	}
	tcpm->tcpm_port = tcpm_port;

	/* Bind callback targets and only then enable the hardware IRQs. */
	ret = tcpm->pdphy_start(tcpm, tcpm_port);
	if (ret)
		goto quiesce_unregister;

	ret = tcpm->port_start(tcpm, tcpm_port);
	if (ret)
		goto quiesce_unregister;

	/* Reconcile any edge that arrived while IRQs were deliberately masked. */
	tcpm_vbus_change(tcpm_port);
	tcpm_cc_change(tcpm_port);

	dev_info(dev, "registered SMB2 Type-C port (%s)\n",
		 pd_disabled ? "PD disabled" : "PD enabled");
	return 0;

quiesce_unregister:
	tcpm->port_quiesce(tcpm);
	tcpm->pdphy_quiesce(tcpm);
	tcpm->tcpm_port = NULL;
	tcpm_unregister_port(tcpm_port);
stop_port:
	tcpm->port_stop(tcpm);
stop_pdphy:
	tcpm->pdphy_stop(tcpm);
put_fwnode:
	fwnode_handle_put(tcpm->tcpc.fwnode);
	return ret;
}

static void smb2_typec_teardown(struct platform_device *pdev)
{
	struct pmic_typec *tcpm = platform_get_drvdata(pdev);
	struct tcpm_port *tcpm_port;

	if (!tcpm)
		return;

	tcpm_port = tcpm->tcpm_port;
	if (!tcpm_port)
		return;
	tcpm->tcpm_port = NULL;

	/* Stop asynchronous callbacks before TCPM tears down its port object. */
	tcpm->port_quiesce(tcpm);
	tcpm->pdphy_quiesce(tcpm);
	tcpm_unregister_port(tcpm_port);

	/* TCPM has made its final tcpc calls; hardware can now be powered down. */
	tcpm->port_stop(tcpm);
	tcpm->pdphy_stop(tcpm);
	fwnode_handle_put(tcpm->tcpc.fwnode);
	tcpm->tcpc.fwnode = NULL;
}

static void smb2_typec_remove(struct platform_device *pdev)
{
	smb2_typec_teardown(pdev);
}

static void smb2_typec_shutdown(struct platform_device *pdev)
{
	/* Leave no VBUS, VCONN, or PD PHY state active across a warm handoff. */
	smb2_typec_teardown(pdev);
}

static const struct smb2_typec_resources pm660_typec_res = {
	.pdphy_res = &smb2_pdphy_res,
	.port_res = &smb2_port_res,
	.charger_compatible = "qcom,pm660-charger",
	.pmic_subtype = PM660_SUBTYPE,
};

static const struct smb2_typec_resources pmi8998_typec_res = {
	.pdphy_res = &smb2_pdphy_res,
	.port_res = &smb2_port_res,
	.charger_compatible = "qcom,pmi8998-charger",
	.pmic_subtype = PMI8998_SUBTYPE,
};

static const struct of_device_id smb2_typec_of_match[] = {
	{ .compatible = "qcom,pm660-typec", .data = &pm660_typec_res },
	{ .compatible = "qcom,pmi8998-typec", .data = &pmi8998_typec_res },
	{ }
};
MODULE_DEVICE_TABLE(of, smb2_typec_of_match);

static struct platform_driver smb2_typec_driver = {
	.probe = smb2_typec_probe,
	.remove = smb2_typec_remove,
	.shutdown = smb2_typec_shutdown,
	.driver = {
		.name = "qcom-pmic-typec-smb2",
		.of_match_table = smb2_typec_of_match,
	},
};
module_platform_driver(smb2_typec_driver);

MODULE_DESCRIPTION("Qualcomm PM660/PMI8998 SMB2 USB Type-C driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: qcom_smbx");
