// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm PM660/PMI8998 (SMB2 generation) USB Type-C port controller.
 *
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 * Copyright (c) 2026, Sam Day
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/usb/tcpm.h>
#include <linux/workqueue.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_smb2_port.h"

/* Registers relative to the Type-C peripheral at 0x1300. */
#define SMB2_TYPE_C_STATUS_1_REG			0x0b
#define SMB2_UFP_TYPEC_MASK			GENMASK(7, 5)
#define SMB2_UFP_TYPEC_RDSTD_BIT			BIT(7)
#define SMB2_UFP_TYPEC_RD1P5_BIT			BIT(6)
#define SMB2_UFP_TYPEC_RD3P0_BIT			BIT(5)

#define SMB2_TYPE_C_STATUS_2_REG			0x0c
#define SMB2_DFP_TYPEC_MASK			GENMASK(3, 0)
#define SMB2_DFP_RD_OPEN_BIT			BIT(3)
#define SMB2_DFP_RD_RA_VCONN_BIT		BIT(2)
#define SMB2_DFP_RD_RD_BIT			BIT(1)
#define SMB2_DFP_RA_RA_BIT			BIT(0)

#define SMB2_TYPE_C_STATUS_4_REG			0x0e
#define SMB2_UFP_DFP_MODE_STATUS_BIT		BIT(7)
#define SMB2_TYPEC_VBUS_STATUS_BIT		BIT(6)
#define SMB2_TYPEC_VBUS_ERROR_STATUS_BIT	BIT(5)
#define SMB2_TYPEC_DEBOUNCE_DONE_STATUS_BIT	BIT(4)
#define SMB2_TYPEC_VCONN_OVERCURR_STATUS_BIT	BIT(2)
#define SMB2_CC_ORIENTATION_BIT			BIT(1)
#define SMB2_CC_ATTACHED_BIT			BIT(0)

#define SMB2_TYPE_C_STATUS_5_REG			0x0f
#define SMB2_TIMER_STAGE_2_BIT			BIT(4)

#define SMB2_TYPE_C_CFG_REG			0x58
#define SMB2_FACTORY_MODE_DETECTION_EN_BIT	BIT(5)
#define SMB2_VCONN_OC_CFG_BIT			BIT(1)

#define SMB2_TYPE_C_CFG_2_REG			0x59
#define SMB2_DFP_CC_1P4V_OR_1P6V_BIT		BIT(6)
#define SMB2_VCONN_SOFTSTART_CFG_MASK		GENMASK(5, 4)
#define SMB2_EN_TRY_SOURCE_MODE_BIT		BIT(3)
#define SMB2_TYPE_C_UFP_MODE_BIT			BIT(1)
#define SMB2_EN_80UA_180UA_CUR_SOURCE_BIT	BIT(0)

#define SMB2_TYPE_C_CFG_3_REG			0x5a
#define SMB2_TYPEC_LEGACY_CABLE_INT_EN_BIT	BIT(6)
#define SMB2_TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT BIT(5)
#define SMB2_TYPEC_TRYSOURCE_DETECT_INT_EN_BIT	BIT(4)
#define SMB2_TYPEC_TRYSINK_DETECT_INT_EN_BIT	BIT(3)
#define SMB2_EN_TRYSINK_MODE_BIT			BIT(2)

#define SMB2_TAPER_TIMER_SEL_CFG_REG		0x64
#define SMB2_TYPEC_SPARE_CFG_BIT			BIT(7)

#define SMB2_TYPE_C_INTRPT_ENB_REG		0x67
#define SMB2_TYPEC_CCOUT_DETACH_INT_EN_BIT	BIT(7)
#define SMB2_TYPEC_CCOUT_ATTACH_INT_EN_BIT	BIT(6)
#define SMB2_TYPEC_VBUS_ERROR_INT_EN_BIT	BIT(5)
#define SMB2_TYPEC_UFP_AUDIOADAPT_INT_EN_BIT	BIT(4)
#define SMB2_TYPEC_DEBOUNCE_DONE_INT_EN_BIT	BIT(3)
#define SMB2_TYPEC_CCSTATE_CHANGE_INT_EN_BIT	BIT(2)
#define SMB2_TYPEC_VBUS_DEASSERT_INT_EN_BIT	BIT(1)
#define SMB2_TYPEC_VBUS_ASSERT_INT_EN_BIT	BIT(0)

#define SMB2_TYPE_C_SW_CTRL_REG			0x68
#define SMB2_EXIT_SNK_BASED_ON_CC_BIT		BIT(7)
#define SMB2_VCONN_EN_ORIENTATION_BIT		BIT(6)
#define SMB2_TYPEC_VCONN_OC_INT_EN_BIT		BIT(5)
#define SMB2_VCONN_EN_SRC_BIT			BIT(4)
#define SMB2_VCONN_EN_VALUE_BIT			BIT(3)
#define SMB2_TYPEC_POWER_ROLE_CMD_MASK		GENMASK(2, 0)
#define SMB2_UFP_EN_CMD_BIT			BIT(2)
#define SMB2_DFP_EN_CMD_BIT			BIT(1)
#define SMB2_TYPEC_DISABLE_CMD_BIT		BIT(0)

/* Absolute PMIC addresses used by the documented SMB2 workarounds. */
#define SMB2_MISC_CFG_REG			0x1652
#define SMB2_TCC_DEBOUNCE_20MS_BIT		BIT(5)
#define SMB2_TM_IO_DTEST4_SEL_REG		0x16e9
#define SMB2_PBS_ENABLED_VALUE			0xa5

#define SMB2_CC_DEBOUNCE_MS			2
#define SMB2_CC2_MAX_ATTEMPTS			100
#define SMB2_CC2_SAVED_REGS			5
#define SMB2_VCONN_MAX_ATTEMPTS			3
#define SMB2_VCONN_OC_FALL_TRIES		10
#define SMB2_VBUS_POLL_US			100
#define SMB2_VBUS_SETTLE_TIMEOUT_US		250000

