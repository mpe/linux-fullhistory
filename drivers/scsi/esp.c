/* esp.c:  EnhancedScsiProcessor Sun SCSI driver code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include "scsi.h"
#include "hosts.h"
#include "esp.h"

#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/vaddrs.h>
#include <asm/io.h>

#define DEBUG_ESP
/* #define DEBUG_ESP_SG */

#if defined(DEBUG_ESP)
#define ESPLOG(foo)  printk foo
#else
#define ESPLOG(foo)
#endif /* (DEBUG_ESP) */

#define INTERNAL_ESP_ERROR \
        (panic ("Internal ESP driver error in file %s, line %d\n", \
		__FILE__, __LINE__))

#define INTERNAL_ESP_ERROR_NOPANIC \
        (printk ("Internal ESP driver error in file %s, line %d\n", \
		 __FILE__, __LINE__))

/* This enum will be expanded when we have sync code written. */
enum {
	not_issued   = 0x01,  /* Still in the issue_SC queue.          */
	in_selection = 0x02,  /* ESP is arbitrating, awaiting IRQ      */
	in_datain    = 0x04,  /* Data is transferring over the bus     */
	in_dataout   = 0x08,  /* Data is transferring over the bus     */
	in_status    = 0x10,  /* Awaiting status/msg bytes from target */
	in_finale    = 0x11,  /* Sent Msg ack, awaiting disconnect     */
};

