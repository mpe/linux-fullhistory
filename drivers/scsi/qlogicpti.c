/* qlogicpti.c: Performance Technologies QlogicISP sbus card driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 *
 * A lot of this driver was directly stolen from Erik H. Moe's PCI
 * Qlogic ISP driver.  Mucho kudos to him for this code.
 *
 * An even bigger kudos to John Grana at Performance Technologies
 * for providing me with the hardware to write this driver, you rule
 * John you really do.
 *
 * May, 2, 1997: Added support for QLGC,isp --jj
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>

#include <asm/byteorder.h>

#include "scsi.h"
#include "hosts.h"
#include "qlogicpti.h"

#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/spinlock.h>
#include <asm/machines.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/vaddrs.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/module.h>

#define MAX_TARGETS	16
#define MAX_LUNS	8

#define DEFAULT_LOOP_COUNT	1000000

#include "qlogicpti_asm.c"

static struct qlogicpti *qptichain;
static int qptis_running = 0;

#define PACKB(a, b)			(((a)<<4)|(b))

const u_char mbox_param[] = {
	PACKB(1, 1),	/* MBOX_NO_OP */
	PACKB(5, 5),	/* MBOX_LOAD_RAM */
	PACKB(2, 0),	/* MBOX_EXEC_FIRMWARE */
	PACKB(5, 5),	/* MBOX_DUMP_RAM */
	PACKB(3, 3),	/* MBOX_WRITE_RAM_WORD */
	PACKB(2, 3),	/* MBOX_READ_RAM_WORD */
	PACKB(6, 6),	/* MBOX_MAILBOX_REG_TEST */
	PACKB(2, 3),	/* MBOX_VERIFY_CHECKSUM	*/
	PACKB(1, 3),	/* MBOX_ABOUT_FIRMWARE */
	PACKB(0, 0),	/* 0x0009 */
	PACKB(0, 0),	/* 0x000a */
	PACKB(0, 0),	/* 0x000b */
	PACKB(0, 0),	/* 0x000c */
	PACKB(0, 0),	/* 0x000d */
	PACKB(1, 2),	/* MBOX_CHECK_FIRMWARE */
	PACKB(0, 0),	/* 0x000f */
	PACKB(5, 5),	/* MBOX_INIT_REQ_QUEUE */
	PACKB(6, 6),	/* MBOX_INIT_RES_QUEUE */
	PACKB(4, 4),	/* MBOX_EXECUTE_IOCB */
	PACKB(2, 2),	/* MBOX_WAKE_UP	*/
	PACKB(1, 6),	/* MBOX_STOP_FIRMWARE */
	PACKB(4, 4),	/* MBOX_ABORT */
	PACKB(2, 2),	/* MBOX_ABORT_DEVICE */
	PACKB(3, 3),	/* MBOX_ABORT_TARGET */
	PACKB(2, 2),	/* MBOX_BUS_RESET */
	PACKB(2, 3),	/* MBOX_STOP_QUEUE */
	PACKB(2, 3),	/* MBOX_START_QUEUE */
	PACKB(2, 3),	/* MBOX_SINGLE_STEP_QUEUE */
	PACKB(2, 3),	/* MBOX_ABORT_QUEUE */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_STATUS */
	PACKB(0, 0),	/* 0x001e */
	PACKB(1, 3),	/* MBOX_GET_FIRMWARE_STATUS */
	PACKB(1, 2),	/* MBOX_GET_INIT_SCSI_ID */
	PACKB(1, 2),	/* MBOX_GET_SELECT_TIMEOUT */
	PACKB(1, 3),	/* MBOX_GET_RETRY_COUNT	*/
	PACKB(1, 2),	/* MBOX_GET_TAG_AGE_LIMIT */
	PACKB(1, 2),	/* MBOX_GET_CLOCK_RATE */
	PACKB(1, 2),	/* MBOX_GET_ACT_NEG_STATE */
	PACKB(1, 2),	/* MBOX_GET_ASYNC_DATA_SETUP_TIME */
	PACKB(1, 3),	/* MBOX_GET_SBUS_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_TARGET_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x002a */
	PACKB(0, 0),	/* 0x002b */
	PACKB(0, 0),	/* 0x002c */
	PACKB(0, 0),	/* 0x002d */
	PACKB(0, 0),	/* 0x002e */
	PACKB(0, 0),	/* 0x002f */
	PACKB(2, 2),	/* MBOX_SET_INIT_SCSI_ID */
	PACKB(2, 2),	/* MBOX_SET_SELECT_TIMEOUT */
	PACKB(3, 3),	/* MBOX_SET_RETRY_COUNT	*/
	PACKB(2, 2),	/* MBOX_SET_TAG_AGE_LIMIT */
	PACKB(2, 2),	/* MBOX_SET_CLOCK_RATE */
	PACKB(2, 2),	/* MBOX_SET_ACTIVE_NEG_STATE */
	PACKB(2, 2),	/* MBOX_SET_ASYNC_DATA_SETUP_TIME */
	PACKB(3, 3),	/* MBOX_SET_SBUS_CONTROL_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_TARGET_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x003a */
	PACKB(0, 0),	/* 0x003b */
	PACKB(0, 0),	/* 0x003c */
	PACKB(0, 0),	/* 0x003d */
	PACKB(0, 0),	/* 0x003e */
	PACKB(0, 0),	/* 0x003f */
	PACKB(0, 0),	/* 0x0040 */
	PACKB(0, 0),	/* 0x0041 */
	PACKB(0, 0)	/* 0x0042 */
};

#define MAX_MBOX_COMMAND	(sizeof(mbox_param)/sizeof(u_short))

/* queue length's _must_ be power of two: */
#define QUEUE_DEPTH(in, out, ql)	((in - out) & (ql))
#define REQ_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, 		     \
						    QLOGICISP_REQ_QUEUE_LEN)
#define RES_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, RES_QUEUE_LEN)