#define SMB2_TYPEC_INTR_ENB_MASK \
	(SMB2_TYPEC_CCOUT_DETACH_INT_EN_BIT | \
	 SMB2_TYPEC_CCOUT_ATTACH_INT_EN_BIT | \
	 SMB2_TYPEC_VBUS_ERROR_INT_EN_BIT | \
	 SMB2_TYPEC_UFP_AUDIOADAPT_INT_EN_BIT | \
	 SMB2_TYPEC_DEBOUNCE_DONE_INT_EN_BIT | \
	 SMB2_TYPEC_CCSTATE_CHANGE_INT_EN_BIT | \
	 SMB2_TYPEC_VBUS_DEASSERT_INT_EN_BIT | \
	 SMB2_TYPEC_VBUS_ASSERT_INT_EN_BIT)

#define SMB2_TYPEC_CFG_3_INTR_EN_MASK \
	(SMB2_TYPEC_TRYSOURCE_DETECT_INT_EN_BIT | \
	 SMB2_TYPEC_TRYSINK_DETECT_INT_EN_BIT)

struct smb2_saved_reg {
	unsigned int reg;
	unsigned int mask;
	unsigned int value;
};

struct pmic_typec_port {
	struct device *dev;
	struct tcpm_port *tcpm_port;
	struct regmap *regmap;
	struct regulator *vbus;
	struct mutex lock;
	struct delayed_work cc_debounce_work;
	struct work_struct cc2_detach_work;
	struct work_struct vconn_oc_work;
	struct smb2_saved_reg cc2_saved[SMB2_CC2_SAVED_REGS];
	u32 base;
	int irq;
	unsigned int cc2_saved_count;
	unsigned int cc2_attempts;
	unsigned int vconn_attempts;
	enum typec_cc_status cc;
	int try_role;
	bool started;
	bool irqs_enabled;
	bool vbus_enabled;
	bool vbus_high;
	bool vbus_error_active;
	bool vconn_enabled;
	bool vconn_oc_active;
	bool debouncing_cc;
	bool cc_debounce_notify;
	bool pbs_wa;
	bool cc2_detach_wa;
	bool cc2_detach_active;
};

static const char * const typec_cc_status_name[] = {
	[TYPEC_CC_OPEN] = "Open",
	[TYPEC_CC_RA] = "Ra",
	[TYPEC_CC_RD] = "Rd",
	[TYPEC_CC_RP_DEF] = "Rp-def",
	[TYPEC_CC_RP_1_5] = "Rp-1.5",
	[TYPEC_CC_RP_3_0] = "Rp-3.0",
};

static const char *smb2_cc_name(enum typec_cc_status cc)
{
	if (cc >= ARRAY_SIZE(typec_cc_status_name) || !typec_cc_status_name[cc])
		return "unknown";

	return typec_cc_status_name[cc];
}

/* STATUS_4 orientation is zero for CC1 and one for CC2. */
static const char *smb2_active_cc_name(unsigned int status4)
{
	return status4 & SMB2_CC_ORIENTATION_BIT ? "cc2" : "cc1";
}

static const char *smb2_vconn_cc_name(unsigned int status4)
{
	return status4 & SMB2_CC_ORIENTATION_BIT ? "cc1" : "cc2";
}

static void smb2_schedule_cc_debounce_locked(struct pmic_typec_port *port)
{
	port->debouncing_cc = true;
	mod_delayed_work(system_wq, &port->cc_debounce_work,
			 msecs_to_jiffies(SMB2_CC_DEBOUNCE_MS));
}

static void smb2_cc_debounce_work(struct work_struct *work)
{
	struct pmic_typec_port *port =
		container_of(to_delayed_work(work), struct pmic_typec_port,
			     cc_debounce_work);
	struct tcpm_port *tcpm_port;
	bool notify;

	mutex_lock(&port->lock);
	port->debouncing_cc = false;
	notify = port->cc_debounce_notify;
	port->cc_debounce_notify = false;
	tcpm_port = port->tcpm_port;
	mutex_unlock(&port->lock);

	if (notify && tcpm_port)
		tcpm_cc_change(tcpm_port);
}

static int smb2_set_pbs_locked(struct pmic_typec_port *port, bool forced_sink)
{
	if (!port->pbs_wa)
		return 0;

	return regmap_write(port->regmap, SMB2_TM_IO_DTEST4_SEL_REG,
			    forced_sink ? 0 : SMB2_PBS_ENABLED_VALUE);
}

static int smb2_cc2_save_update_locked(struct pmic_typec_port *port,
				       unsigned int reg, unsigned int mask,
				       unsigned int value)
{
	struct smb2_saved_reg *saved;
	unsigned int old;
	int ret;

	if (port->cc2_saved_count >= ARRAY_SIZE(port->cc2_saved))
		return -EOVERFLOW;

	ret = regmap_read(port->regmap, reg, &old);
	if (ret)
		return ret;

	saved = &port->cc2_saved[port->cc2_saved_count++];
	saved->reg = reg;
	saved->mask = mask;
	saved->value = old & mask;

	return regmap_update_bits(port->regmap, reg, mask, value);
}

static int smb2_cc2_restore_locked(struct pmic_typec_port *port)
{
	int first_error = 0;

	while (port->cc2_saved_count) {
		struct smb2_saved_reg *saved;
		int ret;

		saved = &port->cc2_saved[--port->cc2_saved_count];
		ret = regmap_update_bits(port->regmap, saved->reg, saved->mask,
					 saved->value);
		if (ret && !first_error)
			first_error = ret;
	}

	return first_error;
}