struct proc_dir_entry proc_scsi_esp = {
	PROC_SCSI_ESP, 3, "esp",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

struct Sparc_ESP *espchain;

static void esp_intr(int irq, void *dev_id, struct pt_regs *pregs);
static void esp_done(struct Sparc_ESP *esp, int error);

/* Debugging routines */
struct esp_cmdstrings {
	unchar cmdchar;
	char *text;
} esp_cmd_strings[] = {
	/* Miscellaneous */
	{ ESP_CMD_NULL, "ESP_NOP", },
	{ ESP_CMD_FLUSH, "FIFO_FLUSH", },
	{ ESP_CMD_RC, "RSTESP", },
	{ ESP_CMD_RS, "RSTSCSI", },
	/* Disconnected State Group */
	{ ESP_CMD_RSEL, "RESLCTSEQ", },
	{ ESP_CMD_SEL, "SLCTNATN", },
	{ ESP_CMD_SELA, "SLCTATN", },
	{ ESP_CMD_SELAS, "SLCTATNSTOP", },
	{ ESP_CMD_ESEL, "ENSLCTRESEL", },
	{ ESP_CMD_DSEL, "DISSELRESEL", },
	{ ESP_CMD_SA3, "SLCTATN3", },
	{ ESP_CMD_RSEL3, "RESLCTSEQ", },
	/* Target State Group */
	{ ESP_CMD_SMSG, "SNDMSG", },
	{ ESP_CMD_SSTAT, "SNDSTATUS", },
	{ ESP_CMD_SDATA, "SNDDATA", },
	{ ESP_CMD_DSEQ, "DISCSEQ", },
	{ ESP_CMD_TSEQ, "TERMSEQ", },
	{ ESP_CMD_TCCSEQ, "TRGTCMDCOMPSEQ", },
	{ ESP_CMD_DCNCT, "DISC", },
	{ ESP_CMD_RMSG, "RCVMSG", },
	{ ESP_CMD_RCMD, "RCVCMD", },
	{ ESP_CMD_RDATA, "RCVDATA", },
	{ ESP_CMD_RCSEQ, "RCVCMDSEQ", },
	/* Initiator State Group */
	{ ESP_CMD_TI, "TRANSINFO", },
	{ ESP_CMD_ICCSEQ, "INICMDSEQCOMP", },
	{ ESP_CMD_MOK, "MSGACCEPTED", },
	{ ESP_CMD_TPAD, "TPAD", },
	{ ESP_CMD_SATN, "SATN", },
	{ ESP_CMD_RATN, "RATN", },
};
#define NUM_ESP_COMMANDS  ((sizeof(esp_cmd_strings)) / (sizeof(struct esp_cmdstrings)))

/* Print textual representation of an ESP command */
static inline void esp_print_cmd(unchar espcmd)
{
	unchar dma_bit = espcmd & ESP_CMD_DMA;
	int i;

	espcmd &= ~dma_bit;
	for(i=0; i<NUM_ESP_COMMANDS; i++)
		if(esp_cmd_strings[i].cmdchar == espcmd)
			break;
	if(i==NUM_ESP_COMMANDS)
		printk("ESP_Unknown");
	else
		printk("%s%s", esp_cmd_strings[i].text,
		       ((dma_bit) ? "+DMA" : ""));
}

/* Print the status register's value */
static inline void esp_print_statreg(unchar statreg)
{
	unchar phase;

	printk("STATUS<");
	phase = statreg & ESP_STAT_PMASK;
	printk("%s,", (phase == ESP_DOP ? "DATA-OUT" :
		       (phase == ESP_DIP ? "DATA-IN" :
			(phase == ESP_CMDP ? "COMMAND" :
			 (phase == ESP_STATP ? "STATUS" :
			  (phase == ESP_MOP ? "MSG-OUT" :
			   (phase == ESP_MIP ? "MSG_IN" :
			    "unknown")))))));
	if(statreg & ESP_STAT_TDONE)
		printk("TRANS_DONE,");
	if(statreg & ESP_STAT_TCNT)
		printk("TCOUNT_ZERO,");
	if(statreg & ESP_STAT_PERR)
		printk("P_ERROR,");
	if(statreg & ESP_STAT_SPAM)
		printk("SPAM,");
	if(statreg & ESP_STAT_INTR)
		printk("IRQ,");
	printk(">");
}

/* Print the interrupt register's value */
static inline void esp_print_ireg(unchar intreg)
{
	printk("INTREG< ");
	if(intreg & ESP_INTR_S)
		printk("SLCT_NATN ");
	if(intreg & ESP_INTR_SATN)
		printk("SLCT_ATN ");
	if(intreg & ESP_INTR_RSEL)
		printk("RSLCT ");
	if(intreg & ESP_INTR_FDONE)
		printk("FDONE ");
	if(intreg & ESP_INTR_BSERV)
		printk("BSERV ");
	if(intreg & ESP_INTR_DC)
		printk("DISCNCT ");
	if(intreg & ESP_INTR_IC)
		printk("ILL_CMD ");
	if(intreg & ESP_INTR_SR)
		printk("SCSI_BUS_RESET ");
	printk(">");
}

/* Print the sequence step registers contents */
static inline void esp_print_seqreg(unchar stepreg)
{
	stepreg &= ESP_STEP_VBITS;
	printk("STEP<%s>",
	       (stepreg == ESP_STEP_ASEL ? "SLCT_ARB_CMPLT" :
		(stepreg == ESP_STEP_SID ? "1BYTE_MSG_SENT" :
		 (stepreg == ESP_STEP_NCMD ? "NOT_IN_CMD_PHASE" :
		  (stepreg == ESP_STEP_PPC ? "CMD_BYTES_LOST" :
		   (stepreg == ESP_STEP_FINI ? "CMD_SENT_OK" :
		    "UNKNOWN"))))));
}

/* Manipulation of the ESP command queues.  Thanks to the aha152x driver
 * and its author, Juergen E. Fischer, for the methods used here.
 * Note that these are per-ESP queues, not global queues like
 * the aha152x driver uses.
 */
static inline void append_SC(Scsi_Cmnd **SC, Scsi_Cmnd *new_SC)
{
	Scsi_Cmnd *end;
	unsigned long flags;

	save_flags(flags); cli();
	new_SC->host_scribble = (unsigned char *) NULL;
	if(!*SC)
		*SC = new_SC;
	else {
		for(end=*SC;end->host_scribble;end=(Scsi_Cmnd *)end->host_scribble)
			;
		end->host_scribble = (unsigned char *) new_SC;
	}
	restore_flags(flags);
}

static inline Scsi_Cmnd *remove_first_SC(Scsi_Cmnd **SC)
{
	Scsi_Cmnd *ptr;
	unsigned long flags;

	save_flags(flags); cli();
	ptr = *SC;
	if(ptr)
		*SC = (Scsi_Cmnd *) (*SC)->host_scribble;
	restore_flags(flags);
	return ptr;
}

static inline Scsi_Cmnd *remove_SC(Scsi_Cmnd **SC, int target, int lun)
{
	Scsi_Cmnd *ptr, *prev;
	unsigned long flags;

	save_flags(flags); cli();
	for(ptr = *SC, prev = NULL;
	    ptr && ((ptr->target != target) || (ptr->lun != lun));
	    prev = ptr, ptr = (Scsi_Cmnd *) ptr->host_scribble)
		;
	if(ptr) {
		if(prev)
			prev->host_scribble=ptr->host_scribble;
		else
			*SC=(Scsi_Cmnd *)ptr->host_scribble;
	}
	restore_flags(flags);
	return ptr;
}

static inline void do_pause(unsigned amount)
{
	unsigned long the_time = jiffies + amount;

	while(jiffies < the_time)
		barrier(); /* Not really needed, but... */
}

/* This places the ESP into a known state at boot time. */
static inline void esp_bootup_reset(struct Sparc_ESP *esp, struct Sparc_ESP_regs *eregs)
{
	struct sparc_dma_registers *dregs = esp->dregs;
	volatile unchar trash;

	/* Punt the DVMA into a known state. */
	dregs->cond_reg |= DMA_RST_SCSI;
	do_pause(100);
	dregs->cond_reg &= ~(DMA_RST_SCSI);
	if(esp->dma->revision == dvmarev2)
		if(esp->erev != esp100)
			dregs->cond_reg |= DMA_3CLKS;
	else if(esp->dma->revision == dvmarev3)
		if(esp->erev == fas236 || esp->erev == fas100a) {
			dregs->cond_reg &= ~(DMA_3CLKS);
			dregs->cond_reg |= DMA_2CLKS;
		}
	else if(esp->dma->revision == dvmaesc1)
		dregs->cond_reg |= DMA_ADD_ENABLE;
	DMA_INTSON(dregs);

	/* Now reset the ESP chip */
	eregs->esp_cmd = ESP_CMD_RC;
	eregs->esp_cmd = (ESP_CMD_NULL | ESP_CMD_DMA);
	eregs->esp_cmd = (ESP_CMD_NULL | ESP_CMD_DMA); /* borken hardware... */

	/* Reload the configuration registers */
	eregs->esp_cfg1  = esp->config1;
	eregs->esp_cfact = esp->cfact;
	eregs->esp_stp   = 0;
	eregs->esp_soff  = 0;
	eregs->esp_timeo = esp->sync_defp;
	if(esp->erev == esp100a || esp->erev == esp236)
		eregs->esp_cfg2 = esp->config2;
	if(esp->erev == esp236)
		eregs->esp_cfg3 = esp->config3[0];
	/* Eat any bitrot in the chip */
	trash = eregs->esp_intrpt;

	/* Reset the SCSI bus, but tell ESP not to generate an irq */
	eregs->esp_cfg1 |= ESP_CONFIG1_SRRDISAB;
	eregs->esp_cmd = ESP_CMD_RS;
	do_pause(200);
	eregs->esp_cfg1 = esp->config1;

	/* Eat any bitrot in the chip and we are done... */
	trash = eregs->esp_intrpt;
}

/* Detecting ESP chips on the machine.  This is the simple and easy
 * version.
 */
int esp_detect(Scsi_Host_Template *tpnt)
{
	struct Sparc_ESP *esp, *elink;
	struct Scsi_Host *esp_host;
	struct linux_sbus *sbus;
	struct linux_sbus_device *esp_dev, *sbdev_iter;
	struct Sparc_ESP_regs *eregs;
	struct sparc_dma_registers *dregs;
	struct Linux_SBus_DMA *dma, *dlink;
	unsigned int fmhz;
	unchar ccf, bsizes, bsizes_more;
	int nesps = 0;
	int esp_node;

	espchain = 0;
	if(!SBus_chain)
		panic("No SBUS in esp_detect()");
	for_each_sbus(sbus) {
		for_each_sbusdev(sbdev_iter, sbus) {
			/* Is it an esp sbus device? */
			esp_dev = sbdev_iter;
			if(strcmp(esp_dev->prom_name, "esp") &&
			   strcmp(esp_dev->prom_name, "SUNW,esp")) {
				if(!esp_dev->child ||
				   strcmp(esp_dev->prom_name, "espdma"))
					continue; /* nope... */
				esp_dev = esp_dev->child;
				if(strcmp(esp_dev->prom_name, "esp") &&
				   strcmp(esp_dev->prom_name, "SUNW,esp"))
					continue; /* how can this happen? */
			}
			esp_host = scsi_register(tpnt, sizeof(struct Sparc_ESP));
			if(!esp_host)
				panic("Cannot register ESP SCSI host");
			esp = (struct Sparc_ESP *) esp_host->hostdata;
			if(!esp)
				panic("No esp in hostdata");
			esp->ehost = esp_host;
			esp->edev = esp_dev;
			/* Put into the chain of esp chips detected */
			if(espchain) {
				elink = espchain;
				while(elink->next) elink = elink->next;
				elink->next = esp;
			} else {
				espchain = esp;
			}
			esp->next = 0;

			/* Get misc. prom information */
#define ESP_IS_MY_DVMA(esp, dma)  \
	((esp->edev->my_bus == dma->SBus_dev->my_bus) && \
         (esp->edev->slot == dma->SBus_dev->slot) && \
	 (!strcmp(dma->SBus_dev->prom_name, "dma") || \
	  !strcmp(dma->SBus_dev->prom_name, "espdma")))

			esp_node = esp_dev->prom_node;
			prom_getstring(esp_node, "name", esp->prom_name,
				       sizeof(esp->prom_name));
			esp->prom_node = esp_node;
			for_each_dvma(dlink) {
				if(ESP_IS_MY_DVMA(esp, dlink) && !dlink->allocated)
					break;
			}
#undef ESP_IS_MY_DVMA
			/* If we don't know how to handle the dvma, do not use this device */
			if(!dlink){
				printk ("Cannot find dvma for ESP SCSI\n");
				scsi_unregister (esp_host);
				continue;
			}
			if (dlink->allocated){
				printk ("esp: can't use my espdma\n");
				scsi_unregister (esp_host);
				continue;
			}
			dlink->allocated = 1;
			dma = dlink;
			esp->dma = dma;
			esp->dregs = dregs = dma->regs;

			/* Map in the ESP registers from I/O space */
			prom_apply_sbus_ranges(esp->edev->reg_addrs, 1);
			esp->eregs = eregs = (struct Sparc_ESP_regs *)
				sparc_alloc_io(esp->edev->reg_addrs[0].phys_addr, 0,
					       PAGE_SIZE, "ESP Registers",
					       esp->edev->reg_addrs[0].which_io, 0x0);
			if(!eregs)
				panic("ESP registers unmappable");
			esp->esp_command =
				sparc_dvma_malloc(16, "ESP DVMA Cmd Block");
			if(!esp->esp_command)
				panic("ESP DVMA transport area unmappable");

			/* Set up the irq's etc. */
			esp->ehost->base = (unsigned char *) esp->eregs;
			esp->ehost->io_port = (unsigned int) esp->eregs;
			esp->ehost->n_io_port = (unsigned char)
				esp->edev->reg_addrs[0].reg_size;
			/* XXX The following may be different on sun4ms XXX */
			esp->ehost->irq = esp->irq = esp->edev->irqs[0].pri;

			/* Allocate the irq only if necessary */
			for_each_esp(elink) {
				if((elink != esp) && (esp->irq == elink->irq)) {
					goto esp_irq_acquired; /* BASIC rulez */
				}
			}
			/* XXX We have shared interrupts per level now, maybe
			 * XXX use them, maybe not...
			 */
			if(request_irq(esp->ehost->irq, esp_intr, SA_INTERRUPT,
				       "Sparc ESP SCSI", NULL))
				panic("Cannot acquire ESP irq line");
esp_irq_acquired:
			printk("esp%d: IRQ %d ", nesps, esp->ehost->irq);
			/* Figure out our scsi ID on the bus */
			esp->scsi_id = prom_getintdefault(esp->prom_node,
							  "initiator-id", -1);
			if(esp->scsi_id == -1)
				esp->scsi_id = prom_getintdefault(esp->prom_node,
								  "scsi-initiator-id", -1);
			if(esp->scsi_id == -1)
				esp->scsi_id =
					prom_getintdefault(esp->edev->my_bus->prom_node,
							   "scsi-initiator-id", 7);
			esp->ehost->this_id = esp->scsi_id;
			esp->scsi_id_mask = (1 << esp->scsi_id);
			/* Check for differential bus */
			esp->diff = prom_getintdefault(esp->prom_node, "differential", -1);
			esp->diff = (esp->diff == -1) ? 0 : 1;
			/* Check out the clock properties of the chip */
			fmhz = prom_getintdefault(esp->prom_node, "clock-frequency", -1);
			if(fmhz==-1)
				fmhz = prom_getintdefault(esp->edev->my_bus->prom_node,
							  "clock-frequency", -1);
			if(fmhz <= (5000))
				ccf = 0;
			else
				ccf = (((5000 - 1) + (fmhz))/(5000));
			if(!ccf || ccf > 8) {
				ccf = ESP_CCF_F4;
				fmhz = (5000 * 4);
			}
			if(ccf==(ESP_CCF_F7+1))
				esp->cfact = ESP_CCF_F0;
			else if(ccf == ESP_CCF_NEVER)
				esp->cfact = ESP_CCF_F2;
			else
				esp->cfact = ccf;
			esp->cfreq = fmhz;
			esp->ccycle = ((1000000000) / ((fmhz)/1000));
			esp->ctick = ((7682 * esp->cfact * esp->ccycle)/1000);
			esp->sync_defp = ((7682 + esp->ctick - 1) / esp->ctick);

			/* XXX HACK HACK HACK XXX */
			if (esp->sync_defp < 153)
				esp->sync_defp = 153;

			printk("SCSI ID %d  Clock %d MHz Period %2x ", esp->scsi_id,
			       (fmhz / 1000), esp->sync_defp);

			/* Find the burst sizes this dma supports. */
			bsizes = prom_getintdefault(esp->prom_node, "burst-sizes", 0xff);
			bsizes_more = prom_getintdefault(esp->edev->my_bus->prom_node,
							 "burst-sizes", 0xff);
			if(bsizes_more != 0xff) bsizes &= bsizes_more;
			if(bsizes == 0xff || (bsizes & DMA_BURST16)==0 ||
			   (bsizes & DMA_BURST32)==0)
				bsizes = (DMA_BURST32 - 1);
			esp->bursts = bsizes;

			/* Probe the revision of this esp */
			esp->config1 = (ESP_CONFIG1_PENABLE | (esp->scsi_id & 7));
			esp->config2 = (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY);
			esp->config3[0] = ESP_CONFIG3_TENB;
			eregs->esp_cfg2 = esp->config2;
			if((eregs->esp_cfg2 & ~(ESP_CONFIG2_MAGIC)) !=
			   (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY)) {
				printk("NCR53C90(esp100) detected\n");
				esp->erev = esp100;
			} else {
				eregs->esp_cfg2 = esp->config2 = 0;
				eregs->esp_cfg3 = 0;
				eregs->esp_cfg3 = esp->config3[0] = 5;
				if(eregs->esp_cfg3 != 5) {
					printk("NCR53C90A(esp100a) detected\n");
					esp->erev = esp100a;
				} else {
					int target;

					for(target=0; target<8; target++)
						esp->config3[target] = 0;
					eregs->esp_cfg3 = 0;
					if(esp->cfact > ESP_CCF_F5) {
						printk("NCR53C9XF(espfast) detected\n");
						esp->erev = fast;
						esp->config2 |= ESP_CONFIG2_FENAB;
						eregs->esp_cfg2 = esp->config2;
					} else {
						printk("NCR53C9x(esp236) detected\n");
						esp->erev = esp236;
						eregs->esp_cfg2 = esp->config2 = 0;
					}
				}
			}				

			/* Initialize the command queues */
			esp->current_SC = 0;
			esp->disconnected_SC = 0;
			esp->issue_SC = 0;

			/* Reset the thing before we try anything... */
			esp_bootup_reset(esp, eregs);

			nesps++;
#ifdef THREADED_ESP_DRIVER
			kernel_thread(esp_kernel_thread, esp, 0);
#endif
		} /* for each sbusdev */
	} /* for each sbus */
	return nesps;
}

/*
 * The info function will return whatever useful
 * information the developer sees fit.  If not provided, then
 * the name field will be used instead.
 */
const char *esp_info(struct Scsi_Host *host)
{
	struct Sparc_ESP *esp;

	esp = (struct Sparc_ESP *) host->hostdata;
	switch(esp->erev) {
	case esp100:
		return "Sparc ESP100 (NCR53C90)";
	case esp100a:
		return "Sparc ESP100A (NCR53C90A)";
	case esp236:
		return "Sparc ESP236";
	case fast:
		return "Sparc ESP-FAST (236 or 100A)";
	case fas236:
		return "Sparc ESP236-FAST";
	case fas100a:
		return "Sparc ESP100A-FAST";
	default:
		panic("Bogon ESP revision");
	};
}

/* Execute a SCSI command when the bus is free.   All callers
 * turn off all interrupts, so we don't need to explicitly do
 * it here.
 */
static inline void esp_exec_cmd(struct Sparc_ESP *esp)
{
	struct sparc_dma_registers *dregs;
	struct Sparc_ESP_regs *eregs;
	Scsi_Cmnd *SCptr;
	int i;

	eregs = esp->eregs;
	dregs = esp->dregs;

	/* Grab first member of the issue queue. */
	SCptr = esp->current_SC = remove_first_SC(&esp->issue_SC);
	if(!SCptr)
		goto bad;
	SCptr->SCp.phase = in_selection;

	/* NCR docs say:
	 * 1) Load select/reselect Bus ID register with target ID
	 * 2) Load select/reselect Timeout Reg with desired value
	 * 3) Load Synchronous offset register with zero (for
	 *    asynchronous transfers).
	 * 4) Load Synchronous Transfer Period register (if
	 *    synchronous)
	 * 5) Load FIFO with 6, 10, or 12 byte SCSI command
	 * 6) Issue SELECTION_WITHOUT_ATTENTION command
	 *
	 * They also mention that a DMA NOP command must be issued
	 * to the SCSI chip under many circumstances, plus it's
	 * also a good idea to flush out the fifo just in case.
	 */

	/* Load zeros into COUNTER via 2 DMA NOP chip commands
	 * due to flaky implementations of the 53C9x which don't
	 * get the idea the first time around.
	 */
	dregs->cond_reg = (DMA_INT_ENAB | DMA_FIFO_INV);

	eregs->esp_tclow = 0;
	eregs->esp_tcmed = 0;
	eregs->esp_cmd   = (ESP_CMD_NULL | ESP_CMD_DMA);

	/* Flush the fifo of excess garbage. */
	eregs->esp_cmd   = ESP_CMD_FLUSH;

	/* Load bus-id and timeout values. */
	eregs->esp_busid = (SCptr->target & 7);
	eregs->esp_timeo = esp->sync_defp;

	eregs->esp_soff  = 0; /* This means async transfer... */
	eregs->esp_stp   = 0;

	/* Load FIFO with the actual SCSI command. */
	for(i=0; i < SCptr->cmd_len; i++)
		eregs->esp_fdata = SCptr->cmnd[i];

	/* Make sure the dvma forwards the ESP interrupt. */
	dregs->cond_reg = DMA_INT_ENAB;

	/* Tell ESP to SELECT without asserting ATN. */
	eregs->esp_cmd = ESP_CMD_SEL;
	return;

bad:
	panic("esp: daaarrrkk starrr crashesss....");
}

/* Queue a SCSI command delivered from the mid-level Linux SCSI code. */
int esp_queue(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
	struct Sparc_ESP *esp;
	unsigned long flags;

	save_flags(flags); cli();

	/* Set up func ptr and initial driver cmd-phase. */
	SCpnt->scsi_done = done;
	SCpnt->SCp.phase = not_issued;

	esp = (struct Sparc_ESP *) SCpnt->host->hostdata;

	/* We use the scratch area. */
	if(!SCpnt->use_sg) {
		SCpnt->SCp.this_residual    = SCpnt->request_bufflen;
		SCpnt->SCp.buffer           =
			(struct scatterlist *) SCpnt->request_buffer;
		SCpnt->SCp.buffers_residual = 0;
		SCpnt->SCp.Status           = CHECK_CONDITION;
		SCpnt->SCp.Message          = 0;
		SCpnt->SCp.have_data_in     = 0;
		SCpnt->SCp.sent_command     = 0;
		SCpnt->SCp.ptr = mmu_get_scsi_one((char *)SCpnt->SCp.buffer,
						     SCpnt->SCp.this_residual,
						     esp->edev->my_bus);
	} else {
#ifdef DEBUG_ESP_SG
		printk("esp: sglist at %p with %d buffers\n",
		       SCpnt->buffer, SCpnt->use_sg);
#endif
		SCpnt->SCp.buffer           = (struct scatterlist *) SCpnt->buffer;
		SCpnt->SCp.buffers_residual = SCpnt->use_sg - 1;
		SCpnt->SCp.this_residual    = SCpnt->SCp.buffer->length;
		mmu_get_scsi_sgl((struct mmu_sglist *) SCpnt->SCp.buffer,
				 SCpnt->SCp.buffers_residual,
				 esp->edev->my_bus);
		SCpnt->SCp.ptr              = (char *) SCpnt->SCp.buffer->alt_address;
	}

	/* Place into our queue. */
	append_SC(&esp->issue_SC, SCpnt);

	/* Run it now if we can */
	if(!esp->current_SC)
		esp_exec_cmd(esp);

	restore_flags(flags);
	return 0;
}

/* Only queuing supported in this ESP driver. */
int esp_command(Scsi_Cmnd *SCpnt)
{
	ESPLOG(("esp: esp_command() called...\n"));
	return -1;
}

/* Abort a command.  Those that are on the bus force a SCSI bus
 * reset.
 */
int esp_abort(Scsi_Cmnd *SCpnt)
{
	ESPLOG(("esp_abort: Not implemented yet\n"));
	return SCSI_ABORT_ERROR;
}

/* Reset ESP chip, reset hanging bus, then kill active and
 * disconnected commands for targets without soft reset.
 */
int esp_reset(Scsi_Cmnd *SCptr, unsigned int how)
{
	ESPLOG(("esp_reset: Not implemented yet\n"));
	return SCSI_RESET_ERROR;
}

/* Internal ESP done function. */
static inline void esp_done(struct Sparc_ESP *esp, int error)
{
	unsigned long flags;
	Scsi_Cmnd *done_SC;

	if(esp->current_SC) {
		/* Critical section... */
		save_flags(flags); cli();
		done_SC = esp->current_SC;
		esp->current_SC = NULL;
		/* Free dvma entry. */
		if(!done_SC->use_sg) {
			mmu_release_scsi_one(done_SC->SCp.ptr,
						done_SC->SCp.this_residual,
						esp->edev->my_bus);
		} else {
			struct scatterlist *scl = (struct scatterlist *)done_SC->buffer;
#ifdef DEBUG_ESP_SG
			printk("esp: unmapping sg ");
#endif
			mmu_release_scsi_sgl((struct mmu_sglist *) scl,
					     done_SC->use_sg - 1,
					     esp->edev->my_bus);
#ifdef DEBUG_ESP_SG
			printk("done.\n");
#endif
		}
		done_SC->result = error;
		if(done_SC->scsi_done)
			done_SC->scsi_done(done_SC);
		else
			panic("esp: esp->current_SC->scsi_done() == NULL");

		/* Bus is free, issue any commands in the queue. */
		if(esp->issue_SC)
			esp_exec_cmd(esp);

		restore_flags(flags);
		/* End of critical section... */
	} else
		panic("esp: done() called with NULL esp->current_SC");
}

#ifdef THREADED_ESP_DRIVER /* planning stage... */

/* With multiple lots of commands being processed I frequently
 * see a situation where we see galloping esp herds.  esp_done()
 * wakes the entire world up and each interrupt causes a reschedule.
 * This kernel thread fixes some of these unwanted effects during
 * IO intensive activity.... I hope...
 */

static void esp_kernel_thread(void *opaque)
{
	struct Sparc_ESP *esp = opaque;

	for(;;) {
		unsigned long flags;

		while(esp->eatme_SC) {
			struct Scsi_Cmnd *SCpnt;

			SCpnt = remove_first_SC(esp->eatme_SC);
			esp_done(esp, error, SCpnt);
		}
		sleep();
	}
}
#endif

/* Read the interrupt status registers on this ESP board */
static inline void esp_updatesoft(struct Sparc_ESP *esp, struct Sparc_ESP_regs *eregs)
{
	/* Update our software copies of the three ESP status
	 * registers for this ESP.  Be careful, reading the
	 * ESP interrupt register clears the status and sequence
	 * step registers (unlatches them, you get the idea).
	 * So read the interrupt register last.
	 */

	esp->seqreg = eregs->esp_sstep;
	esp->sreg = eregs->esp_status;

	/* Supposedly, the ESP100A and above assert the highest
	 * bit in the status register if an interrupt is pending.
	 * I've never seen this work properly, so let's clear it
	 * manually while we are here.  If I see any esp chips
	 * for which this bit is reliable I will conditionalize
	 * this.  However, I don't see what this extra bit can
	 * buy me with all the tests I'll have to place all over
	 * the code to actually use it when I 'can'.  Plus the
	 * 'pending interrupt' condition can more than reliably
	 * be obtained from the DVMA control register.
	 *
	 * "Broken hardware"  -Linus
	 */
	esp->sreg &= (~ESP_STAT_INTR);
	esp->ireg = eregs->esp_intrpt;   /* Must be last or we lose */
}

/* #define ESP_IRQ_TRACE */

#ifdef ESP_IRQ_TRACE
#define ETRACE(foo)  printk foo
#else
#define ETRACE(foo)
#endif

static char last_fflags, last_status, last_msg;

/* Main interrupt handler for an esp adapter. */
static inline void esp_handle(struct Sparc_ESP *esp)
{
	struct sparc_dma_registers *dregs;
	struct Sparc_ESP_regs *eregs;
	Scsi_Cmnd *SCptr;

	eregs = esp->eregs;
	dregs = esp->dregs;
	SCptr = esp->current_SC;

	DMA_IRQ_ENTRY(esp->dma, dregs);
	esp_updatesoft(esp, eregs);

	ETRACE(("ESPIRQ: <%2x,%2x,%2x> --> ", esp->ireg, esp->sreg, esp->seqreg));

	/* Check for errors. */
	if(!SCptr)
		panic("esp_handle: current_SC == penguin within interrupt!");

	/* At this point in time, this esp driver should not see
	 * scsibus resets, parity errors, or gross errors unless
	 * something truly terrible happens which we are not ready
	 * to properly recover from yet.
	 */
	if((esp->ireg & (ESP_INTR_SR | ESP_INTR_IC)) ||
	   (esp->sreg & (ESP_STAT_PERR | ESP_STAT_SPAM))) {
		printk("esp: really bad error detected\n");
		printk("esp: intr<%2x> stat<%2x> seq<%2x>",
		       esp->ireg, esp->sreg, esp->seqreg);
		printk("esp: SCptr->SCp.phase = %d\n", SCptr->SCp.phase);
		panic("esp: cannot continue\n");
	}
	if(dregs->cond_reg & DMA_HNDL_ERROR) {
		printk("esp: DMA shows an error cond_reg<%08lx> addr<%p>\n",
		       dregs->cond_reg, dregs->st_addr);
		printk("esp: intr<%2x> stat<%2x> seq<%2x>",
		       esp->ireg, esp->sreg, esp->seqreg);
		printk("esp: SCptr->SCp.phase = %d\n", SCptr->SCp.phase);
		panic("esp: cannot continue\n");
	}
	if(esp->sreg & ESP_STAT_PERR) {
		printk("esp: SCSI bus parity error\n");
		printk("esp: intr<%2x> stat<%2x> seq<%2x>",
		       esp->ireg, esp->sreg, esp->seqreg);
		printk("esp: SCptr->SCp.phase = %d\n", SCptr->SCp.phase);
		panic("esp: cannot continue\n");
	}

	/* Service interrupt. */
	switch(SCptr->SCp.phase) {
	case not_issued:
		panic("Unexpected ESP interrupt, current_SC not issued.");
		break;
	case in_selection:
		if(esp->ireg & ESP_INTR_RSEL) {
			/* XXX Some day XXX */
			panic("ESP penguin reselected in async mode.");
		} else if(esp->ireg & ESP_INTR_DC) {
			/* Either we are scanning the bus and no-one
			 * lives at this target or it didn't respond.
			 */
			ETRACE(("DISCONNECT\n"));
#ifdef THREADED_ESP_DRIVER
			append_SC(esp->eatme_SC, esp->current_SC);
			esp->current_SC = 0;
			wake_up(esp_kernel_thread);
#else
			esp_done(esp, (DID_NO_CONNECT << 16));
#endif
			goto esp_handle_done;
		} else if((esp->ireg & (ESP_INTR_FDONE | ESP_INTR_BSERV)) ==
			  (ESP_INTR_FDONE | ESP_INTR_BSERV)) {
			/* Selection successful, check the sequence step. */
			/* XXX I know, I know... add error recovery.  XXX */
			switch(esp->seqreg & ESP_STEP_VBITS) {
			case ESP_STEP_NCMD:
				panic("esp: penguin didn't enter cmd phase.");
				break;
			case ESP_STEP_PPC:
				panic("esp: penguin prematurely changed from cmd phase.");
				break;
			case ESP_STEP_FINI:
				/* At the completion of every command
				 * or message-out phase, we _must_
				 * unlatch the fifo-flags register
				 * with an ESP nop command.
				 */
				eregs->esp_cmd = ESP_CMD_NULL;

				/* Selection/Command sequence completed.  We
				 * (at least for this driver) will be in
				 * either one of the data phases or status
				 * phase, check the status register to find
				 * out.
				 */
				switch(esp->sreg & ESP_STAT_PMASK) {
				default:
					printk("esp: Not datain/dataout/status.\n");
					panic("esp: penguin phase transition after selection.");
					break;
				case ESP_DOP:
					/* Data out phase. */
					dregs->cond_reg |= DMA_FIFO_INV;
					while(dregs->cond_reg & DMA_FIFO_ISDRAIN)
						barrier();
					SCptr->SCp.phase = in_dataout;
#ifdef DEBUG_ESP_SG
					if(SCptr->use_sg)
						printk("esp: sg-start <%p,%d>",
						       SCptr->SCp.ptr,
						       SCptr->SCp.this_residual);
#endif
					eregs->esp_tclow = SCptr->SCp.this_residual;
					eregs->esp_tcmed = (SCptr->SCp.this_residual>>8);
					eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_NULL);

					/* This is either the one buffer dvma ptr,
					 * or the first one in the scatter gather
					 * list.  Check out esp_queue to see how
					 * this is set up.
					 */
					dregs->st_addr = SCptr->SCp.ptr;
					dregs->cond_reg &= ~(DMA_ST_WRITE);
					dregs->cond_reg |= (DMA_ENABLE | DMA_INT_ENAB);
					eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_TI);
					ETRACE(("DATA_OUT\n"));
					goto esp_handle_done;
				case ESP_DIP:
					/* Data in phase. */
					dregs->cond_reg |= DMA_FIFO_INV;
					while(dregs->cond_reg & DMA_FIFO_ISDRAIN)
						barrier();
					SCptr->SCp.phase = in_datain;
#ifdef DEBUG_ESP_SG
					if(SCptr->use_sg)
						printk("esp: sg-start <%p,%d>",
						       SCptr->SCp.ptr,
						       SCptr->SCp.this_residual);
#endif
					eregs->esp_tclow = SCptr->SCp.this_residual;
					eregs->esp_tcmed = (SCptr->SCp.this_residual>>8);
					eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_NULL);

					/* This is either the one buffer dvma ptr,
					 * or the first one in the scatter gather
					 * list.  Check out esp_queue to see how
					 * this is set up.
					 */
					dregs->st_addr = SCptr->SCp.ptr;
					dregs->cond_reg |= (DMA_ENABLE | DMA_ST_WRITE | DMA_INT_ENAB);
					eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_TI);
					ETRACE(("DATA_IN\n"));
					goto esp_handle_done;
				case ESP_STATP:
					/* Status phase. */
					SCptr->SCp.phase = in_status;
					eregs->esp_cmd = ESP_CMD_ICCSEQ;
					ETRACE(("STATUS\n"));
					goto esp_handle_done; /* Wait for message. */
				};
			};
		} else if(esp->ireg & ESP_INTR_FDONE) {
			/* I'd like to investigate why this happens... */
			ESPLOG(("esp: This is weird, halfway through "));
			ESPLOG(("selection, trying to continue anyways.\n"));
			goto esp_handle_done;
		} else {
			panic("esp: Did not get bus service during selection.");
			goto esp_handle_done;
		}
		panic("esp: Mr. Potatoe Head is on the loose!");

	case in_datain:
		/* Drain the fifo for writes to memory. */
		switch(esp->dma->revision) {
		case dvmarev0:
		case dvmarev1:
		case dvmarevplus:
		case dvmarev2:
		case dvmarev3:
			/* Force a drain. */
			dregs->cond_reg |= DMA_FIFO_STDRAIN;

			/* fall through */
		case dvmaesc1:
			/* Wait for the fifo to drain completely. */
			while(dregs->cond_reg & DMA_FIFO_ISDRAIN)
				barrier();
			break;
		};

	case in_dataout:
		dregs->cond_reg &= ~DMA_ENABLE;

		/* We may be pipelining an sg-list. */
		if(SCptr->use_sg) {
			if(SCptr->SCp.buffers_residual) {
				/* If we do not see a BUS SERVICE interrupt
				 * at this point, or we see that we have left
				 * the current data phase, then we lose.
				 */
				if(!(esp->ireg & ESP_INTR_BSERV) ||
				   ((esp->sreg & ESP_STAT_PMASK) > 1))
					panic("esp: Aiee penguin on the SCSI-bus.");

				++SCptr->SCp.buffer;
				--SCptr->SCp.buffers_residual;
				SCptr->SCp.this_residual = SCptr->SCp.buffer->length;
				SCptr->SCp.ptr = SCptr->SCp.buffer->alt_address;

#ifdef DEBUG_ESP_SG
				printk("<%p,%d> ", SCptr->SCp.ptr,
				       SCptr->SCp.this_residual);
#endif

				/* Latch in new esp counters... */
				eregs->esp_tclow = SCptr->SCp.this_residual;
				eregs->esp_tcmed = (SCptr->SCp.this_residual>>8);
				eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_NULL);

				/* Reload DVMA gate array with new vaddr and enab. */
				dregs->st_addr = SCptr->SCp.ptr;
				dregs->cond_reg |= DMA_ENABLE;

				/* Tell the esp to start transferring. */
				eregs->esp_cmd = (ESP_CMD_DMA | ESP_CMD_TI);
				goto esp_handle_done;
			}
