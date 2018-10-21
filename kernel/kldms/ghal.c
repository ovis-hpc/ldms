/*
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "ghal.h"

int (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
void (*remove)(struct pci_dev *pdev);

struct pci_dev			gpcdr_pdev;

void * ghal_get_subsys_data(struct pci_dev *pdev, int flag)
{
	return pci_get_drvdata(pdev);
}

void ghal_set_subsys_data(struct pci_dev *pdev, int flag, void *arg)
{
	pci_set_drvdata(pdev, arg);

	return;
}

int ghal_get_devnum(struct pci_dev *pdev)
{
	return 0;
}

int ghal_get_subsystem(int subsysid)
{
	return GHAL_SUBSYS_GPCD_REPORTING;
}

void ghal_remove_subsystem(int flag)
{
	(*remove)(&gpcdr_pdev);

	return;
}

int ghal_add_subsystem(int flag,
	int (*gpcdr_probe)(struct pci_dev *pdev, const struct pci_device_id *id),
	void (*gpcdr_remove)(struct pci_dev *pdev))
{
	probe = gpcdr_probe;
	remove = gpcdr_remove;

	return (*probe)(&gpcdr_pdev, NULL);
}


int gpcd_invalid_read_registers(int numregs, uint64_t * regs)
{
	return 0;
}

int gpcd_read_registers(int numregs, uint64_t * regs, uint64_t * current_sample)
{
	int i;

	for (i = 0; i < numregs; i++) {
		current_sample[i] = i;
	}
	return numregs;
}

int gpcd_hal_init(void)
{
	return 0;
}
