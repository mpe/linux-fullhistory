/************************************************************
 *                                                          *
 *                  Linux EATA SCSI driver                  *
 *                                                          *
 *  based on the CAM document CAM/89-004 rev. 2.0c,         *
 *  DPT's driver kit, some internal documents and source,   *
 *  and several other Linux scsi drivers and kernel docs.   *
 *                                                          *
 *  The driver currently:                                   *
 *      -supports all ISA based EATA-DMA boards             *
 *      -supports all EISA based EATA-DMA boards            *
 *      -supports all PCI based EATA-DMA boards             *
 *      -supports multiple HBAs with & without IRQ sharing  *
 *      -supports all SCSI channels on multi channel boards *
 *      -displays (more or less useful) infos in /proc/scsi *
 *      -can be loaded as module                            *
 *                                                          *
 *  (c)1993,94,95 Michael Neuffer                           *
 *                neuffer@goofy.zdv.uni-mainz.de            *
 *                                                          *
 *  This program is free software; you can redistribute it  *
 *  and/or modify it under the terms of the GNU General     *
 *  Public License as published by the Free Software        *
 *  Foundation; either version 2 of the License, or         *
 *  (at your option) any later version.                     *
 *                                                          *
 *  This program is distributed in the hope that it will be *
 *  useful, but WITHOUT ANY WARRANTY; without even the      *
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A    *
 *  PARTICULAR PURPOSE.  See the GNU General Public License *
 *  for more details.                                       *
 *                                                          *
 *  You should have received a copy of the GNU General      *
 *  Public License along with this kernel; if not, write to *
 *  the Free Software Foundation, Inc., 675 Mass Ave,       *
 *  Cambridge, MA 02139, USA.                               *
 *                                                          *
 * I have to thank DPT for their excellent support. I took  *
 * me almost a year and a stopover at their HQ, on my first *
 * trip to the USA, to get it, but since then they've been  *
 * very helpful and tried to give me all the infos and      *
 * support I need.                                          *
 *                                                          *
 * Thanks also to Greg Hosler who did a lot of testing and  *
 * found quite a number of bugs during the development.     *
 ************************************************************
 *  last change: 95/04/10       OS: Linux 1.2.00 or higher  *
 ************************************************************/

/* Look in eata_dma.h for configuration and revision information */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif
 
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/in.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/dma.h>
#include "../block/blk.h"
#include "scsi.h"
#include "sd.h"
#include "hosts.h"
#include <linux/scsicam.h>
#include "eata_dma.h"

#if EATA_DMA_PROC  
#include "eata_dma_proc.h"  /* If you're interested send me a mail */ 
ulong  reads[13];           /* /proc/scsi probably won't get       */ 
ulong  writes[13];          /* into the kernel before pl. 1.3      */
#endif

