/*
 * QLogic ISP1020 Intelligent SCSI Processor Driver (PCI)
 * Written by Erik H. Moe, ehm@cris.com
 * Copyright 1995, Erik H. Moe
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/* Renamed and updated to 1.3.x by Michael Griffith <grif@cs.ucr.edu> */

/*
 * $Date: 1995/09/22 02:23:15 $
 * $Revision: 0.5 $
 *
 * $Log: isp1020.c,v $
 * Revision 0.5  1995/09/22  02:23:15  root
 * do auto request sense
 *
 * Revision 0.4  1995/08/07  04:44:33  root
 * supply firmware with driver.
 * numerous bug fixes/general cleanup of code.
 *
 * Revision 0.3  1995/07/16  16:15:39  root
 * added reset/abort code.
 *
 * Revision 0.2  1995/06/29  03:14:19  root
 * fixed biosparam.
 * added queue protocol.
 *
 * Revision 0.1  1995/06/25  01:55:45  root
 * Initial release.
 *
 */

#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <unistd.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "sd.h"
#include "hosts.h"
#include "qlogicisp.h"

/* Configuration section *****************************************************/

/* Set the following macro to 1 to reload the ISP1020's firmware.  This is
   the latest firmware provided by QLogic.  This may be an earlier/later
   revision than supplied by your board. */

#define RELOAD_FIRMWARE		1

/* Set the following macro to 1 to reload the ISP1020's defaults from nvram.
   If you are not sure of your settings, leave this alone, the driver will
   use a set of 'safe' defaults */

#define USE_NVRAM_DEFAULTS	0

/*  Set this macro to 1 if you want to create a scsi loadable module. */

#define MODULE			0

/*  Macros used for debuging */

#define DEBUG_ISP1020		0
#define DEBUG_ISP1020_INT	0
#define DEBUG_ISP1020_SETUP	0

/* End Configuration section *************************************************/

#include <linux/module.h>

#if DEBUG_ISP1020
#define ENTER(x)	printk("isp1020 : entering %s()\n", x);
#define LEAVE(x)	printk("isp1020 : leaving %s()\n", x);
#define DEBUG(x)	x
#else
#define ENTER(x)
#define LEAVE(x)
#define DEBUG(x)
#endif /* DEBUG_ISP1020 */

#if DEBUG_ISP1020_INTR
#define ENTER_INTR(x)	printk("isp1020 : entering %s()\n", x);
#define LEAVE_INTR(x)	printk("isp1020 : leaving %s()\n", x);
#define DEBUG_INTR(x)	x
#else
#define ENTER_INTR(x)
#define LEAVE_INTR(x)
#define DEBUG_INTR(x)
#endif /* DEBUG ISP1020_INTR */

#define ISP1020_REV_ID	1

/* host configuration and control registers */
#define HOST_HCCR	0xc0	/* host command and control */

/* pci bus interface registers */
#define PCI_ID_LOW	0x00	/* vendor id */
#define PCI_ID_HIGH	0x02	/* device id */
#define ISP_CFG0	0x04	/* configuration register #0 */
#define ISP_CFG1	0x08	/* configuration register #1 */
#define PCI_INTF_CTL	0x08	/* pci interface control */
#define PCI_INTF_STS	0x0a	/* pci interface status */
#define PCI_SEMAPHORE	0x0c	/* pci semaphore */
#define PCI_NVRAM	0x0e	/* pci nvram interface */

/* mailbox registers */
#define MBOX0		0x70	/* mailbox 0 */
#define MBOX1		0x72	/* mailbox 1 */
#define MBOX2		0x74	/* mailbox 2 */
#define MBOX3		0x76	/* mailbox 3 */
#define MBOX4		0x78	/* mailbox 4 */
#define MBOX5		0x7a	/* mailbox 5 */

/* mailbox command complete status codes */
#define MBOX_COMMAND_COMPLETE		0x4000
#define INVALID_COMMAND			0x4001
#define HOST_INTERFACE_ERROR		0x4002
#define TEST_FAILED			0x4003
#define COMMAND_ERROR			0x4005
#define COMMAND_PARAM_ERROR		0x4006

/* async event status codes */
#define ASYNC_SCSI_BUS_RESET		0x8001
#define SYSTEM_ERROR			0x8002
#define REQUEST_TRANSFER ERROR		0x8003
#define RESPONSE_TRANSFER_ERROR		0x8004
#define REQUEST_QUEUE_WAKEUP		0x8005
#define EXECUTION_TIMEOUT_RESET		0x8006

struct Entry_header {
    u_char    entry_type;
    u_char    entry_cnt;
    u_char    sys_def_1;
    u_char    flags;
};

/* entry header type commands */
#define ENTRY_COMMAND		1
#define ENTRY_CONTINUATION	2
#define ENTRY_STATUS		3
#define ENTRY_MARKER		4
#define ENTRY_EXTENDED_COMMAND	5

/* entry header flag definitions */
#define EFLAG_CONTINUATION	1
#define EFLAG_BUSY		2
#define EFLAG_BAD_HEADER	4
#define EFLAG_BAD_PAYLOAD	8

struct dataseg {
    caddr_t   d_base;
    u_long    d_count;
};

struct Command_Entry {
    struct    Entry_header    hdr;
    caddr_t   handle;
    u_char    target_lun;
    u_char    target_id;
    u_short   cdb_length;
    u_short   control_flags;
    u_short   rsvd;
    u_short   time_out;
    u_short   segment_cnt;
    u_char    cdb[12];
    struct    dataseg    dataseg0;
    struct    dataseg    dataseg1;
    struct    dataseg    dataseg2;
    struct    dataseg    dataseg3;
};

/* command entry control flag definitions */
#define CFLAG_NODISC		0x01
#define CFLAG_HEAD_TAG		0x02
#define CFLAG_ORDERED_TAG	0x04
#define CFLAG_SIMPLE_TAG	0x08
#define CFLAG_TAR_RTN		0x10
#define CFLAG_READ		0x20
#define CFLAG_WRITE		0x40

struct Ext_Command_Entry {
    struct    Entry_header    hdr;
    caddr_t   handle;
    u_char    target_lun;
    u_char    target_id;
    u_short   cdb_length;
    u_short   control_flags;
    u_short   rsvd;
    u_short   time_out;
    u_short   segment_cnt;
    u_char    cdb[44];
};

struct Continuation_Entry {
    struct    Entry_header    hdr;
    u_long    reserved;
    struct    dataseg    dataseg0;
    struct    dataseg    dataseg1;
    struct    dataseg    dataseg2;
    struct    dataseg    dataseg3;
    struct    dataseg    dataseg4;
    struct    dataseg    dataseg5;
    struct    dataseg    dataseg6;
};