static int smb2_cc2_enter_locked(struct pmic_typec_port *port,
				 unsigned int status4)
{
	int ret;

	if (!port->cc2_detach_wa || port->cc2_detach_active)
		return 0;

	if (!(status4 & SMB2_CC_ATTACHED_BIT) ||
	    !(status4 & SMB2_TYPEC_DEBOUNCE_DONE_STATUS_BIT) ||
	    (status4 & SMB2_UFP_DFP_MODE_STATUS_BIT) ||
	    !(status4 & SMB2_CC_ORIENTATION_BIT))
		return 0;

	port->cc2_saved_count = 0;
	ret = smb2_cc2_save_update_locked(port,
			port->base + SMB2_TYPE_C_CFG_2_REG,
			SMB2_TYPE_C_UFP_MODE_BIT | SMB2_EN_TRY_SOURCE_MODE_BIT,
			SMB2_TYPE_C_UFP_MODE_BIT);
	if (ret)
		goto restore;

	ret = smb2_cc2_save_update_locked(port,
			port->base + SMB2_TYPE_C_CFG_3_REG,
			SMB2_EN_TRYSINK_MODE_BIT, 0);
	if (ret)
		goto restore;

	ret = smb2_cc2_save_update_locked(port,
			port->base + SMB2_TAPER_TIMER_SEL_CFG_REG,
			SMB2_TYPEC_SPARE_CFG_BIT, SMB2_TYPEC_SPARE_CFG_BIT);
	if (ret)
		goto restore;

	ret = smb2_cc2_save_update_locked(port,
			port->base + SMB2_TYPE_C_SW_CTRL_REG,
			SMB2_VCONN_EN_ORIENTATION_BIT, 0);
	if (ret)
		goto restore;

	ret = smb2_cc2_save_update_locked(port, SMB2_MISC_CFG_REG,
			SMB2_TCC_DEBOUNCE_20MS_BIT,
			SMB2_TCC_DEBOUNCE_20MS_BIT);
	if (ret)
		goto restore;

	port->cc2_attempts = 0;
	port->cc2_detach_active = true;
	return 1;

restore:
	smb2_cc2_restore_locked(port);
	return ret;
}

static void smb2_cc2_cancel(struct pmic_typec_port *port)
{
	bool restore;

	mutex_lock(&port->lock);
	restore = port->cc2_detach_active || port->cc2_saved_count;
	port->cc2_detach_active = false;
	mutex_unlock(&port->lock);

	/* Always synchronize a worker that may already hold a stale TCPM target. */
	cancel_work_sync(&port->cc2_detach_work);

	if (!restore)
		return;

	mutex_lock(&port->lock);
	if (smb2_cc2_restore_locked(port))
		dev_err(port->dev, "failed to restore CC2-detach settings\n");
	mutex_unlock(&port->lock);
}

static void smb2_cc2_detach_work(struct work_struct *work)
{
	struct pmic_typec_port *port =
		container_of(work, struct pmic_typec_port, cc2_detach_work);
	struct tcpm_port *tcpm_port = NULL;
	unsigned int status4, status5;
	bool rerun = false;
	bool failed = false;
	int ret;

	mutex_lock(&port->lock);
	if (!port->cc2_detach_active)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_UFP_EN_CMD_BIT | SMB2_DFP_EN_CMD_BIT,
				 SMB2_UFP_EN_CMD_BIT | SMB2_DFP_EN_CMD_BIT);
	if (ret)
		goto fail;

	usleep_range(10000, 11000);

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_UFP_EN_CMD_BIT | SMB2_DFP_EN_CMD_BIT,
				 SMB2_UFP_EN_CMD_BIT);
	if (ret)
		goto fail;

	usleep_range(30000, 31000);

	ret = regmap_read(port->regmap,
			  port->base + SMB2_TYPE_C_STATUS_4_REG, &status4);
	if (ret)
		goto fail;

	ret = regmap_read(port->regmap,
			  port->base + SMB2_TYPE_C_STATUS_5_REG, &status5);
	if (ret)
		goto fail;

	if ((status4 & SMB2_TYPEC_DEBOUNCE_DONE_STATUS_BIT) ||
	    (status5 & SMB2_TIMER_STAGE_2_BIT)) {
		if (++port->cc2_attempts < SMB2_CC2_MAX_ATTEMPTS) {
			rerun = true;
			goto unlock;
		}

		dev_warn(port->dev,
			 "CC2-detach workaround timed out after %u attempts\n",
			 port->cc2_attempts);
	}

	port->cc2_detach_active = false;
	regmap_update_bits(port->regmap, port->base + SMB2_TYPE_C_SW_CTRL_REG,
			   SMB2_EXIT_SNK_BASED_ON_CC_BIT, 0);
	ret = smb2_cc2_restore_locked(port);
	if (ret)
		dev_err(port->dev, "failed to restore CC2-detach settings: %d\n",
			ret);
	tcpm_port = port->tcpm_port;
	goto unlock;

fail:
	failed = true;
	port->cc2_detach_active = false;
	smb2_cc2_restore_locked(port);
	tcpm_port = port->tcpm_port;
unlock:
	mutex_unlock(&port->lock);

	if (failed)
		dev_err(port->dev, "CC2-detach workaround failed: %d\n", ret);
	if (rerun)
		schedule_work(&port->cc2_detach_work);
	else if (tcpm_port)
		tcpm_cc_change(tcpm_port);
}

static int smb2_set_vconn_locked(struct pmic_typec_port *port, bool on,
				 unsigned int status4)
{
	unsigned int mask;
	unsigned int value;

	if (on) {
		mask = SMB2_VCONN_EN_ORIENTATION_BIT | SMB2_VCONN_EN_VALUE_BIT |
		       SMB2_VCONN_EN_SRC_BIT;
		value = SMB2_VCONN_EN_VALUE_BIT | SMB2_VCONN_EN_SRC_BIT;
		if (!(status4 & SMB2_CC_ORIENTATION_BIT))
			value |= SMB2_VCONN_EN_ORIENTATION_BIT;
	} else {
		mask = SMB2_VCONN_EN_VALUE_BIT;
		value = 0;
	}

	return regmap_update_bits(port->regmap,
				  port->base + SMB2_TYPE_C_SW_CTRL_REG,
				  mask, value);
}