#ifdef DEBUG_ESP_SG
			printk("done.\n");
#endif
		}
		/* Take a look at what happened. */
		if(esp->ireg & ESP_INTR_DC) {
			panic("esp: target disconnects during data transfer.");
			goto esp_handle_done;
		} else if(esp->ireg & ESP_INTR_BSERV) {
			if((esp->sreg & ESP_STAT_PMASK) != ESP_STATP) {
				panic("esp: Not status phase after data phase.");
				goto esp_handle_done;
			}
			SCptr->SCp.phase = in_status;
			eregs->esp_cmd = ESP_CMD_ICCSEQ;
			ETRACE(("STATUS\n"));
			goto esp_handle_done; /* Wait for message. */
		} else {
			printk("esp: did not get bus service after data transfer.");
			printk("esp_status: intr<%2x> stat<%2x> seq<%2x>\n",
			       esp->ireg, esp->sreg, esp->seqreg);
			panic("esp: penguin data transfer.");
			goto esp_handle_done;
		}
	case in_status:
		if(esp->ireg & ESP_INTR_DC) {
			panic("esp: penguin disconnects in status phase.");
			goto esp_handle_done;
		} else if (esp->ireg & ESP_INTR_FDONE) {
			/* Status and Message now sit in the fifo for us. */
			last_fflags = eregs->esp_fflags;
			SCptr->SCp.phase   = in_finale;
			last_status = SCptr->SCp.Status  = eregs->esp_fdata;
			last_msg = SCptr->SCp.Message = eregs->esp_fdata;
			eregs->esp_cmd = ESP_CMD_MOK;
			ETRACE(("FINALE\n"));
			goto esp_handle_done;
		} else {
			panic("esp: penguin status phase.");
		}
	case in_finale:
		if(esp->ireg & ESP_INTR_BSERV) {
			panic("esp: penguin doesn't disconnect after status msg-ack.");
			goto esp_handle_done;
		} else if(esp->ireg & ESP_INTR_DC) {
			/* Nexus is complete. */
#ifdef THREADED_ESP_DRIVER
			append_SC(esp->eatme_SC, esp->current_SC);
			esp->current_SC = 0;
			wake_up(esp_kernel_thread);
#else
			esp_done(esp, ((SCptr->SCp.Status & 0xff) |
				 ((SCptr->SCp.Message & 0xff) << 8) |
				 (DID_OK << 16)));
#endif
			ETRACE(("NEXUS_COMPLETE\n"));
			goto esp_handle_done;
		} else {
			printk("esp: wacky state while in in_finale phase.\n");
			printk("esp_status: intr<%2x> stat<%2x> seq<%2x>\n",
			       esp->ireg, esp->sreg, esp->seqreg);
			panic("esp: penguin esp state.");
			goto esp_handle_done;
		}
	default:
		panic("esp: detected penguin phase.");
		goto esp_handle_done;
	}
	panic("esp: Heading to the promised land.");

esp_handle_done:
	DMA_IRQ_EXIT(esp->dma, dregs);
	return;
}

static void esp_intr(int irq, void *dev_id, struct pt_regs *pregs)
{
	struct Sparc_ESP *esp;

	/* Handle all ESP interrupts showing */
	for_each_esp(esp) {
		if(DMA_IRQ_P(esp->dregs)) {
			esp_handle(esp);
		}
	}
}
