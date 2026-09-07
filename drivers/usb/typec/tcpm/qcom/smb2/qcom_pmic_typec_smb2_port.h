/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */
#ifndef __QCOM_PMIC_TYPEC_SMB2_PORT_H__
#define __QCOM_PMIC_TYPEC_SMB2_PORT_H__

#include <linux/platform_device.h>
#include <linux/usb/tcpm.h>

struct pmic_typec_port_resources {
	const char	*irq_name;
};

/* API */

extern const struct pmic_typec_port_resources smb2_port_res;

int qcom_pmic_typec_smb2_port_probe(struct platform_device *pdev,
				    struct pmic_typec *tcpm,
				    const struct pmic_typec_port_resources *res,
				    struct regmap *regmap,
				    u32 base);

#endif /* __QCOM_PMIC_TYPEC_SMB2_PORT_H__ */