static uint ISAbases[] =
{0x1F0, 0x170, 0x330, 0x230};
static unchar EISAbases[] =
{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static uint registered_HBAs = 0;
static struct Scsi_Host *last_HBA = NULL;
static struct Scsi_Host *first_HBA = NULL;
static unchar reg_IRQ[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static unchar reg_IRQL[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static struct eata_sp *status = 0;   /* Statuspacket array   */
static void *dma_scratch = 0;

static uint internal_command_finished = TRUE;
static unchar HBA_interpret = FALSE;
static u32 fake_int_base;
static u32 fake_int_result;
static struct geom_emul geometry;	/* Drive 1 & 2 geometry */

static ulong int_counter = 0;
static ulong queue_counter = 0;

void eata_scsi_done (Scsi_Cmnd * SCpnt)
{
    return;
}	

void eata_fake_int_handler(s32 irq, struct pt_regs * regs)
{
    fake_int_result = inb(fake_int_base + HA_RSTATUS);
    DBG(DBG_INTR3, printk("eata_fake_int_handler called irq%ld base %#lx res %#lx\n", 
			  irq, fake_int_base, fake_int_result));
    return;
}

#if EATA_DMA_PROC 
#include "eata_dma_proc.c"
#endif

int eata_release(struct Scsi_Host *sh)
{
  if (sh->irq && reg_IRQ[sh->irq] == 1) free_irq(sh->irq);
  else reg_IRQ[sh->irq]--;
    
    scsi_init_free((void *)status, 512);
    
  if (SD(sh)->channel == 0) {
      if (sh->dma_channel != 0xff) free_dma(sh->dma_channel);
      if (sh->io_port && sh->n_io_port)
	  release_region(sh->io_port, sh->n_io_port);
  }
  return(TRUE);
}

const char *eata_info(struct Scsi_Host *host)
{
    static char *information = "EATA SCSI HBA Driver";
    return information;
}

void eata_int_handler(int irq, struct pt_regs * regs)
{
    uint i, result = 0;
    uint hba_stat, scsi_stat, eata_stat;
    Scsi_Cmnd *cmd;
    struct eata_ccb *cp;
    struct eata_sp *sp;
    uint base;
    ulong flags;
    uint x;
    struct Scsi_Host *sh;

    save_flags(flags);
    cli();

    for (x = 1, sh = first_HBA; x <= registered_HBAs; x++, sh = SD(sh)->prev) {
        if (sh->irq != irq)
	    continue;
        if (!(inb((uint)sh->base + HA_RAUXSTAT) & HA_AIRQ))
	    continue;

	int_counter++;

	sp=&SD(sh)->sp;

	cp = sp->ccb;
	cmd = cp->cmd;
	base = (uint) cmd->host->base;

	hba_stat = sp->hba_stat;

 	scsi_stat = (sp->scsi_stat >> 1) && 0x1f; 

	if (sp->EOC == FALSE) {
	    eata_stat = inb(base + HA_RSTATUS);
	    printk("eata_dma: int_handler, board: %x cmd %lx returned "
		   "unfinished.\nEATA: %x HBA: %x SCSI: %x spadr %lx spadrirq "
		   "%lx, irq%d\n", base, (long)cp, eata_stat, hba_stat, 
		   scsi_stat,(long)&status, (long)&status[irq], irq);
	    DBG(DBG_DELAY,DEL2(800));
	    restore_flags(flags);
	    return;
	} 

	if (cp->status == LOCKED) {
	    cp->status = FREE;
	    eata_stat = inb(base + HA_RSTATUS);
	    printk("eata_dma: int_handler, freeing locked queueslot\n");
	    DBG(DBG_INTR&&DBG_DELAY,DEL2(800));
	    restore_flags(flags);
	    return;
	}

	eata_stat = inb(base + HA_RSTATUS);	
	DBG(DBG_INTR, printk("IRQ %d received, base %#.4x, pid %ld, target: "
			     "%x, lun: %x, ea_s: %#.2x, hba_s: %#.2x \n", 
			     irq, base, cmd->pid, cmd->target, cmd->lun, 
			     eata_stat, hba_stat));

	switch (hba_stat) {
	case HA_NO_ERROR:       /* NO Error */
	    if (scsi_stat == CONDITION_GOOD
		&& cmd->device->type == TYPE_DISK
		&& (HD(cmd)->t_state[cmd->target] == RESET))
	        result = DID_BUS_BUSY << 16;	    
            else if (scsi_stat == GOOD)
	      HD(cmd)->t_state[cmd->target] = FALSE;
	    else if (scsi_stat == CHECK_CONDITION
                     && cmd->device->type == TYPE_DISK
                     && (cmd->sense_buffer[2] & 0xf) == RECOVERED_ERROR)
	      result = DID_BUS_BUSY << 16;
	    else
	        result = DID_OK << 16;
	    HD(cmd)->t_timeout[cmd->target] = FALSE;
	    break;
	case HA_ERR_SEL_TO:		/* Selection Timeout */
	    result = DID_BAD_TARGET << 16;  
	    break;
	case HA_ERR_CMD_TO:		/* Command Timeout   */
	    if (HD(cmd)->t_timeout[cmd->target] > 1)
		result = DID_ERROR << 16;
	    else {
		result = DID_TIME_OUT << 16;
		HD(cmd)->t_timeout[cmd->target]++;
	    }
	    break;
        case HA_ERR_RESET:              /* SCSI Bus Reset Received */
        case HA_INIT_POWERUP:           /* Initial Controller Power-up */
	    if (cmd->device->type != TYPE_TAPE)
		result = DID_BUS_BUSY << 16;
	    else
		result = DID_ERROR << 16;

	    for (i = 0; i < MAXTARGET; i++)
		HD(cmd)->t_state[i] = RESET;
	    break;
        case HA_UNX_BUSPHASE:       /* Unexpected Bus Phase */
        case HA_UNX_BUS_FREE:       /* Unexpected Bus Free */
        case HA_BUS_PARITY:         /* Bus Parity Error */
        case HA_SCSI_HUNG:          /* SCSI Hung */
        case HA_UNX_MSGRJCT:        /* Unexpected Message Reject */
        case HA_RESET_STUCK:        /* SCSI Bus Reset Stuck */
        case HA_RSENSE_FAIL:        /* Auto Request-Sense Failed */
        case HA_PARITY_ERR:         /* Controller Ram Parity */
	default:
	    result = DID_ERROR << 16;
	    break;
	}
	cmd->result = result | (scsi_stat << 1); 

#if DBG_INTR2
	if (scsi_stat || result || hba_stat || eata_stat != 0x50) 
	    printk("eata_stat: %#x hba_stat: %#.2x,scsi_stat: %#.2x, "
		   "sense_key: %#x, result: %#.8x\n", eata_stat, hba_stat, 
		 scsi_stat,cmd->sense_buffer[2] & 0xf, cmd->result); 
	DBG(DBG_INTR&&DBG_DELAY,DEL2(800));
#endif

	cp->status = FREE;   /* now we can release the slot  */
 
	restore_flags(flags);
	if(cmd->scsi_done != eata_scsi_done) cmd->scsi_done(cmd);
	else {
	    internal_command_finished = TRUE;
	    HBA_interpret = FALSE;
	}
	save_flags(flags);
	cli();
    }
    restore_flags(flags);

    return;
}

inline uint eata_send_command(ulong addr, uint base, unchar command)
{
    uint loop = R_LIMIT;

    while (inb(base + HA_RAUXSTAT) & HA_ABUSY)
        if (--loop == 0)
            return(FALSE);

    outb(addr & 0x000000ff, base + HA_WDMAADDR);
    outb((addr & 0x0000ff00) >> 8, base + HA_WDMAADDR + 1);
    outb((addr & 0x00ff0000) >> 16, base + HA_WDMAADDR + 2);
    outb((addr & 0xff000000) >> 24, base + HA_WDMAADDR + 3);
    outb(command, base + HA_WCOMMAND);
    return(TRUE);
}

int eata_queue(Scsi_Cmnd * cmd, void *(done) (Scsi_Cmnd *))
{
    uint i, x, y;
    long flags;

    hostdata *hd;
    struct Scsi_Host *sh;
    struct eata_ccb *cp;
    struct scatterlist *sl;

    save_flags(flags);
    cli();

    queue_counter++;

    if (done == (void *)eata_scsi_done) { 
        if (internal_command_finished == TRUE)
	    internal_command_finished = FALSE;
        else 
	    cmd->result = (DID_ERROR << 16) + QUEUE_FULL;
    }
    
    hd = HD(cmd);
    sh = cmd->host;
 
    /* check for free slot */
     for (y = hd->last_ccb + 1, x = 0; x < sh->can_queue; x++, y++) { 
	if (y >= sh->can_queue)
	    y = 0;
	if (hd->ccb[y].status == FREE)
	    break;
    }

    hd->last_ccb = y;

    if (x == sh->can_queue) { 

        DBG(DBG_QUEUE, printk("can_queue %d, x %d, y %d\n",sh->can_queue,x,y));
#if DEBUG_EATA
        panic("eata_dma: run out of queue slots cmdno:%ld intrno: %ld\n", 
	      queue_counter, int_counter);
#else
        panic("eata_dma: run out of queue slots....\n");
#endif
    }

    cp = &hd->ccb[y];

    memset(cp, 0, sizeof(struct eata_ccb));

    cp->status = USED;		/* claim free slot */
    
    DBG(DBG_QUEUE, printk("eata_queue pid %ld, target: %x, lun: %x, y %d\n",
			  cmd->pid, cmd->target, cmd->lun, y));
    DBG(DBG_QUEUE && DBG_DELAY, DEL2(250));
 
    cmd->scsi_done = (void *)done;

    switch (cmd->cmnd[0]) {
    case CHANGE_DEFINITION: case COMPARE:         case COPY:
    case COPY_VERIFY:       case LOG_SELECT:      case MODE_SELECT:
    case MODE_SELECT_10:    case SEND_DIAGNOSTIC: case WRITE_BUFFER:
    case FORMAT_UNIT:       case REASSIGN_BLOCKS: case RESERVE:
    case SEARCH_EQUAL:      case SEARCH_HIGH:     case SEARCH_LOW:
    case WRITE_6:           case WRITE_10:        case WRITE_VERIFY:
    case UPDATE_BLOCK:      case WRITE_LONG:      case WRITE_SAME:      
    case SEARCH_HIGH_12:    case SEARCH_EQUAL_12: case SEARCH_LOW_12:
    case WRITE_12:          case WRITE_VERIFY_12: case SET_WINDOW: 
    case MEDIUM_SCAN:       case SEND_VOLUME_TAG:            
    case 0xea:		/* alternate number for WRITE LONG */
	cp->DataOut = TRUE;	/* Output mode */
        break;
    case 0x00:
    default:
	cp->DataIn = TRUE;	/* Input mode  */
    }

    if (done == (void *) eata_scsi_done && HBA_interpret == TRUE) 
        cp->Interpret = TRUE;   /* Interpret command */
   
    if (cmd->use_sg) {
	cp->scatter = TRUE;	/* SG mode     */
	cp->cp_dataDMA = htonl((long)&cp->sg_list);
        cp->cp_datalen = htonl(cmd->use_sg*8);
	sl=(struct scatterlist *)cmd->request_buffer;
 
	for(i = 0; i < cmd->use_sg; i++, sl++){
	    cp->sg_list[i].data = htonl((ulong) sl->address);
	    cp->sg_list[i].len = htonl((ulong) sl->length);
  	}
    } else {
        cp->scatter = FALSE;
	cp->cp_datalen = htonl(cmd->request_bufflen);
	cp->cp_dataDMA = htonl((ulong)cmd->request_buffer);
    }

    cp->Auto_Req_Sen = TRUE;
    cp->cp_reqDMA = htonl((ulong) cmd->sense_buffer);
    cp->reqlen = sizeof(cmd->sense_buffer);

    cp->cp_id = cmd->target;
    cp->cp_lun = cmd->lun;
    cp->cp_dispri = TRUE;
    cp->cp_identify = TRUE;
    memcpy(cp->cp_cdb, cmd->cmnd, cmd->cmd_len);

    cp->cp_statDMA = htonl((ulong) &(hd->sp));

    cp->cp_viraddr = cp;
    cp->cmd = cmd;
    cmd->host_scribble = (char *)&hd->ccb[y];	

    if(eata_send_command((ulong) cp, (uint) sh->base, EATA_CMD_DMA_SEND_CP) == FALSE) {
      cmd->result = DID_ERROR << 16;
      printk("eata_queue target %d, pid %ld, HBA busy, returning DID_ERROR, done.\n",
              cmd->target, cmd->pid);
      restore_flags(flags);
      if(done != (void *)eata_scsi_done) done(cmd);
      return (0);
    }
    DBG(DBG_QUEUE,printk("Queued base %#.4lx pid: %ld target: %x lun: %x slot %d irq %d\n",
	   (long)sh->base, cmd->pid, cmd->target, cmd->lun, y, sh->irq));
    DBG(DBG_QUEUE && DBG_DELAY, DEL2(200));
    restore_flags(flags);
    return (0);
}

static volatile int internal_done_flag = 0;
static volatile int internal_done_errcode = 0;

static void internal_done(Scsi_Cmnd * cmd)
{
    internal_done_errcode = cmd->result;
    ++internal_done_flag;
}

int eata_command(Scsi_Cmnd * cmd)
{

    DBG(DBG_COM, printk("eata_command: calling eata_queue\n"));

    eata_queue(cmd, (void *)internal_done);

    while (!internal_done_flag);
    internal_done_flag = 0;
    return (internal_done_errcode);
}

int eata_abort(Scsi_Cmnd * cmd)
{
    ulong flags;
    uint loop = R_LIMIT;

    save_flags(flags);
    cli();

    DBG(DBG_ABNORM, printk("eata_abort called pid: %ld target: %x lun: %x reason %x\n",
			   cmd->pid, cmd->target, cmd->lun, cmd->abort_reason));
    DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 

    while (inb((uint)(cmd->host->base) + HA_RAUXSTAT) & HA_ABUSY)
        if (--loop == 0) {
	    printk("eata_dma: abort, timeout error.\n");
	    restore_flags(flags);
	    DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
	    return (SCSI_ABORT_ERROR);
	}
    if (CD(cmd)->status == FREE) {
        DBG(DBG_ABNORM, printk("Returning: SCSI_ABORT_NOT_RUNNING\n")); 
	restore_flags(flags);
	return (SCSI_ABORT_NOT_RUNNING);
    }
    if (CD(cmd)->status == USED) {
        DBG(DBG_ABNORM, printk("Returning: SCSI_ABORT_BUSY\n"));
 	restore_flags(flags);
	return (SCSI_ABORT_BUSY);  /* SNOOZE */ 
    }
    if (CD(cmd)->status == RESET) {
	restore_flags(flags);
        printk("eata_dma: abort, command reset error.\n");
	DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	return (SCSI_ABORT_ERROR);
    }
    if (CD(cmd)->status == LOCKED) {
	restore_flags(flags);
        DBG(DBG_ABNORM, printk("eata_dma: abort, queue slot locked.\n"));
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	return (SCSI_ABORT_NOT_RUNNING);
    } else
	panic("eata_dma: abort: invalid slot status\n");
}

int eata_reset(Scsi_Cmnd * cmd)
{
    uint x, z, time, limit = 0;
    uint loop = R_LIMIT;
    ulong flags;
    unchar success = FALSE;
    Scsi_Cmnd *sp; 

    save_flags(flags);
    cli();

    DBG(DBG_ABNORM, printk("eata_reset called pid:%ld target: %x lun: %x reason %x\n",
			   cmd->pid, cmd->target, cmd->lun, cmd->abort_reason));


    if (HD(cmd)->state == RESET) {
	printk("eata_reset: exit, already in reset.\n");
	restore_flags(flags);
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	return (SCSI_RESET_ERROR);
    }

    while (inb((uint)(cmd->host->base) + HA_RAUXSTAT) & HA_ABUSY)
        if (--loop == 0) {
 	    printk("eata_reset: exit, timeout error.\n");
	    restore_flags(flags);
	    DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
	    return (SCSI_RESET_ERROR);
	}
    for (z = 0; z < MAXTARGET; z++) {
	HD(cmd)->t_state[z] = RESET;
	HD(cmd)->t_timeout[z] = FALSE;
    }

    for (x = 0; x < cmd->host->can_queue; x++) {

	if (HD(cmd)->ccb[x].status == FREE)
	    continue;

	if (HD(cmd)->ccb[x].status == LOCKED) {
	    HD(cmd)->ccb[x].status = FREE;
	    printk("eata_reset: locked slot %d forced free.\n", x);
	    DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	    continue;
	}
	sp = HD(cmd)->ccb[x].cmd;
	HD(cmd)->ccb[x].status = RESET;
	printk("eata_reset: slot %d in reset, pid %ld.\n", x, sp->pid);
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 
	if (sp == NULL)
	    panic("eata_reset: slot %d, sp==NULL.\n", x);
            DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 
	if (sp == cmd)
	    success = TRUE;
    }

    /* hard reset the HBA  */
    inb((uint) (cmd->host->base) + HA_RSTATUS);  /* This might cause trouble */
    eata_send_command(0, (uint) cmd->host->base, EATA_CMD_RESET);

    DBG(DBG_ABNORM, printk("eata_reset: board reset done, enabling interrupts.\n"));
    HD(cmd)->state = RESET;

    restore_flags(flags);

    time = jiffies;
    while (jiffies < (time + 300) && limit++ < 10000000);

    save_flags(flags);
    cli();

    DBG(DBG_ABNORM, printk("eata_reset: interrupts disabled, loops %d.\n", limit));
    DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 
    for (x = 0; x < cmd->host->can_queue; x++) {

	/* Skip slots already set free by interrupt */
	if (HD(cmd)->ccb[x].status != RESET)
	    continue;

	sp = HD(cmd)->ccb[x].cmd;
	sp->result = DID_RESET << 16;

	/* This mailbox is still waiting for its interrupt */
	HD(cmd)->ccb[x].status = LOCKED;

	printk("eata_reset: slot %d locked, DID_RESET, pid %ld done.\n",
	    x, sp->pid);
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
	restore_flags(flags);
	sp->scsi_done(sp);
	cli();
    }

    HD(cmd)->state = FALSE;
    restore_flags(flags);

    if (success) {
	DBG(DBG_ABNORM, printk("eata_reset: exit, success.\n"));
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	return (SCSI_RESET_SUCCESS);
    } else {
	DBG(DBG_ABNORM, printk("eata_reset: exit, wakeup.\n"));
        DBG(DBG_ABNORM && DBG_DELAY, DEL2(500));
 	return (SCSI_RESET_PUNT);
    }
}

char * get_board_data(ulong base, uint irq, uint id)
{
    struct eata_ccb cp;
    struct eata_sp  sp;
    static char *buff;
    u32 i;

    buff = dma_scratch;

    memset(&cp, 0, sizeof(struct eata_ccb));
    memset(&sp, 0, sizeof(struct eata_sp));
    memset(buff, 0, 256);

    cp.DataIn = TRUE;     
    cp.Interpret = TRUE;   /* Interpret command */
 
    cp.cp_datalen = htonl(255);  
    cp.cp_dataDMA = htonl((s32)buff);
    cp.cp_viraddr = &cp;

    cp.cp_id = id;
    cp.cp_lun = 0;

    cp.cp_cdb[0] = INQUIRY;
    cp.cp_cdb[1] = 0;
    cp.cp_cdb[2] = 0;
    cp.cp_cdb[3] = 0;
    cp.cp_cdb[4] = 255;
    cp.cp_cdb[5] = 0;

    cp.cp_statDMA = htonl((ulong) &sp);

    fake_int_base = base;
    fake_int_result = 0;

    eata_send_command((u32) &cp, (u32) base, EATA_CMD_DMA_SEND_CP);
    
    i = jiffies + 300;
    while (!fake_int_result && jiffies <= i) 
        /* nothing */;
    
    DBG(DBG_INTR3, printk("fake_int_result: %#lx hbastat %#lx scsistat %#lx,"
			  " buff %p sp %p\n",
                          fake_int_result, (u32) (sp.hba_stat & 0x7f), 
			  (u32) sp.scsi_stat, buff, &sp));
    
    if (jiffies > i || (fake_int_result & 1))
        return (NULL);
    else
        return (buff);
}
    
int check_blink_state(long base)
{
    uint loops = 10;
    ulong blinkindicator = 0x42445054;
    ulong state = 0x12345678;
    ulong oldstate = 0;

    while ((loops--) && (state != oldstate)) {
	oldstate = state;
	state = inl((uint) base + 1);
    }

    DBG(DBG_BLINK, printk("Did Blink check. Status: %d\n",
			  (state == oldstate) && (state == blinkindicator)));

    if ((state == oldstate) && (state == blinkindicator))
        return(TRUE);
    else
        return (FALSE);
}

int get_conf_PIO(struct eata_register *base, struct get_conf *buf)
{
    ulong loop = R_LIMIT;
    ushort *p;

    u8  warning = FALSE;
    
    if(check_region((int) base, 9)) {
        if ((int)base == 0x1f0 || (int)base == 0x170) {
            warning = 1;
        } else
        return (FALSE);
    }
 
    memset(buf, 0, sizeof(struct get_conf));

    while (inb((uint) base + HA_RSTATUS) & HA_SBUSY)
	if (--loop == 0) 
	    return (FALSE);
       
    DBG(DBG_PIO && DBG_PROBE,printk("Issuing PIO READ CONFIG to HBA at %lx\n", 
				   (long)base));
    eata_send_command(0, (uint) base, EATA_CMD_PIO_READ_CONFIG);
    loop = R_LIMIT;
    for (p = (ushort *) buf; 
         (long)p <= ((long)buf + (sizeof(struct get_conf)/ 2)); p++) {
        while (!(inb((uint) base + HA_RSTATUS) & HA_SDRQ))
	    if (--loop == 0)
 		return (FALSE);
	loop = R_LIMIT;
	*p = inw((uint) base + HA_RDATA);
    }
    if (!(inb((uint) base + HA_RSTATUS) & HA_SERROR)) {	        /* Error ? */
        DBG(DBG_PIO&&DBG_PROBE, printk("\nSignature: %c%c%c%c\n", 
			      (char)buf->sig[0], (char)buf->sig[1], 
			      (char)buf->sig[2], (char)buf->sig[3]));

	if ((buf->sig[0] == 'E') && (buf->sig[1] == 'A')
	    && (buf->sig[2] == 'T') && (buf->sig[3] == 'A')) {
	    DBG(DBG_PIO&&DBG_PROBE, printk("EATA Controller found at %x "
		      "EATA Level: %x\n", (uint) base, (uint) (buf->version)));
	
	    while (inb((uint) base + HA_RSTATUS) & HA_SDRQ) 
	        inw((uint) base + HA_RDATA);
            if (warning == TRUE)
                printk("Warning: HBA with IO on 0x%p detected,\n"
                       "         this IO space is already allocated, probably by the IDE driver.\n"
                       "         This might lead to problems.", base);
 	    return (TRUE);
	} 
    } else {
        DBG(DBG_PROBE, printk("eata_dma: get_conf_PIO, error during transfer "
			      "for HBA at %lx\n", (long)base));
    }
    return (FALSE);
}

void print_config(struct get_conf *gc)
{
    printk("Please check values: (read config data)\n");
    printk("LEN: %d ver:%d OCS:%d TAR:%d TRNXFR:%d MORES:%d DMAS:%d\n",
	(uint) ntohl(gc->len), gc->version,
	gc->OCS_enabled, gc->TAR_support, gc->TRNXFR, gc->MORE_support,
	gc->DMA_support);
    printk("DMAV:%d HAAV:%d SCSIID0:%d ID1:%d ID2:%d QUEUE:%d SG:%d SEC:%d\n",
	gc->DMA_valid, gc->HAA_valid, gc->scsi_id[3], gc->scsi_id[2],
	gc->scsi_id[1], ntohs(gc->queuesiz), ntohs(gc->SGsiz), gc->SECOND);
    printk("IRQ:%d IRQT:%d DMAC:%d FORCADR:%d MCH:%d RIDQ:%d PCI:%d EISA:%d\n",
	gc->IRQ, gc->IRQ_TR, (8 - gc->DMA_channel) & 7, gc->FORCADR, 
	gc->MAX_CHAN, gc->ID_qest, gc->is_PCI, gc->is_EISA);
    DBG(DPT_DEBUG, DELAY(1400));
}

int register_HBA(long base, struct get_conf *gc, Scsi_Host_Template * tpnt)
{
    ulong size = 0;
    unchar dma_channel = 0;
    char *buff;
    uint i;
    struct Scsi_Host *sh;
    hostdata *hd;
    
    DBG(DBG_REGISTER, print_config(gc));

    if(gc->HAA_valid == FALSE || ntohl(gc->len) < 0x22) 
        gc->MAX_CHAN = 0;

    if (!reg_IRQ[gc->IRQ]) {	/* Interrupt already registered ? */
        if (!request_irq(gc->IRQ, (void *) eata_fake_int_handler, SA_INTERRUPT, "eata_dma")){
	    reg_IRQ[gc->IRQ] += (gc->MAX_CHAN+1);
	    if (!gc->IRQ_TR)
		reg_IRQL[gc->IRQ] = TRUE;	/* IRQ is edge triggered */
	} else {
	    printk("Couldn't allocate IRQ %d, Sorry.", gc->IRQ);
	    return (FALSE);
	}
    } else {			/* More than one HBA on this IRQ */
	if (reg_IRQL[gc->IRQ] == TRUE) {
	    printk("Can't support more than one HBA on this IRQ,\n"
		   "  if the IRQ is edge triggered. Sorry.\n");
	    return (FALSE);
	} else
	    reg_IRQ[gc->IRQ] += (gc->MAX_CHAN+1);
    }

    /* if gc->DMA_valid it must be an ISA HBA and we have to register it */
    dma_channel = 0xff;
    if (gc->DMA_valid) {
        if (request_dma(dma_channel = (8 - gc->DMA_channel) & 7, "eata_dma")) {
            printk("Unable to allocate DMA channel %d for ISA HBA at %#.4lx.\n",
                   dma_channel, base);
            reg_IRQ[gc->IRQ] -= (gc->MAX_CHAN+1);
            if (reg_IRQ[gc->IRQ] == 0)
                free_irq(gc->IRQ);
            if (!gc->IRQ_TR)
                reg_IRQL[gc->IRQ] = FALSE; 
            return (FALSE);
        }
    } 
    
    buff = get_board_data(base, gc->IRQ, gc->scsi_id[3]);

    if (buff == NULL) {
        if (gc->DMA_support == FALSE)
            printk("HBA at %#.4lx doesn't support DMA. Sorry\n", base);
        else
            printk("HBA at %#.4lx didn't react on INQUIRY. Sorry.\n", base);
        if (gc->DMA_valid) 
            free_dma(dma_channel);
        reg_IRQ[gc->IRQ] -= (gc->MAX_CHAN+1);
        if (reg_IRQ[gc->IRQ] == 0)
            free_irq(gc->IRQ);
        if (!gc->IRQ_TR)
            reg_IRQL[gc->IRQ] = FALSE; 
        return (FALSE);
    }
    
    if (gc->DMA_support == FALSE && buff != NULL)  
        printk("HBA %.12sat %#.4lx doesn't set the DMA_support flag correctly.\n",
               &buff[16], base);
    
    request_region(base, 9, "eata_dma"); /* We already checked the 
                                          * availability, so this could 
                                          * only fail if we're on 
					  * 0x1f0 or 0x170.
                                          */

    if(ntohs(gc->queuesiz) == 0) {
        gc->queuesiz = ntohs(64);
	    printk("Warning: Queue size had to be corrected.\n"
		   "This might be a PM2012 with a defective Firmware\n");
    }

    size = sizeof(hostdata) + ((sizeof(struct eata_ccb) * ntohs(gc->queuesiz))/
			       (gc->MAX_CHAN + 1));

    if (gc->MAX_CHAN) {
	printk("This is a multichannel HBA. Linux doesn't support them,\n");
	printk("so we'll try to register every channel as a virtual HBA.\n");
    }
    
    for (i = 0; i <= gc->MAX_CHAN; i++) {

	sh = scsi_register(tpnt, size);

	if(sh == NULL) {
	    if (gc->DMA_valid) 
	        free_dma(dma_channel);
	    reg_IRQ[gc->IRQ] -= 1;
	    if (reg_IRQ[gc->IRQ] == 0)
	        free_irq(gc->IRQ);
	    if (!gc->IRQ_TR)
	        reg_IRQL[gc->IRQ] = FALSE; 
	    return (FALSE);
	}

	hd = SD(sh);                   

	memset(hd->ccb, 0, (sizeof(struct eata_ccb) * ntohs(gc->queuesiz)) / 
	       (gc->MAX_CHAN + 1));
        memset(hd->reads, 0, sizeof(ulong) * 26); 
 
	strncpy(SD(sh)->vendor, &buff[8], 8);
	SD(sh)->vendor[8] = 0;
	strncpy(SD(sh)->name, &buff[16], 17);
	SD(sh)->name[17] = 0;
	SD(sh)->revision[0] = buff[32];
	SD(sh)->revision[1] = buff[33];
	SD(sh)->revision[2] = buff[34];
	SD(sh)->revision[3] = '.';
	SD(sh)->revision[4] = buff[35];
	SD(sh)->revision[5] = 0;
	switch (ntohl(gc->len)) {
	case 0x1c:
	    SD(sh)->EATA_revision = 'a';
	    break;
	case 0x1e:
	    SD(sh)->EATA_revision = 'b';
	    break;
	case 0x22:
	    SD(sh)->EATA_revision = 'c';
	    break;
	default:
	    SD(sh)->EATA_revision = '?';
 	}
	sh->base = (char *) base;
	sh->io_port = (ushort) base;
	sh->n_io_port = 9;
 	sh->irq = gc->IRQ;
	sh->dma_channel = dma_channel;
	sh->this_id = gc->scsi_id[3 - i];
	sh->can_queue = ntohs(gc->queuesiz) / (gc->MAX_CHAN + 1);

	if (gc->OCS_enabled == TRUE) {
	    sh->cmd_per_lun = sh->can_queue/C_P_L_DIV; 
#if 0   /* The memory management seems to be more stable now */
	    if (sh->cmd_per_lun > C_P_L_CURRENT_MAX)
	        sh->cmd_per_lun = C_P_L_CURRENT_MAX;
#endif
	} else {
	    sh->cmd_per_lun = 1;
	}
	sh->sg_tablesize = ntohs(gc->SGsiz);
	if (sh->sg_tablesize > SG_SIZE || sh->sg_tablesize == 0) {
	    sh->sg_tablesize = SG_SIZE;
	    if (ntohs(gc->SGsiz) == 0)
	        printk("Warning: SG size had to be corrected.\n"
		       "This might be a PM2012 with a defective Firmware\n");
	}

	hd->channel = i;

	if (buff[21] == '4')
	    hd->bustype = 'P';
	else if (buff[21] == '2')
	    hd->bustype = 'E';
	else
	    hd->bustype = 'I';

	if (gc->SECOND)
	    hd->primary = FALSE;
	else
	    hd->primary = TRUE;

	if (hd->bustype != 'I') {
	    sh->unchecked_isa_dma = FALSE;
	    sh->wish_block = FALSE;        
	}
	else {
	    sh->unchecked_isa_dma = TRUE;   /* We're doing ISA DMA */
	    sh->wish_block = TRUE;          /* This will reduce performance */
	}
	if((hd->primary == TRUE) && (i == 0) && HARDCODED){                  
	  geometry.drv[0].heads = HEADS0;          
	  geometry.drv[0].sectors = SECTORS0;      
	  geometry.drv[0].cylinder = CYLINDER0;
	  geometry.drv[0].id = ID0;
	  geometry.drv[0].trans = TRUE;
	  geometry.drv[1].heads = HEADS1;
	  geometry.drv[1].sectors = SECTORS1;
	  geometry.drv[1].cylinder = CYLINDER1;
	  geometry.drv[1].id = ID1;
	  geometry.drv[1].trans = TRUE;
	} else {
	  geometry.drv[0].id=-1;
	  geometry.drv[1].id=-1;
	}

	hd->next = NULL;	/* build a linked list of all HBAs */
	hd->prev = last_HBA;
	if(hd->prev != NULL)
	    SD(hd->prev)->next = sh;
	last_HBA = sh;
	if (first_HBA == NULL)
	    first_HBA = sh;
	registered_HBAs++;
    }
    return (1);
}


long find_EISA(struct get_conf *buf)
{
    struct eata_register *base;
    int i;

#if CHECKPAL
    unsigned char pal1, pal2, pal3, *p;
#endif

    for (i = 0; i < MAXEISA; i++) {
	if (EISAbases[i] == TRUE) {	/* Still a possibility ?          */

	    base = (void *)0x1c88 + (i * 0x1000);
#if CHECKPAL
	    p = (char *)base;
	    pal1 = *(p - 8);
	    pal2 = *(p - 7);
	    pal3 = *(p - 6);

	    if (((pal1 == 0x12) && (pal2 == 0x14)) ||
		((pal1 == 0x38) && (pal2 == 0xa3) && (pal3 == 0x82)) ||
		((pal1 == 0x06) && (pal2 == 0x94) && (pal3 == 0x24))) {
		DBG(DBG_PROBE, printk("EISA EATA id tags found: %x %x %x \n",
			(int)pal1, (int)pal2, (int)pal3));
#endif
		if (get_conf_PIO(base, buf) == TRUE) {
		    DBG(DBG_PROBE&&DBG_EISA,print_config(buf));
		    if (buf->IRQ) {          /* We'll check the 
					      * primary/secondary stuff
					      * later
					      */
			EISAbases[i] = 0;
			return ((ulong)base);
		    } 
		    printk("No valid IRQ. HBA removed from list\n");
		} 
		/* Nothing found here so we take it from the list */
		EISAbases[i] = 0;  
#if CHECKPAL
	    } 
#endif
	}
    }
    return (0l);		/* Nothing found  :-(             */
}

long find_ISA(struct get_conf *buf)
{
    int l;
    long ret;

    for (l = 0; l < MAXISA; l++) {	
        if (ISAbases[l]) {	
	    if (get_conf_PIO((struct eata_register *)ISAbases[l],buf) == TRUE){
	        ret = ISAbases[l];
		ISAbases[l] = 0;
		return (ret);
	    } else
	        ISAbases[l] = 0;
        }
    }
    return ((long)NULL);
}

void find_PCI(struct get_conf *buf, Scsi_Host_Template * tpnt)
{

#ifndef CONFIG_PCI
    printk("Kernel PCI support not enabled. Skipping scan for PCI HBAs.\n");
#else

    unchar pci_bus, pci_device_fn;
    static short pci_index = 0;	/* Device index to PCI BIOS calls */
    ulong base = 0;
    ushort com_adr;
    ushort rev_device;
    uint error, i, x;

    if (pcibios_present()) {
	for (i = 0; i <= MAXPCI; ++i, ++pci_index) {

	    if (pcibios_find_device(PCI_VENDOR_ID_DPT, PCI_DEVICE_ID_DPT, 
				    pci_index, &pci_bus, &pci_device_fn))
		break;
	    DBG(DBG_PROBE && DBG_PCI, printk("eata_dma: HBA at bus %d, device %d,"
				" function %d, index %d\n", (int)pci_bus, 
				(int)((pci_device_fn & 0xf8) >> 3),
				(int)(pci_device_fn & 7), pci_index));

	    if (!(error = pcibios_read_config_word(pci_bus, pci_device_fn, 
					         PCI_CLASS_DEVICE, &rev_device))) {
	        if (rev_device == PCI_CLASS_STORAGE_SCSI) {
		    if (!(error = pcibios_read_config_word(pci_bus, 
							   pci_device_fn, PCI_COMMAND, 
							   (ushort *) & com_adr))) {
		        if (!((com_adr & PCI_COMMAND_IO) && 
			      (com_adr & PCI_COMMAND_MASTER))) {
			    printk("HBA has IO or BUSMASTER mode disabled\n");
			    continue;
			}
		    } else
		        printk("error %x while reading PCI_COMMAND\n", error);
	        } else
		  printk("DEVICECLASSID %x didn't match\n", rev_device);
	    } else {
	      printk("error %x while reading PCI_CLASS_BASE\n", error);
	      continue;
	    }

	    if (!(error = pcibios_read_config_dword(pci_bus, pci_device_fn,
						  PCI_BASE_ADDRESS_0, &base))){

	        /* Check if the address is valid */
	        if (base & 0x01) {
		    base &= 0xfffffffe;
		                        /* EISA tag there ? */
		    if ((inb(base) == 0x12) && (inb(base + 1) == 0x14))
		        continue;	/* Jep, it's forced, so move on  */
		    base += 0x10;	/* Now, THIS is the real address */
		    if (base != 0x1f8) {
		        /* We didn't find it in the primary search */
		        if (get_conf_PIO((struct eata_register *)base, buf)) {
			    if (buf->FORCADR)	/* If the address is forced */
			        continue;       /* we'll find it later      */

			    /* OK. We made it till here, so we can go now  
			     * and register it. We  only have to check and 
			     * eventually remove it from the EISA and ISA list 
			     */

			    register_HBA(base, buf, tpnt);

			    if (base < 0x1000) {
			        for (x = 0; x < MAXISA; ++x) {
				    if (ISAbases[x] == base) {
				        ISAbases[x] = 0;
					break;
				    }
			        }
			    } else if ((base & 0x0fff) == 0x0c88) {
			        x = (base >> 12) & 0x0f;
				EISAbases[x] = 0;
			    }
			    continue;  /*break;*/
			} else if (check_blink_state(base)) {
			    printk("HBA is in BLINK state. Consult your HBAs "
				   " Manual to correct this.\n");
			}
		    }
		}
	    } else
	      printk("error %x while reading PCI_BASE_ADDRESS_0\n", error);
	}
    } else
    printk("No BIOS32 extensions present. This release still depends on it."
	     " Sorry.\n");
#endif /* #ifndef CONFIG_PCI */
    return;
}

int eata_detect(Scsi_Host_Template * tpnt)
{
    struct Scsi_Host *HBA_ptr;
    struct get_conf gc;
    ulong base = 0;
    int i;
 
    geometry.drv[0].trans = geometry.drv[1].trans = 0;

    DBG((DBG_PROBE && DBG_DELAY)|| DPT_DEBUG,
	printk("Using lots of delays to let you read the debugging output\n"));

    status = scsi_init_malloc(512, GFP_ATOMIC | GFP_DMA);
    dma_scratch = scsi_init_malloc(512, GFP_ATOMIC | GFP_DMA);

    find_PCI(&gc, tpnt);

    for (i = 0; i < MAXEISA; i++) {
  	base = find_EISA(&gc);
	if (base)
	    register_HBA(base, &gc, tpnt);
    }

    for (i = 0; i <= MAXISA; i++) {
	base = find_ISA(&gc);
	if (base)
	    register_HBA(base, &gc, tpnt);
    }

    for (i = 0; i <= MAXIRQ; i++)
	if (reg_IRQ[i]){
	    free_irq(i);
	    request_irq(i, eata_int_handler, SA_INTERRUPT, "EATA-DMA");
	}

    HBA_ptr = first_HBA;

    if (registered_HBAs != 0) {
        printk("EATA (Extended Attachment) driver version: %d.%d%s\n"
	       "developed in co-operation with DPT\n"             
	       "(c) 1993-95 Michael Neuffer  neuffer@goofy.zdv.uni-mainz.de\n",
	       VER_MAJOR, VER_MINOR, VER_SUB);
    printk("Registered HBAs:\n");
    printk("HBA no. Boardtype: Revis: EATA: Bus: BaseIO: IRQ: DMA: Ch: ID: Pr: QS: SG: CPL:\n");
    for (i = 1; i <= registered_HBAs; i++) {
	    printk("scsi%-2d: %.10s v%s 2.0%c  %s %#.4lx   %2d",
	       HBA_ptr->host_no, SD(HBA_ptr)->name, SD(HBA_ptr)->revision,
	       SD(HBA_ptr)->EATA_revision, (SD(HBA_ptr)->bustype == 'P')? 
	       "PCI ":(SD(HBA_ptr)->bustype == 'E')?"EISA":"ISA ",
                   (u32) HBA_ptr->base, HBA_ptr->irq);
            if(HBA_ptr->dma_channel != 0xff)
	        printk("   %2x ", HBA_ptr->dma_channel);
            else
                printk("  %s", "BMST");
            printk("  %d   %d   %c  %2d  %2d   %2d\n", SD(HBA_ptr)->channel, 
                   HBA_ptr->this_id, (SD(HBA_ptr)->primary == TRUE)?'Y':'N', 
	       HBA_ptr->can_queue, HBA_ptr->sg_tablesize, HBA_ptr->cmd_per_lun);
        HBA_ptr = SD(HBA_ptr)->next;
    }
    } else 
        scsi_init_free((void *)status, 512);
    
    scsi_init_free((void *)dma_scratch, 512);

    DBG(DPT_DEBUG,DELAY(1200));

    return (registered_HBAs);
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = EATA_DMA;

#include "scsi_module.c"
#endif
 