static void smb2_vconn_oc_work(struct work_struct *work)
{
	struct pmic_typec_port *port =
		container_of(work, struct pmic_typec_port, vconn_oc_work);
	struct tcpm_port *tcpm_port = NULL;
	unsigned int status4 = 0;
	bool error_recovery = false;
	int ret = 0;
	int i;

	mutex_lock(&port->lock);
	if (!port->vconn_oc_active || !port->started || !port->vconn_enabled)
		goto clear_active;

	dev_err(port->dev, "VCONN overcurrent detected\n");
	ret = smb2_set_vconn_locked(port, false, 0);
	if (ret)
		goto fail;

	if (++port->vconn_attempts > SMB2_VCONN_MAX_ATTEMPTS) {
		dev_err(port->dev,
			"VCONN recovery failed after %u attempts\n",
			port->vconn_attempts - 1);
		ret = -EIO;
		goto fail;
	}

	for (i = 0; i < SMB2_VCONN_OC_FALL_TRIES; i++) {
		usleep_range(1000, 2000);
		ret = regmap_read(port->regmap,
				  port->base + SMB2_TYPE_C_STATUS_4_REG,
				  &status4);
		if (!ret &&
		    !(status4 & SMB2_TYPEC_VCONN_OVERCURR_STATUS_BIT))
			break;
	}
	if (i == SMB2_VCONN_OC_FALL_TRIES) {
		dev_err(port->dev, "VCONN overcurrent did not clear\n");
		if (!ret)
			ret = -ETIMEDOUT;
		goto fail;
	}

	/* A detach makes VCONN recovery unnecessary. */
	if (!(status4 & SMB2_CC_ATTACHED_BIT)) {
		port->vconn_enabled = false;
		port->vconn_attempts = 0;
		goto clear_active;
	}

	ret = smb2_set_vconn_locked(port, true, status4);
	if (ret)
		goto fail;

	port->vconn_oc_active = false;
	mutex_unlock(&port->lock);
	return;

fail:
	/* Keep the physical command off and force TCPM through ErrorRecovery. */
	if (smb2_set_vconn_locked(port, false, 0))
		dev_err(port->dev, "failed to force VCONN off after fault\n");
	port->vconn_enabled = false;
	port->vconn_attempts = 0;
	tcpm_port = port->tcpm_port;
	error_recovery = !!tcpm_port;
	dev_err(port->dev, "VCONN fault is not recoverable: %d\n", ret);

clear_active:
	port->vconn_oc_active = false;
	mutex_unlock(&port->lock);

	if (error_recovery)
		tcpm_port_error_recovery(tcpm_port);
}

static int smb2_vbus_disable_locked(struct pmic_typec_port *port,
				    bool *notify);

static irqreturn_t smb2_typec_port_isr(int irq, void *data)
{
	struct pmic_typec_port *port = data;
	struct tcpm_port *tcpm_port;
	unsigned int status4;
	bool cancel_cc2 = false;
	bool start_cc2 = false;
	bool start_vconn_oc = false;
	bool vbus_error_recovery = false;
	bool vbus_change = false;
	bool cc_change = false;
	bool vbus_high;
	int fault_ret;
	int ret;

	mutex_lock(&port->lock);
	ret = regmap_read(port->regmap,
			  port->base + SMB2_TYPE_C_STATUS_4_REG, &status4);
	if (ret)
		goto unlock;

	vbus_high = !!(status4 & SMB2_TYPEC_VBUS_STATUS_BIT);
	if (port->vbus_high != vbus_high) {
		bool was_high = port->vbus_high;

		port->vbus_high = vbus_high;
		vbus_change = true;

		if (!was_high && vbus_high && port->cc2_detach_active) {
			port->cc2_detach_active = false;
			cancel_cc2 = true;
		} else if (was_high && !vbus_high) {
			ret = smb2_cc2_enter_locked(port, status4);
			if (ret < 0)
				dev_err(port->dev,
					"failed to enter CC2-detach workaround: %d\n",
					ret);
			else
				start_cc2 = ret > 0;
		}
	}

	if (status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT) {
		if (!port->vbus_error_active) {
			port->vbus_error_active = true;
			vbus_error_recovery = true;
			fault_ret = smb2_vbus_disable_locked(port,
							    &vbus_change);
			if (fault_ret)
				dev_err(port->dev,
					"failed to force VBUS off after fault: %d\n",
					fault_ret);
		}
	} else {
		port->vbus_error_active = false;
	}

	if ((status4 & SMB2_TYPEC_VCONN_OVERCURR_STATUS_BIT) &&
	    port->vconn_enabled && !port->vconn_oc_active) {
		port->vconn_oc_active = true;
		start_vconn_oc = true;
	}

	if (!port->debouncing_cc && !port->cc2_detach_active)
		cc_change = true;

unlock:
	tcpm_port = port->tcpm_port;
	mutex_unlock(&port->lock);

	if (cancel_cc2) {
		cancel_work_sync(&port->cc2_detach_work);
		mutex_lock(&port->lock);
		if (smb2_cc2_restore_locked(port))
			dev_err(port->dev,
				"failed to restore CC2-detach settings\n");
		mutex_unlock(&port->lock);
	}

	if (start_cc2)
		schedule_work(&port->cc2_detach_work);
	if (start_vconn_oc)
		schedule_work(&port->vconn_oc_work);
	if (vbus_change && tcpm_port)
		tcpm_vbus_change(tcpm_port);
	if (cc_change && tcpm_port)
		tcpm_cc_change(tcpm_port);
	if (vbus_error_recovery && tcpm_port) {
		dev_err(port->dev, "Type-C VBUS error detected\n");
		tcpm_port_error_recovery(tcpm_port);
	}

	return IRQ_HANDLED;
}

static int smb2_vbus_detect_locked(struct pmic_typec_port *port, bool *changed,
				    unsigned int *status4_out)
{
	unsigned int status4;
	bool was_high = port->vbus_high;
	int ret;

	ret = regmap_read(port->regmap,
			  port->base + SMB2_TYPE_C_STATUS_4_REG, &status4);
	if (ret)
		return ret;

	port->vbus_high = !!(status4 & SMB2_TYPEC_VBUS_STATUS_BIT);
	if (changed)
		*changed = was_high != port->vbus_high;
	if (status4_out)
		*status4_out = status4;
	return port->vbus_high;
}

static int smb2_vbus_wait_locked(struct pmic_typec_port *port, bool high,
				  bool *changed)
{
	unsigned int status4;
	bool was_high = port->vbus_high;
	int ret;

	ret = regmap_read_poll_timeout(port->regmap,
			port->base + SMB2_TYPE_C_STATUS_4_REG, status4,
			!!(status4 & SMB2_TYPEC_VBUS_STATUS_BIT) == high,
			SMB2_VBUS_POLL_US, SMB2_VBUS_SETTLE_TIMEOUT_US);
	if (!ret || ret == -ETIMEDOUT)
		port->vbus_high = !!(status4 & SMB2_TYPEC_VBUS_STATUS_BIT);
	if (changed)
		*changed = was_high != port->vbus_high;

	return ret;
}

