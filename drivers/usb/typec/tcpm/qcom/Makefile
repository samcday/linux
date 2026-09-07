# SPDX-License-Identifier: GPL-2.0
#
obj-$(CONFIG_TYPEC_QCOM_PMIC)		+= qcom_pmic_tcpm.o
qcom_pmic_tcpm-y			+= qcom_pmic_typec.o \
					   qcom_pmic_typec_port.o \
					   qcom_pmic_typec_pdphy.o \
					   qcom_pmic_typec_pdphy_stub.o

obj-$(CONFIG_TYPEC_QCOM_PMIC_SMB2)	+= qcom_pmic_typec_smb2.o
qcom_pmic_typec_smb2-y			:= smb2/qcom_pmic_typec_smb2_main.o \
					   smb2/qcom_pmic_typec_smb2_port.o \
					   smb2/qcom_pmic_typec_pdphy.o \
					   smb2/qcom_pmic_typec_pdphy_stub.o