struct Marker_Entry {
    struct    Entry_header    hdr;
    caddr_t   reserved;
    u_char    target_lun;
    u_char    target_id;
    u_char    modifier;
    u_char    rsvd;
    u_char    rsvds[52];
};

/* marker entry modifier definitions */
#define SYNC_DEVICE	0
#define SYNC_TARGET	1
#define SYNC_ALL	2

struct Status_Entry {
    struct    Entry_header    hdr;
    caddr_t   handle;
    u_short   scsi_status;
    u_short   completion_status;
    u_short   state_flags;
    u_short   status_flags;
    u_short   time;
    u_short   req_sense_len;
    u_long    residual;
    u_char    rsvd[8];
    u_char    req_sense_data[32];
};

/* status entry completion status definitions */
#define CS_COMPLETE			0x0000
#define CS_INCOMPLETE			0x0001
#define CS_DMA_ERROR			0x0002
#define CS_TRANSPORT_ERROR		0x0003
#define CS_RESET_OCCURRED		0x0004
#define CS_ABORTED			0x0005
#define CS_TIMEOUT			0x0006
#define CS_DATA_OVERRUN			0x0007
#define CS_COMMAND_OVERRUN		0x0008
#define CS_STATUS_OVERRUN		0x0009
#define CS_BAD_MESSAGE			0x000a
#define CS_NO_MESSAGE_OUT		0x000b
#define CS_EXT_ID_FAILED		0x000c
#define CS_IDE_MSG_FAILED		0x000d
#define CS_ABORT_MSG_FAILED		0x000e
#define CS_REJECT_MSG_FAILED		0x000f
#define CS_NOP_MSG_FAILED		0x0010
#define CS_PARITY_ERROR_MSG_FAILED	0x0011
#define CS_DEVICE_RESET_MSG_FAILED	0x0012
#define CS_ID_MSG_FAILED		0x0013
#define CS_UNEXP_BUS_FREE		0x0014
#define CS_DATA_UNDERRUN		0x0015

/* status entry state flag definitions */
#define SF_GOT_BUS			0x0100
#define SF_GOT_TARGET			0x0200
#define SF_SENT_CDB			0x0400
#define SF_TRANSFERRED_DATA		0x0800
#define SF_GOT_STATUS			0x1000
#define SF_GOT_SENSE			0x2000

/* status entry status flag definitions */
#define STF_DISCONNECT			0x0001
#define STF_SYNCHRONOUS			0x0002
#define STF_PARITY_ERROR		0x0004
#define STF_BUS_RESET			0x0008
#define STF_DEVICE_RESET		0x0010
#define STF_ABORTED			0x0020
#define STF_TIMEOUT			0x0040
#define STF_NEGOTIATION			0x0080

/* host control commands */
#define HCCR_NOP			0x0000
#define HCCR_RESET			0x1000
#define HCCR_PAUSE			0x2000
#define HCCR_RELEASE			0x3000
#define HCCR_SINGLE_STEP		0x4000
#define HCCR_SET_HOST_INTR		0x5000
#define HCCR_CLEAR_HOST_INTR		0x6000
#define HCCR_CLEAR_RISC_INTR		0x7000
#define HCCR_BP_ENABLE			0x8000
#define HCCR_BIOS_ENABLE		0x9000
#define HCCR_TEST_MODE			0xf000

/* mailbox commands */
#define MBOX_NO_OP			0x0000
#define MBOX_LOAD_RAM			0x0001
#define MBOX_EXEC_FIRMWARE		0x0002
#define MBOX_DUMP_RAM			0x0003
#define MBOX_WRITE_RAM_WORD		0x0004
#define MBOX_READ_RAM_WORD		0x0005
#define MBOX_MAILBOX_REG_TEST		0x0006
#define MBOX_VERIFY_CHECKSUM		0x0007
#define MBOX_ABOUT_FIRMWARE		0x0008
#define MBOX_CHECK_FIRMWARE		0x000e
#define MBOX_INIT_REQ_QUEUE		0x0010
#define MBOX_INIT_RES_QUEUE		0x0011
#define MBOX_EXECUTE_IOCB		0x0012
#define MBOX_WAKE_UP			0x0013
#define MBOX_STOP_FIRMWARE		0x0014
#define MBOX_ABORT			0x0015
#define MBOX_ABORT_DEVICE		0x0016
#define MBOX_ABORT_TARGET		0x0017
#define MBOX_BUS_RESET			0x0018
#define MBOX_STOP_QUEUE			0x0019
#define MBOX_START_QUEUE		0x001a
#define MBOX_SINGLE_STEP_QUEUE		0x001b
#define MBOX_ABORT_QUEUE		0x001c
#define MBOX_GET_DEV_QUEUE_STATUS	0x001d
#define MBOX_GET_FIRMWARE_STATUS	0x001f
#define MBOX_GET_INIT_SCSI_ID		0x0020
#define MBOX_GET_SELECT_TIMEOUT		0x0021
#define MBOX_GET_RETRY_COUNT		0x0022
#define MBOX_GET_TAG_AGE_LIMIT		0x0023
#define MBOX_GET_CLOCK_RATE		0x0024
#define MBOX_GET_ACT_NEG_STATE		0x0025
#define MBOX_GET_ASYNC_DATA_SETUP_TIME	0x0026
#define MBOX_GET_PCI_PARAMS		0x0027
#define MBOX_GET_TARGET_PARAMS		0x0028
#define MBOX_GET_DEV_QUEUE_PARAMS	0x0029
#define MBOX_SET_INIT_SCSI_ID		0x0030
#define MBOX_SET_SELECT_TIMEOUT		0x0031
#define MBOX_SET_RETRY_COUNT		0x0032
#define MBOX_SET_TAG_AGE_LIMIT		0x0033
#define MBOX_SET_CLOCK_RATE		0x0034
#define MBOX_SET_ACTIVE_NEG_STATE	0x0035
#define MBOX_SET_ASYNC_DATA_SETUP_TIME	0x0036
#define MBOX_SET_PCI_CONTROL_PARAMS	0x0037
#define MBOX_SET_TARGET_PARAMS		0x0038
#define MBOX_SET_DEV_QUEUE_PARAMS	0x0039
#define MBOX_RETURN_BIOS_BLOCK_ADDR	0x0040
#define MBOX_WRITE_FOUR_RAM_WORDS	0x0041
#define MBOX_EXEC_BIOS_IOCB		0x0042