/*
 * Balance only this consumer's regulator reference.  Hardware may already
 * have shut the boost path down, but the regulator-core reference still has
 * to be released exactly once.  A disable failure deliberately preserves
 * ownership so TCPM's subsequent ErrorRecovery can retry it.
 */
static int smb2_vbus_disable_locked(struct pmic_typec_port *port, bool *notify)
{
	unsigned int status4;
	bool changed = false;
	int settle_ret;
	int sensed;
	int ret;

	if (!port->vbus_enabled)
		return 0;

	ret = regulator_disable(port->vbus);
	if (ret) {
		sensed = smb2_vbus_detect_locked(port, &changed, &status4);
		*notify |= changed;
		if (sensed < 0) {
			dev_warn_ratelimited(port->dev,
				"failed to reconcile VBUS after disable error: %d\n",
				sensed);
		} else {
			port->vbus_error_active =
				status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT;
		}
		return ret;
	}

	port->vbus_enabled = false;
	settle_ret = smb2_vbus_wait_locked(port, false, &changed);
	*notify |= changed;
	if (settle_ret)
		dev_warn(port->dev, "VBUS did not settle low: %d\n",
			 settle_ret);

	/* Rearm fault reporting only after STATUS4 proves the fault is low. */
	sensed = smb2_vbus_detect_locked(port, &changed, &status4);
	*notify |= changed;
	if (sensed < 0)
		dev_warn_ratelimited(port->dev,
			"failed to verify VBUS fault clear: %d\n", sensed);
	else
		port->vbus_error_active =
			status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT;

	/* Our source is off even if an external rail legitimately remains. */
	return 0;
}

static int smb2_typec_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	int ret;

	mutex_lock(&port->lock);
	ret = smb2_vbus_detect_locked(port, NULL, NULL);
	if (ret < 0) {
		dev_warn_ratelimited(port->dev,
				     "failed to read sensed VBUS: %d\n", ret);
		ret = port->vbus_high;
	}
	mutex_unlock(&port->lock);

	return ret;
}

static int smb2_typec_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	struct tcpm_port *tcpm_port;
	union power_supply_propval value = { .intval = sink };
	unsigned int status4;
	bool notify = false;
	bool changed = false;
	bool vbus_error_recovery = false;
	int rollback_ret;
	int settle_ret;
	int sensed;
	int ret = 0;

	/* Never feed our boost rail back into the charger's input path. */
	if (on && sink)
		return -EINVAL;

	mutex_lock(&port->lock);
	if (!sink) {
		ret = power_supply_set_property(tcpm->charger,
						POWER_SUPPLY_PROP_STATUS, &value);
		if (ret) {
			dev_err(port->dev, "failed to suspend charger input: %d\n",
				ret);
			/* Still turn an existing source off during error recovery. */
			if (!on)
				smb2_vbus_disable_locked(port, &notify);
			goto unlock;
		}
	}
	if (port->vbus_enabled == on) {
		if (on) {
			ret = smb2_vbus_wait_locked(port, true, &changed);
			notify |= changed;
			if (ret)
				dev_err(port->dev,
					"enabled VBUS is not present: %d\n", ret);
		} else {
			sensed = smb2_vbus_detect_locked(port, &changed, &status4);
			notify |= changed;
			if (sensed < 0) {
				dev_warn_ratelimited(port->dev,
					"failed to reconcile disabled VBUS: %d\n",
					sensed);
			} else {
				port->vbus_error_active =
					status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT;
			}
		}
		goto set_sink;
	}

	if (on) {
		sensed = smb2_vbus_detect_locked(port, &changed, &status4);
		notify |= changed;
		if (sensed < 0) {
			ret = sensed;
			goto unlock;
		}
		if (status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT) {
			if (!port->vbus_error_active) {
				port->vbus_error_active = true;
				vbus_error_recovery = true;
			}
			ret = -EIO;
			dev_err(port->dev,
				"refusing to source while VBUS error is asserted\n");
			goto unlock;
		}
		port->vbus_error_active = false;
		if (sensed) {
			ret = -EBUSY;
			dev_err(port->dev,
				"refusing to source while VBUS is already present\n");
			goto unlock;
		}

		ret = regulator_enable(port->vbus);
		if (ret)
			goto unlock;
		port->vbus_enabled = true;

		ret = smb2_vbus_wait_locked(port, true, &changed);
		notify |= changed;
		if (ret) {
			rollback_ret = regulator_disable(port->vbus);
			if (rollback_ret) {
				dev_err(port->dev,
					"VBUS failed to rise and rollback failed: %d\n",
					rollback_ret);
			} else {
				port->vbus_enabled = false;
				settle_ret = smb2_vbus_wait_locked(port, false,
								   &changed);
				notify |= changed;
				if (settle_ret)
					dev_warn(port->dev,
						 "VBUS rollback did not settle low: %d\n",
						 settle_ret);
			}
			goto unlock;
		}
	} else {
		ret = smb2_vbus_disable_locked(port, &notify);
	}
set_sink:
	/* Resume input only after the source regulator is successfully off. */
	if (!ret && sink) {
		ret = power_supply_set_property(tcpm->charger,
						POWER_SUPPLY_PROP_STATUS, &value);
		if (ret)
			dev_err(port->dev, "failed to enable charger input: %d\n",
				ret);
	}
unlock:
	tcpm_port = port->tcpm_port;
	mutex_unlock(&port->lock);
	if (notify && tcpm_port)
		tcpm_vbus_change(tcpm_port);
	if (vbus_error_recovery && tcpm_port)
		tcpm_port_error_recovery(tcpm_port);

	dev_dbg(port->dev, "set_vbus: source=%d sink=%d (%d)\n", on, sink, ret);
	return ret;
}