static struct proc_dir_entry proc_scsi_qlogicpti = {
	PROC_SCSI_QLOGICPTI, 7, "qlogicpti",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

static inline void qlogicpti_enable_irqs(struct qlogicpti_regs *qregs)
{
	qregs->sbus_ctrl = SBUS_CTRL_ERIRQ | SBUS_CTRL_GENAB;
}


static inline void qlogicpti_disable_irqs(struct qlogicpti_regs *qregs)
{
	qregs->sbus_ctrl = 0;
}

static inline void set_sbus_cfg1(struct qlogicpti_regs *qregs, unsigned char bursts)
{
	if(bursts & DMA_BURST32) {
		qregs->sbus_cfg1 = (SBUS_CFG1_BENAB | SBUS_CFG1_B32);
	} else if(bursts & DMA_BURST16) {
		qregs->sbus_cfg1 = (SBUS_CFG1_BENAB | SBUS_CFG1_B16);
	} else if(bursts & DMA_BURST8) {
		qregs->sbus_cfg1 = (SBUS_CFG1_BENAB | SBUS_CFG1_B8);
	} else {
		qregs->sbus_cfg1 = 0; /* No sbus bursts for you... */
	}
}

static int qlogicpti_mbox_command(struct qlogicpti *qpti, u_short param[], int force)
{
	struct qlogicpti_regs *qregs = qpti->qregs;
	int loop_count;

	if(mbox_param[param[0]] == 0)
		return 1;

	loop_count = DEFAULT_LOOP_COUNT;
	while(--loop_count && (qregs->hcctrl & HCCTRL_HIRQ))
		barrier();
	if(!loop_count)
		printk(KERN_EMERG "qlogicpti: mbox_command loop timeout #1\n");

	switch(mbox_param[param[0]] >> 4) {
	case 6: qregs->mbox5 = param[5];
	case 5: qregs->mbox4 = param[4];
	case 4: qregs->mbox3 = param[3];
	case 3: qregs->mbox2 = param[2];
	case 2: qregs->mbox1 = param[1];
	case 1: qregs->mbox0 = param[0];
	}

	qregs->hcctrl |= HCCTRL_SHIRQ;

	loop_count = DEFAULT_LOOP_COUNT;
	while(--loop_count && !(qregs->sbus_semaphore & SBUS_SEMAPHORE_LCK))
		udelay(20);
	if(!loop_count)
		printk(KERN_EMERG "qlogicpti: mbox_command loop timeout #2\n");

	loop_count = DEFAULT_LOOP_COUNT;
	while(--loop_count && (qregs->mbox0 == 0x04))
		udelay(20);
	if(!loop_count)
		printk(KERN_EMERG "qlogicpti: mbox_command loop timeout #3\n");

	if(force) {
		qregs->hcctrl = HCCTRL_CRIRQ;
	} else {
		if((qregs->mbox5 - qpti->res_out_ptr) == 0)
			qregs->hcctrl = HCCTRL_CRIRQ;
	}

	switch(mbox_param[param[0]] & 0xf) {
	case 6: param[5] = qregs->mbox5;
	case 5: param[4] = qregs->mbox4;
	case 4: param[3] = qregs->mbox3;
	case 3: param[2] = qregs->mbox2;
	case 2: param[1] = qregs->mbox1;
	case 1: param[0] = qregs->mbox0;
	}

	qregs->sbus_semaphore &= ~(SBUS_SEMAPHORE_LCK);
	return 0;
}

static int qlogicpti_reset_hardware(struct Scsi_Host *host)
{
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	struct qlogicpti_regs *qregs = qpti->qregs;
	u_short param[6];
	int loop_count, i;
	unsigned long flags;

	save_flags(flags); cli();

	qregs->hcctrl = HCCTRL_PAUSE;

	/* Only reset the scsi bus if it is not free. */
	if(qregs->cpu_pctrl & CPU_PCTRL_BSY) {
		qregs->cpu_oride = CPU_ORIDE_RMOD;
		qregs->cpu_cmd = CPU_CMD_BRESET;
		udelay(400);
	}

	qregs->sbus_ctrl = SBUS_CTRL_RESET;
	qregs->cmd_dma_ctrl = (DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ);
	qregs->data_dma_ctrl = (DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ);

	loop_count = DEFAULT_LOOP_COUNT;
	while(--loop_count && ((qregs->mbox0 & 0xff) == 0x04))
		udelay(20);
	if(!loop_count)
		printk(KERN_EMERG "qlogicpti: reset_hardware loop timeout\n");

	qregs->hcctrl = HCCTRL_PAUSE;
	set_sbus_cfg1(qregs, qpti->bursts);
	qlogicpti_enable_irqs(qregs);

	if(qregs->risc_psr & RISC_PSR_ULTRA) {
		qpti->ultra = 1;
		qregs->risc_mtreg = (RISC_MTREG_P0ULTRA | RISC_MTREG_P1ULTRA);
	} else {
		qpti->ultra = 0;
		qregs->risc_mtreg = (RISC_MTREG_P0DFLT | RISC_MTREG_P1DFLT);
	}

	/* Release the RISC processor. */
	qregs->hcctrl = HCCTRL_REL;

	/* Get RISC to start executing the firmware code. */
	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr01;
	if(qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot execute ISP firmware.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	/* Set initiator scsi ID. */
	param[0] = MBOX_SET_INIT_SCSI_ID;
	param[1] = qpti->host_param.initiator_scsi_id;
	if(qlogicpti_mbox_command(qpti, param, 1) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot set initiator SCSI ID.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	/* Initialize state of the queues, both hw and sw. */
	qpti->req_in_ptr = qpti->res_out_ptr = 0;

	param[0] = MBOX_INIT_RES_QUEUE;
	param[1] = RES_QUEUE_LEN + 1;
	param[2] = (u_short) (qpti->res_dvma >> 16);
	param[3] = (u_short) (qpti->res_dvma & 0xffff);
	param[4] = param[5] = 0;
	if(qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot init response queue.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	param[0] = MBOX_INIT_REQ_QUEUE;
	param[1] = QLOGICISP_REQ_QUEUE_LEN + 1;
	param[2] = (u_short) (qpti->req_dvma >> 16);
	param[3] = (u_short) (qpti->req_dvma & 0xffff);
	param[4] = param[5] = 0;
	if(qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot init request queue.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	param[0] = MBOX_SET_RETRY_COUNT;
	param[1] = qpti->host_param.retry_count;
	param[2] = qpti->host_param.retry_delay;
	qlogicpti_mbox_command(qpti, param, 0);

	param[0] = MBOX_SET_TAG_AGE_LIMIT;
	param[1] = qpti->host_param.tag_aging;
	qlogicpti_mbox_command(qpti, param, 0);

	for(i = 0; i < MAX_TARGETS; i++) {
		param[0] = MBOX_GET_DEV_QUEUE_PARAMS;
		param[1] = (i << 8);
		qlogicpti_mbox_command(qpti, param, 0);
	}

	param[0] = MBOX_GET_FIRMWARE_STATUS;
	qlogicpti_mbox_command(qpti, param, 0);

	param[0] = MBOX_SET_SELECT_TIMEOUT;
	param[1] = qpti->host_param.selection_timeout;
	qlogicpti_mbox_command(qpti, param, 0);

	for(i = 0; i < MAX_TARGETS; i++) {
		param[0] = MBOX_SET_TARGET_PARAMS;
		param[1] = (i << 8);
		param[2] = (qpti->dev_param[i].device_flags << 8);
		param[3] = (qpti->dev_param[i].synchronous_offset << 8) |
			qpti->dev_param[i].synchronous_period;
		qlogicpti_mbox_command(qpti, param, 0);
	}

	restore_flags(flags);
	return 0;
}

#define PTI_RESET_LIMIT 400

__initfunc(static int qlogicpti_load_firmware(struct qlogicpti *qpti))
{
	struct qlogicpti_regs *qregs = qpti->qregs;
	unsigned short csum = 0;
	unsigned short param[6];
	unsigned long flags;
#if !defined(MODULE) && !defined(__sparc_v9__)
	unsigned long dvma_addr;
#endif
	int i, timeout;

	save_flags(flags); cli();

	/* Verify the checksum twice, one before loading it, and once
	 * afterwards via the mailbox commands.
	 */
	for(i = 0; i < risc_code_length01; i++)
		csum += risc_code01[i];
	if(csum) {
		printk(KERN_EMERG "qlogicpti%d: AIeee, firmware checksum failed!",
		       qpti->qpti_id);
		return 1;
	}		
	qregs->sbus_ctrl = SBUS_CTRL_RESET;
	qregs->cmd_dma_ctrl = (DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ);
	qregs->data_dma_ctrl = (DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ);
	timeout = PTI_RESET_LIMIT;
	while(--timeout && (qregs->sbus_ctrl & SBUS_CTRL_RESET))
		udelay(20);
	if(!timeout) {
		printk(KERN_EMERG "qlogicpti%d: Cannot reset the ISP.", qpti->qpti_id);
		return 1;
	}

	qregs->hcctrl = HCCTRL_RESET;
	mdelay(1);

	qregs->sbus_ctrl = (SBUS_CTRL_GENAB | SBUS_CTRL_ERIRQ);
	set_sbus_cfg1(qregs, qpti->bursts);
	qregs->sbus_semaphore = 0;

	if(qregs->risc_psr & RISC_PSR_ULTRA) {
		qpti->ultra = 1;
		qregs->risc_mtreg = (RISC_MTREG_P0ULTRA | RISC_MTREG_P1ULTRA);
	} else {
		qpti->ultra = 0;
		qregs->risc_mtreg = (RISC_MTREG_P0DFLT | RISC_MTREG_P1DFLT);
	}

	qregs->hcctrl = HCCTRL_REL;

	/* Pin lines are only stable while RISC is paused. */
	qregs->hcctrl = HCCTRL_PAUSE;
	if(qregs->cpu_pdiff & CPU_PDIFF_MODE)
		qpti->differential = 1;
	else
		qpti->differential = 0;
	qregs->hcctrl = HCCTRL_REL;

	/* XXX Talk to PTI engineer about the following, ISP always
	 * XXX returns 0x4001 return status for stop firmware command,
	 * XXX documentation claims this means the cmd is unsupported
	 * XXX on this ISP.  I think something fishy is going on.
	 */
	param[0] = MBOX_STOP_FIRMWARE;
	param[1] = param[2] = param[3] = param[4] = param[5] = 0;
	if(qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot stop firmware for reload.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}		

	/* Load the firmware. */
#if !defined(MODULE) && !defined(__sparc_v9__)
	if (sparc_cpu_model != sun4d) {
		dvma_addr = (unsigned long) mmu_lockarea((char *)&risc_code01[0],
							 (sizeof(u_short) * risc_code_length01));
		param[0] = MBOX_LOAD_RAM;
		param[1] = risc_code_addr01;
		param[2] = (dvma_addr >> 16);
		param[3] = (dvma_addr & 0xffff);
		param[4] = (sizeof(u_short) * risc_code_length01);
		if(qlogicpti_mbox_command(qpti, param, 1) ||
		   (param[0] != MBOX_COMMAND_COMPLETE)) {
			printk(KERN_EMERG "qlogicpti%d: Firmware dload failed, I'm bolixed!\n",
			       qpti->qpti_id);
			restore_flags(flags);
			return 1;
		}
		mmu_unlockarea((char *)dvma_addr, (sizeof(u_short) * risc_code_length01));
	} else
#endif
	/* We need to do it this slow way always on Ultra, SS[12]000. */
		for(i = 0; i < risc_code_length01; i++) {
			param[0] = MBOX_WRITE_RAM_WORD;
			param[1] = risc_code_addr01 + i;
			param[2] = risc_code01[i];
			if(qlogicpti_mbox_command(qpti, param, 1) ||
			   param[0] != MBOX_COMMAND_COMPLETE) {
				printk("qlogicpti%d: Firmware dload failed, I'm bolixed!\n",
				       qpti->qpti_id);
				restore_flags(flags);
				return 1;
			}
		}

	/* Reset the ISP again. */
	qregs->hcctrl = HCCTRL_RESET;
	mdelay(1);

	qlogicpti_enable_irqs(qregs);
	qregs->sbus_semaphore = 0;
	qregs->hcctrl = HCCTRL_REL;

	/* Ask ISP to verify the checksum of the new code. */
	param[0] = MBOX_VERIFY_CHECKSUM;
	param[1] = risc_code_addr01;
	if(qlogicpti_mbox_command(qpti, param, 1) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: New firmware csum failure!\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	/* Start using newly downloaded firmware. */
	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr01;
	qlogicpti_mbox_command(qpti, param, 1);

	param[0] = MBOX_ABOUT_FIRMWARE;
	if(qlogicpti_mbox_command(qpti, param, 1) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: AboutFirmware cmd fails.\n",
		       qpti->qpti_id);
		restore_flags(flags);
		return 1;
	}

	/* Snag the major and minor revisions from the result. */
	qpti->fware_majrev = param[1];
	qpti->fware_minrev = param[2];

	/* Load scsi initiator ID and interrupt level into sbus static ram. */
	param[0] = MBOX_WRITE_RAM_WORD;
	param[1] = 0xff80;
	param[2] = (unsigned short) qpti->scsi_id;
	qlogicpti_mbox_command(qpti, param, 1);

	param[0] = MBOX_WRITE_RAM_WORD;
	param[1] = 0xff00;
	param[2] = (unsigned short) 3;
	qlogicpti_mbox_command(qpti, param, 1);

	restore_flags(flags);
	return 0;
}

static int qlogicpti_verify_tmon(struct qlogicpti *qpti)
{
	int curstat = *qpti->sreg;

	curstat &= 0xf0;
	if(!(curstat & SREG_FUSE) && (qpti->swsreg & SREG_FUSE))
		printk("qlogicpti%d: Fuse returned to normal state.\n", qpti->qpti_id);
	if(!(curstat & SREG_TPOWER) && (qpti->swsreg & SREG_TPOWER))
		printk("qlogicpti%d: termpwr back to normal state.\n", qpti->qpti_id);
	if(curstat != qpti->swsreg) {
		int error = 0;
		if(curstat & SREG_FUSE) {
			error++;
			printk("qlogicpti%d: Fuse is open!\n", qpti->qpti_id);
		}
		if(curstat & SREG_TPOWER) {
			error++;
			printk("qlogicpti%d: termpwr failure\n", qpti->qpti_id);
		}
		if(qpti->differential &&
		   (curstat & SREG_DSENSE) != SREG_DSENSE) {
			error++;
			printk("qlogicpti%d: You have a single ended device on a "
			       "differential bus!  Please fix!\n", qpti->qpti_id);
		}
		qpti->swsreg = curstat;
		return error;
	}
	return 0;
}

static inline void qlogicpti_set_hostdev_defaults(struct qlogicpti *qpti)
{
	int i;

	qpti->host_param.initiator_scsi_id = qpti->scsi_id;
	qpti->host_param.bus_reset_delay = 3;
	qpti->host_param.retry_count = 0;
	qpti->host_param.retry_delay = 5;
	qpti->host_param.async_data_setup_time = 3;
	qpti->host_param.req_ack_active_negation = 1;
	qpti->host_param.data_line_active_negation = 1;
	qpti->host_param.data_dma_burst_enable = 1;
	qpti->host_param.command_dma_burst_enable = 1;
	qpti->host_param.tag_aging = 8;
	qpti->host_param.selection_timeout = 250;
	qpti->host_param.max_queue_depth = 256;

	for(i = 0; i < MAX_TARGETS; i++) {
		qpti->dev_param[i].device_flags = 0xf9;
		qpti->dev_param[i].execution_throttle = 16;
		qpti->dev_param[i].synchronous_period = 16;
		qpti->dev_param[i].synchronous_offset = 12;
		qpti->dev_param[i].device_enable = 1;
	}
}

static void do_qlogicpti_intr_handler(int irq, void *dev_id, struct pt_regs *regs);
#ifndef __sparc_v9__
static void do_qlogicpti_intr_handler_sun4m(int irq, void *dev_id, struct pt_regs *regs);
#endif

/* Detect all PTI Qlogic ISP's in the machine. */
__initfunc(int qlogicpti_detect(Scsi_Host_Template *tpnt))
{
	struct qlogicpti *qpti, *qlink;
	struct Scsi_Host *qpti_host;
	struct linux_sbus *sbus;
	struct linux_sbus_device *qpti_dev, *sbdev_iter;
	struct qlogicpti_regs *qregs;
	volatile unsigned char *sreg;
	unsigned char bsizes, bsizes_more;
	int nqptis = 0, nqptis_in_use = 0;
	int qpti_node;
	int is_pti;

	tpnt->proc_dir = &proc_scsi_qlogicpti;
	qptichain = 0;
	if(!SBus_chain)
		return 0;
	for_each_sbus(sbus) {
		for_each_sbusdev(sbdev_iter, sbus) {
			qpti_dev = sbdev_iter;

			/* Is this a red snapper? */
			if(strcmp(qpti_dev->prom_name, "ptisp") &&
			   strcmp(qpti_dev->prom_name, "PTI,ptisp") &&
			   strcmp(qpti_dev->prom_name, "QLGC,isp"))
				continue;

			/* Sometimes Antares cards come up not completely
			 * setup, and we get a report of a zero IRQ.
			 * Skip over them in such cases so we survive.
			 */
			if(qpti_dev->irqs[0] == 0) {
				printk("qpti%d: Adapter reports no interrupt, "
				       "skipping over this card.", nqptis);
				continue;
			}

			/* Yep, register and allocate software state. */
			qpti_host = scsi_register(tpnt, sizeof(struct qlogicpti));
			if(!qpti_host)
				panic("Cannot register PTI Qlogic ISP SCSI host");
			qpti = (struct qlogicpti *) qpti_host->hostdata;
			if(!qpti)
				panic("No qpti in hostdata");

			/* We are wide capable, 16 targets. */
			qpti_host->max_id = MAX_TARGETS;

			/* Setup back pointers and misc. state. */
			qpti->qhost = qpti_host;
			qpti->qdev = qpti_dev;
			qpti->qpti_id = nqptis++;

			/* Insert this one into the global interrupt service chain. */
			if(qptichain) {
				qlink = qptichain;
				while(qlink->next) qlink = qlink->next;
				qlink->next = qpti;
			} else {
				qptichain = qpti;
			}
			qpti->next = 0;

			/* More misc. prom information. */
			qpti_node = qpti_dev->prom_node;
			prom_getstring(qpti_node, "name", qpti->prom_name,
				       sizeof(qpti->prom_name));
			qpti->prom_node = qpti_node;
			
			is_pti = strcmp (qpti->prom_name, "QLGC,isp");

			/* Setup the reg property for this device. */
			prom_apply_sbus_ranges(qpti->qdev->my_bus,
					       qpti->qdev->reg_addrs,
					       1, qpti->qdev);

			/* Map in Qlogic,ISP regs and the PTI status reg. */
			qpti->qregs = qregs = (struct qlogicpti_regs *)
				sparc_alloc_io(qpti->qdev->reg_addrs[0].phys_addr, 0,
					       qpti->qdev->reg_addrs[0].reg_size,
					       "PTI Qlogic/ISP Registers",
					       qpti->qdev->reg_addrs[0].which_io, 0x0);
			if(!qregs)
				panic("PTI Qlogic/ISP registers unmappable");
				
			if(is_pti) {
				/* Map this one read only. */
				qpti->sreg = sreg = (volatile unsigned char *)
					sparc_alloc_io((qpti->qdev->reg_addrs[0].phys_addr +
						       (16 * 4096)), 0,
						       sizeof(unsigned char),
						       "PTI Qlogic/ISP Status Reg",
						       qpti->qdev->reg_addrs[0].which_io, 1);
				if(!sreg)
					panic("PTI Qlogic/ISP status reg unmappable");
				qpti->swsreg = 0;
			}

			qpti_host->base = (unsigned char *)qregs;
			qpti_host->io_port = (unsigned int) ((unsigned long)qregs);
			qpti_host->n_io_port = (unsigned char)
				qpti->qdev->reg_addrs[0].reg_size;

			qpti_host->irq = qpti->irq = qpti->qdev->irqs[0];

			/* On Ultra and S{S1,C2}000 we must always call request_irq for each
			 * qpti, so that imap registers get setup etc.
			 * But irq values are different in that case anyway...
			 * Otherwise allocate the irq only if necessary.
			 */
			for_each_qlogicpti(qlink) {
				if((qlink != qpti) && (qpti->irq == qlink->irq)) {
					goto qpti_irq_acquired; /* BASIC rulez */
				}
			}
			if(request_irq(qpti->qhost->irq, 
#ifndef __sparc_v9__			
				       (sparc_cpu_model == sun4m || sparc_cpu_model == sun4c) ?
				           do_qlogicpti_intr_handler_sun4m :
#endif
				           do_qlogicpti_intr_handler,
				       SA_SHIRQ, "PTI Qlogic/ISP SCSI", qpti)) {
				printk("Cannot acquire PTI Qlogic/ISP irq line\n");
				/* XXX Unmap regs, unregister scsi host, free things. */
				continue;
			}
qpti_irq_acquired:
			printk("qpti%d: IRQ %s ",
			       qpti->qpti_id, __irq_itoa(qpti->qhost->irq));

			/* Figure out our scsi ID on the bus */
			qpti->scsi_id = prom_getintdefault(qpti->prom_node,
							   "initiator-id",
							   -1);
			if(qpti->scsi_id == -1)
				qpti->scsi_id = prom_getintdefault(qpti->prom_node,
								   "scsi-initiator-id",
								   -1);
			if(qpti->scsi_id == -1)
				qpti->scsi_id =
					prom_getintdefault(qpti->qdev->my_bus->prom_node,
							   "scsi-initiator-id", 7);
			qpti->qhost->this_id = qpti->scsi_id;
			printk("SCSI ID %d ", qpti->scsi_id);

			/* Check for what the best SBUS burst we can use happens
			 * to be on this machine.
			 */
			bsizes = prom_getintdefault(qpti->prom_node,"burst-sizes",0xff);
			bsizes &= 0xff;
			bsizes_more = prom_getintdefault(qpti->qdev->my_bus->prom_node,
							 "burst-sizes", 0xff);
			if(bsizes_more != 0xff)
				bsizes &= bsizes_more;
			if(bsizes == 0xff || (bsizes & DMA_BURST16)==0 ||
			   (bsizes & DMA_BURST32) == 0)
				bsizes = (DMA_BURST32 - 1);
			qpti->bursts = bsizes;

			/* The request and response queues must each be aligned
			 * on a page boundry.
			 */

#define QSIZE(entries)	(((entries) + 1) * QUEUE_ENTRY_LEN)

			qpti->res_cpu = sparc_dvma_malloc(QSIZE(RES_QUEUE_LEN),
							  "PTISP Response Queue",
							  &qpti->res_dvma);
			qpti->req_cpu = sparc_dvma_malloc(QSIZE(QLOGICISP_REQ_QUEUE_LEN),
							  "PTISP Request Queue",
							  &qpti->req_dvma);

#undef QSIZE


			/* Set adapter and per-device default values. */
			qlogicpti_set_hostdev_defaults(qpti);
			
			if (is_pti) {
				/* Load the firmware. */
				if(qlogicpti_load_firmware(qpti))
					panic("PTI Qlogic/ISP firmware load failed");

				/* Check the PTI status reg. */
				if(qlogicpti_verify_tmon(qpti))
					panic("PTI Qlogic/ISP tmon verification failed");
			}

			/* Reset the ISP and init res/req queues. */
			if(qlogicpti_reset_hardware(qpti_host))
				panic("PTI Qlogic/ISP cannot be reset");

			if (is_pti) {
				printk("(Firmware v%d.%d)",
				       qpti->fware_majrev, qpti->fware_minrev);
			} else {
				char buffer[60];
				
				prom_getstring (qpti_node, "isp-fcode", buffer, 60);
				printk("(Firmware %s)", buffer);
			}
			
			printk (" [%s Wide, using %s interface]\n",
			       (qpti->ultra ? "Ultra" : "Fast"),
			       (qpti->differential ? "differential" : "single ended"));

			nqptis_in_use++;
		}
	}
	if (nqptis)
		printk("QPTI: Total of %d PTI Qlogic/ISP hosts found, %d actually in use.\n",
		       nqptis, nqptis_in_use);
	qptis_running = nqptis_in_use;
	return nqptis;
}

int qlogicpti_release(struct Scsi_Host *host)
{
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	struct qlogicpti_regs *qregs = qpti->qregs;

	/* Shut up the card. */
	qregs->sbus_ctrl = 0;

	/* Free IRQ handler and unmap Qlogic,ISP and PTI status regs. */
	free_irq(host->irq, NULL);
	unmapioaddr((unsigned long)qregs);
	/* QLGC,isp doesn't have status reg */
	if (strcmp (qpti->prom_name, "QLGC,isp"))
		unmapioaddr((unsigned long)qpti->sreg);

	return 0;
}

const char *qlogicpti_info(struct Scsi_Host *host)
{
	static char buf[80];
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;

	sprintf(buf, "PTI Qlogic,ISP SBUS SCSI irq %s regs at %08lx",
		__irq_itoa(qpti->qhost->irq), (unsigned long) qpti->qregs);
	return buf;
}

/* I am a certified frobtronicist. */
static inline void marker_frob(struct Command_Entry *cmd)
{
	struct Marker_Entry *marker = (struct Marker_Entry *) cmd;

	memset(marker, 0, sizeof(struct Marker_Entry));
	marker->hdr.entry_cnt = 1;
	marker->hdr.entry_type = ENTRY_MARKER;
	marker->modifier = SYNC_ALL;
	marker->rsvd = 0;
}

static inline void cmd_frob(struct Command_Entry *cmd, Scsi_Cmnd *Cmnd,
			    struct qlogicpti *qpti)
{
	memset(cmd, 0, sizeof(struct Command_Entry));
	cmd->hdr.entry_cnt = 1;
	cmd->hdr.entry_type = ENTRY_COMMAND;
#ifdef __sparc_v9__
	cmd->handle = (u_int) (((unsigned long)Cmnd) - PAGE_OFFSET); /* magic mushroom */
#else
	cmd->handle = (u_int) ((unsigned long)Cmnd);                 /* magic mushroom */
#endif
	cmd->target_id = Cmnd->target;
	cmd->target_lun = Cmnd->lun;
	cmd->cdb_length = Cmnd->cmd_len;
	cmd->control_flags = 0;
	if(Cmnd->device->tagged_supported) {
		if(qpti->cmd_count[Cmnd->target] == 0)
			qpti->tag_ages[Cmnd->target] = jiffies;
		if((jiffies - qpti->tag_ages[Cmnd->target]) > (5*HZ)) {
			cmd->control_flags = CFLAG_ORDERED_TAG;
			qpti->tag_ages[Cmnd->target] = jiffies;
		} else
			cmd->control_flags = CFLAG_SIMPLE_TAG;
	}
	if((Cmnd->cmnd[0] == WRITE_6) ||
	   (Cmnd->cmnd[0] == WRITE_10) ||
	   (Cmnd->cmnd[0] == WRITE_12))
		cmd->control_flags |= CFLAG_WRITE;
	else
		cmd->control_flags |= CFLAG_READ;
	cmd->time_out = 30;
	memcpy(cmd->cdb, Cmnd->cmnd, Cmnd->cmd_len);
}

/* Do it to it baby. */
static inline u_int load_cmd(Scsi_Cmnd *Cmnd, struct Command_Entry *cmd,
			     struct qlogicpti *qpti, struct qlogicpti_regs *qregs,
			     u_int in_ptr, u_int out_ptr)
{
	struct dataseg * ds;
	struct scatterlist *sg;
	int sg_count = Cmnd->use_sg;
	int i, n;

	if(sg_count) {
		mmu_get_scsi_sgl((struct mmu_sglist *)Cmnd->buffer, (Cmnd->use_sg - 1),
				 qpti->qdev->my_bus);

		cmd->segment_cnt = sg_count;
		sg = (struct scatterlist *) Cmnd->request_buffer;
		ds = cmd->dataseg;

		/* Fill in first four sg entries: */
		n = sg_count;
		if(n > 4)
			n = 4;
		for(i = 0; i < n; i++, sg++) {
			ds[i].d_base = (u_int) sg->dvma_address;
			ds[i].d_count = (u_int) sg->length;
		}
		sg_count -= 4;
		while(sg_count > 0) {
			struct Continuation_Entry *cont;

			++cmd->hdr.entry_cnt;
			cont = (struct Continuation_Entry *) &qpti->req_cpu[in_ptr];
			in_ptr = NEXT_REQ_PTR(in_ptr);
			if(in_ptr == out_ptr) {
				printk(KERN_EMERG "qlogicpti: Unexpected request queue overflow\n");
				return -1;
			}
			cont->hdr.entry_type = ENTRY_CONTINUATION;
			cont->hdr.entry_cnt = 0;
			cont->hdr.sys_def_1 = 0;
			cont->hdr.flags = 0;
			cont->reserved = 0;
			ds = cont->dataseg;
			n = sg_count;
			if(n > 7)
				n = 7;
			for(i = 0; i < n; i++, sg++) {
				ds[i].d_base = (u_int) sg->dvma_address;
				ds[i].d_count = (u_int) sg->length;
			}
			sg_count -= n;
		}
	} else {
		/* XXX Casts are extremely gross, but with 64-bit cpu addresses
		 * XXX and 32-bit SBUS DVMA addresses what am I to do? -DaveM
		 */
		Cmnd->SCp.ptr = (char *)((unsigned long)
					 mmu_get_scsi_one((char *)Cmnd->request_buffer,
							  Cmnd->request_bufflen,
							  qpti->qdev->my_bus));

		cmd->dataseg[0].d_base = (u_int) ((unsigned long)Cmnd->SCp.ptr);
		cmd->dataseg[0].d_count = Cmnd->request_bufflen;
		cmd->segment_cnt = 1;
	}
	qpti->cmd_count[Cmnd->target]++;
	qregs->mbox4 = in_ptr;
	qpti->req_in_ptr = in_ptr;
	return in_ptr;
}

static inline void update_can_queue(struct Scsi_Host *host, u_int in_ptr, u_int out_ptr)
{
	/* Temporary workaround until bug is found and fixed (one bug has been found
	   already, but fixing it makes things even worse) -jj */
	int num_free = QLOGICISP_REQ_QUEUE_LEN - REQ_QUEUE_DEPTH(in_ptr, out_ptr) - 64;
	host->can_queue = host->host_busy + num_free;
	host->sg_tablesize = QLOGICISP_MAX_SG(num_free);
}

/*
 * The middle SCSI layer ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (though the
 * interrupt handler may call this routine as part of
 * request-completion handling).
 *
 * "This code must fly." -davem
 */
int qlogicpti_queuecommand(Scsi_Cmnd *Cmnd, void (*done)(Scsi_Cmnd *))
{
	struct Scsi_Host *host = Cmnd->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	struct qlogicpti_regs *qregs = qpti->qregs;
	u_int in_ptr = qpti->req_in_ptr;
	u_int out_ptr = qregs->mbox4;
	struct Command_Entry *cmd = (struct Command_Entry *) &qpti->req_cpu[in_ptr];

	Cmnd->scsi_done = done;
	in_ptr = NEXT_REQ_PTR(in_ptr);
	if(in_ptr == out_ptr) {
		printk(KERN_EMERG "qlogicpti%d: request queue overflow\n",qpti->qpti_id);

		/* Unfortunately, unless you use the new EH code, which
		 * we don't, the midlayer will ignore the return value,
		 * which is insane.  We pick up the pieces like this.
		 */
		Cmnd->result = DID_BUS_BUSY;
		done(Cmnd);
		return 1;
	}
	if(qpti->send_marker) {
		marker_frob(cmd);
		qpti->send_marker = 0;
		if(NEXT_REQ_PTR(in_ptr) == out_ptr) {
			qregs->mbox4 = in_ptr;
			qpti->req_in_ptr = in_ptr;
			printk(KERN_EMERG "qlogicpti%d: request queue overflow\n",
			       qpti->qpti_id);

			/* Unfortunately, unless you use the new EH code, which
			 * we don't, the midlayer will ignore the return value,
			 * which is insane.  We pick up the pieces like this.
			 */
			Cmnd->result = DID_BUS_BUSY;
			done(Cmnd);
			return 1;
		}
		cmd = (struct Command_Entry *) &qpti->req_cpu[in_ptr];
		in_ptr = NEXT_REQ_PTR(in_ptr);
	}
	cmd_frob(cmd, Cmnd, qpti);
	if((in_ptr = load_cmd(Cmnd, cmd, qpti, qregs, in_ptr, out_ptr)) == -1) {
		/* Unfortunately, unless you use the new EH code, which
		 * we don't, the midlayer will ignore the return value,
		 * which is insane.  We pick up the pieces like this.
		 */
		Cmnd->result = DID_BUS_BUSY;
		done(Cmnd);
		return 1;
	}
	update_can_queue(host, in_ptr, out_ptr);
	return 0;
}

static int qlogicpti_return_status(struct Status_Entry *sts)
{
	int host_status = DID_ERROR;

	switch(sts->completion_status) {
	      case CS_COMPLETE:
		host_status = DID_OK;
		break;
	      case CS_INCOMPLETE:
		if (!(sts->state_flags & SF_GOT_BUS))
			host_status = DID_NO_CONNECT;
		else if (!(sts->state_flags & SF_GOT_TARGET))
			host_status = DID_BAD_TARGET;
		else if (!(sts->state_flags & SF_SENT_CDB))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_TRANSFERRED_DATA))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_STATUS))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_SENSE))
			host_status = DID_ERROR;
		break;
	      case CS_DMA_ERROR:
	      case CS_TRANSPORT_ERROR:
		host_status = DID_ERROR;
		break;
	      case CS_RESET_OCCURRED:
	      case CS_BUS_RESET:
		host_status = DID_RESET;
		break;
	      case CS_ABORTED:
		host_status = DID_ABORT;
		break;
	      case CS_TIMEOUT:
		host_status = DID_TIME_OUT;
		break;
	      case CS_DATA_OVERRUN:
	      case CS_COMMAND_OVERRUN:
	      case CS_STATUS_OVERRUN:
	      case CS_BAD_MESSAGE:
	      case CS_NO_MESSAGE_OUT:
	      case CS_EXT_ID_FAILED:
	      case CS_IDE_MSG_FAILED:
	      case CS_ABORT_MSG_FAILED:
	      case CS_NOP_MSG_FAILED:
	      case CS_PARITY_ERROR_MSG_FAILED:
	      case CS_DEVICE_RESET_MSG_FAILED:
	      case CS_ID_MSG_FAILED:
	      case CS_UNEXP_BUS_FREE:
		host_status = DID_ERROR;
		break;
	      case CS_DATA_UNDERRUN:
		host_status = DID_OK;
		break;
	      default:
		printk(KERN_EMERG "qlogicpti : unknown completion status 0x%04x\n",
		       sts->completion_status);
		host_status = DID_ERROR;
		break;
	}

	return (sts->scsi_status & STATUS_MASK) | (host_status << 16);
}