#include "qlogicisp_asm.c"

#define PACKB(a, b)			(((a)<<4)|(b))

u_char mbox_param[] = {
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
    PACKB(1, 3),	/* MBOX_GET_PCI_PARAMS */
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
    PACKB(3, 3),	/* MBOX_SET_PCI_CONTROL_PARAMS */
    PACKB(4, 4),	/* MBOX_SET_TARGET_PARAMS */
    PACKB(4, 4),	/* MBOX_SET_DEV_QUEUE_PARAMS */
    PACKB(0, 0),	/* 0x003a */
    PACKB(0, 0),	/* 0x003b */
    PACKB(0, 0),	/* 0x003c */
    PACKB(0, 0),	/* 0x003d */
    PACKB(0, 0),	/* 0x003e */
    PACKB(0, 0),	/* 0x003f */
    PACKB(1, 2),	/* MBOX_RETURN_BIOS_BLOCK_ADDR */
    PACKB(6, 1),	/* MBOX_WRITE_FOUR_RAM_WORDS */
    PACKB(2, 3)		/* MBOX_EXEC_BIOS_IOCB */
};

#define MAX_MBOX_COMMAND	(sizeof(mbox_param)/sizeof(u_short))

struct host_param {
    u_short    fifo_threshold;
    u_short    host_adapter_enable;
    u_short    initiator_scsi_id;
    u_short    bus_reset_delay;
    u_short    retry_count;
    u_short    retry_delay;
    u_short    async_data_setup_time;
    u_short    req_ack_active_negation;
    u_short    data_line_active_negation;
    u_short    data_dma_burst_enable;
    u_short    command_dma_burst_enable;
    u_short    tag_aging;
    u_short    selection_timeout;
    u_short    max_queue_depth;
};

/*
 * Device Flags:
 *
 * Bit  Name
 * ---------
 *  7   Disconnect Privilege
 *  6   Parity Checking
 *  5   Wide Data Transfers
 *  4   Syncronous Data Transfers
 *  3   Tagged Queuing
 *  2   Automatic Request Sense
 *  1   Stop Queue on Check Condition
 *  0   Renegotiate on Error
 */

struct dev_param {
    u_short    device_flags;
    u_short    execution_throttle;
    u_short    synchronous_period;
    u_short    synchronous_offset;
    u_short    device_enable;
};

#define REQ_QUEUE_LEN		32
#define RES_QUEUE_LEN		32
#define QUEUE_ENTRY_LEN		64

struct isp1020_hostdata {
    u_char    irq;
    u_char    bus;
    u_long    io_base;
    u_char    revision;
    u_char    device_fn;
    u_short   res_queue_in_ptr;
    u_short   res_queue_out_ptr;
    u_short   req_queue_in_ptr;
    u_short   req_queue_out_ptr;
    struct    host_param host_param;
    struct    dev_param dev_param[16];
    char      res_queue[RES_QUEUE_LEN][QUEUE_ENTRY_LEN];
    char      req_queue[REQ_QUEUE_LEN][QUEUE_ENTRY_LEN];
};

struct isp1020_hostdata *irq2host[16];

void isp1020_enable_irqs(struct isp1020_hostdata *);
void isp1020_disable_irqs(struct isp1020_hostdata *);
int isp1020_init(struct Scsi_Host *);
int isp1020_reset_hardware(struct isp1020_hostdata *);
int isp1020_get_defaults(struct isp1020_hostdata *);
int isp1020_set_defaults(struct isp1020_hostdata *);
int isp1020_load_parameters(struct isp1020_hostdata *);
int isp1020_mbox_command(struct isp1020_hostdata *, u_short []); 
u_short isp1020_read_nvram_word(struct isp1020_hostdata *, u_short);
int isp1020_verify_nvram(struct isp1020_hostdata *);
void isp1020_print_status_entry(struct Status_Entry *);
void isp1020_print_scsi_cmd(Scsi_Cmnd *);
void isp1020_scsi_done(Scsi_Cmnd *);
int isp1020_return_status(struct Status_Entry *);
void isp1020_intr_handler(int, struct pt_regs *);


int isp1020_detect(Scsi_Host_Template *tmpt)
{
    int hosts = 0;
    u_short index;
    u_char bus, device_fn;
    struct Scsi_Host *scsihost;
    struct isp1020_hostdata *hostdata;

    ENTER("isp1020_detect");

    if (pcibios_present() == 0) {
        printk("qlogicisp : PCI bios not present\n");
        return 0;
    }

    memset(irq2host, 0, sizeof(irq2host));

    for (index = 0; pcibios_find_device(PCI_VENDOR_ID_QLOGIC,
            PCI_DEVICE_ID_QLOGIC_ISP1020, index, &bus, &device_fn) == 0;
                index++) {

        scsihost = scsi_register(tmpt, sizeof(struct isp1020_hostdata));
        hostdata = (struct isp1020_hostdata *) scsihost->hostdata;

        memset(hostdata, 0, sizeof(struct isp1020_hostdata));
        hostdata->bus = bus;
        hostdata->device_fn = device_fn;

        if (isp1020_init(scsihost) || isp1020_reset_hardware(hostdata)
#if USE_NVRAM_DEFAULTS
                || isp1020_get_defaults(hostdata)
#else
                || isp1020_set_defaults(hostdata)
#endif /* USE_NVRAM_DEFAULTS */
                || isp1020_load_parameters(hostdata)) {
            scsi_unregister(scsihost);
            continue;
        }

        scsihost->this_id = hostdata->host_param.initiator_scsi_id;

        if (request_irq(hostdata->irq, isp1020_intr_handler, 0,
                "qlogicisp", NULL)) {
            printk("qlogicisp : interrupt %d already in use\n", hostdata->irq);
            scsi_unregister(scsihost);
            continue;
        }

        if (check_region(hostdata->io_base, 0xff)) {
            printk("qlogicisp : i/o region 0x%04lx-0x%04lx already in use\n",
                hostdata->io_base, hostdata->io_base + 0xff);
            free_irq(hostdata->irq, NULL);
            scsi_unregister(scsihost);
            continue;
        }

        request_region(hostdata->io_base, 0xff, "qlogicisp");
        irq2host[hostdata->irq] = hostdata;

        isp1020_enable_irqs(hostdata);

        hosts++;
    }

    LEAVE("isp1020_detect");

    return hosts;
}