static int smb2_typec_get_cc(struct tcpc_dev *tcpc,
			     enum typec_cc_status *cc1,
			     enum typec_cc_status *cc2)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	enum typec_cc_status active_cc = TYPEC_CC_OPEN;
	unsigned int status4 = 0;
	unsigned int status = 0;
	bool attached = false;
	int ret = 0;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	mutex_lock(&port->lock);
	ret = regmap_read(port->regmap,
			  port->base + SMB2_TYPE_C_STATUS_4_REG, &status4);
	if (ret)
		goto unlock;

	attached = !!(status4 & SMB2_CC_ATTACHED_BIT);
	if (port->debouncing_cc || port->cc2_detach_active) {
		ret = -EBUSY;
		goto unlock;
	}
	if (!attached)
		goto unlock;

	if (status4 & SMB2_UFP_DFP_MODE_STATUS_BIT) {
		ret = regmap_read(port->regmap,
				  port->base + SMB2_TYPE_C_STATUS_2_REG, &status);
		if (ret)
			goto unlock;

		switch (status & SMB2_DFP_TYPEC_MASK) {
		case SMB2_DFP_RA_RA_BIT:
			active_cc = TYPEC_CC_RA;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		case SMB2_DFP_RD_RD_BIT:
			active_cc = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RD;
			*cc2 = TYPEC_CC_RD;
			break;
		case SMB2_DFP_RD_OPEN_BIT:
			active_cc = TYPEC_CC_RD;
			break;
		case SMB2_DFP_RD_RA_VCONN_BIT:
			active_cc = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		default:
			dev_warn_ratelimited(port->dev,
					     "unexpected source CC status %#x\n",
					     status);
			active_cc = TYPEC_CC_RD;
			break;
		}
	} else {
		ret = regmap_read(port->regmap,
				  port->base + SMB2_TYPE_C_STATUS_1_REG, &status);
		if (ret)
			goto unlock;

		switch (status & SMB2_UFP_TYPEC_MASK) {
		case SMB2_UFP_TYPEC_RDSTD_BIT:
			active_cc = TYPEC_CC_RP_DEF;
			break;
		case SMB2_UFP_TYPEC_RD1P5_BIT:
			active_cc = TYPEC_CC_RP_1_5;
			break;
		case SMB2_UFP_TYPEC_RD3P0_BIT:
			active_cc = TYPEC_CC_RP_3_0;
			break;
		default:
			dev_warn_ratelimited(port->dev,
					     "unexpected sink CC status %#x\n",
					     status);
			active_cc = TYPEC_CC_RP_DEF;
			break;
		}
	}

	if (status4 & SMB2_CC_ORIENTATION_BIT)
		*cc2 = active_cc;
	else
		*cc1 = active_cc;

unlock:
	mutex_unlock(&port->lock);
	dev_dbg(port->dev,
		"get_cc: status4=%#x cc1=%s cc2=%s attached=%d active=%s\n",
		status4, smb2_cc_name(*cc1), smb2_cc_name(*cc2), attached,
		smb2_active_cc_name(status4));
	return ret;
}

static int smb2_set_rp_locked(struct pmic_typec_port *port,
			      enum typec_cc_status cc)
{
	unsigned int value;

	switch (cc) {
	case TYPEC_CC_RP_DEF:
		value = 0;
		break;
	case TYPEC_CC_RP_1_5:
		value = SMB2_EN_80UA_180UA_CUR_SOURCE_BIT;
		break;
	case TYPEC_CC_RP_3_0:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(port->regmap,
				  port->base + SMB2_TYPE_C_CFG_2_REG,
				  SMB2_EN_80UA_180UA_CUR_SOURCE_BIT, value);
}

static int smb2_typec_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	unsigned int role;
	bool forced_sink = false;
	int ret;

	mutex_lock(&port->lock);
	if (port->cc2_detach_active) {
		ret = -EBUSY;
		goto unlock;
	}

	switch (cc) {
	case TYPEC_CC_OPEN:
		role = SMB2_TYPEC_DISABLE_CMD_BIT;
		break;
	case TYPEC_CC_RP_DEF:
	case TYPEC_CC_RP_1_5:
	case TYPEC_CC_RP_3_0:
		ret = smb2_set_rp_locked(port, cc);
		if (ret)
			goto unlock;
		role = SMB2_DFP_EN_CMD_BIT;
		break;
	case TYPEC_CC_RD:
		role = SMB2_UFP_EN_CMD_BIT;
		forced_sink = true;
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	ret = smb2_set_pbs_locked(port, forced_sink);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_TYPEC_POWER_ROLE_CMD_MASK, role);
	if (ret)
		goto unlock;

	port->cc = cc;
	port->cc_debounce_notify = true;
	smb2_schedule_cc_debounce_locked(port);

unlock:
	mutex_unlock(&port->lock);
	dev_dbg(port->dev, "set_cc: %s (%d)\n", smb2_cc_name(cc), ret);
	return ret;
}

static int smb2_typec_set_polarity(struct tcpc_dev *tcpc,
				   enum typec_cc_polarity polarity)
{
	/* The connector graph lets the QMP PHY switch USB3 polarity. */
	return 0;
}

static int smb2_typec_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	unsigned int status4 = 0;
	int ret;

	mutex_lock(&port->lock);
	if (port->cc2_detach_active && on) {
		ret = -EBUSY;
		goto unlock;
	}
	if (port->vconn_enabled == on) {
		ret = 0;
		if (!on)
			goto reset_state;
		goto unlock;
	}

	if (on) {
		ret = regmap_read(port->regmap,
				  port->base + SMB2_TYPE_C_STATUS_4_REG,
				  &status4);
		if (ret)
			goto unlock;
		if (!(status4 & SMB2_CC_ATTACHED_BIT)) {
			ret = -ENOTCONN;
			goto unlock;
		}
		if (status4 & SMB2_TYPEC_VCONN_OVERCURR_STATUS_BIT) {
			ret = -EIO;
			goto unlock;
		}
	}

	ret = smb2_set_vconn_locked(port, on, status4);
	if (ret)
		goto unlock;
	port->vconn_enabled = on;

reset_state:
	port->vconn_attempts = 0;
	if (!on)
		port->vconn_oc_active = false;

unlock:
	mutex_unlock(&port->lock);
	dev_dbg(port->dev, "set_vconn: %s on %s (%d)\n", str_on_off(on),
		smb2_vconn_cc_name(status4), ret);
	return ret;
}