static __inline__ int qlogicpti_intr_handler(struct qlogicpti *qpti)
{
	Scsi_Cmnd *Cmnd;
	struct Status_Entry *sts;
	u_int in_ptr, out_ptr;
	struct qlogicpti_regs *qregs;

	if(!(qpti->qregs->sbus_stat & SBUS_STAT_RINT))
		return 0;
		
	qregs = qpti->qregs;

	in_ptr = qregs->mbox5;
	qregs->hcctrl = HCCTRL_CRIRQ;
	if(qregs->sbus_semaphore & SBUS_SEMAPHORE_LCK) {
		switch(qregs->mbox0) {
		case ASYNC_SCSI_BUS_RESET:
		case EXECUTION_TIMEOUT_RESET:
			qpti->send_marker = 1;
			break;
		case INVALID_COMMAND:
		case HOST_INTERFACE_ERROR:
		case COMMAND_ERROR:
		case COMMAND_PARAM_ERROR:
			break;
		}
		qregs->sbus_semaphore = 0;
	}

	/* This looks like a network driver! */
	out_ptr = qpti->res_out_ptr;
	while(out_ptr != in_ptr) {
		sts = (struct Status_Entry *) &qpti->res_cpu[out_ptr];
		out_ptr = NEXT_RES_PTR(out_ptr);
		Cmnd = (Scsi_Cmnd *) (((unsigned long)sts->handle)+PAGE_OFFSET);

		if(sts->completion_status == CS_RESET_OCCURRED ||
		   sts->completion_status == CS_ABORTED ||
		   (sts->status_flags & STF_BUS_RESET))
			qpti->send_marker = 1;

		if(sts->state_flags & SF_GOT_SENSE)
			memcpy(Cmnd->sense_buffer, sts->req_sense_data,
			       sizeof(Cmnd->sense_buffer));

		if(sts->hdr.entry_type == ENTRY_STATUS)
			Cmnd->result = qlogicpti_return_status(sts);
		else
			Cmnd->result = DID_ERROR << 16;

		if(Cmnd->use_sg)
			mmu_release_scsi_sgl((struct mmu_sglist *)
					     Cmnd->buffer,
					     Cmnd->use_sg - 1,
					     qpti->qdev->my_bus);
		else
			mmu_release_scsi_one((__u32)((unsigned long)Cmnd->SCp.ptr),
					     Cmnd->request_bufflen,
					     qpti->qdev->my_bus);

		qpti->cmd_count[Cmnd->target]--;
		qregs->mbox5 = out_ptr;
		Cmnd->scsi_done(Cmnd);
	}
	qpti->res_out_ptr = out_ptr;
	return 1;
}