int isp1020_release(struct Scsi_Host *host)
{
    struct isp1020_hostdata *hostdata;

    ENTER("isp1020_release");

    hostdata = (struct isp1020_hostdata *) host->hostdata;

    outw(0x0, hostdata->io_base + PCI_INTF_CTL);
    free_irq(hostdata->irq, NULL);

    release_region(hostdata->io_base, 0xff);

    LEAVE("isp1020_release");

    return 0;
}


const char *isp1020_info(struct Scsi_Host *host)
{
    static char buf[80];
    struct isp1020_hostdata *hostdata;

    ENTER("isp1020_info");

    hostdata = (struct isp1020_hostdata *) host->hostdata;
    sprintf(buf, "QLogic ISP1020 SCSI on PCI bus %d, device %d, irq %d",
        hostdata->bus, (hostdata->device_fn & 0xf8) >> 3, hostdata->irq);

    LEAVE("isp1020_info");

    return buf;
}


#define REQ_QUEUE_DEPTH() \
    (hostdata->req_queue_in_ptr >= hostdata->req_queue_out_ptr \
        ? hostdata->req_queue_in_ptr - hostdata->req_queue_out_ptr \
        : (REQ_QUEUE_LEN - hostdata->req_queue_out_ptr) \
        + hostdata->req_queue_in_ptr)
    

int isp1020_queuecommand(Scsi_Cmnd *Cmnd, void (* done)(Scsi_Cmnd *))
{
    int i, *iptr, sg_count;
    struct scatterlist *sg;
    struct Command_Entry *cmd;
    struct isp1020_hostdata *hostdata;

    ENTER("isp1020_queuecommand");

    hostdata = (struct isp1020_hostdata *) Cmnd->host->hostdata;
    Cmnd->scsi_done = done;

    DEBUG(isp1020_print_scsi_cmd(Cmnd);)

    hostdata->req_queue_out_ptr = inw(hostdata->io_base + MBOX4);

    if ((hostdata->req_queue_in_ptr + 1) % REQ_QUEUE_LEN ==
            hostdata->req_queue_out_ptr) {
        printk("qlogicisp : request queue overflow\n");
        return 1;
    }

    DEBUG(printk("qlogicisp : request queue depth %d\n", REQ_QUEUE_DEPTH()));

    cmd = (struct Command_Entry *)
        &hostdata->req_queue[hostdata->req_queue_in_ptr][0];

    memset(cmd, 0, sizeof(struct Command_Entry));

    cmd->hdr.entry_type = ENTRY_COMMAND;
    cmd->hdr.entry_cnt = 1;

    cmd->handle = (caddr_t) Cmnd;
    cmd->target_lun = Cmnd->lun;
    cmd->target_id = Cmnd->target;
    cmd->cdb_length = Cmnd->cmd_len;
    cmd->control_flags = CFLAG_READ | CFLAG_WRITE;

    for (i = 0; i < Cmnd->cmd_len; i++)
        cmd->cdb[i] = Cmnd->cmnd[i];

    if (Cmnd->use_sg) {
        cmd->segment_cnt = sg_count = Cmnd->use_sg;
        sg = (struct scatterlist *) Cmnd->request_buffer;
        iptr = (int *) &cmd->dataseg0.d_base;

        for (i = 0; sg_count > 0; sg_count--, i++) {
            *iptr++ = (int) sg[i].address;
            *iptr++ = sg[i].length;
        }
    }
    else {
        cmd->dataseg0.d_base = (caddr_t) Cmnd->request_buffer;
        cmd->dataseg0.d_count = (u_long) Cmnd->request_bufflen;
        cmd->segment_cnt = 1;
    }

    hostdata->req_queue_in_ptr = (hostdata->req_queue_in_ptr + 1)
        % REQ_QUEUE_LEN;

    outw(hostdata->req_queue_in_ptr, hostdata->io_base + MBOX4);

    LEAVE("isp1020_queuecommand");

    return 0;
}


#define RES_QUEUE_DEPTH() \
    (hostdata->res_queue_in_ptr >= hostdata->res_queue_out_ptr \
        ? hostdata->res_queue_in_ptr - hostdata->res_queue_out_ptr \
        : (RES_QUEUE_LEN - hostdata->res_queue_out_ptr) \
        + hostdata->res_queue_in_ptr)
 

int isp1020_abort(Scsi_Cmnd *Cmnd)
{
    u_short param[6];
    struct isp1020_hostdata *hostdata;
    int return_status = SCSI_ABORT_SUCCESS;

    ENTER("isp1020_abort");

    hostdata = (struct isp1020_hostdata *) Cmnd->host->hostdata;

    isp1020_disable_irqs(hostdata);

    DEBUG(printk("qlogicisp : response queue depth %d\n", RES_QUEUE_DEPTH()));

    param[0] = MBOX_ABORT;
    param[1] = (((u_short) Cmnd->target) << 8) | Cmnd->lun;
    param[2] = (u_long) Cmnd >> 16;
    param[3] = (u_long) Cmnd & 0xffff;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : scsi abort failure: %x\n", param[0]);
        return_status = SCSI_ABORT_ERROR;
    }

    isp1020_enable_irqs(hostdata);

    LEAVE("isp1020_abort");

    return return_status;
}


int isp1020_reset(Scsi_Cmnd *Cmnd)
{
    u_short param[6];
    struct isp1020_hostdata *hostdata;
    int return_status = SCSI_RESET_SUCCESS;

    ENTER("isp1020_reset");

    hostdata = (struct isp1020_hostdata *) Cmnd->host->hostdata;

    param[0] = MBOX_BUS_RESET;
    param[1] = hostdata->host_param.bus_reset_delay;

    isp1020_disable_irqs(hostdata);

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : scsi bus reset failure: %x\n", param[0]);
        return_status = SCSI_RESET_ERROR;
    }
	
    isp1020_enable_irqs(hostdata);

    LEAVE("isp1020_reset");

    return SCSI_RESET_SUCCESS;
}


int isp1020_biosparam(Disk *disk, int n, int ip[])
{
    int size = disk->capacity;

    ENTER("isp1020_biosparam");

    ip[0] = 64;
    ip[1] = 32;
    ip[2] = size >> 11;
    if (ip[2] > 1024) {
        ip[0] = 255;
        ip[1] = 63;
        ip[2] = size / (ip[0] * ip[1]);
        if (ip[2] > 1023)
            ip[2] = 1023;
    }

    LEAVE("isp1020_biosparam");

    return 0;
}