static int smb2_typec_start_toggling(struct tcpc_dev *tcpc,
				     enum typec_port_type port_type,
				     enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	unsigned int role;
	unsigned int trysink;
	unsigned int trysource = 0;
	bool forced_sink;
	int ret;

	mutex_lock(&port->lock);
	if (port->cc2_detach_active) {
		ret = -EBUSY;
		goto unlock;
	}

	switch (port_type) {
	case TYPEC_PORT_SRC:
		ret = smb2_set_rp_locked(port, cc);
		if (ret)
			goto unlock;
		role = SMB2_DFP_EN_CMD_BIT;
		trysink = 0;
		forced_sink = false;
		break;
	case TYPEC_PORT_SNK:
		role = SMB2_UFP_EN_CMD_BIT;
		trysink = 0;
		forced_sink = true;
		break;
	case TYPEC_PORT_DRP:
		/* TCPM enters sink-preferred DRP with Rd, not a source Rp hint. */
		ret = smb2_set_rp_locked(port, cc == TYPEC_CC_RD ?
					TYPEC_CC_RP_DEF : cc);
		if (ret)
			goto unlock;
		role = 0;
		trysink = port->try_role == TYPEC_SINK ?
			  SMB2_EN_TRYSINK_MODE_BIT : 0;
		trysource = port->try_role == TYPEC_SOURCE ?
			    SMB2_EN_TRY_SOURCE_MODE_BIT : 0;
		forced_sink = false;
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	ret = smb2_set_pbs_locked(port, forced_sink);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_CFG_2_REG,
				 SMB2_EN_TRY_SOURCE_MODE_BIT, trysource);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_CFG_3_REG,
				 SMB2_EN_TRYSINK_MODE_BIT, trysink);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_TYPEC_POWER_ROLE_CMD_MASK, role);
	if (ret)
		goto unlock;

	port->cc = cc;
	port->cc_debounce_notify = true;
	smb2_schedule_cc_debounce_locked(port);

unlock:
	mutex_unlock(&port->lock);
	return ret;
}

static int smb2_typec_try_role(struct tcpc_dev *tcpc, int role)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *port = tcpm->pmic_typec_port;

	if (role != TYPEC_NO_PREFERRED_ROLE && role != TYPEC_SINK &&
	    role != TYPEC_SOURCE)
		return -EINVAL;

	mutex_lock(&port->lock);
	/* Applied when TCPM next starts connection detection. */
	port->try_role = role;
	mutex_unlock(&port->lock);

	return 0;
}

static int smb2_typec_port_start(struct pmic_typec *tcpm,
				 struct tcpm_port *tcpm_port)
{
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	unsigned int status4;
	bool enable_irq_line = false;
	bool vbus_error_recovery = false;
	int ret;

	mutex_lock(&port->lock);
	port->tcpm_port = tcpm_port;
	if (port->started) {
		if (tcpm_port && !port->irqs_enabled) {
			ret = regmap_read(port->regmap,
					  port->base + SMB2_TYPE_C_STATUS_4_REG,
					  &status4);
			if (ret)
				goto unlock;
			port->vbus_error_active =
				status4 & SMB2_TYPEC_VBUS_ERROR_STATUS_BIT;
			vbus_error_recovery = port->vbus_error_active;
			port->irqs_enabled = true;
			enable_irq_line = true;
		}
		ret = 0;
		goto unlock;
	}

	ret = smb2_set_pbs_locked(port, false);
	if (ret)
		goto unlock;

	ret = regmap_write(port->regmap,
			   port->base + SMB2_TYPE_C_INTRPT_ENB_REG,
			   SMB2_TYPEC_INTR_ENB_MASK);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_CFG_REG,
				 SMB2_FACTORY_MODE_DETECTION_EN_BIT |
				 SMB2_VCONN_OC_CFG_BIT, 0);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_CFG_2_REG,
				 SMB2_DFP_CC_1P4V_OR_1P6V_BIT |
				 SMB2_VCONN_SOFTSTART_CFG_MASK,
				 SMB2_DFP_CC_1P4V_OR_1P6V_BIT |
				 SMB2_VCONN_SOFTSTART_CFG_MASK);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_CFG_3_REG,
				 SMB2_TYPEC_LEGACY_CABLE_INT_EN_BIT |
				 SMB2_TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT |
				 SMB2_TYPEC_CFG_3_INTR_EN_MASK |
				 SMB2_EN_TRYSINK_MODE_BIT,
				 SMB2_TYPEC_CFG_3_INTR_EN_MASK |
				 SMB2_EN_TRYSINK_MODE_BIT);
	if (ret)
		goto unlock;

	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_TYPEC_POWER_ROLE_CMD_MASK |
				 SMB2_TYPEC_VCONN_OC_INT_EN_BIT |
				 SMB2_VCONN_EN_SRC_BIT |
				 SMB2_VCONN_EN_VALUE_BIT,
				 SMB2_TYPEC_VCONN_OC_INT_EN_BIT |
				 SMB2_VCONN_EN_SRC_BIT);
	if (ret)
		goto unlock;

	ret = smb2_vbus_detect_locked(port, NULL, NULL);
	if (ret < 0)
		goto unlock;

	port->started = true;
	if (tcpm_port) {
		port->irqs_enabled = true;
		enable_irq_line = true;
	}
	ret = 0;

unlock:
	if (ret) {
		regmap_write(port->regmap,
			     port->base + SMB2_TYPE_C_INTRPT_ENB_REG, 0);
		regmap_update_bits(port->regmap,
				   port->base + SMB2_TYPE_C_SW_CTRL_REG,
				   SMB2_TYPEC_POWER_ROLE_CMD_MASK |
				   SMB2_TYPEC_VCONN_OC_INT_EN_BIT |
				   SMB2_VCONN_EN_VALUE_BIT,
				   SMB2_TYPEC_DISABLE_CMD_BIT);
		smb2_set_pbs_locked(port, false);
		port->tcpm_port = NULL;
		port->irqs_enabled = false;
	}
	mutex_unlock(&port->lock);
	if (enable_irq_line)
		enable_irq(port->irq);
	if (vbus_error_recovery) {
		dev_err(port->dev,
			"Type-C VBUS error present while enabling interrupts\n");
		tcpm_port_error_recovery(tcpm_port);
	}
	return ret;
}

