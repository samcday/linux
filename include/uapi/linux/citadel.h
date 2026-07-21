/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Userspace interface for the Google Citadel SPI transport.
 *
 * Copyright (C) 2017 Google Inc.
 */

#ifndef _UAPI_LINUX_CITADEL_H
#define _UAPI_LINUX_CITADEL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CITADEL_IOC_MAGIC	'c'

struct citadel_ioc_tpm_datagram {
	__u64 buf;
	__u32 len;
	__u32 command;
};

#define CITADEL_IOC_TPM_DATAGRAM	_IOW(CITADEL_IOC_MAGIC, 1, \
					     struct citadel_ioc_tpm_datagram)

#endif /* _UAPI_LINUX_CITADEL_H */