int isp1020_reset_hardware(struct isp1020_hostdata *hostdata)
{
    u_short param[6];

    ENTER("isp1020_reset_hardware");

    outw(0x0001, hostdata->io_base + PCI_INTF_CTL);
    outw(HCCR_RESET, hostdata->io_base + HOST_HCCR);
    outw(HCCR_RELEASE, hostdata->io_base + HOST_HCCR);
    outw(HCCR_BIOS_ENABLE, hostdata->io_base + HOST_HCCR);

    while (inw(hostdata->io_base + MBOX0))
        barrier();

#if DEBUG_ISP1020
    printk("qlogicisp : mbox 0 0x%04x \n", inw(hostdata->io_base + MBOX0));
    printk("qlogicisp : mbox 1 0x%04x \n", inw(hostdata->io_base + MBOX1));
    printk("qlogicisp : mbox 2 0x%04x \n", inw(hostdata->io_base + MBOX2));
    printk("qlogicisp : mbox 3 0x%04x \n", inw(hostdata->io_base + MBOX3));
    printk("qlogicisp : mbox 4 0x%04x \n", inw(hostdata->io_base + MBOX4));
    printk("qlogicisp : mbox 5 0x%04x \n", inw(hostdata->io_base + MBOX5));
#endif /* DEBUG_ISP1020 */

    DEBUG(printk("qlogicisp : loading risc ram\n");)

#if RELOAD_FIRMWARE
    { int i;
        for (i = 0; i < risc_code_length01; i++) {
            param[0] = MBOX_WRITE_RAM_WORD;
            param[1] = risc_code_addr01 + i;
            param[2] = risc_code01[i];

            isp1020_mbox_command(hostdata, param);

            if (param[0] != MBOX_COMMAND_COMPLETE) {
                printk("qlogicisp : firmware load failure\n");
                return 1;
            }
        }
    }
#endif /* RELOAD_FIRMEARE */

    DEBUG(printk("qlogicisp : verifing checksum\n");)

    param[0] = MBOX_VERIFY_CHECKSUM;
    param[1] = risc_code_addr01;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : ram checksum failure\n");
        return 1;
    }

    DEBUG(printk("qlogicisp : executing firmware\n");)

    param[0] = MBOX_EXEC_FIRMWARE;
    param[1] = risc_code_addr01;

    isp1020_mbox_command(hostdata, param);

    param[0] = MBOX_ABOUT_FIRMWARE;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : about firmware failure\n");
        return 1;
    }

    DEBUG(printk("qlogicisp : firmware major revision %d\n", param[1]);)
    DEBUG(printk("qlogicisp : firmware minor revision %d\n", param[2]);)

    LEAVE("isp1020_reset_hardware");

    return 0;
}


int isp1020_init(struct Scsi_Host *sh)
{
    u_long io_base;
    struct isp1020_hostdata *hostdata;
    u_char bus, device_fn, revision, irq;
    u_short vendor_id, device_id, command;

    ENTER("isp1020_init");

    hostdata = (struct isp1020_hostdata *) sh->hostdata;
    bus = hostdata->bus;
    device_fn = hostdata->device_fn;

    if (pcibios_read_config_word(bus, device_fn, PCI_VENDOR_ID, &vendor_id)
            || pcibios_read_config_word(bus, device_fn,
                PCI_DEVICE_ID, &device_id)
            || pcibios_read_config_word(bus, device_fn,
                PCI_COMMAND, &command)
            || pcibios_read_config_dword(bus, device_fn,
                PCI_BASE_ADDRESS_0, &io_base)
	    || pcibios_read_config_byte(bus, device_fn,
                PCI_CLASS_REVISION, &revision)
            || pcibios_read_config_byte(bus, device_fn,
                PCI_INTERRUPT_LINE, &irq)) {
        printk("qlogicisp : error reading PCI configuration\n");
        return 1;
    }

    if (vendor_id != PCI_VENDOR_ID_QLOGIC) {
        printk("qlogicisp : 0x%04x is not QLogic vendor ID\n", vendor_id);
        return 1;
    }

    if (device_id != PCI_DEVICE_ID_QLOGIC_ISP1020) {
        printk("qlogicisp : 0x%04x does not match ISP1020 device id\n",
            device_id);
        return 1;
    }

    if (command & PCI_COMMAND_IO && (io_base & 3) == 1)
        io_base &= PCI_BASE_ADDRESS_IO_MASK;
    else {
        printk("qlogicisp : i/o mapping is disabled\n");
        return 1;
    }

    if (!(command & PCI_COMMAND_MASTER)) {
        printk("qlogicisp : bus mastering is disabled\n");
        return 1;
    }

    if (revision != ISP1020_REV_ID)
       printk("qlogicisp : warning : new isp1020 revision id\n");

    if (inw(io_base + PCI_ID_LOW) != PCI_VENDOR_ID_QLOGIC
            || inw(io_base + PCI_ID_HIGH) != PCI_DEVICE_ID_QLOGIC_ISP1020) {
        printk("qlogicisp : can't decode i/o address space\n");
        return 1;
    }

    hostdata->io_base = io_base;
    hostdata->revision = revision;
    hostdata->irq = irq;

    LEAVE("isp1020_init");

    return 0;
}