static void smb2_typec_port_quiesce(struct pmic_typec *tcpm)
{
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	bool disable_irq_line = false;

	mutex_lock(&port->lock);
	port->tcpm_port = NULL;
	if (port->irqs_enabled) {
		port->irqs_enabled = false;
		disable_irq_line = true;
	}
	mutex_unlock(&port->lock);

	if (disable_irq_line)
		disable_irq(port->irq);

	cancel_delayed_work_sync(&port->cc_debounce_work);
	cancel_work_sync(&port->vconn_oc_work);
	smb2_cc2_cancel(port);

	mutex_lock(&port->lock);
	port->debouncing_cc = false;
	port->cc_debounce_notify = false;
	port->vconn_oc_active = false;
	mutex_unlock(&port->lock);
}

static void smb2_typec_port_stop(struct pmic_typec *tcpm)
{
	struct pmic_typec_port *port = tcpm->pmic_typec_port;
	union power_supply_propval value = { .intval = 0 };
	int ret;

	smb2_typec_port_quiesce(tcpm);

	mutex_lock(&port->lock);
	port->started = false;
	ret = power_supply_set_property(tcpm->charger, POWER_SUPPLY_PROP_STATUS,
					&value);
	if (ret)
		dev_err(port->dev, "failed to suspend charger during removal: %d\n",
			ret);
	ret = regmap_write(port->regmap,
			   port->base + SMB2_TYPE_C_INTRPT_ENB_REG, 0);
	if (ret)
		dev_err(port->dev, "failed to mask Type-C interrupts: %d\n", ret);
	ret = regmap_update_bits(port->regmap,
				 port->base + SMB2_TYPE_C_SW_CTRL_REG,
				 SMB2_TYPEC_POWER_ROLE_CMD_MASK |
				 SMB2_TYPEC_VCONN_OC_INT_EN_BIT |
				 SMB2_VCONN_EN_VALUE_BIT,
				 SMB2_TYPEC_DISABLE_CMD_BIT);
	if (ret)
		dev_err(port->dev, "failed to disable Type-C/VCONN: %d\n", ret);
	ret = smb2_set_pbs_locked(port, false);
	if (ret)
		dev_err(port->dev, "failed to restore PBS control: %d\n", ret);
	port->vconn_enabled = false;
	port->vconn_oc_active = false;
	port->vconn_attempts = 0;
	port->vbus_error_active = false;
	if (port->vbus_enabled) {
		ret = regulator_disable(port->vbus);
		if (ret)
			dev_err(port->dev,
				"failed to disable VBUS during removal: %d\n", ret);
		else
			port->vbus_enabled = false;
	}
	port->tcpm_port = NULL;
	mutex_unlock(&port->lock);
}

int qcom_pmic_typec_smb2_port_probe(struct platform_device *pdev,
				    struct pmic_typec *tcpm,
				    const struct pmic_typec_port_resources *res,
				    struct regmap *regmap, u32 base)
{
	struct device *dev = &pdev->dev;
	struct pmic_typec_port *port;
	struct device_node *connector;
	const char *try_role;
	int ret;

	if (base != 0x1300)
		return dev_err_probe(dev, -EINVAL,
				     "unsupported Type-C base %#x\n", base);
	if (!res || !res->irq_name)
		return -EINVAL;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;
	port->regmap = regmap;
	port->base = base;
	port->cc = TYPEC_CC_OPEN;
	port->try_role = TYPEC_NO_PREFERRED_ROLE;
	if (!fwnode_property_read_string(tcpm->tcpc.fwnode, "try-power-role",
					 &try_role)) {
		ret = typec_find_power_role(try_role);
		if (ret < 0)
			return dev_err_probe(dev, ret, "invalid try-power-role\n");
		port->try_role = ret;
	}
	port->pbs_wa = tcpm->pbs_wa;
	port->cc2_detach_wa = tcpm->cc2_detach_wa;
	mutex_init(&port->lock);
	INIT_DELAYED_WORK(&port->cc_debounce_work, smb2_cc_debounce_work);
	INIT_WORK(&port->cc2_detach_work, smb2_cc2_detach_work);
	INIT_WORK(&port->vconn_oc_work, smb2_vconn_oc_work);

	connector = to_of_node(tcpm->tcpc.fwnode);
	if (connector)
		port->vbus = devm_of_regulator_get_optional(dev, connector,
							     "vbus");
	else
		port->vbus = ERR_PTR(-ENODEV);

	if (IS_ERR(port->vbus) && PTR_ERR(port->vbus) == -ENODEV)
		port->vbus = devm_regulator_get_optional(dev, "vdd-vbus");
	if (IS_ERR(port->vbus))
		return dev_err_probe(dev, PTR_ERR(port->vbus),
				     "missing connector vbus-supply\n");

	port->irq = platform_get_irq_byname(pdev, res->irq_name);
	if (port->irq < 0)
		return port->irq;

	ret = devm_request_threaded_irq(dev, port->irq, NULL,
					smb2_typec_port_isr,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					res->irq_name, port);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request Type-C IRQ\n");

	tcpm->pmic_typec_port = port;
	tcpm->tcpc.get_vbus = smb2_typec_get_vbus;
	tcpm->tcpc.set_vbus = smb2_typec_set_vbus;
	tcpm->tcpc.set_cc = smb2_typec_set_cc;
	tcpm->tcpc.get_cc = smb2_typec_get_cc;
	tcpm->tcpc.set_polarity = smb2_typec_set_polarity;
	tcpm->tcpc.set_vconn = smb2_typec_set_vconn;
	tcpm->tcpc.start_toggling = smb2_typec_start_toggling;
	tcpm->tcpc.try_role = smb2_typec_try_role;
	tcpm->port_start = smb2_typec_port_start;
	tcpm->port_quiesce = smb2_typec_port_quiesce;
	tcpm->port_stop = smb2_typec_port_stop;

	return 0;
}

const struct pmic_typec_port_resources smb2_port_res = {
	.irq_name = "type-c-change",
};

#if IS_ENABLED(CONFIG_TYPEC_QCOM_PMIC_SMB2_KUNIT_TEST)
#include "qcom_pmic_typec_smb2_test.c"
#endif
