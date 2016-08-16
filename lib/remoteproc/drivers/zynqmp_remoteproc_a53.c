/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 * Copyright (c) 2015 Xilinx, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of Mentor Graphics Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************
 * FILE NAME
 *
 *       platform.c
 *
 * DESCRIPTION
 *
 *       This file is the Implementation of IPC hardware layer interface
 *       for Xilinx Zynq ZC702EVK platform.
 *
 **************************************************************************/

#include <string.h>
#include "metal/io.h"
#include "metal/device.h"
#include "metal/utilities.h"
#include "metal/atomic.h"
#include "openamp/hil.h"
#include "openamp/remoteproc_plat.h"
#include "machine.h"

/* -- FIX ME: ipi info is to be defined -- */
struct ipi_info {
	uint32_t ipi_chn_mask;
	int need_reg;
};

/*--------------------------- Declare Functions ------------------------ */
static int _enable_interrupt(struct proc_vring *vring_hw);
static void _notify(int cpu_id, struct proc_intr *intr_info);
static int _boot_cpu(int cpu_id, unsigned int load_addr);
static void _shutdown_cpu(int cpu_id);
static struct hil_proc *_initialize(void *pdata, int cpu_id);
static void _release(struct hil_proc *proc);

static void _ipi_handler(int vect_id, void *data);

/*--------------------------- Globals ---------------------------------- */
struct hil_platform_ops zynqmp_r5_a53_proc_ops = {
	.enable_interrupt     = _enable_interrupt,
	.notify               = _notify,
	.boot_cpu             = _boot_cpu,
	.shutdown_cpu         = _shutdown_cpu,
	.initialize    = _initialize,
	.release    = _release,
};

/* Extern functions defined out from OpenAMP lib */
extern void ipi_enable_interrupt(unsigned int vector);
extern void ipi_isr(int vect_id, void *data);

/*------------------- Extern variable -----------------------------------*/
extern struct hil_proc proc_table[];
extern const int proc_table_size;

void _ipi_handler(int vect_id, void *data)
{
	(void) vect_id;
	struct proc_vring *vring_hw = (struct proc_vring *)(data);
	struct ipi_info *ipi =
		(struct ipi_info *)(vring_hw->intr_info.data);
	struct metal_io_region *io = vring_hw->intr_info.io;
	unsigned int ipi_intr_status =
	    (unsigned int)metal_io_read32(io, IPI_ISR_OFFSET);
	if (ipi_intr_status & ipi->ipi_chn_mask) {
		platform_dcache_all_flush();
		platform_isr(vect_id, data);
		metal_io_write32(io, IPI_ISR_OFFSET,
				ipi->ipi_chn_mask);
	}
}

static int _enable_interrupt(struct proc_vring *vring_hw)
{
	struct ipi_info *ipi =
	    (struct ipi_info *)(vring_hw->intr_info.data);
	struct metal_io_region *io = vring_hw->intr_info.io;

	if (!ipi->need_reg) {
		return 0;
	}

	/* Register ISR */
	env_register_isr_shared(vring_hw->intr_info.vect_id,
			 vring_hw, _ipi_handler, "remoteproc_a53", 1);
	/* Enable IPI interrupt */
	env_enable_interrupt(vring_hw->intr_info.vect_id,
			     vring_hw->intr_info.priority,
			     vring_hw->intr_info.trigger_type);
	metal_io_write32(io, IPI_IER_OFFSET, ipi->ipi_chn_mask);
	return 0;
}

static void _notify(int cpu_id, struct proc_intr *intr_info)
{

	(void)cpu_id;
	struct ipi_info *ipi = (struct ipi_info *)(intr_info->data);
	if (ipi == NULL)
		return;
	platform_dcache_all_flush();

	/* Trigger IPI */
	metal_io_write32(intr_info->io, IPI_TRIG_OFFSET, ipi->ipi_chn_mask);
}

static int _boot_cpu(int cpu_id, unsigned int load_addr)
{
	(void)cpu_id;
	(void)load_addr;
	return -1;
}

static void _shutdown_cpu(int cpu_id)
{
	(void)cpu_id;
	return;
}

static struct hil_proc * _initialize(void *pdata, int cpu_id)
{
	(void) cpu_id;

	struct hil_proc *proc;
	int ret;
	struct metal_io_region *io;
	struct proc_intr *intr_info;
	struct ipi_info *ipi;
	unsigned int ipi_intr_status;

	/* Allocate memory for proc instance */
	proc = env_allocate_memory(sizeof(struct hil_proc));
	if (!proc) {
		return NULL;
	}
	memset(proc, 0, sizeof(struct hil_proc));

	ret = rproc_init_plat_data(pdata, proc);
	if (ret)
		goto error;
	intr_info = &(proc->vdev.vring_info[1].intr_info);
	io = intr_info->io;
	ipi = (struct ipi_info *)(intr_info->data);
	ipi_intr_status =
	    (unsigned int)metal_io_read32(io, IPI_ISR_OFFSET);
	if (ipi_intr_status & ipi->ipi_chn_mask) {
		metal_io_write32(io, IPI_ISR_OFFSET, ipi->ipi_chn_mask);
	}
	/* Enable mapping for the shared memory region */
	if (proc->sh_buff.size)
		metal_io_mem_map((metal_phys_addr_t)proc->sh_buff.start_addr,
			proc->sh_buff.io,
			proc->sh_buff.size);
	return proc;
error:
	if (proc) {
		rproc_close_plat(proc);
		env_free_memory(proc);
	}
	return NULL;
}

static void _release(struct hil_proc *proc)
{
	if (proc) {
		struct proc_intr *intr_info =
			&(proc->vdev.vring_info[1].intr_info);
		struct metal_io_region *io = intr_info->io;
		struct ipi_info *ipi = (struct ipi_info *)(intr_info->data);
		metal_io_write32(io, IPI_IDR_OFFSET, ipi->ipi_chn_mask);

		rproc_close_plat(proc);
		env_free_memory(proc);
	}
}