int isp1020_get_defaults(struct isp1020_hostdata *hostdata)
{
    int i;
    u_short value;

    ENTER("isp1020_get_defaults");

    if (!isp1020_verify_nvram(hostdata)) {
        printk("qlogicisp : nvram checksum failure\n");
        return 1;
    }

    value = isp1020_read_nvram_word(hostdata, 2);
    hostdata->host_param.fifo_threshold = (value >> 8) & 0x03;
    hostdata->host_param.host_adapter_enable = (value >> 11) & 0x01;
    hostdata->host_param.initiator_scsi_id = (value >> 12) & 0x0f;

    value = isp1020_read_nvram_word(hostdata, 3);
    hostdata->host_param.bus_reset_delay = value & 0xff;
    hostdata->host_param.retry_count = value >> 8;

    value = isp1020_read_nvram_word(hostdata, 4);
    hostdata->host_param.retry_delay = value & 0xff;
    hostdata->host_param.async_data_setup_time = (value >> 8) & 0x0f;
    hostdata->host_param.req_ack_active_negation = (value >> 12) & 0x01;
    hostdata->host_param.data_line_active_negation = (value >> 13) & 0x01;
    hostdata->host_param.data_dma_burst_enable = (value >> 14) & 0x01;
    hostdata->host_param.command_dma_burst_enable = (value >> 15);

    value = isp1020_read_nvram_word(hostdata, 5);
    hostdata->host_param.tag_aging = value & 0xff;

    value = isp1020_read_nvram_word(hostdata, 6);
    hostdata->host_param.selection_timeout = value & 0xffff;

    value = isp1020_read_nvram_word(hostdata, 7);
    hostdata->host_param.max_queue_depth = value & 0xffff;

#if DEBUG_ISP1020_SETUP
    printk("qlogicisp : fifo threshold=%d\n",
        hostdata->host_param.fifo_threshold);
    printk("qlogicisp : initiator scsi id=%d\n",
        hostdata->host_param.initiator_scsi_id);
    printk("qlogicisp : bus reset delay=%d\n",
        hostdata->host_param.bus_reset_delay);
    printk("qlogicisp : retry count=%d\n",
	hostdata->host_param.retry_count);
    printk("qlogicisp : retry delay=%d\n",
        hostdata->host_param.retry_delay);
    printk("qlogicisp : async data setup time=%d\n",
        hostdata->host_param.async_data_setup_time);
    printk("qlogicisp : req/ack active negation=%d\n",
        hostdata->host_param.req_ack_active_negation);
    printk("qlogicisp : data line active negation=%d\n",
        hostdata->host_param.data_line_active_negation);
    printk("qlogicisp : data DMA burst enable=%d\n",
        hostdata->host_param.data_dma_burst_enable);
    printk("qlogicisp : command DMA burst enable=%d\n",
        hostdata->host_param.command_dma_burst_enable);
    printk("qlogicisp : tag age limit=%d\n",
        hostdata->host_param.tag_aging);
    printk("qlogicisp : selection timeout limit=%d\n",
        hostdata->host_param.selection_timeout);
    printk("qlogicisp : max queue depth=%d\n",
        hostdata->host_param.max_queue_depth);
#endif /* DEBUG_ISP1020_SETUP */

    for (i = 0; i < 16; i++) {

        value = isp1020_read_nvram_word(hostdata, 14 + i * 3);
        hostdata->dev_param[i].device_flags = value & 0xff;
        hostdata->dev_param[i].execution_throttle = value >> 8;

        value = isp1020_read_nvram_word(hostdata, 15 + i * 3);
        hostdata->dev_param[i].synchronous_period = value & 0xff;
        hostdata->dev_param[i].synchronous_offset = (value >> 8) & 0x0f;
        hostdata->dev_param[i].device_enable = (value >> 12) & 0x01;

#if DEBUG_ISP1020_SETUP
        printk("qlogicisp : target 0x%02x\n", i);
        printk("qlogicisp :     device flags=0x%02x\n",
            hostdata->dev_param[i].device_flags);
        printk("qlogicisp :     execution throttle=%d\n",
            hostdata->dev_param[i].execution_throttle);
        printk("qlogicisp :     synchronous period=%d\n",
            hostdata->dev_param[i].synchronous_period);
        printk("qlogicisp :     synchronous offset=%d\n",
            hostdata->dev_param[i].synchronous_offset);
        printk("qlogicisp :     device enable=%d\n",
            hostdata->dev_param[i].device_enable);
#endif /* DEBUG_ISP1020_SETUP */
    }

    LEAVE("isp1020_get_defaults");

    return 0;
}


int isp1020_set_defaults(struct isp1020_hostdata *hostdata)
{
    int i;

    ENTER("isp1020_set_defaults");

    hostdata->host_param.fifo_threshold = 2;
    hostdata->host_param.host_adapter_enable = 1;
    hostdata->host_param.initiator_scsi_id = 7;
    hostdata->host_param.bus_reset_delay = 3;
    hostdata->host_param.retry_count = 0;
    hostdata->host_param.retry_delay = 0;
    hostdata->host_param.async_data_setup_time = 6;
    hostdata->host_param.req_ack_active_negation = 1;
    hostdata->host_param.data_line_active_negation = 1;
    hostdata->host_param.data_dma_burst_enable = 1;
    hostdata->host_param.command_dma_burst_enable = 1;
    hostdata->host_param.tag_aging = 8;
    hostdata->host_param.selection_timeout = 250;
    hostdata->host_param.max_queue_depth = 256;

    for (i = 0; i < 16; i++) {
        hostdata->dev_param[i].device_flags = 0xc4;
        hostdata->dev_param[i].execution_throttle = 16;
        hostdata->dev_param[i].synchronous_period = 25;
        hostdata->dev_param[i].synchronous_offset = 12;
        hostdata->dev_param[i].device_enable = 1;
    }

    LEAVE("isp1020_set_defaults");

    return 0;
}