#ifndef __sparc_v9__

static void do_qlogicpti_intr_handler_sun4m(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	struct qlogicpti *qpti;
	int again;

	spin_lock_irqsave(&io_request_lock, flags);
	again = 0;
	do {
		for_each_qlogicpti(qpti)
			again |= qlogicpti_intr_handler(qpti);
	} while (again);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

#endif

static void do_qlogicpti_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	qlogicpti_intr_handler((struct qlogicpti *)dev_id);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

int qlogicpti_abort(Scsi_Cmnd *Cmnd)
{
	u_short param[6];
	struct Scsi_Host *host = Cmnd->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	int return_status = SCSI_ABORT_SUCCESS;

	printk(KERN_WARNING "qlogicpti : Aborting cmd for tgt[%d] lun[%d]\n",
	       (int)Cmnd->target, (int)Cmnd->lun);
	qlogicpti_disable_irqs(qpti->qregs);
	param[0] = MBOX_ABORT;
	param[1] = (((u_short) Cmnd->target) << 8) | Cmnd->lun;
	param[2] = ((unsigned int)((unsigned long)Cmnd)) >> 16;
	param[3] = ((unsigned int)((unsigned long)Cmnd)) & 0xffff;
	if(qlogicpti_mbox_command(qpti, param, 0) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti : scsi abort failure: %x\n", param[0]);
		return_status = SCSI_ABORT_ERROR;
	}
	qlogicpti_enable_irqs(qpti->qregs);
	return return_status;
}

int qlogicpti_reset(Scsi_Cmnd *Cmnd, unsigned int reset_flags)
{
	u_short param[6];
	struct Scsi_Host *host = Cmnd->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	int return_status = SCSI_RESET_SUCCESS;

	printk(KERN_WARNING "qlogicpti : Resetting SCSI bus!\n");
	qlogicpti_disable_irqs(qpti->qregs);
	param[0] = MBOX_BUS_RESET;
	param[1] = qpti->host_param.bus_reset_delay;
	if(qlogicpti_mbox_command(qpti, param, 0) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicisp : scsi bus reset failure: %x\n", param[0]);
		return_status = SCSI_RESET_ERROR;
	}
	qlogicpti_enable_irqs(qpti->qregs);
	return return_status;
}

#ifdef MODULE
Scsi_Host_Template driver_template = QLOGICPTI;

#include "scsi_module.c"

EXPORT_NO_SYMBOLS;
#endif /* MODULE */