int isp1020_load_parameters(struct isp1020_hostdata *hostdata)
{
    int i, k;
    u_long queue_addr;
    u_short param[6];
    u_short isp_cfg1;

    ENTER("isp1020_load_parameters");

    outw(hostdata->host_param.fifo_threshold, hostdata->io_base + ISP_CFG1);

    param[0] = MBOX_SET_INIT_SCSI_ID;
    param[1] = hostdata->host_param.initiator_scsi_id;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set initiator id failure\n");
        return 1;
    }

    param[0] = MBOX_SET_RETRY_COUNT;
    param[1] = hostdata->host_param.retry_count;
    param[2] = hostdata->host_param.retry_delay;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set retry count failure\n");
        return 1;
    }

    param[0] = MBOX_SET_ASYNC_DATA_SETUP_TIME;
    param[1] = hostdata->host_param.async_data_setup_time;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : async data setup time failure\n");
        return 1;
    }

    param[0] = MBOX_SET_ACTIVE_NEG_STATE;
    param[1] = (hostdata->host_param.req_ack_active_negation << 4)
        | (hostdata->host_param.data_line_active_negation << 5);

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set active negation state failure\n");
        return 1;
    }

    param[0] = MBOX_SET_PCI_CONTROL_PARAMS;
    param[1] = hostdata->host_param.data_dma_burst_enable << 1;
    param[2] = hostdata->host_param.command_dma_burst_enable << 1;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set pci control parameter failure\n");
        return 1;
    }

    isp_cfg1 = inw(hostdata->io_base + ISP_CFG1);

    if (hostdata->host_param.data_dma_burst_enable 
            || hostdata->host_param.command_dma_burst_enable)
        isp_cfg1 |= 0x0004;
    else
        isp_cfg1 &= 0xfffb;

    outw(isp_cfg1, hostdata->io_base + ISP_CFG1);
			
    param[0] = MBOX_SET_TAG_AGE_LIMIT;
    param[1] = hostdata->host_param.tag_aging;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set tag age limit failure\n");
        return 1;
    }

    param[0] = MBOX_SET_SELECT_TIMEOUT;
    param[1] = hostdata->host_param.selection_timeout;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set selection timeout failure\n");
        return 1;
    }

    for (i = 0; i < 16; i++) {

        if (!hostdata->dev_param[i].device_enable)
            continue;

        param[0] = MBOX_SET_TARGET_PARAMS;
        param[1] = i << 8;
        param[2] = hostdata->dev_param[i].device_flags << 8;
        param[3] = (hostdata->dev_param[i].synchronous_offset << 8)
            | hostdata->dev_param[i].synchronous_period;

        isp1020_mbox_command(hostdata, param);

        if (param[0] != MBOX_COMMAND_COMPLETE) {
            printk("qlogicisp : set target parameter failure\n");
            return 1;
        }

        for (k = 0; k < 8; k++) {

            param[0] = MBOX_SET_DEV_QUEUE_PARAMS;
            param[1] = (i << 8) | k;
            param[2] = hostdata->host_param.max_queue_depth;
            param[3] = hostdata->dev_param[i].execution_throttle;

            isp1020_mbox_command(hostdata, param);

            if (param[0] != MBOX_COMMAND_COMPLETE) {
                printk("qlogicisp : set device queue parameter failure\n");
                return 1;
            }
        }
    }

    queue_addr = (u_long) &hostdata->res_queue[0][0];

    param[0] = MBOX_INIT_RES_QUEUE;
    param[1] = RES_QUEUE_LEN;
    param[2] = (u_short) (queue_addr >> 16);
    param[3] = (u_short) (queue_addr & 0xffff);
    param[4] = 0;
    param[5] = 0;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set response queue failure\n");
        return 1;
    }

    queue_addr = (u_long) &hostdata->req_queue[0][0];

    param[0] = MBOX_INIT_REQ_QUEUE;
    param[1] = REQ_QUEUE_LEN;
    param[2] = (u_short) (queue_addr >> 16);
    param[3] = (u_short) (queue_addr & 0xffff);
    param[4] = 0;

    isp1020_mbox_command(hostdata, param);

    if (param[0] != MBOX_COMMAND_COMPLETE) {
        printk("qlogicisp : set request queue failure\n");
        return 1;
    }

    LEAVE("isp1020_load_parameters");

    return 0;
}


int isp1020_mbox_command(struct isp1020_hostdata *hostdata, u_short param[])
{
    if (mbox_param[param[0]] == 0)
        return 1;

    while (inw(hostdata->io_base + HOST_HCCR) & 0x0080)
        barrier();

    switch(mbox_param[param[0]] >> 4) {
        case 6: outw(param[5], hostdata->io_base + MBOX5);
        case 5: outw(param[4], hostdata->io_base + MBOX4);
        case 4: outw(param[3], hostdata->io_base + MBOX3);
        case 3: outw(param[2], hostdata->io_base + MBOX2);
        case 2: outw(param[1], hostdata->io_base + MBOX1);
        case 1: outw(param[0], hostdata->io_base + MBOX0);
    }

    outw(HCCR_CLEAR_RISC_INTR, hostdata->io_base + HOST_HCCR);
    outw(0x0, hostdata->io_base + PCI_SEMAPHORE);

    outw(HCCR_SET_HOST_INTR, hostdata->io_base + HOST_HCCR);

    while (!(inw(hostdata->io_base + PCI_INTF_STS) & 0x04))
        barrier();

    while (inw(hostdata->io_base + MBOX0) == 0x04)
        barrier();

    outw(HCCR_CLEAR_RISC_INTR, hostdata->io_base + HOST_HCCR);
    outw(0x0, hostdata->io_base + PCI_SEMAPHORE);

    switch(mbox_param[param[0]] & 0xf) {
        case 6: param[5] = inw(hostdata->io_base + MBOX5);
        case 5: param[4] = inw(hostdata->io_base + MBOX4);
        case 4: param[3] = inw(hostdata->io_base + MBOX3);
        case 3: param[2] = inw(hostdata->io_base + MBOX2);
        case 2: param[1] = inw(hostdata->io_base + MBOX1);
        case 1: param[0] = inw(hostdata->io_base + MBOX0);
    }

    return 0;
}


#define RESPONSE_QUEUE_UPDATE	0x01

void isp1020_intr_handler(int irq, struct pt_regs *regs)
{
    Scsi_Cmnd *Cmnd;
    struct Status_Entry *sts;
    struct Marker_Entry *marker;
    u_short status, add_marker = 0;
    struct isp1020_hostdata *hostdata;

    ENTER_INTR("isp1020_intr_handler");

    if ((hostdata = irq2host[irq]) == NULL) {
        printk("qlogicisp : unexpected interrupt on line %d\n", irq);
        return;
    }

    DEBUG_INTR(printk("qlogicisp : interrupt on line %d\n", irq);)

    while (!(inw(hostdata->io_base + PCI_INTF_STS) & 0x04))
        barrier();

    hostdata->res_queue_in_ptr = inw(hostdata->io_base + MBOX5);
    outw(HCCR_CLEAR_RISC_INTR, hostdata->io_base + HOST_HCCR);
    status = inw(hostdata->io_base + PCI_SEMAPHORE);

    if ((status & RESPONSE_QUEUE_UPDATE) == 0) {

        DEBUG_INTR(printk("qlogicisp : response queue update\n");)
        DEBUG_INTR(printk("qlogicisp : response queue depth %d\n",
            RES_QUEUE_DEPTH());)

        while (hostdata->res_queue_out_ptr != hostdata->res_queue_in_ptr) {

            sts = (struct Status_Entry *)
                &hostdata->res_queue[hostdata->res_queue_out_ptr][0];

            Cmnd = (Scsi_Cmnd *) sts->handle;

            if (sts->completion_status == CS_RESET_OCCURRED
                    || sts->completion_status == CS_ABORTED
                    || (sts->status_flags & STF_BUS_RESET))
                add_marker++;

            if (sts->state_flags & SF_GOT_SENSE)
                memcpy(Cmnd->sense_buffer, sts->req_sense_data,
                    sizeof(Cmnd->sense_buffer));

            DEBUG_INTR(isp1020_print_status_entry(sts);)

            if (sts->hdr.entry_type == ENTRY_STATUS)
                Cmnd->result = isp1020_return_status(sts);
            else
                Cmnd->result = DID_ERROR << 16;

            hostdata->res_queue_out_ptr = (hostdata->res_queue_out_ptr + 1)
                % RES_QUEUE_LEN;

            outw(hostdata->res_queue_out_ptr, hostdata->io_base + MBOX5);

            (Cmnd->scsi_done)(Cmnd);
        }
    }
    else {

        DEBUG_INTR(printk("qlogicisp : mbox completion\n");)

        status = inw(hostdata->io_base + MBOX0);
        outw(0x0, hostdata->io_base + PCI_SEMAPHORE);

        DEBUG_INTR(printk("qlogicisp : mbox completion status: %x\n", status);)

        switch (status) {
            case ASYNC_SCSI_BUS_RESET:
            case EXECUTION_TIMEOUT_RESET:
                add_marker++;
                break;
            case INVALID_COMMAND:
            case HOST_INTERFACE_ERROR:
            case COMMAND_ERROR:
            case COMMAND_PARAM_ERROR:
                printk("qlogicisp : bad mailbox return status\n");
                break;
        }
    }

    if (add_marker) {

        DEBUG_INTR(printk("qlogicisp : adding marker entry\n");)

        if ((hostdata->req_queue_in_ptr + 1) % REQ_QUEUE_LEN
                == hostdata->req_queue_out_ptr) {
            printk("qlogicisp : request queue overflow\n");
            return;
        }

        marker = (struct Marker_Entry *)
            &hostdata->req_queue[hostdata->req_queue_in_ptr][0];

        memset(marker, 0, sizeof(struct Marker_Entry));

        marker->hdr.entry_type = ENTRY_MARKER;
        marker->hdr.entry_cnt = 1;
        marker->modifier = SYNC_ALL;

        hostdata->req_queue_in_ptr = (hostdata->req_queue_in_ptr + 1)
            % REQ_QUEUE_LEN;

        outw(hostdata->req_queue_in_ptr, hostdata->io_base + MBOX4);
    }

    LEAVE_INTR("isp1020_intr_handler");
}


#define NVRAM_DELAY() { \
    int counter = 0; while (counter++ < 0xc8) barrier(); }


u_short isp1020_read_nvram_word(struct isp1020_hostdata *hostdata, u_short byte)
{
    int i;
    u_short value, output, input;

    byte &= 0x3f; byte |= 0x0180;

    for (i = 8; i >= 0; i--) {
        output = ((byte >> i) & 0x1) ? 0x4 : 0x0;
        outw(output | 0x2, hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
        outw(output | 0x3, hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
        outw(output | 0x2, hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
    }

    for (i = 0xf, value = 0; i >= 0; i--) {
        value = value << 0x1;
        outw(0x3, hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
        input = inw(hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
        outw(0x2, hostdata->io_base + PCI_NVRAM); NVRAM_DELAY();
        if (input & 0x8) value |= 0x1;
    }

    outw(0x0, hostdata->io_base + PCI_NVRAM);

    return(value);
}

#define ISP1020_NVRAM_LEN	0x40
#define ISP1020_NVRAM_SIG1	0x5349
#define ISP1020_NVRAM_SIG2	0x2050

int isp1020_verify_nvram(struct isp1020_hostdata *hostdata)
{
    int	i;
    u_short value;
    u_char checksum = 0;

    for (i = 0; i < ISP1020_NVRAM_LEN; i++) {
        value = isp1020_read_nvram_word(hostdata, i);

        switch (i) {
            case 0:
                if (value != ISP1020_NVRAM_SIG1) return 0;
                break;
            case 1:
                if (value != ISP1020_NVRAM_SIG2) return 0;
                break;
            case 2:
                if ((value & 0xff) != 0x02) return 0;
                break;
        }
        checksum += value & 0xff;
        checksum += value >> 8;
    }

    return (checksum == 0);
}


void isp1020_enable_irqs(struct isp1020_hostdata *hostdata)
{
    outw(0x6, hostdata->io_base + PCI_INTF_CTL);
}


void isp1020_disable_irqs(struct isp1020_hostdata *hostdata)
{
    outw(0x0, hostdata->io_base + PCI_INTF_CTL);
}


#if DEBUG_ISP1020_INTR

void isp1020_print_status_entry(struct Status_Entry *status)
{
    int i;

    printk("qlogicisp : entry count = 0x%02x, type = 0x%02x, flags = 0x%02x\n",
        status->hdr.entry_cnt, status->hdr.entry_type, status->hdr.flags);
    printk("qlogicisp : scsi status = 0x%04x, compeltion status = 0x%04x\n",
        status->scsi_status, status->completion_status);
    printk("qlogicisp : state flags = 0x%04x, status flags = 0x%04x\n",
        status->state_flags, status->status_flags);
    printk("qlogicisp : time = 0x%04x, request sense length = 0x%04x\n",
        status->time, status->req_sense_len);
    printk("qlogicisp : residual transfer length = 0x%08lx\n", status->residual);

    for (i = 0; i < status->req_sense_len; i++)
        printk("qlogicisp : sense data = 0x%02x\n", status->req_sense_data[i]);
}

#endif /* DEBUG_ISP1020_INTR */


#if DEBUG_ISP1020

void isp1020_print_scsi_cmd(Scsi_Cmnd *cmd)
{
    int i;

    printk("qlogicisp : target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
	cmd->target, cmd->lun, cmd->cmd_len);
    printk("qlogicisp : command = ");
    for (i = 0; i < cmd->cmd_len; i++)
        printk("0x%02x ", cmd->cmnd[i]);
    printk("\n");
}

#endif /* DEBUG_ISP1020 */


int isp1020_return_status(struct Status_Entry *sts)
{
    int host_status = DID_ERROR;
#if DEBUG_ISP1020_INTR
    static char *reason[] = {
        "DID_OK",
        "DID_NO_CONNECT",
        "DID_BUS_BUSY",
        "DID_TIME_OUT",
        "DID_BAD_TARGET",
        "DID_ABORT",
        "DID_PARITY",
        "DID_ERROR",
        "DID_RESET",
        "DID_BAD_INTR"
    };
#endif /* DEBUG_ISP1020_INTR */

    ENTER("isp1020_return_status");

    DEBUG(printk("qlogicisp : completion status = 0x%04x\n",
        sts->completion_status);)

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
            printk("qlogicisp : unknown completion status 0x%04x\n",
                sts->completion_status);
            host_status = DID_ERROR;
            break;
    }

    DEBUG_INTR(printk("qlogicisp : host status (%s) scsi status %x\n",
        reason[host_status], sts->scsi_status);)

    LEAVE("isp1020_return_status");

    return (sts->scsi_status & STATUS_MASK) | (host_status << 16);
}

#if MODULE
Scsi_Host_Template driver_template = ISP1020;

#include "scsi_module.c"
#endif /* MODULE */
