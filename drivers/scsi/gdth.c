/************************************************************************
 * GDT ISA/EISA/PCI Disk Array Controller driver for Linux              *
 *                                                                      *
 * gdth.c                                                               *
 * Copyright (C) 1995-97 ICP vortex Computersysteme GmbH, Achim Leubner *
 *                                                                      *
 * <achim@vortex.de>                                                    *
 *                                                                      *
 * This program is free software; you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published    *
 * by the Free Software Foundation; either version 2 of the License,    *
 * or (at your option) any later version.                               *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the         *
 * GNU General Public License for more details.                         *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this kernel; if not, write to the Free Software           *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.            *
 *                                                                      *
 * Tested with Linux 1.2.13, ..., 2.1.61                                *
 *                                                                      *
 * $Log: gdth.c,v $
 * Revision 1.3  1998/02/25 23:52:32  ecd
 * Final round of PCI device driver patches by Martin Mares.
 *
 * I could not verify each and every change to the drivers locally,
 * please consult linux/Documentation/pci.txt to understand changes
 * made in case patching should be necessary.
 *
 * Revision 1.2  1997/11/12 23:58:51  davem
 * Merge to 2.1.63 to get the Ingo P5 bugfix.
 * I did not touch the sound changes at all, Alan
 * please look into that stuff as it is your
 * territory.
 *
 * Revision 1.10  1997/10/31 12:29:57  achim
 * Read heads/sectors from host drive
 *
 * Revision 1.9  1997/09/04 10:07:25  achim
 * IO-mapping with virt_to_bus(), readb(), writeb(), ...
 * register_reboot_notifier() to get a notify on shutdown used
 *
 * Revision 1.8  1997/04/02 12:14:30  achim
 * Version 1.00 (see gdth.h), tested with kernel 2.0.29
 *
 * Revision 1.7  1997/03/12 13:33:37  achim
 * gdth_reset() changed, new async. events
 *
 * Revision 1.6  1997/03/04 14:01:11  achim
 * Shutdown routine gdth_halt() implemented
 *
 * Revision 1.5  1997/02/21 09:08:36  achim
 * New controller included (RP, RP1, RP2 series)
 * IOCTL interface implemented
 *
 * Revision 1.4  1996/07/05 12:48:55  achim
 * Function gdth_bios_param() implemented
 * New constant GDTH_MAXC_P_L inserted
 * GDT_WRITE_THR, GDT_EXT_INFO implemented
 * Function gdth_reset() changed
 *
 * Revision 1.3  1996/05/10 09:04:41  achim
 * Small changes for Linux 1.2.13
 *
 * Revision 1.2  1996/05/09 12:45:27  achim
 * Loadable module support implemented
 * /proc support corrections made
 *
 * Revision 1.1  1996/04/11 07:35:57  achim
 * Initial revision
 *
 *
 * $Id: gdth.c,v 1.4 1998/04/15 14:35:26 mj Exp $ 
 ************************************************************************/

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/timer.h>
#if LINUX_VERSION_CODE >= 0x020100
#include <linux/reboot.h>
#else
#include <linux/bios32.h>
#endif

#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/spinlock.h>

#if LINUX_VERSION_CODE >= 0x010300
#include <linux/blk.h>
#else
#include "../block/blk.h"
#endif
#include "scsi.h"
#include "hosts.h"
#include "sd.h"

#include "gdth.h"

#if LINUX_VERSION_CODE >= 0x010346
static void gdth_interrupt(int irq,void *dev_id,struct pt_regs *regs);
static void do_gdth_interrupt(int irq,void *dev_id,struct pt_regs *regs);
#else
static void gdth_interrupt(int irq,struct pt_regs *regs);
#endif
static int gdth_sync_event(int hanum,int service,unchar index,Scsi_Cmnd *scp);
static int gdth_async_event(int hanum,int service);

static void gdth_putq(int hanum,Scsi_Cmnd *scp,unchar priority);
static void gdth_next(int hanum);
static int gdth_fill_raw_cmd(int hanum,Scsi_Cmnd *scp,unchar b);
static int gdth_special_cmd(int hanum,Scsi_Cmnd *scp,unchar b);
static gdth_evt_str *gdth_store_event(ushort source, ushort idx,
                                      gdth_evt_data *evt);
static int gdth_read_event(int handle, gdth_evt_str *estr);
static void gdth_readapp_event(unchar application, gdth_evt_str *estr);
static void gdth_clear_events(void);

static void gdth_copy_internal_data(Scsi_Cmnd *scp,char *buffer,ushort count);
static int gdth_internal_cache_cmd(int hanum,Scsi_Cmnd *scp,
                                   unchar b,ulong *flags);
static int gdth_fill_cache_cmd(int hanum,Scsi_Cmnd *scp,ushort hdrive);

static int gdth_search_eisa(ushort eisa_adr);
static int gdth_search_isa(ulong bios_adr);
static int gdth_search_pci(ushort device_id,ushort index,gdth_pci_str *pcistr);
static int gdth_init_eisa(ushort eisa_adr,gdth_ha_str *ha);
static int gdth_init_isa(ulong bios_adr,gdth_ha_str *ha);
static int gdth_init_pci(gdth_pci_str *pcistr,gdth_ha_str *ha);

static void gdth_enable_int(int hanum);
static int gdth_get_status(unchar *pIStatus,int irq);
static int gdth_test_busy(int hanum);
static int gdth_get_cmd_index(int hanum);
static void gdth_release_event(int hanum);
static int gdth_wait(int hanum,int index,ulong time);
static int gdth_internal_cmd(int hanum,unchar service,ushort opcode,ulong p1,
                             ulong p2,ulong p3);
static int gdth_search_drives(int hanum);

static void *gdth_mmap(ulong paddr, ulong size);
static void gdth_munmap(void *addr);

static const char *gdth_ctr_name(int hanum);
#if LINUX_VERSION_CODE >= 0x020100
static int gdth_halt(struct notifier_block *nb, ulong event, void *buf);
#else
void gdth_halt(void);
#endif

#ifdef DEBUG_GDTH
static unchar   DebugState = DEBUG_GDTH;
extern int sys_syslog(int,char*,int);
#define LOGEN           sys_syslog(7,NULL,0);
#define WAITSEC(a)      mdelay((a)*1000)

#ifdef SLOWMOTION_GDTH
#define SLOWM   WAITSEC(2)  
#undef  INIT_RETRIES
#undef  INIT_TIMEOUT
#undef  POLL_TIMEOUT
#define INIT_RETRIES    15
#define INIT_TIMEOUT    150
#define POLL_TIMEOUT    150
#else
#define SLOWM
#endif

#ifdef __SERIAL__
#define MAX_SERBUF 160
static void ser_init(void);
static void ser_puts(char *str);
static void ser_putc(char c);
static int  ser_printk(const char *fmt, ...);
static char strbuf[MAX_SERBUF+1];
#ifdef __COM2__
#define COM_BASE 0x2f8
#else
#define COM_BASE 0x3f8
#endif
static void ser_init()
{
    unsigned port=COM_BASE;

    outb(0x80,port+3);
    outb(0,port+1);
    /* 19200 Baud, if 9600: outb(12,port) */
    outb(6, port);
    outb(3,port+3);
    outb(0,port+1);
    /*
    ser_putc('I');
    ser_putc(' ');
    */
}

static void ser_puts(char *str)
{
    char *ptr;

    ser_init();
    for (ptr=str;*ptr;++ptr)
        ser_putc(*ptr);
}

static void ser_putc(char c)
{
    unsigned port=COM_BASE;

    while ((inb(port+5) & 0x20)==0);
    outb(c,port);
    if (c==0x0a)
    {
        while ((inb(port+5) & 0x20)==0);
        outb(0x0d,port);
    }
}

static int ser_printk(const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args,fmt);
    i = vsprintf(strbuf,fmt,args);
    ser_puts(strbuf);
    va_end(args);
    return i;
}

#define TRACE(a)    {if (DebugState==1) {ser_printk a; SLOWM}}
#define TRACE2(a)   {if (DebugState==1 || DebugState==2) {ser_printk a; SLOWM}}
#define TRACE3(a)   {if (DebugState!=0) {ser_printk a; SLOWM}}

#else /* !__SERIAL__ */
#define TRACE(a)    {if (DebugState==1) {LOGEN;printk a; SLOWM}}
#define TRACE2(a)   {if (DebugState==1 || DebugState==2) {LOGEN;printk a; SLOWM}}
#define TRACE3(a)   {if (DebugState!=0) {LOGEN;printk a; SLOWM}}
#endif

#else /* !DEBUG */
#define TRACE(a)
#define TRACE2(a)
#define TRACE3(a)
#endif

#ifdef GDTH_STATISTICS
static ulong max_rq=0, max_index=0, max_sg=0;
static ulong act_ints=0, act_ios=0, act_stats=0, act_rq=0;
#define GDTH_TIMER      31                      /* see linux/timer.h ! */
#endif

#define PTR2USHORT(a)   (ushort)(ulong)(a)
#define JIFFYWAIT(a)    {ulong gdtjf;gdtjf=jiffies+(a);while(gdtjf>jiffies);}
#define GDTOFFSOF(a,b)  (size_t)&(((a*)0)->b)   
#define INDEX_OK(i,t)   ((i)<sizeof(t)/sizeof((t)[0]))

#define NUMDATA(a)      ( (gdth_num_str  *)((a)->hostdata))
#define HADATA(a)       (&((gdth_ext_str *)((a)->hostdata))->haext)
#define CMDDATA(a)      (&((gdth_ext_str *)((a)->hostdata))->cmdext)
#define DMADATA(a)      (&((gdth_ext_str *)((a)->hostdata))->dmaext)


#if LINUX_VERSION_CODE < 0x010300
static void *gdth_mmap(ulong paddr, ulong size) 
{
    if (paddr >= high_memory)
	return NULL; 
    else
	return (void *)paddr;
}
static void gdth_munmap(void *addr) 
{
}
inline ulong virt_to_phys(volatile void *addr)
{
    return (ulong)addr;
}
inline void *phys_to_virt(ulong addr)
{
    return (void *)addr;
}
#define virt_to_bus		virt_to_phys
#define bus_to_virt		phys_to_virt
#define readb(addr)		(*(volatile unchar *)(addr))
#define readw(addr)		(*(volatile ushort *)(addr))
#define readl(addr)		(*(volatile ulong *)(addr))
#define writeb(b,addr)		(*(volatile unchar *)(addr) = (b))
#define writew(b,addr)		(*(volatile ushort *)(addr) = (b))
#define writel(b,addr)		(*(volatile ulong *)(addr) = (b))
#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#elif LINUX_VERSION_CODE < 0x020100
static int remapped = FALSE;
static void *gdth_mmap(ulong paddr, ulong size) 
{
    if ( paddr >= high_memory) {
	remapped = TRUE;
	return vremap(paddr, size);
    } else {
	return (void *)paddr; 
    }
}
static void gdth_munmap(void *addr) 
{
    if (remapped)
	vfree(addr);
    remapped = FALSE;
}
#else
static void *gdth_mmap(ulong paddr, ulong size) 
{ 
    return ioremap(paddr, size); 
}
static void gdth_munmap(void *addr) 
{
    return iounmap(addr);
}
#endif


static unchar   gdth_drq_tab[4] = {5,6,7,7};            /* DRQ table */
static unchar   gdth_irq_tab[6] = {0,10,11,12,14,0};    /* IRQ table */
static unchar   gdth_polling;                           /* polling if TRUE */
static unchar   gdth_from_wait  = FALSE;                /* gdth_wait() */
static int      wait_index,wait_hanum;                  /* gdth_wait() */
static int      gdth_ctr_count  = 0;                    /* controller count */
static int      gdth_ctr_vcount = 0;                    /* virt. ctr. count */
static struct Scsi_Host *gdth_ctr_tab[MAXHA];           /* controller table */
static struct Scsi_Host *gdth_ctr_vtab[MAXHA*MAXBUS];   /* virt. ctr. table */
static unchar   gdth_write_through = FALSE;             /* write through */
static char *gdth_ioctl_tab[4][MAXHA];                  /* ioctl buffer */
static gdth_evt_str ebuffer[MAX_EVENTS];                /* event buffer */
static int elastidx;
static int eoldidx;

static struct {
    Scsi_Cmnd   *cmnd;                          /* pending request */
    ushort      service;                        /* service */
} gdth_cmd_tab[GDTH_MAXCMDS][MAXHA];            /* table of pend. requests */

#define DIN     1                               /* IN data direction */
#define DOU     2                               /* OUT data direction */
#define DNO     DIN                             /* no data transfer */
#define DUN     DIN                             /* unknown data direction */
static unchar gdth_direction_tab[0x100] = {
    DNO,DNO,DIN,DIN,DOU,DIN,DIN,DOU,DIN,DUN,DOU,DOU,DUN,DUN,DUN,DIN,
    DNO,DIN,DIN,DOU,DIN,DOU,DNO,DNO,DOU,DNO,DIN,DNO,DIN,DOU,DNO,DUN,
    DIN,DUN,DIN,DUN,DOU,DIN,DUN,DUN,DIN,DIN,DIN,DUN,DUN,DIN,DIN,DIN,
    DIN,DIN,DIN,DNO,DIN,DNO,DNO,DIN,DIN,DIN,DIN,DIN,DIN,DIN,DIN,DIN,
    DIN,DIN,DIN,DIN,DIN,DNO,DUN,DNO,DNO,DNO,DUN,DNO,DIN,DIN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DIN,DUN,DUN,DUN,DUN,DIN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DNO,DNO,DUN,DIN,DNO,DIN,DUN,DNO,DUN,DIN,DIN,
    DIN,DIN,DIN,DNO,DUN,DIN,DIN,DIN,DIN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DOU,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN
};

/* LILO params: gdth=<IRQ>
 *
 * Where: <IRQ> is any of the valid IRQs for EISA controllers (10,11,12,14)
 * Sets the IRQ of the GDT3000/3020 EISA controller to this value,
 * if the IRQ can not automat. detect (controller BIOS disabled)
 * See gdth_init_eisa() 
 *
 * You can use the command line gdth=0 to disable the driver 
 */
static unchar irqs[MAXHA] = {0xff};
static unchar disable_gdth_scan = FALSE;

/* /proc support */
#if LINUX_VERSION_CODE >= 0x010300
#include <linux/stat.h> 
struct proc_dir_entry proc_scsi_gdth = {
    PROC_SCSI_GDTH, 4, "gdth",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};
#include "gdth_proc.h"
#include "gdth_proc.c"
#endif

#if LINUX_VERSION_CODE >= 0x020100
/* notifier block to get a notify on system shutdown/halt/reboot */
static struct notifier_block gdth_notifier = {
    gdth_halt, NULL, 0
};
#endif

/* controller search and initialization functions */

static int gdth_search_eisa(ushort eisa_adr)
{
    ulong id;
    
    TRACE(("gdth_search_eisa() adr. %x\n",eisa_adr));
    id = inl(eisa_adr+ID0REG);
    if (id == GDT3A_ID || id == GDT3B_ID) {     /* GDT3000A or GDT3000B */
        if ((inb(eisa_adr+EISAREG) & 8) == 0)   
            return 0;                           /* not EISA configured */
        return 1;
    }
    if (id == GDT3_ID)                          /* GDT3000 */
        return 1;

    return 0;                                   
}


static int gdth_search_isa(ulong bios_adr)
{
    void *addr;
    ulong id;

    TRACE(("gdth_search_isa() bios adr. %lx\n",bios_adr));
    if ((addr = gdth_mmap(bios_adr+BIOS_ID_OFFS, sizeof(ulong))) != NULL) {
	id = readl(addr);
	gdth_munmap(addr);
	if (id == GDT2_ID)                          /* GDT2000 */
	    return 1;
    }
    return 0;
}


static int gdth_search_pci(ushort device_id,ushort index,gdth_pci_str *pcistr)
{
    int error;
    ulong base0,base1,base2;

    TRACE(("gdth_search_pci() device_id %d, index %d\n",
                 device_id,index));

    if (!pci_present())
        return 0;

    if (pcibios_find_device(PCI_VENDOR_ID_VORTEX,device_id,index,
                             &pcistr->bus,&pcistr->device_fn))
        return 0;

    /* GDT PCI controller found, now read resources from config space */
#if LINUX_VERSION_CODE >= 0x20155
    {
	struct pci_dev *pdev = pci_find_slot(pcistr->bus, pcistr->device_fn);
	base0 = pdev->base_address[0];
	base1 = pdev->base_address[1];
	base2 = pdev->base_address[2];
	if ((error = pcibios_read_config_dword(pcistr->bus,pcistr->device_fn,
                                           PCI_ROM_ADDRESS,
                                           (int *) &pcistr->bios))) {
		printk("GDT-PCI: error %d reading configuration space", error);
		return -1;
		}
	pcistr->irq = pdev->irq;
    }
#else
#if LINUX_VERSION_CODE >= 0x010300
#define GDTH_BASEP      (int *)
#else
#define GDTH_BASEP
#endif
    if ((error = pcibios_read_config_dword(pcistr->bus,pcistr->device_fn,
                                           PCI_BASE_ADDRESS_0,
                                           GDTH_BASEP&base0)) ||
        (error = pcibios_read_config_dword(pcistr->bus,pcistr->device_fn,
                                           PCI_BASE_ADDRESS_1,
                                           GDTH_BASEP&base1)) ||
        (error = pcibios_read_config_dword(pcistr->bus,pcistr->device_fn,
                                           PCI_BASE_ADDRESS_2,
                                           GDTH_BASEP&base2)) ||
        (error = pcibios_read_config_dword(pcistr->bus,pcistr->device_fn,
                                           PCI_ROM_ADDRESS,
                                           GDTH_BASEP&pcistr->bios)) ||
        (error = pcibios_read_config_byte(pcistr->bus,pcistr->device_fn,
                                          PCI_INTERRUPT_LINE,&pcistr->irq))) {
        printk("GDT-PCI: error %d reading configuration space", error);
        return -1;
    }
#endif

    pcistr->device_id = device_id;
    if (device_id <= PCI_DEVICE_ID_VORTEX_GDT6000B ||   /* GDT6000 or GDT6000B */
        device_id >= PCI_DEVICE_ID_VORTEX_GDT6x17RP) {  /* MPR */
        if ((base0 & PCI_BASE_ADDRESS_SPACE)!=PCI_BASE_ADDRESS_SPACE_MEMORY)
            return -1;
        pcistr->dpmem = base0 & PCI_BASE_ADDRESS_MEM_MASK;
    } else {                                    /* GDT6110, GDT6120, .. */
        if ((base0 & PCI_BASE_ADDRESS_SPACE)!=PCI_BASE_ADDRESS_SPACE_MEMORY ||
            (base2 & PCI_BASE_ADDRESS_SPACE)!=PCI_BASE_ADDRESS_SPACE_MEMORY ||
            (base1 & PCI_BASE_ADDRESS_SPACE)!=PCI_BASE_ADDRESS_SPACE_IO)
            return -1;
        pcistr->dpmem = base2 & PCI_BASE_ADDRESS_MEM_MASK;
        pcistr->io_mm = base0 & PCI_BASE_ADDRESS_MEM_MASK;
        pcistr->io    = base1 & PCI_BASE_ADDRESS_IO_MASK;
    }
    return 1;
}


static int gdth_init_eisa(ushort eisa_adr,gdth_ha_str *ha)
{
    ulong retries,id;
    unchar prot_ver,eisacf,i,irq_found;

    TRACE(("gdth_init_eisa() adr. %x\n",eisa_adr));
    
    /* disable board interrupts, deinitialize services */
    outb(0xff,eisa_adr+EDOORREG);
    outb(0x00,eisa_adr+EDENABREG);
    outb(0x00,eisa_adr+EINTENABREG);
    
    outb(0xff,eisa_adr+LDOORREG);
    retries = INIT_RETRIES;
    JIFFYWAIT(2);
    while (inb(eisa_adr+EDOORREG) != 0xff) {
        if (--retries == 0) {
            printk("GDT-EISA: Initialization error (DEINIT failed)\n");
            return 0;
        }
        mdelay(1);
        TRACE2(("wait for DEINIT: retries=%ld\n",retries));
    }
    prot_ver = inb(eisa_adr+MAILBOXREG);
    outb(0xff,eisa_adr+EDOORREG);
    if (prot_ver != PROTOCOL_VERSION) {
        printk("GDT-EISA: Illegal protocol version\n");
        return 0;
    }
    ha->bmic = eisa_adr;
    ha->brd_phys = (ulong)eisa_adr >> 12;

    outl(0,eisa_adr+MAILBOXREG);
    outl(0,eisa_adr+MAILBOXREG+4);
    outl(0,eisa_adr+MAILBOXREG+8);
    outl(0,eisa_adr+MAILBOXREG+12);

    /* detect IRQ */ 
    if ((id = inl(eisa_adr+ID0REG)) == GDT3_ID) {
        ha->type = GDT_EISA;
        ha->stype = id;
        outl(1,eisa_adr+MAILBOXREG+8);
        outb(0xfe,eisa_adr+LDOORREG);
        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (inb(eisa_adr+EDOORREG) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-EISA: Initialization error (get IRQ failed)\n");
                return 0;
            }
            mdelay(1);
        }
        ha->irq = inb(eisa_adr+MAILBOXREG);
        outb(0xff,eisa_adr+EDOORREG);
        TRACE2(("GDT3000/3020: IRQ=%d\n",ha->irq));
	/* check the result */
	if (ha->irq == 0) {
	    TRACE2(("Unknown IRQ, check IRQ table from cmd line !\n"));
	    for (i=0,irq_found=FALSE; i<MAXHA && irqs[i]!=0xff; ++i) {
		if (irqs[i]!=0) {
		    irq_found=TRUE;
		    break;
		}
	    }
	    if (irq_found) {
		ha->irq = irqs[i];
		irqs[i] = 0;
		printk("GDT-EISA: Can not detect controller IRQ,\n");
		printk("Use IRQ setting from command line (IRQ = %d)\n",
		       ha->irq);
	    } else {
		printk("GDT-EISA: Initialization error (unknown IRQ), Enable\n");
		printk("the controller BIOS or use command line parameters\n");
		return 0;
	    }
	}
    } else {
        eisacf = inb(eisa_adr+EISAREG) & 7;
        if (eisacf > 4)                         /* level triggered */
            eisacf -= 4;
        ha->irq = gdth_irq_tab[eisacf];
        ha->type = GDT_EISA;
        ha->stype= id;
    }
    return 1;
}

       
static int gdth_init_isa(ulong bios_adr,gdth_ha_str *ha)
{
    register gdt2_dpram_str *dp2_ptr;
    int i;
    unchar irq_drq,prot_ver;
    ulong retries;

    TRACE(("gdth_init_isa() bios adr. %lx\n",bios_adr));

    ha->brd = gdth_mmap(bios_adr, sizeof(gdt2_dpram_str));
    if (ha->brd == NULL) {
	printk("GDT-ISA: Initialization error (DPMEM remap error)\n");
	return 0;
    }
    dp2_ptr = (gdt2_dpram_str *)ha->brd;
    writeb(1, &dp2_ptr->io.memlock);			/* switch off write protection */
    /* reset interface area */
    memset_io((char *)&dp2_ptr->u,0,sizeof(dp2_ptr->u));

    /* disable board interrupts, read DRQ and IRQ */
    writeb(0xff, &dp2_ptr->io.irqdel);
    writeb(0x00, &dp2_ptr->io.irqen);
    writeb(0x00, &dp2_ptr->u.ic.S_Status);
    writeb(0x00, &dp2_ptr->u.ic.Cmd_Index);

    irq_drq = readb(&dp2_ptr->io.rq);
    for (i=0; i<3; ++i) {
        if ((irq_drq & 1)==0)
            break;
        irq_drq >>= 1;
    }
    ha->drq = gdth_drq_tab[i];

    irq_drq = readb(&dp2_ptr->io.rq) >> 3;
    for (i=1; i<5; ++i) {
        if ((irq_drq & 1)==0)
            break;
        irq_drq >>= 1;
    }
    ha->irq = gdth_irq_tab[i];

    /* deinitialize services */
    writel(bios_adr, &dp2_ptr->u.ic.S_Info[0]);
    writeb(0xff, &dp2_ptr->u.ic.S_Cmd_Indx);
    writeb(0, &dp2_ptr->io.event);
    retries = INIT_RETRIES;
    JIFFYWAIT(2);
    while (readb(&dp2_ptr->u.ic.S_Status) != 0xff) {
        if (--retries == 0) {
            printk("GDT-ISA: Initialization error (DEINIT failed)\n");
	    gdth_munmap(ha->brd);
            return 0;
        }
        mdelay(1);
    }
    prot_ver = (unchar)readl(&dp2_ptr->u.ic.S_Info[0]);
    writeb(0, &dp2_ptr->u.ic.Status);
    writeb(0xff, &dp2_ptr->io.irqdel);
    if (prot_ver != PROTOCOL_VERSION) {
        printk("GDT-ISA: Illegal protocol version\n");
	gdth_munmap(ha->brd);
        return 0;
    }

    ha->type = GDT_ISA;
    ha->ic_all_size = sizeof(dp2_ptr->u);
    ha->stype= GDT2_ID;
    ha->brd_phys = bios_adr >> 4;

    /* special request to controller BIOS */
    writel(0x00, &dp2_ptr->u.ic.S_Info[0]);
    writel(0x00, &dp2_ptr->u.ic.S_Info[1]);
    writel(0x01, &dp2_ptr->u.ic.S_Info[2]);
    writel(0x00, &dp2_ptr->u.ic.S_Info[3]);
    writeb(0xfe, &dp2_ptr->u.ic.S_Cmd_Indx);
    writeb(0, &dp2_ptr->io.event);
    retries = INIT_RETRIES;
    JIFFYWAIT(2);
    while (readb(&dp2_ptr->u.ic.S_Status) != 0xfe) {
        if (--retries == 0) {
            printk("GDT-ISA: Initialization error\n");
	    gdth_munmap(ha->brd);
            return 0;
        }
        mdelay(1);
    }
    writeb(0, &dp2_ptr->u.ic.Status);
    writeb(0xff, &dp2_ptr->io.irqdel);
    return 1;
}


static int gdth_init_pci(gdth_pci_str *pcistr,gdth_ha_str *ha)
{
    register gdt6_dpram_str *dp6_ptr;
    register gdt6c_dpram_str *dp6c_ptr;
    register gdt6m_dpram_str *dp6m_ptr;
    ulong retries;
    unchar prot_ver;

    TRACE(("gdth_init_pci()\n"));

    ha->brd_phys = (pcistr->bus << 8) | (pcistr->device_fn & 0xf8);
    ha->stype    = (ulong)pcistr->device_id;
    ha->irq      = pcistr->irq;
    
    if (ha->stype <= PCI_DEVICE_ID_VORTEX_GDT6000B) {   /* GDT6000 or GDT6000B */
        TRACE2(("init_pci() dpmem %lx irq %d\n",pcistr->dpmem,ha->irq));
	ha->brd = gdth_mmap(pcistr->dpmem, sizeof(gdt6_dpram_str));
	if (ha->brd == NULL) {
	    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
	    return 0;
	}
        dp6_ptr = (gdt6_dpram_str *)ha->brd;
        /* reset interface area */
        memset_io((char *)&dp6_ptr->u,0,sizeof(dp6_ptr->u));
        if (readl(&dp6_ptr->u) != 0) {
            printk("GDT-PCI: Initialization error (DPMEM write error)\n");
	    gdth_munmap(ha->brd);
            return 0;
        }
        
        /* disable board interrupts, deinit services */
        writeb(0xff, &dp6_ptr->io.irqdel);
        writeb(0x00, &dp6_ptr->io.irqen);;
        writeb(0x00, &dp6_ptr->u.ic.S_Status);
        writeb(0x00, &dp6_ptr->u.ic.Cmd_Index);

        writel(pcistr->dpmem, &dp6_ptr->u.ic.S_Info[0]);
        writeb(0xff, &dp6_ptr->u.ic.S_Cmd_Indx);
        writeb(0, &dp6_ptr->io.event);
        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        prot_ver = (unchar)readl(&dp6_ptr->u.ic.S_Info[0]);
        writeb(0, &dp6_ptr->u.ic.S_Status);
        writeb(0xff, &dp6_ptr->io.irqdel);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
	    gdth_munmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCI;
        ha->ic_all_size = sizeof(dp6_ptr->u);
        
        /* special command to controller BIOS */
        writel(0x00, &dp6_ptr->u.ic.S_Info[0]);
        writel(0x00, &dp6_ptr->u.ic.S_Info[1]);
        writel(0x01, &dp6_ptr->u.ic.S_Info[2]);
        writel(0x00, &dp6_ptr->u.ic.S_Info[3]);
        writeb(0xfe, &dp6_ptr->u.ic.S_Cmd_Indx);
        writeb(0, &dp6_ptr->io.event);
        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        writeb(0, &dp6_ptr->u.ic.S_Status);
        writeb(0xff, &dp6_ptr->io.irqdel);

    } else if (ha->stype <= PCI_DEVICE_ID_VORTEX_GDT6555) { /* GDT6110, GDT6120, .. */
	ha->plx = (gdt6c_plx_regs *)pcistr->io;
	TRACE2(("init_pci_new() dpmem %lx io %lx irq %d\n",
		pcistr->dpmem,(ulong)ha->plx,ha->irq));
	ha->brd = gdth_mmap(pcistr->dpmem, sizeof(gdt6c_dpram_str));
	if (ha->brd == NULL) {
	    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
	    gdth_munmap(ha->brd);
	    return 0;
	}
        dp6c_ptr = (gdt6c_dpram_str *)ha->brd;
        /* reset interface area */
        memset_io((char *)&dp6c_ptr->u,0,sizeof(dp6c_ptr->u));
        if (readl(&dp6c_ptr->u) != 0) {
            printk("GDT-PCI: Initialization error (DPMEM write error)\n");
	    gdth_munmap(ha->brd);
            return 0;
        }
        
        /* disable board interrupts, deinit services */
        outb(0x00,PTR2USHORT(&ha->plx->control1));
        outb(0xff,PTR2USHORT(&ha->plx->edoor_reg));
        
        writeb(0x00, &dp6c_ptr->u.ic.S_Status);
        writeb(0x00, &dp6c_ptr->u.ic.Cmd_Index);

        writel(pcistr->dpmem, &dp6c_ptr->u.ic.S_Info[0]);
        writeb(0xff, &dp6c_ptr->u.ic.S_Cmd_Indx);

        outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6c_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        prot_ver = (unchar)readl(&dp6c_ptr->u.ic.S_Info[0]);
        writeb(0, &dp6c_ptr->u.ic.Status);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
	    gdth_munmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCINEW;
        ha->ic_all_size = sizeof(dp6c_ptr->u);

        /* special command to controller BIOS */
        writel(0x00, &dp6c_ptr->u.ic.S_Info[0]);
        writel(0x00, &dp6c_ptr->u.ic.S_Info[1]);
        writel(0x01, &dp6c_ptr->u.ic.S_Info[2]);
        writel(0x00, &dp6c_ptr->u.ic.S_Info[3]);
        writeb(0xfe, &dp6c_ptr->u.ic.S_Cmd_Indx);
        
        outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6c_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        writeb(0, &dp6c_ptr->u.ic.S_Status);

    } else {                                            /* MPR */
	TRACE2(("init_pci_mpr() dpmem %lx irq %d\n",pcistr->dpmem,ha->irq));
	ha->brd = gdth_mmap(pcistr->dpmem, sizeof(gdt6m_dpram_str));
	if (ha->brd == NULL) {
	    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
	    return 0;
	}

        dp6m_ptr = (gdt6m_dpram_str *)ha->brd;
        /* reset interface area */
        memset_io((char *)&dp6m_ptr->u,0,sizeof(dp6m_ptr->u));
        if (readl(&dp6m_ptr->u) != 0) {
            printk("GDT-PCI: Initialization error (DPMEM write error)\n");
	    gdth_munmap(ha->brd);
            return 0;
        }
        
        /* disable board interrupts, deinit services */
        writeb(readb(&dp6m_ptr->i960r.edoor_en_reg) | 4,
	       &dp6m_ptr->i960r.edoor_en_reg);
        writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        writeb(0x00, &dp6m_ptr->u.ic.S_Status);
        writeb(0x00, &dp6m_ptr->u.ic.Cmd_Index);

        writel(pcistr->dpmem, &dp6m_ptr->u.ic.S_Info[0]);
        writeb(0xff, &dp6m_ptr->u.ic.S_Cmd_Indx);
        writeb(1, &dp6m_ptr->i960r.ldoor_reg);
        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6m_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        prot_ver = (unchar)readl(&dp6m_ptr->u.ic.S_Info[0]);
        writeb(0, &dp6m_ptr->u.ic.S_Status);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
	    gdth_munmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCIMPR;
        ha->ic_all_size = sizeof(dp6m_ptr->u);
        
        /* special command to controller BIOS */
        writel(0x00, &dp6m_ptr->u.ic.S_Info[0]);
        writel(0x00, &dp6m_ptr->u.ic.S_Info[1]);
        writel(0x01, &dp6m_ptr->u.ic.S_Info[2]);
        writel(0x00, &dp6m_ptr->u.ic.S_Info[3]);
        writeb(0xfe, &dp6m_ptr->u.ic.S_Cmd_Indx);
        writeb(1, &dp6m_ptr->i960r.ldoor_reg);
        retries = INIT_RETRIES;
        JIFFYWAIT(2);
        while (readb(&dp6m_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
		gdth_munmap(ha->brd);
                return 0;
            }
            mdelay(1);
        }
        writeb(0, &dp6m_ptr->u.ic.S_Status);
    }

    return 1;
}


/* controller protocol functions */

static void gdth_enable_int(int hanum)
{
    gdth_ha_str *ha;
    ulong flags;
    gdt2_dpram_str *dp2_ptr;
    gdt6_dpram_str *dp6_ptr;
    gdt6m_dpram_str *dp6m_ptr;

    TRACE(("gdth_enable_int() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);

    save_flags(flags);
    cli();

    if (ha->type == GDT_EISA) {
        outb(0xff, ha->bmic + EDOORREG);
        outb(0xff, ha->bmic + EDENABREG);
        outb(0x01, ha->bmic + EINTENABREG);
    } else if (ha->type == GDT_ISA) {
        dp2_ptr = (gdt2_dpram_str *)ha->brd;
        writeb(1, &dp2_ptr->io.irqdel);
        writeb(0, &dp2_ptr->u.ic.Cmd_Index);
        writeb(1, &dp2_ptr->io.irqen);
    } else if (ha->type == GDT_PCI) {
        dp6_ptr = (gdt6_dpram_str *)ha->brd;
        writeb(1, &dp6_ptr->io.irqdel);
        writeb(0, &dp6_ptr->u.ic.Cmd_Index);
        writeb(1, &dp6_ptr->io.irqen);
    } else if (ha->type == GDT_PCINEW) {
        outb(0xff, PTR2USHORT(&ha->plx->edoor_reg));
        outb(0x03, PTR2USHORT(&ha->plx->control1));
    } else if (ha->type == GDT_PCIMPR) {
        dp6m_ptr = (gdt6m_dpram_str *)ha->brd;
        writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        writeb(readb(&dp6m_ptr->i960r.edoor_en_reg) & ~4,
	       &dp6m_ptr->i960r.edoor_en_reg);
    }
    restore_flags(flags);
}


static int gdth_get_status(unchar *pIStatus,int irq)
{
    register gdth_ha_str *ha;
    int i;

    TRACE(("gdth_get_status() irq %d ctr_count %d\n",
                 irq,gdth_ctr_count));
    
    *pIStatus = 0;
    for (i=0; i<gdth_ctr_count; ++i) {
        ha = HADATA(gdth_ctr_tab[i]);
        if (ha->irq != (unchar)irq)             /* check IRQ */
            continue;
        if (ha->type == GDT_EISA)
            *pIStatus = inb((ushort)ha->bmic + EDOORREG);
        else if (ha->type == GDT_ISA)
            *pIStatus = readb(&((gdt2_dpram_str *)ha->brd)->u.ic.Cmd_Index);
        else if (ha->type == GDT_PCI)
            *pIStatus = readb(&((gdt6_dpram_str *)ha->brd)->u.ic.Cmd_Index);
        else if (ha->type == GDT_PCINEW) 
            *pIStatus = inb(PTR2USHORT(&ha->plx->edoor_reg));
        else if (ha->type == GDT_PCIMPR)
            *pIStatus = readb(&((gdt6m_dpram_str *)ha->brd)->i960r.edoor_reg);
   
        if (*pIStatus)                                  
            return i;                           /* board found */
    }
    return -1;
}
                 
    
static int gdth_test_busy(int hanum)
{
    register gdth_ha_str *ha;
    register int gdtsema0 = 0;

    TRACE(("gdth_test_busy() hanum %d\n",hanum));
    
    ha = HADATA(gdth_ctr_tab[hanum]);
    if (ha->type == GDT_EISA)
        gdtsema0 = (int)inb(ha->bmic + SEMA0REG);
    else if (ha->type == GDT_ISA)
        gdtsema0 = (int)readb(&((gdt2_dpram_str *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCI)
        gdtsema0 = (int)readb(&((gdt6_dpram_str *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCINEW) 
        gdtsema0 = (int)inb(PTR2USHORT(&ha->plx->sema0_reg));
    else if (ha->type == GDT_PCIMPR)
        gdtsema0 = (int)readb(&((gdt6m_dpram_str *)ha->brd)->i960r.sema0_reg);

    return (gdtsema0 & 1);
}


static int gdth_get_cmd_index(int hanum)
{
    register gdth_ha_str *ha;
    int i;

    TRACE(("gdth_get_cmd_index() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    for (i=0; i<GDTH_MAXCMDS; ++i) {
        if (gdth_cmd_tab[i][hanum].cmnd == UNUSED_CMND) {
            gdth_cmd_tab[i][hanum].cmnd = ha->pccb->RequestBuffer;
            gdth_cmd_tab[i][hanum].service = ha->pccb->Service;
            ha->pccb->CommandIndex = (ulong)i+2;
            return (i+2);
        }
    }
    return 0;
}


static void gdth_set_sema0(int hanum)
{
    register gdth_ha_str *ha;

    TRACE(("gdth_set_sema0() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    if (ha->type == GDT_EISA)
        outb(1, ha->bmic + SEMA0REG);
    else if (ha->type == GDT_ISA)
        writeb(1, &((gdt2_dpram_str *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCI)
        writeb(1, &((gdt6_dpram_str *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCINEW)  
        outb(1, PTR2USHORT(&ha->plx->sema0_reg));
    else if (ha->type == GDT_PCIMPR)
        writeb(1, &((gdt6m_dpram_str *)ha->brd)->i960r.sema0_reg);
    
}


static void gdth_copy_command(int hanum)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmd_ptr;
    register gdt6m_dpram_str *dp6m_ptr;
    register gdt6c_dpram_str *dp6c_ptr;
    gdt6_dpram_str *dp6_ptr;
    gdt2_dpram_str *dp2_ptr;
    ushort cp_count,dp_offset,cmd_no;
    
    TRACE(("gdth_copy_command() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    cp_count = ha->cmd_len;
    dp_offset= ha->cmd_offs_dpmem;
    cmd_no   = ha->cmd_cnt;
    cmd_ptr  = ha->pccb;

    ++ha->cmd_cnt;                                                      
    if (ha->type == GDT_EISA)
        return;                                 /* no DPMEM, no copy */

    /* set cpcount dword aligned */
    if (cp_count & 3)
        cp_count += (4 - (cp_count & 3));

    ha->cmd_offs_dpmem += cp_count;
    
    /* set offset and service, copy command to DPMEM */
    if (ha->type == GDT_ISA) {
        dp2_ptr = (gdt2_dpram_str *)ha->brd;
        writew(dp_offset + DPMEM_COMMAND_OFFSET, 
	       &dp2_ptr->u.ic.comm_queue[cmd_no].offset);
        writew((ushort)cmd_ptr->Service, 
	       &dp2_ptr->u.ic.comm_queue[cmd_no].serv_id);
	memcpy_toio(&dp2_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCI) {
        dp6_ptr = (gdt6_dpram_str *)ha->brd;
        writew(dp_offset + DPMEM_COMMAND_OFFSET, 
	       &dp6_ptr->u.ic.comm_queue[cmd_no].offset);
        writew((ushort)cmd_ptr->Service, 
	       &dp6_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp6_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCINEW) {
        dp6c_ptr = (gdt6c_dpram_str *)ha->brd;
        writew(dp_offset + DPMEM_COMMAND_OFFSET, 
	       &dp6c_ptr->u.ic.comm_queue[cmd_no].offset);
	writew((ushort)cmd_ptr->Service, 
	       &dp6c_ptr->u.ic.comm_queue[cmd_no].serv_id);
	memcpy_toio(&dp6c_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCIMPR) {
        dp6m_ptr = (gdt6m_dpram_str *)ha->brd;
        writew(dp_offset + DPMEM_COMMAND_OFFSET, 
	       &dp6m_ptr->u.ic.comm_queue[cmd_no].offset);
        writew((ushort)cmd_ptr->Service, 
	       &dp6m_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp6m_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    }
}


static void gdth_release_event(int hanum)
{
    register gdth_ha_str *ha;

#ifdef GDTH_STATISTICS
    ulong i,j;
    for (i=0,j=0; j<GDTH_MAXCMDS; ++j) {
        if (gdth_cmd_tab[j][hanum].cmnd != UNUSED_CMND)
            ++i;
    }
    if (max_index < i) {
        max_index = i;
        TRACE3(("GDT: max_index = %d\n",(ushort)i));
    }
#endif

    TRACE(("gdth_release_event() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);

    if (ha->pccb->OpCode == GDT_INIT)
        ha->pccb->Service |= 0x80;

    if (ha->type == GDT_EISA) {
        outb(ha->pccb->Service, ha->bmic + LDOORREG);
        if (ha->pccb->OpCode == GDT_INIT)               /* store DMA buffer */
            outl((ulong)ha->pccb, ha->bmic + MAILBOXREG);
    } else if (ha->type == GDT_ISA)
        writeb(0, &((gdt2_dpram_str *)ha->brd)->io.event);
    else if (ha->type == GDT_PCI)
        writeb(0, &((gdt6_dpram_str *)ha->brd)->io.event);
    else if (ha->type == GDT_PCINEW) 
        outb(1, PTR2USHORT(&ha->plx->ldoor_reg));
    else if (ha->type == GDT_PCIMPR)
        writeb(1, &((gdt6m_dpram_str *)ha->brd)->i960r.ldoor_reg);
}

    
static int gdth_wait(int hanum,int index,ulong time)
{
    gdth_ha_str *ha;
    int answer_found = FALSE;

    TRACE(("gdth_wait() hanum %d index %d time %ld\n",hanum,index,time));

    ha = HADATA(gdth_ctr_tab[hanum]);
    if (index == 0)
        return 1;                               /* no wait required */

    gdth_from_wait = TRUE;
    do {
#if LINUX_VERSION_CODE >= 0x010346
        gdth_interrupt((int)ha->irq,NULL,NULL);
#else
        gdth_interrupt((int)ha->irq,NULL);
#endif
        if (wait_hanum==hanum && wait_index==index) {
            answer_found = TRUE;
            break;
        }
        mdelay(1);
    } while (--time);
    gdth_from_wait = FALSE;
    
    while (gdth_test_busy(hanum))
        udelay(1);

    return (answer_found);
}


static int gdth_internal_cmd(int hanum,unchar service,ushort opcode,ulong p1,
                             ulong p2,ulong p3)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmd_ptr;
    int retries,index;

    TRACE2(("gdth_internal_cmd() service %d opcode %d\n",service,opcode));

    ha = HADATA(gdth_ctr_tab[hanum]);
    cmd_ptr = ha->pccb;
    memset((char*)cmd_ptr,0,sizeof(gdth_cmd_str));

    /* make command  */
    for (retries = INIT_RETRIES;;) {
        cmd_ptr->Service          = service;
        cmd_ptr->RequestBuffer    = INTERNAL_CMND;
        if (!(index=gdth_get_cmd_index(hanum))) {
            TRACE(("GDT: No free command index found\n"));
            return 0;
        }
        gdth_set_sema0(hanum);
        cmd_ptr->OpCode           = opcode;
        cmd_ptr->BoardNode        = LOCALBOARD;
        if (service == CACHESERVICE) {
            if (opcode == GDT_IOCTL) {
                cmd_ptr->u.ioctl.subfunc = p1;
                cmd_ptr->u.ioctl.channel = p2;
                cmd_ptr->u.ioctl.param_size = (ushort)p3;
                cmd_ptr->u.ioctl.p_param = virt_to_bus(ha->pscratch);
            } else {
                cmd_ptr->u.cache.DeviceNo = (ushort)p1;
                cmd_ptr->u.cache.BlockNo  = p2;
            }
        } else if (service == SCSIRAWSERVICE) {
            cmd_ptr->u.raw.direction  = p1;
            cmd_ptr->u.raw.bus        = (unchar)p2;
            cmd_ptr->u.raw.target     = (unchar)p3;
            cmd_ptr->u.raw.lun        = 0;
        }
        ha->cmd_len          = sizeof(gdth_cmd_str);
        ha->cmd_offs_dpmem   = 0;
        ha->cmd_cnt          = 0;
        gdth_copy_command(hanum);
        gdth_release_event(hanum);
        JIFFYWAIT(2);
        if (!gdth_wait(hanum,index,INIT_TIMEOUT)) {
            printk("GDT: Initialization error (timeout service %d)\n",service);
            return 0;
        }
        if (ha->status != S_BSY || --retries == 0)
            break;
        mdelay(1);   
    }   
    
    return (ha->status != S_OK ? 0:1);
}
    

/* search for devices */

static int gdth_search_drives(int hanum)
{
    register gdth_ha_str *ha;
    ushort cdev_cnt,i;
    unchar b,t,pos_found;
    ulong drv_cyls, drv_hds, drv_secs;
    ulong bus_no;
    gdth_getch_str *chn;
    
    TRACE(("gdth_search_drives() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);

    /* initialize controller services, at first: screen service */
    if (!gdth_internal_cmd(hanum,SCREENSERVICE,GDT_INIT,0,0,0)) {
        printk("GDT: Initialization error screen service (code %d)\n",
               ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): SCREENSERVICE initialized\n"));
    
    /* initialize cache service */
    if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_INIT,LINUX_OS,0,0)) {
        printk("GDT: Initialization error cache service (code %d)\n",
               ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): CACHESERVICE initialized\n"));
    cdev_cnt = (ushort)ha->info;

    /* mount all cache devices */
    gdth_internal_cmd(hanum,CACHESERVICE,GDT_MOUNT,0xffff,1,0);
    TRACE2(("gdth_search_drives(): mountall CACHESERVICE OK\n"));

    /* initialize cache service after mountall */
    if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_INIT,LINUX_OS,0,0)) {
	printk("GDT: Initialization error cache service (code %d)\n",
	       ha->status);
	return 0;
    }
    TRACE2(("gdth_search_drives() CACHES. init. after mountall\n"));
    cdev_cnt = (ushort)ha->info;

    /* detect number of SCSI buses */
    chn = (gdth_getch_str *)DMADATA(gdth_ctr_tab[hanum]);
    for (bus_no=0; bus_no<MAXBUS; ++bus_no) {
	chn->channel_no = bus_no;
	if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
			       SCSI_CHAN_CNT | L_CTRL_PATTERN,
			       IO_CHANNEL | INVALID_CHANNEL,
			       sizeof(gdth_getch_str))) {
	    if (bus_no == 0) {
		printk("GDT: Error detecting SCSI channel count (0x%x)\n",
		       ha->status);
		return 0;
	    }
	    break;
	}
	if (chn->siop_id < MAXID)
	    ha->id[bus_no][chn->siop_id].type = SIOP_DTYP;
    }       
    ha->bus_cnt = (unchar)bus_no;
    TRACE2(("gdth_search_drives() %d SCSI channels\n",ha->bus_cnt));

    /* read cache configuration */
    if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,CACHE_INFO,
			   INVALID_CHANNEL,sizeof(gdth_cinfo_str))) {
	printk("GDT: Initialization error cache service (code %d)\n",
	       ha->status);
	return 0;
    }
    ha->cpar = ((gdth_cinfo_str *)DMADATA(gdth_ctr_tab[hanum]))->cpar;
    TRACE2(("gdth_search_drives() cinfo: vs %lx sta %d str %d dw %d b %d\n",
	    ha->cpar.version,ha->cpar.state,ha->cpar.strategy,
	    ha->cpar.write_back,ha->cpar.block_size));

    /* initialize raw service */
    if (!gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_INIT,0,0,0)) {
        printk("GDT: Initialization error raw service (code %d)\n",
               ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): RAWSERVICE initialized\n"));

    /* set/get features raw service (scatter/gather) */
    ha->raw_feat = 0;
    if (gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_SET_FEAT,SCATTER_GATHER,
                          0,0)) {
        TRACE2(("gdth_search_drives(): set features RAWSERVICE OK\n"));
        if (gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_GET_FEAT,0,0,0))
        {
            TRACE2(("gdth_search_dr(): get feat RAWSERVICE %ld\n",
                          ha->info));
            ha->raw_feat = (ushort)ha->info;
        }
    } 

    /* set/get features cache service (equal to raw service) */
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_SET_FEAT,0,
                          SCATTER_GATHER,0)) {
        TRACE2(("gdth_search_drives(): set features CACHESERVICE OK\n"));
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_GET_FEAT,0,0,0)) {
            TRACE2(("gdth_search_dr(): get feat CACHESERV. %ld\n",
                          ha->info));
            ha->cache_feat = (ushort)ha->info;
        }
    }

    /* scanning for raw devices */
    for (b=0; b<ha->bus_cnt; ++b) {
        for (t=0; t<MAXID; ++t) {
            TRACE(("gdth_search_drives() rawd. bus %d id %d\n",b,t));
            if (ha->id[b][t].type != SIOP_DTYP && 
                gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_INFO,0,b,t)) {
                ha->id[b][t].type = RAW_DTYP;
            }
        }
    }

    /* scanning for cache devices */
    for (i=0; i<cdev_cnt && i<MAX_HDRIVES; ++i) {
        TRACE(("gdth_search_drives() cachedev. %d\n",i));
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_INFO,i,0,0)) {
            /* dynamic relation between host drive number and Bus/ID */
            /* search free position */
            pos_found = FALSE;
            for (b=0,t=0; b<ha->bus_cnt; ++b) {
                for (t=0; t<MAXID; ++t) {
                    if (ha->id[b][t].type == EMPTY_DTYP) {
                        pos_found = TRUE;
                        break;
                    }
                }
                if (pos_found)
                    break;
            }
            TRACE(("gdth_search_dr() drive %d free pos at bus/id %d/%d\n",
                         i,b,t));

            ha->id[b][t].type      = CACHE_DTYP;
            ha->id[b][t].devtype   = 0;
            ha->id[b][t].size      = ha->info;
            ha->id[b][t].hostdrive = i;

            /* evaluate mapping (sectors per head, heads per cylinder) */
	    ha->id[b][t].size &= ~SECS32;
	    if (ha->info2 == 0) {
		drv_cyls = ha->id[b][t].size /HEADS/SECS;
		if (drv_cyls <= MAXCYLS) {
		    drv_hds = HEADS;
		    drv_secs= SECS;
		} else {                            /* too high for 64*32 */
		    drv_cyls = ha->id[b][t].size /MEDHEADS/MEDSECS;
		    if (drv_cyls <= MAXCYLS) {
			drv_hds = MEDHEADS;
			drv_secs= MEDSECS;
		    } else {                        /* too high for 127*63 */
			drv_cyls = ha->id[b][t].size /BIGHEADS/BIGSECS;
			drv_hds = BIGHEADS;
			drv_secs= BIGSECS;
		    }
		}
            } else {
		drv_hds = ha->info2 & 0xff;
		drv_secs = (ha->info2 >> 8) & 0xff;
		drv_cyls = ha->id[b][t].size /drv_hds/drv_secs;
	    }
            ha->id[b][t].heads = (unchar)drv_hds;
            ha->id[b][t].secs  = (unchar)drv_secs;
            /* round size */
            ha->id[b][t].size  = drv_cyls * drv_hds * drv_secs;
            TRACE2(("gdth_search_dr() cdr. %d size %ld hds %ld scs %ld\n",
                   i,ha->id[b][t].size,drv_hds,drv_secs));
            
            /* get informations about device */
            if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_DEVTYPE,i,
                                  0,0)) {
                TRACE(("gdth_search_dr() cache drive %d devtype %ld\n",
                       i,ha->info));
                ha->id[b][t].devtype = (ushort)ha->info;
            }
        }
    }

    TRACE(("gdth_search_drives() OK\n"));
    return 1;
}


/* command queueing/sending functions */

static void gdth_putq(int hanum,Scsi_Cmnd *scp,unchar priority)
{
    register gdth_ha_str *ha;
    register Scsi_Cmnd *pscp;
    register Scsi_Cmnd *nscp;
    ulong flags;
    unchar b, t;

    TRACE(("gdth_putq() priority %d\n",priority));
    save_flags(flags);
    cli();

    ha = HADATA(gdth_ctr_tab[hanum]);
    scp->SCp.this_residual = (int)priority;
    gdth_update_timeout(scp, scp->timeout * 6);
#if LINUX_VERSION_CODE >= 0x020000
    b = scp->channel;
#else
    b = NUMDATA(nscp->host)->busnum;
#endif
    t = scp->target;
#if LINUX_VERSION_CODE >= 0x010300
    if (priority >= DEFAULT_PRI && ha->id[b][t].lock) {
        TRACE2(("gdth_putq(): locked IO -> update_timeout()\n"));
        scp->SCp.buffers_residual = gdth_update_timeout(scp, 0);
    }
#endif

    if (ha->req_first==NULL) {
        ha->req_first = scp;                    /* queue was empty */
        scp->SCp.ptr = NULL;
    } else {                                    /* queue not empty */
        pscp = ha->req_first;
        nscp = (Scsi_Cmnd *)pscp->SCp.ptr;
        /* priority: 0-highest,..,0xff-lowest */
        while (nscp && (unchar)nscp->SCp.this_residual <= priority) {
            pscp = nscp;
            nscp = (Scsi_Cmnd *)pscp->SCp.ptr;
        }
        pscp->SCp.ptr = (char *)scp;
        scp->SCp.ptr  = (char *)nscp;
    }
    restore_flags(flags);

#ifdef GDTH_STATISTICS
    flags = 0;
    for (nscp=ha->req_first; nscp; nscp=(Scsi_Cmnd*)nscp->SCp.ptr)
        ++flags;
    if (max_rq < flags) {
        max_rq = flags;
        TRACE3(("GDT: max_rq = %d\n",(ushort)max_rq));
    }
#endif
}

static void gdth_next(int hanum)
{
    register gdth_ha_str *ha;
    register Scsi_Cmnd *pscp;
    register Scsi_Cmnd *nscp;
    unchar b, t, next_cmd, firsttime;
    ushort hdrive;
    ulong flags;
    int cmd_index;

    TRACE(("gdth_next() hanum %d\n",hanum));
    save_flags(flags);
    cli();

    ha = HADATA(gdth_ctr_tab[hanum]);
    ha->cmd_cnt = ha->cmd_offs_dpmem = 0;
    next_cmd = firsttime = TRUE;
    cmd_index = 0;

    for (nscp = pscp = ha->req_first; nscp; nscp = (Scsi_Cmnd *)nscp->SCp.ptr) {
        if (nscp != pscp && nscp != (Scsi_Cmnd *)pscp->SCp.ptr)
            pscp = (Scsi_Cmnd *)pscp->SCp.ptr;
#if LINUX_VERSION_CODE >= 0x020000
        b = nscp->channel;
#else
        b = NUMDATA(nscp->host)->busnum;
#endif
        t = nscp->target;
        if (nscp->SCp.this_residual < DEFAULT_PRI || !ha->id[b][t].lock) {

            if (firsttime) {
                if (gdth_test_busy(hanum)) {        /* controller busy ? */
                    TRACE(("gdth_next() controller %d busy !\n",hanum));
                    if (!gdth_polling) {
                        restore_flags(flags);
                        return;
                    }
                    while (gdth_test_busy(hanum))
                        mdelay(1);
                }
                firsttime = FALSE;
            }

#if LINUX_VERSION_CODE >= 0x010300
            if (nscp->done == gdth_scsi_done) {
                if (!(cmd_index=gdth_special_cmd(hanum,nscp,b)))
                    next_cmd = FALSE;
            } else
#endif
            if (ha->id[b][t].type != CACHE_DTYP) {
                if (!(cmd_index=gdth_fill_raw_cmd(hanum,nscp,b)))
                    next_cmd = FALSE;
            } else {
                hdrive = ha->id[b][t].hostdrive;
                switch (nscp->cmnd[0]) {
                  case TEST_UNIT_READY:
                  case INQUIRY:
                  case REQUEST_SENSE:
                  case READ_CAPACITY:
                  case VERIFY:
                  case START_STOP:
                  case MODE_SENSE:
                    TRACE2(("cache cmd %x/%x/%x/%x/%x/%x\n",nscp->cmnd[0],
                        nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                        nscp->cmnd[4],nscp->cmnd[5]));
                        gdth_internal_cache_cmd(hanum,nscp,b,&flags);
                    break;

                  case ALLOW_MEDIUM_REMOVAL:
                    TRACE2(("cache cmd %x/%x/%x/%x/%x/%x\n",nscp->cmnd[0],
                        nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                        nscp->cmnd[4],nscp->cmnd[5]));
                    if ( (nscp->cmnd[4]&1) && !(ha->id[b][t].devtype&1) ) {
                        TRACE2(("Prevent r. nonremov. drive->do nothing\n"));
                        nscp->result = DID_OK << 16;
                        restore_flags( flags );
                        nscp->scsi_done(nscp);
                        save_flags( flags );
                        cli();
                    } else {
                        nscp->cmnd[3] = (ha->id[b][t].devtype&1) ? 1:0;
                        TRACE2(("Prevent/allow r. %d rem. drive %d\n",
                            nscp->cmnd[4],nscp->cmnd[3]));
                        if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,hdrive)))
                            next_cmd = FALSE;
                    }
                    break;

                  case READ_6:
                  case WRITE_6:
                  case READ_10:
                  case WRITE_10:
                    if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,hdrive)))
                        next_cmd = FALSE;
                    break;

                  default:
                    TRACE2(("cache cmd %x/%x/%x/%x/%x/%x\n",nscp->cmnd[0],
                        nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                        nscp->cmnd[4],nscp->cmnd[5]));
                    printk("GDT: Unknown SCSI command 0x%x to cache service !\n",
                       nscp->cmnd[0]);
                    nscp->result = DID_ABORT << 16;
                    restore_flags( flags );
                    nscp->scsi_done( nscp );
                    save_flags( flags );
                    cli();
                    break;
                }
            }

            if (!next_cmd)
                break;
            if (nscp == ha->req_first)
                ha->req_first = pscp = (Scsi_Cmnd *)nscp->SCp.ptr;
            else
                pscp->SCp.ptr = nscp->SCp.ptr;
            if (gdth_polling)
                break;
        }
    }

    if (ha->cmd_cnt > 0) {
        gdth_release_event(hanum);
    }

    restore_flags(flags);

    if (gdth_polling && ha->cmd_cnt > 0) {
        if (!gdth_wait(hanum,cmd_index,POLL_TIMEOUT))
            printk("GDT: Controller %d: Command %d timed out !\n",
                   hanum,cmd_index);
    }
}
    
static void gdth_copy_internal_data(Scsi_Cmnd *scp,char *buffer,ushort count)
{
    ushort cpcount,i;
    ushort cpsum,cpnow;
    struct scatterlist *sl;

    cpcount = count<=(ushort)scp->bufflen ? count:(ushort)scp->bufflen;
    if (scp->use_sg) {
        sl = (struct scatterlist *)scp->request_buffer;
        for (i=0,cpsum=0; i<scp->use_sg; ++i,++sl) {
            cpnow = (ushort)sl->length;
            TRACE(("copy_internal() now %d sum %d count %d %d\n",
                          cpnow,cpsum,cpcount,(ushort)scp->bufflen));
            if (cpsum+cpnow > cpcount) 
                cpnow = cpcount - cpsum;
            cpsum += cpnow;
            memcpy((char*)sl->address,buffer,cpnow);
            if (cpsum == cpcount)
                break;
            buffer += cpnow;
        }
    } else {
        TRACE(("copy_internal() count %d\n",cpcount));
        memcpy((char*)scp->request_buffer,buffer,cpcount);
    }
}

static int gdth_internal_cache_cmd(int hanum,Scsi_Cmnd *scp,
                                   unchar b,ulong *flags)
{
    register gdth_ha_str *ha;
    ushort hdrive;
    unchar t;
    gdth_inq_data inq;
    gdth_rdcap_data rdc;
    gdth_sense_data sd;
    gdth_modep_data mpd;

    ha = HADATA(gdth_ctr_tab[hanum]);
    t  = scp->target;
    hdrive = ha->id[b][t].hostdrive;
    TRACE(("gdth_internal_cache_cmd() cmd 0x%x hdrive %d\n",
                 scp->cmnd[0],hdrive));

    if (scp->lun !=0)
        scp->result = DID_BAD_TARGET << 16;
    else {
        switch (scp->cmnd[0]) {
          case TEST_UNIT_READY:
          case VERIFY:
          case START_STOP:
            TRACE2(("Test/Verify/Start hdrive %d\n",hdrive));
            break;

          case INQUIRY:
            TRACE2(("Inquiry hdrive %d devtype %d\n",
                          hdrive,ha->id[b][t].devtype));
            inq.type_qual = (ha->id[b][t].devtype&4) ? TYPE_ROM:TYPE_DISK;
            /* you can here set all disks to removable, if you want to do
               a flush using the ALLOW_MEDIUM_REMOVAL command */
            inq.modif_rmb = ha->id[b][t].devtype&1 ? 0x80:0x00;
            inq.version   = 2;
            inq.resp_aenc = 2;
            inq.add_length= 32;
            strcpy(inq.vendor,"ICP    ");
            sprintf(inq.product,"Host Drive  #%02d",hdrive);
            strcpy(inq.revision,"   ");
            gdth_copy_internal_data(scp,(char*)&inq,sizeof(gdth_inq_data));
            break;

          case REQUEST_SENSE:
            TRACE2(("Request sense hdrive %d\n",hdrive));
            sd.errorcode = 0x70;
            sd.segno     = 0x00;
            sd.key       = NO_SENSE;
            sd.info      = 0;
            sd.add_length= 0;
            gdth_copy_internal_data(scp,(char*)&sd,sizeof(gdth_sense_data));
            break;

          case MODE_SENSE:
            TRACE2(("Mode sense hdrive %d\n",hdrive));
            memset((char*)&mpd,0,sizeof(gdth_modep_data));
            mpd.hd.data_length = sizeof(gdth_modep_data);
            mpd.hd.dev_par     = (ha->id[b][t].devtype&2) ? 0x80:0;
            mpd.hd.bd_length   = sizeof(mpd.bd);
            mpd.bd.block_length[0] = (SECTOR_SIZE & 0x00ff0000) >> 16;
            mpd.bd.block_length[1] = (SECTOR_SIZE & 0x0000ff00) >> 8;
            mpd.bd.block_length[2] = (SECTOR_SIZE & 0x000000ff);
            gdth_copy_internal_data(scp,(char*)&mpd,sizeof(gdth_modep_data));
            break;

          case READ_CAPACITY:
            TRACE2(("Read capacity hdrive %d\n",hdrive));
            rdc.last_block_no = ntohl(ha->id[b][t].size-1);
            rdc.block_length  = ntohl(SECTOR_SIZE);
            gdth_copy_internal_data(scp,(char*)&rdc,sizeof(gdth_rdcap_data));
            break;

          default:
            TRACE2(("Internal cache cmd 0x%x unknown\n",scp->cmnd[0]));
            break;
        }
        scp->result = DID_OK << 16;
    }

    restore_flags(*flags);
    scp->scsi_done(scp);
    save_flags(*flags);
    cli();
    return 1;
}
    
static int gdth_fill_cache_cmd(int hanum,Scsi_Cmnd *scp,ushort hdrive)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    struct scatterlist *sl;
    ushort i;
    int cmd_index;

    ha = HADATA(gdth_ctr_tab[hanum]);
    cmdp = ha->pccb;
    TRACE(("gdth_fill_cache_cmd() cmd 0x%x cmdsize %d hdrive %d\n",
                 scp->cmnd[0],scp->cmd_len,hdrive));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    cmdp->Service = CACHESERVICE;
    cmdp->RequestBuffer = scp;
    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }
    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
        gdth_set_sema0(hanum);

    /* fill command */
    if (scp->cmnd[0]==ALLOW_MEDIUM_REMOVAL) {
        if (scp->cmnd[4] & 1)                   /* prevent ? */
            cmdp->OpCode      = GDT_MOUNT;
        else if (scp->cmnd[3] & 1)              /* removable drive ? */
            cmdp->OpCode      = GDT_UNMOUNT;
        else
            cmdp->OpCode      = GDT_FLUSH;
    } else {
        if (scp->cmnd[0]==WRITE_6 || scp->cmnd[0]==WRITE_10) {
            if (gdth_write_through)
                cmdp->OpCode  = GDT_WRITE_THR;
            else
                cmdp->OpCode  = GDT_WRITE;
        } else {
            cmdp->OpCode      = GDT_READ;
        }
    }

    cmdp->BoardNode           = LOCALBOARD;
    cmdp->u.cache.DeviceNo    = hdrive;

    if (scp->cmnd[0]==ALLOW_MEDIUM_REMOVAL) {
        cmdp->u.cache.BlockNo = 1;
        cmdp->u.cache.sg_canz = 0;
    } else {
        if (scp->cmd_len != 6) {
            cmdp->u.cache.BlockNo = ntohl(*(ulong*)&scp->cmnd[2]);
            cmdp->u.cache.BlockCnt= (ulong)ntohs(*(ushort*)&scp->cmnd[7]);
        } else {
            cmdp->u.cache.BlockNo = ntohl(*(ulong*)&scp->cmnd[0]) & 0x001fffffUL;
            cmdp->u.cache.BlockCnt= scp->cmnd[4]==0 ? 0x100 : scp->cmnd[4];
        }

        if (scp->use_sg) {
            cmdp->u.cache.DestAddr= -1UL;
            sl = (struct scatterlist *)scp->request_buffer;
            for (i=0; i<scp->use_sg; ++i,++sl) {
                cmdp->u.cache.sg_lst[i].sg_ptr = virt_to_bus(sl->address);
                cmdp->u.cache.sg_lst[i].sg_len = (ulong)sl->length;
            }
            cmdp->u.cache.sg_canz = (ulong)i;

#ifdef GDTH_STATISTICS
            if (max_sg < (ulong)i) {
                max_sg = (ulong)i;
                TRACE3(("GDT: max_sg = %d\n",i));
            }
#endif
            if (i<GDTH_MAXSG)
                cmdp->u.cache.sg_lst[i].sg_len = 0;
        } else {
            if (ha->cache_feat & SCATTER_GATHER) {
                cmdp->u.cache.DestAddr = -1UL;
                cmdp->u.cache.sg_canz = 1;
                cmdp->u.cache.sg_lst[0].sg_ptr = virt_to_bus(scp->request_buffer);
                cmdp->u.cache.sg_lst[0].sg_len = scp->request_bufflen;
                cmdp->u.cache.sg_lst[1].sg_len = 0;
            } else {
                cmdp->u.cache.DestAddr  = virt_to_bus(scp->request_buffer);
                cmdp->u.cache.sg_canz= 0;
            }
        }
    }
    TRACE(("cache cmd: addr. %lx sganz %lx sgptr0 %lx sglen0 %lx\n",
                  cmdp->u.cache.DestAddr,cmdp->u.cache.sg_canz,
                  cmdp->u.cache.sg_lst[0].sg_ptr,
                  cmdp->u.cache.sg_lst[0].sg_len));
    TRACE(("cache cmd: cmd %d blockno. %ld, blockcnt %ld\n",
                  cmdp->OpCode,cmdp->u.cache.BlockNo,cmdp->u.cache.BlockCnt));

    /* evaluate command size, check space */
    ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.cache.sg_lst) +
        (ushort)cmdp->u.cache.sg_canz * sizeof(gdth_sg_str);
    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_fill_cache() DPMEM overflow\n"));
            gdth_cmd_tab[cmd_index-2][hanum].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}

static int gdth_fill_raw_cmd(int hanum,Scsi_Cmnd *scp,unchar b)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    struct scatterlist *sl;
    ushort i;
    int cmd_index;
    unchar t,l;

    ha = HADATA(gdth_ctr_tab[hanum]);
    t = scp->target;
    l = scp->lun;
    cmdp = ha->pccb;
    TRACE(("gdth_fill_raw_cmd() cmd 0x%x bus %d ID %d LUN %d\n",
                 scp->cmnd[0],b,t,l));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    cmdp->Service = SCSIRAWSERVICE;
    cmdp->RequestBuffer = scp;
    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }
    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
        gdth_set_sema0(hanum);

    /* fill command */  
    cmdp->OpCode           = GDT_WRITE;         /* always */
    cmdp->BoardNode        = LOCALBOARD;
    cmdp->u.raw.reserved   = 0;
    cmdp->u.raw.mdisc_time = 0;
    cmdp->u.raw.mcon_time  = 0;
    cmdp->u.raw.clen       = scp->cmd_len;
    cmdp->u.raw.target     = t;
    cmdp->u.raw.lun        = l;
    cmdp->u.raw.bus        = b;
    cmdp->u.raw.priority   = 0;
    cmdp->u.raw.link_p     = NULL;
    cmdp->u.raw.sdlen      = scp->request_bufflen;
    cmdp->u.raw.sense_len  = 16;
    cmdp->u.raw.sense_data = virt_to_bus(scp->sense_buffer);
    cmdp->u.raw.direction  = 
        gdth_direction_tab[scp->cmnd[0]]==DOU ? DATA_OUT : DATA_IN;
    memcpy(cmdp->u.raw.cmd,scp->cmnd,12);

    if (scp->use_sg) {
        cmdp->u.raw.sdata  = -1UL;
        sl = (struct scatterlist *)scp->request_buffer;
        for (i=0; i<scp->use_sg; ++i,++sl) {
            cmdp->u.raw.sg_lst[i].sg_ptr = virt_to_bus(sl->address);
            cmdp->u.raw.sg_lst[i].sg_len = (ulong)sl->length;
        }
        cmdp->u.raw.sg_ranz = (ulong)i;

#ifdef GDTH_STATISTICS
        if (max_sg < (ulong)i) {
            max_sg = (ulong)i;
            TRACE3(("GDT: max_sg = %d\n",i));
        }
#endif
        if (i<GDTH_MAXSG)
            cmdp->u.raw.sg_lst[i].sg_len = 0;
    } else {
        if (ha->raw_feat & SCATTER_GATHER) {
            cmdp->u.raw.sdata  = -1UL;
            cmdp->u.raw.sg_ranz= 1;
            cmdp->u.raw.sg_lst[0].sg_ptr = virt_to_bus(scp->request_buffer);
            cmdp->u.raw.sg_lst[0].sg_len = scp->request_bufflen;
            cmdp->u.raw.sg_lst[1].sg_len = 0;
        } else {
            cmdp->u.raw.sdata  = virt_to_bus(scp->request_buffer);
            cmdp->u.raw.sg_ranz= 0;
        }
    }
    TRACE(("raw cmd: addr. %lx sganz %lx sgptr0 %lx sglen0 %lx\n",
                  cmdp->u.raw.sdata,cmdp->u.raw.sg_ranz,
                  cmdp->u.raw.sg_lst[0].sg_ptr,
                  cmdp->u.raw.sg_lst[0].sg_len));

    /* evaluate command size, check space */
    ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw.sg_lst) +
        (ushort)cmdp->u.raw.sg_ranz * sizeof(gdth_sg_str);
    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_fill_raw() DPMEM overflow\n"));
            gdth_cmd_tab[cmd_index-2][hanum].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}

static int gdth_special_cmd(int hanum,Scsi_Cmnd *scp,unchar b)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    int cmd_index;

    ha  = HADATA(gdth_ctr_tab[hanum]);
    cmdp= ha->pccb;
    TRACE2(("gdth_special_cmd(): "));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    memcpy( cmdp, scp->request_buffer, sizeof(gdth_cmd_str));
    cmdp->RequestBuffer = scp;

    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }

    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
       gdth_set_sema0(hanum);

    /* evaluate command size, check space */
    if (cmdp->OpCode == GDT_IOCTL) {
        TRACE2(("IOCTL\n"));
        ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.ioctl.p_param) + sizeof(ulong);
    } else if (cmdp->Service == CACHESERVICE) {
        TRACE2(("cache command %d\n",cmdp->OpCode));
        ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.cache.sg_lst) + sizeof(gdth_sg_str);
    } else if (cmdp->Service == SCSIRAWSERVICE) {
        TRACE2(("raw command %d/%d\n",cmdp->OpCode,cmdp->u.raw.cmd[0]));
        ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw.sg_lst) + sizeof(gdth_sg_str);
    }

    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_special_cmd() DPMEM overflow\n"));
            gdth_cmd_tab[cmd_index-2][hanum].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}    


/* Controller event handling functions */
static gdth_evt_str *gdth_store_event(ushort source, ushort idx,
                                      gdth_evt_data *evt)
{
    gdth_evt_str *e;
    ulong flags;
    struct timeval tv;

    TRACE2(("gdth_store_event() source %d idx %d\n", source, idx));
    if (source == 0)                        /* no source -> no event */
        return 0;

    save_flags(flags);
    cli();
    if (ebuffer[elastidx].event_source == source &&
        ebuffer[elastidx].event_idx == idx &&
        !memcmp((char *)&ebuffer[elastidx].event_data.eu,
            (char *)&evt->eu, evt->size)) {
        e = &ebuffer[elastidx];
        do_gettimeofday(&tv);
        e->last_stamp = tv.tv_sec;
        ++e->same_count;
    } else {
        if (ebuffer[elastidx].event_source != 0) {  /* entry not free ? */
            ++elastidx;
            if (elastidx == MAX_EVENTS)
                elastidx = 0;
            if (elastidx == eoldidx) {              /* reached mark ? */
                ++eoldidx;
                if (eoldidx == MAX_EVENTS)
                    eoldidx = 0;
            }
        }
        e = &ebuffer[elastidx];
        e->event_source = source;
        e->event_idx = idx;
        do_gettimeofday(&tv);
        e->first_stamp = e->last_stamp = tv.tv_sec;
        e->same_count = 1;
        e->event_data = *evt;
    }
    restore_flags(flags);
    return e;
}

static int gdth_read_event(int handle, gdth_evt_str *estr)
{
    gdth_evt_str *e;
    int eindex;
    ulong flags;

    TRACE2(("gdth_read_event() handle %d\n", handle));
    save_flags(flags);
    cli();
    if (handle == -1)
        eindex = eoldidx;
    else
        eindex = handle;
    estr->event_source = 0;

    if (eindex >= MAX_EVENTS) {
        restore_flags(flags);
        return eindex;
    }
    e = &ebuffer[eindex];
    if (e->event_source != 0) {
        if (eindex != elastidx) {
            if (++eindex == MAX_EVENTS)
                eindex = 0;
        } else {
            eindex = -1;
        }
        memcpy(estr, e, sizeof(gdth_evt_str));
    }
    restore_flags(flags);
    return eindex;
}

static void gdth_readapp_event(unchar application, gdth_evt_str *estr)
{
    gdth_evt_str *e;
    int eindex;
    ulong flags;
    unchar found = FALSE;

    TRACE2(("gdth_readapp_event() app. %d\n", application));
    save_flags(flags);
    cli();
    eindex = eoldidx;
    for (;;) {
        e = &ebuffer[eindex];
        if (e->event_source == 0)
            break;
        if ((e->application & application) == 0) {
            e->application |= application;
            found = TRUE;
            break;
        }
        if (eindex == elastidx)
            break;
        if (++eindex == MAX_EVENTS)
            eindex = 0;
    }
    if (found)
        memcpy(estr, e, sizeof(gdth_evt_str));
    else
        estr->event_source = 0;
    restore_flags(flags);
}

static void gdth_clear_events()
{
    ulong flags;

    TRACE(("gdth_clear_events()"));
    save_flags(flags);
    cli();

    eoldidx = elastidx = 0;
    ebuffer[0].event_source = 0;
    restore_flags(flags);
}


/* SCSI interface functions */

#if LINUX_VERSION_CODE >= 0x010346
static void do_gdth_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    unsigned long flags;

    spin_lock_irqsave(&io_request_lock, flags);
    gdth_interrupt(irq, dev_id, regs);
    spin_unlock_irqrestore(&io_request_lock, flags);
}

static void gdth_interrupt(int irq,void *dev_id,struct pt_regs *regs)
#else
static void gdth_interrupt(int irq,struct pt_regs *regs)
#endif
{
    register gdth_ha_str *ha;
    gdt6m_dpram_str *dp6m_ptr;
    gdt6_dpram_str *dp6_ptr;
    gdt2_dpram_str *dp2_ptr;
    Scsi_Cmnd *scp;
    int hanum;
    unchar IStatus;
    ushort CmdStatus, Service = 0;
    ulong InfoBytes, InfoBytes2 = 0;
    gdth_evt_data dvr;

    TRACE(("gdth_interrupt() IRQ %d\n",irq));

    /* if polling and not from gdth_wait() -> return */
    if (gdth_polling) {
        if (!gdth_from_wait) {
            return;
        }
    }

    wait_index = 0;

    /* search controller */
    if ((hanum = gdth_get_status(&IStatus,irq)) == -1) {
        /*
        TRACE2(("gdth_interrupt(): Spurious interrupt received\n"));
        */
        return;
    }

#ifdef GDTH_STATISTICS
    ++act_ints;
#endif
    
    ha = HADATA(gdth_ctr_tab[hanum]);
    if (ha->type == GDT_EISA) {
        if (IStatus & 0x80) {                   /* error flag */
            IStatus &= ~0x80;
            CmdStatus = inw(ha->bmic + MAILBOXREG+8);
            TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,CmdStatus));
            if (IStatus == ASYNCINDEX) {        /* async. event ? */
                Service = inw(ha->bmic + MAILBOXREG+10);
                InfoBytes2 = inl(ha->bmic + MAILBOXREG+4);
            }
        } else                                  /* no error */
            CmdStatus = S_OK;
        InfoBytes = inl(ha->bmic + MAILBOXREG+12);
	if (gdth_polling)			/* init. -> more info */
	    InfoBytes2 = inl(ha->bmic + MAILBOXREG+4);
        outb(0xff, ha->bmic + EDOORREG);    /* acknowledge interrupt */
        outb(0x00, ha->bmic + SEMA1REG);    /* reset status semaphore */
    } else if (ha->type == GDT_ISA) {
        dp2_ptr = (gdt2_dpram_str *)ha->brd;
        if (IStatus & 0x80) {                   /* error flag */
            IStatus &= ~0x80;
            CmdStatus = readw(&dp2_ptr->u.ic.Status);
            TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,CmdStatus));
            if (IStatus == ASYNCINDEX) {        /* async. event ? */
                Service = readw(&dp2_ptr->u.ic.Service);
                InfoBytes2 = readl(&dp2_ptr->u.ic.Info[1]);
            }
        } else                                  /* no error */
            CmdStatus = S_OK;
        InfoBytes = readl(&dp2_ptr->u.ic.Info[0]);
	if (gdth_polling)			/* init. -> more info */
	    InfoBytes2 = readl(&dp2_ptr->u.ic.Info[1]);
        writeb(0xff, &dp2_ptr->io.irqdel);              /* acknowledge interrupt */
        writeb(0, &dp2_ptr->u.ic.Cmd_Index);            /* reset command index */
        writeb(0, &dp2_ptr->io.Sema1);                 /* reset status semaphore */
    } else if (ha->type == GDT_PCI) {
        dp6_ptr = (gdt6_dpram_str *)ha->brd;
        if (IStatus & 0x80) {                   /* error flag */
            IStatus &= ~0x80;
            CmdStatus = readw(&dp6_ptr->u.ic.Status);
            TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,CmdStatus));
            if (IStatus == ASYNCINDEX) {        /* async. event ? */
                Service = readw(&dp6_ptr->u.ic.Service);
                InfoBytes2 = readl(&dp6_ptr->u.ic.Info[1]);
            }
        } else                                  /* no error */
            CmdStatus = S_OK;
        InfoBytes = readl(&dp6_ptr->u.ic.Info[0]);
	if (gdth_polling)			/* init. -> more info */
	    InfoBytes2 = readl(&dp6_ptr->u.ic.Info[1]);
        writeb(0xff, &dp6_ptr->io.irqdel);              /* acknowledge interrupt */
        writeb(0, &dp6_ptr->u.ic.Cmd_Index);            /* reset command index */
        writeb(0, &dp6_ptr->io.Sema1);                 /* reset status semaphore */
    } else if (ha->type == GDT_PCINEW) {
        if (IStatus & 0x80) {                   /* error flag */
            IStatus &= ~0x80;
            CmdStatus = inw(PTR2USHORT(&ha->plx->status));
            TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,CmdStatus));
            if (IStatus == ASYNCINDEX) {        /* async. event ? */
                Service = inw(PTR2USHORT(&ha->plx->service));
                InfoBytes2 = inl(PTR2USHORT(&ha->plx->info[1]));
            }
        } else
            CmdStatus = S_OK;

        InfoBytes = inl(PTR2USHORT(&ha->plx->info[0]));
	if (gdth_polling)			/* init. -> more info */
	    InfoBytes2 = inl(PTR2USHORT(&ha->plx->info[1]));
        outb(0xff, PTR2USHORT(&ha->plx->edoor_reg)); 
        outb(0x00, PTR2USHORT(&ha->plx->sema1_reg)); 
    } else if (ha->type == GDT_PCIMPR) {
        dp6m_ptr = (gdt6m_dpram_str *)ha->brd;
        if (IStatus & 0x80) {                   /* error flag */
            IStatus &= ~0x80;
            CmdStatus = readw(&dp6m_ptr->i960r.status);
            TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,CmdStatus));
            if (IStatus == ASYNCINDEX) {        /* async. event ? */
                Service = readw(&dp6m_ptr->i960r.service);
                InfoBytes2 = readl(&dp6m_ptr->i960r.info[1]);
            }
        } else                                  /* no error */
            CmdStatus = S_OK;
        InfoBytes = readl(&dp6m_ptr->i960r.info[0]);
	if (gdth_polling)			/* init. -> more info */
	    InfoBytes2 = readl(&dp6m_ptr->i960r.info[1]);
        writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        writeb(0, &dp6m_ptr->i960r.sema1_reg);
    } else {
        TRACE2(("gdth_interrupt() unknown controller type\n"));
        return;
    }

    TRACE(("gdth_interrupt() index %d stat %d info %ld\n",
                 IStatus,CmdStatus,InfoBytes));
    ha->status = CmdStatus;
    ha->info   = InfoBytes;
    ha->info2  = InfoBytes2;

    if (gdth_from_wait) {
        wait_hanum = hanum;
        wait_index = (int)IStatus;
    }

    if (IStatus == ASYNCINDEX) {
        TRACE2(("gdth_interrupt() async. event\n"));
        gdth_async_event(hanum,Service);
    } else {
        if (IStatus == SPEZINDEX) {
            TRACE2(("Service unknown or not initialized !\n"));
            dvr.size = sizeof(dvr.eu.driver);
            dvr.eu.driver.ionode = hanum;
            gdth_store_event(ES_DRIVER, 4, &dvr);
            return;
        }
        scp     = gdth_cmd_tab[IStatus-2][hanum].cmnd;
        Service = gdth_cmd_tab[IStatus-2][hanum].service;
        gdth_cmd_tab[IStatus-2][hanum].cmnd = UNUSED_CMND;
        if (scp == UNUSED_CMND) {
            TRACE2(("gdth_interrupt() index to unused command (%d)\n",IStatus));
            dvr.size = sizeof(dvr.eu.driver);
            dvr.eu.driver.ionode = hanum;
            dvr.eu.driver.index = IStatus;
            gdth_store_event(ES_DRIVER, 1, &dvr);
            return;
        }
        if (scp == INTERNAL_CMND) {
            TRACE(("gdth_interrupt() answer to internal command\n"));
            return;
        }
        TRACE(("gdth_interrupt() sync. status\n"));
        gdth_sync_event(hanum,Service,IStatus,scp);
    }
    gdth_next(hanum);
}

static int gdth_sync_event(int hanum,int service,unchar index,Scsi_Cmnd *scp)
{
    register gdth_ha_str *ha;
    gdth_msg_str *msg;
    gdth_cmd_str *cmdp;
    char c='\r';
    ushort i;
    gdth_evt_data dvr;

    ha   = HADATA(gdth_ctr_tab[hanum]);
    cmdp = ha->pccb;
    TRACE(("gdth_sync_event() scp %lx serv %d status %d\n",
                 (ulong)scp,service,ha->status));

    if (service == SCREENSERVICE) {
        msg  = (gdth_msg_str *)ha->pscratch;
        TRACE(("len: %ld, answer: %d, ext: %d, alen: %ld\n",
                     msg->msg_len,msg->msg_answer,msg->msg_ext,msg->msg_alen));
        if (msg->msg_len)
            if (!(msg->msg_answer && msg->msg_ext)) {
                msg->msg_text[msg->msg_len] = '\0';
                printk("%s",msg->msg_text);
            }

        if (msg->msg_ext && !msg->msg_answer) {
            while (gdth_test_busy(hanum))
                udelay(1);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_READ;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.msg_handle= msg->msg_handle;
            cmdp->u.screen.msg_addr  = (ulong)msg;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.msg_addr) 
                + sizeof(ulong);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            gdth_release_event(hanum);
            return 1;
        }

        if (msg->msg_answer && msg->msg_alen) {
            for (i=0; i<msg->msg_alen && i<MSGLEN; ++i) {
                /* getchar() ?? */           
                /* .. */
                if (c == '\r')
                    break;
                msg->msg_text[i] = c; 
            }
            msg->msg_alen -= i;
            if (c!='\r' && msg->msg_alen!=0) {
                msg->msg_answer = 1;
                msg->msg_ext    = 1;
            } else {
                msg->msg_ext    = 0;
                msg->msg_answer = 0;
            }
            msg->msg_len = i;
            while (gdth_test_busy(hanum))
                udelay(1);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_WRITE;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.msg_handle= msg->msg_handle;
            cmdp->u.screen.msg_addr  = (ulong)msg;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.msg_addr) 
                + sizeof(ulong);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            gdth_release_event(hanum);
            return 1;
        }
        printk("\n");

    } else {
        scp->SCp.Message = (int)ha->status;
        /* cache or raw service */
        if (ha->status == S_OK) {
            scp->result = DID_OK << 16;
        } else if (ha->status == S_BSY) {
            TRACE2(("Controller busy -> retry !\n"));
            gdth_putq(hanum,scp,DEFAULT_PRI);
            return 1;
        } else {
            if (service == CACHESERVICE) {
                memset((char*)scp->sense_buffer,0,16);
                scp->sense_buffer[0] = 0x70;
                scp->sense_buffer[2] = NOT_READY;
                scp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);

                if (scp->done != gdth_scsi_done) {
                    dvr.size = sizeof(dvr.eu.sync);
                    dvr.eu.sync.ionode  = hanum;
                    dvr.eu.sync.service = service;
                    dvr.eu.sync.status  = ha->status;
                    dvr.eu.sync.info    = ha->info;
                    dvr.eu.sync.hostdrive =
#if LINUX_VERSION_CODE >= 0x020000
                        ha->id[scp->channel][scp->target].hostdrive;
#else
                        ha->id[NUMDATA(scp->host)->busnum][scp->target].hostdrive;
#endif
                    if (ha->status >= 0x8000)
                        gdth_store_event(ES_SYNC, 0, &dvr);
                    else
                        gdth_store_event(ES_SYNC, service, &dvr);
                }
            } else {
                if (ha->status!=S_RAW_SCSI || ha->status==S_RAW_ILL) {
                    scp->result = DID_BAD_TARGET << 16;
                } else {
                    scp->result = (DID_OK << 16) | ha->info;
                }
            }
        }
        scp->SCp.have_data_in++;
        scp->scsi_done(scp);
    }

    return 1;
}

static char *async_cache_tab[] = {
/* 0*/  "\011\000\002\002\002\004\002\006\004"
        "GDT HA %u, service %u, async. status %u/%lu unknown",
/* 1*/  "\011\000\002\002\002\004\002\006\004"
        "GDT HA %u, service %u, async. status %u/%lu unknown",
/* 2*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu not ready",
/* 3*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu: REASSIGN not successful and/or data error on reassigned blocks. Drive may crash in the future and should be replaced",
/* 4*/  "\005\000\002\006\004"
        "GDT HA %u, mirror update on Host Drive %lu failed",
/* 5*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu failed",
/* 6*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu: REASSIGN not successful and/or data error on reassigned blocks. Drive may crash in the future and should be replaced",
/* 7*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu write protected",
/* 8*/  "\005\000\002\006\004"
        "GDT HA %u, media changed in Host Drive %lu",
/* 9*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu is offline",
/*10*/  "\005\000\002\006\004"
        "GDT HA %u, media change of Mirror Drive %lu",
/*11*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu is write protected",
/*12*/  "\005\000\002\006\004"
        "GDT HA %u, general error on Host Drive %lu. Please check the devices of this drive!",
/*13*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: Cache Drive %u failed",
/*14*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: FAIL state entered",
/*15*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: error",
/*16*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: failed drive replaced by Cache Drive %u",
/*17*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity build failed",
/*18*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild failed",
/*19*/  "\007\000\002\010\002"
        "GDT HA %u, Test of Hot Fix %u failed",
/*20*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive build finished successfully",
/*21*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild finished successfully",
/*22*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: Hot Fix %u activated",
/*23*/  "\005\000\002\006\002"
        "GDT HA %u, Host Drive %u: processing of i/o aborted due to serious drive error",
/*24*/  "\005\000\002\010\002"
        "GDT HA %u, mirror update on Cache Drive %u completed",
/*25*/  "\005\000\002\010\002"
        "GDT HA %u, mirror update on Cache Drive %lu failed",
/*26*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild started",
/*27*/  "\005\000\002\012\001"
        "GDT HA %u, Fault bus %u: SHELF OK detected",
/*28*/  "\005\000\002\012\001"
        "GDT HA %u, Fault bus %u: SHELF not OK detected",
/*29*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug started",
/*30*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: new disk detected",
/*31*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: old disk detected",
/*32*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: plugging an active disk is illegal",
/*33*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: illegal device detected",
/*34*/  "\011\000\002\012\001\013\001\006\004"
        "GDT HA %u, Fault bus %u, ID %u: insufficient disk capacity (%lu MB required)",
/*35*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: disk write protected",
/*36*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: disk not available",
/*37*/  "\007\000\002\012\001\006\004"
        "GDT HA %u, Fault bus %u: swap detected (%lu)",
/*38*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug finished successfully",
/*39*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug aborted due to user Hot Plug",
/*40*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug aborted",
/*41*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug for Hot Fix started",
/*42*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive build started",
/*43*/  "\003\000\002"
        "GDT HA %u, DRAM parity error detected",
/*44*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: update started",
/*45*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Mirror Drive %u: Hot Fix %u activated",
/*46*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: no matching Pool Hot Fix Drive available",
/*47*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: Pool Hot Fix Drive available",
/*48*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: no matching Pool Hot Fix Drive available",
/*49*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: Pool Hot Fix Drive available",
/*50*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, SCSI bus %u, ID %u: IGNORE_WIDE_RESIDUE message received",
/*51*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand started",
/*52*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand finished successfully",
/*53*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand failed",
/*54*/  "\003\000\002"
        "GDT HA %u, CPU temperature critical",
/*55*/  "\003\000\002"
        "GDT HA %u, CPU temperature OK",
/*56*/  "\005\000\002\006\004"
        "GDT HA %u, Host drive %lu created",
/*57*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand restarted",
/*58*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand stopped",
};


static int gdth_async_event(int hanum,int service)
{
    gdth_stackframe stack;
    gdth_evt_data dvr;
    char *f = NULL;
    int i,j;
    gdth_ha_str *ha;
    gdth_msg_str *msg;
    gdth_cmd_str *cmdp;
    int cmd_index;

    ha  = HADATA(gdth_ctr_tab[hanum]);
    cmdp= ha->pccb;
    msg = (gdth_msg_str *)ha->pscratch;
    TRACE2(("gdth_async_event() ha %d serv %d\n",
                 hanum,service));

    if (service == SCREENSERVICE) {
        if (ha->status == MSG_REQUEST) {
            while (gdth_test_busy(hanum))
                udelay(1);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            cmd_index = gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_READ;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.msg_handle= MSG_INV_HANDLE;
            cmdp->u.screen.msg_addr  = (ulong)msg;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.msg_addr) 
                + sizeof(ulong);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            if (ha->type == GDT_EISA)
                printk("[EISA slot %d] ",(ushort)ha->brd_phys);
            else if (ha->type == GDT_ISA)
                printk("[DPMEM 0x%4X] ",(ushort)ha->brd_phys);
            else 
                printk("[PCI %d/%d] ",(ushort)(ha->brd_phys>>8),
                       (ushort)((ha->brd_phys>>3)&0x1f));
            gdth_release_event(hanum);
        }

    } else {
        dvr.size = sizeof(dvr.eu.async);
        dvr.eu.async.ionode   = hanum;
        dvr.eu.async.service = service;
        dvr.eu.async.status  = ha->status;
        dvr.eu.async.info    = ha->info;
        *(ulong *)dvr.eu.async.scsi_coord  = ha->info2;
        gdth_store_event(ES_ASYNC, service, &dvr);

        if (service==CACHESERVICE && INDEX_OK(ha->status,async_cache_tab)) {
            TRACE2(("GDT: Async. event cache service, event no.: %d\n",
                ha->status));
        
            f = async_cache_tab[ha->status];

            /* i: parameter to push, j: stack element to fill */
            for (j=0,i=1; i < f[0]; i+=2) {
                switch (f[i+1]) {
                  case 4:
                    stack.b[j++] = *(ulong*)&dvr.eu.stream[(int)f[i]];
                    break;
                  case 2:
                    stack.b[j++] = *(ushort*)&dvr.eu.stream[(int)f[i]];
                    break;
                  case 1:
                    stack.b[j++] = *(unchar*)&dvr.eu.stream[(int)f[i]];
                    break;
                  default:
                    break;
                }
            }

            printk(&f[f[0]],stack); printk("\n");

        } else {
            printk("GDT: Unknown async. event service %d event no. %d\n",
                service,ha->status);
        }
    }
    return 1;
}

#ifdef GDTH_STATISTICS
void gdth_timeout(void)
{
    ulong flags,i;
    Scsi_Cmnd *nscp;
    gdth_ha_str *ha;
    int hanum = 0;

    save_flags(flags);
    cli();

    for (act_stats=0,i=0; i<GDTH_MAXCMDS; ++i) 
        if (gdth_cmd_tab[i][hanum].cmnd != UNUSED_CMND)
            ++act_stats;

    ha = HADATA(gdth_ctr_tab[hanum]);
    for (act_rq=0,nscp=ha->req_first; nscp; nscp=(Scsi_Cmnd*)nscp->SCp.ptr)
        ++act_rq;

    TRACE2(("gdth_to(): ints %ld, ios %ld, act_stats %ld, act_rq %ld\n",
            act_ints, act_ios, act_stats, act_rq));
    act_ints = act_ios = 0;

    timer_table[GDTH_TIMER].expires = jiffies + 30*HZ;
    timer_active |= 1<<GDTH_TIMER;
    restore_flags(flags);
}
#endif

int gdth_detect(Scsi_Host_Template *shtp)
{
    struct Scsi_Host *shp;
    gdth_ha_str *ha;
    unsigned long flags;
    ulong isa_bios;
    ushort eisa_slot,device_id,index;
    gdth_pci_str pcistr;
    int i,j,hanum;
    unchar b;
    
 
#ifdef DEBUG_GDTH
    printk("GDT: This driver contains debugging information !! Trace level = %d\n",
        DebugState);
    printk("     Destination of debugging information: ");
#ifdef __SERIAL__
#ifdef __COM2__
    printk("Serial port COM2\n");
#else
    printk("Serial port COM1\n");
#endif
#else
    printk("Console\n");
#endif
    WAITSEC(3);
#endif

    TRACE(("gdth_detect()\n"));

    if (disable_gdth_scan) {
        printk("GDT: Controller driver disabled from command line !\n");
        return 0;
    }

    /* initializations */
    gdth_polling = TRUE; b = 0;
    for (i=0; i<GDTH_MAXCMDS; ++i)
        for (j=0; j<MAXHA; ++j)
            gdth_cmd_tab[i][j].cmnd = UNUSED_CMND;
    for (i=0; i<4; ++i)
        for (j=0; j<MAXHA; ++j)
            gdth_ioctl_tab[i][j] = NULL;
    gdth_clear_events();

    /* scanning for controllers, at first: ISA controller */
    for (isa_bios=0xc8000UL; isa_bios<=0xd8000UL; isa_bios+=0x8000UL) {
        if (gdth_search_isa(isa_bios)) {        /* controller found */
            shp = scsi_register(shtp,sizeof(gdth_ext_str));
            ha = HADATA(shp);
            if (!gdth_init_isa(isa_bios,ha)) {
                scsi_unregister(shp);
                continue;
            }
            /* controller found and initialized */
            printk("Configuring GDT-ISA HA at BIOS 0x%05lX IRQ %u DRQ %u\n",
                   isa_bios,ha->irq,ha->drq);

            save_flags(flags);
            cli();
#if LINUX_VERSION_CODE >= 0x010346 
            if (request_irq(ha->irq,do_gdth_interrupt,SA_INTERRUPT,"gdth",NULL))
#else
            if (request_irq(ha->irq,gdth_interrupt,SA_INTERRUPT,"gdth")) 
#endif
            {
                printk("GDT-ISA: Unable to allocate IRQ\n");
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }
            if (request_dma(ha->drq,"gdth")) {
                printk("GDT-ISA: Unable to allocate DMA channel\n");
#if LINUX_VERSION_CODE >= 0x010346 
                free_irq(ha->irq,NULL);
#else
                free_irq(ha->irq);
#endif
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }
            set_dma_mode(ha->drq,DMA_MODE_CASCADE);
            enable_dma(ha->drq);
            shp->unchecked_isa_dma = 1;
            shp->irq = ha->irq;
            shp->dma_channel = ha->drq;
            for (i=0; i<MAXID; ++i) {
                if (ha->id[0][i].type==SIOP_DTYP) {
                    shp->this_id = i;
                    break;
                }
            }
            hanum = gdth_ctr_count;         
            gdth_ctr_tab[gdth_ctr_count++] = shp;
            gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

            NUMDATA(shp)->hanum = (ushort)hanum;
            NUMDATA(shp)->busnum= 0;

            ha->pccb = CMDDATA(shp);
            ha->pscratch = DMADATA(shp);
            ha->req_first = NULL;
            for (i=0; i<MAXBUS; ++i) {
                for (j=0; j<MAXID; ++j) {
                    ha->id[i][j].type = EMPTY_DTYP;
                    ha->id[i][j].lock = 0;
                }
            }
            restore_flags(flags);

            if (!gdth_search_drives(hanum)) {
                printk("GDT-ISA: Error during device scan\n");
                --gdth_ctr_count;
		--gdth_ctr_vcount;
                save_flags(flags);
                cli();
#if LINUX_VERSION_CODE >= 0x010346 
                free_irq(ha->irq,NULL);
#else
                free_irq(ha->irq);
#endif
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }

#if LINUX_VERSION_CODE >= 0x020000
            shp->max_id      = 8;
            shp->max_lun     = 8;
            shp->max_channel = ha->bus_cnt - 1;
#else
            /* register addit. SCSI channels as virtual controllers */
            for (b=1; b<ha->bus_cnt; ++b) {
                shp = scsi_register(shtp,sizeof(gdth_num_str));
                shp->unchecked_isa_dma = 1;
                shp->irq = ha->irq;
                shp->dma_channel = ha->drq;
                for (i=0; i<MAXID; ++i) {
                    if (ha->id[b][i].type==SIOP_DTYP) {
                        shp->this_id = i;
                        break;
                    }
                }
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum = b;
            }
#endif

            gdth_enable_int(hanum);
        }
    }

    /* scanning for EISA controllers */
    for (eisa_slot=0x1000; eisa_slot<=0x8000; eisa_slot+=0x1000) {
        if (gdth_search_eisa(eisa_slot)) {      /* controller found */
            shp = scsi_register(shtp,sizeof(gdth_ext_str));
            ha = HADATA(shp);
            if (!gdth_init_eisa(eisa_slot,ha)) {
                scsi_unregister(shp);
                continue;
            }
            /* controller found and initialized */
            printk("Configuring GDT-EISA HA at Slot %d IRQ %u\n",
                   eisa_slot>>12,ha->irq);

            save_flags(flags);
            cli();
#if LINUX_VERSION_CODE >= 0x010346 
            if (request_irq(ha->irq,do_gdth_interrupt,SA_INTERRUPT,"gdth",NULL))
#else
            if (request_irq(ha->irq,gdth_interrupt,SA_INTERRUPT,"gdth")) 
#endif
            {
                printk("GDT-EISA: Unable to allocate IRQ\n");
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }
            shp->unchecked_isa_dma = 0;
            shp->irq = ha->irq;
            shp->dma_channel = 0xff;
            for (i=0; i<MAXID; ++i) {
                if (ha->id[0][i].type==SIOP_DTYP) {
                    shp->this_id = i;
                    break;
                }
            }
            hanum = gdth_ctr_count;
            gdth_ctr_tab[gdth_ctr_count++] = shp;
            gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

            NUMDATA(shp)->hanum = (ushort)hanum;
            NUMDATA(shp)->busnum= 0;
            TRACE2(("EISA detect Bus 0: shp %lx hanum %d\n",
                          (ulong)shp,NUMDATA(shp)->hanum));

            ha->pccb = CMDDATA(shp);
            ha->pscratch = DMADATA(shp);
            ha->req_first = NULL;
            for (i=0; i<MAXBUS; ++i) {
                for (j=0; j<MAXID; ++j) {
                    ha->id[i][j].type = EMPTY_DTYP;
                    ha->id[i][j].lock = 0;
                }
            }
            restore_flags(flags);

            if (!gdth_search_drives(hanum)) {
                printk("GDT-EISA: Error during device scan\n");
                --gdth_ctr_count;
		--gdth_ctr_vcount;
                save_flags(flags);
                cli();
#if LINUX_VERSION_CODE >= 0x010346 
                free_irq(ha->irq,NULL);
#else
                free_irq(ha->irq);
#endif
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }

#if LINUX_VERSION_CODE >= 0x020000
            shp->max_id      = 8;
            shp->max_lun     = 8;
            shp->max_channel = ha->bus_cnt - 1;
#else
            /* register addit. SCSI channels as virtual controllers */
            for (b=1; b<ha->bus_cnt; ++b) {
                shp = scsi_register(shtp,sizeof(gdth_num_str));
                shp->unchecked_isa_dma = 0;
                shp->irq = ha->irq;
                shp->dma_channel = 0xff;
                for (i=0; i<MAXID; ++i) {
                    if (ha->id[b][i].type==SIOP_DTYP) {
                        shp->this_id = i;
                        break;
                    }
                }
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum = b;
                TRACE2(("EISA detect Bus %d: shp %lx hanum %d\n",
                              NUMDATA(shp)->busnum,(ulong)shp,
                              NUMDATA(shp)->hanum));
            }
#endif

            gdth_enable_int(hanum);
        }
    }

    /* scanning for PCI controllers */
    for (device_id = 0; device_id <= PCI_DEVICE_ID_VORTEX_GDT6x21RP2; ++device_id) {
        if (device_id > PCI_DEVICE_ID_VORTEX_GDT6555 &&
            device_id < PCI_DEVICE_ID_VORTEX_GDT6x17RP)
            continue;
        for (index = 0; ; ++index) {
            if (!gdth_search_pci(device_id,index,&pcistr)) 
                break;                          /* next device_id */
            shp = scsi_register(shtp,sizeof(gdth_ext_str));
            ha = HADATA(shp);
            if (!gdth_init_pci(&pcistr,ha)) {
                scsi_unregister(shp);
                continue;
            }
            /* controller found and initialized */
            printk("Configuring GDT-PCI HA at %d/%d IRQ %u\n",
                   pcistr.bus,pcistr.device_fn>>3,ha->irq);

            save_flags(flags);
            cli();
#if LINUX_VERSION_CODE >= 0x010346 
            if (request_irq(ha->irq,do_gdth_interrupt,SA_INTERRUPT,"gdth",NULL))
#else
            if (request_irq(ha->irq,gdth_interrupt,SA_INTERRUPT,"gdth")) 
#endif
            {
                printk("GDT-PCI: Unable to allocate IRQ\n");
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }
            shp->unchecked_isa_dma = 0;
            shp->irq = ha->irq;
            shp->dma_channel = 0xff;
            for (i=0; i<MAXID; ++i) {
                if (ha->id[0][i].type==SIOP_DTYP) {
                    shp->this_id = i;
                    break;
                }
            }
            hanum = gdth_ctr_count;
            gdth_ctr_tab[gdth_ctr_count++] = shp;
            gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

            NUMDATA(shp)->hanum = (ushort)hanum;
            NUMDATA(shp)->busnum= 0;

            ha->pccb = CMDDATA(shp);
            ha->pscratch = DMADATA(shp);
            ha->req_first = NULL;
            for (i=0; i<MAXBUS; ++i) {
                for (j=0; j<MAXID; ++j) {
                    ha->id[i][j].type = EMPTY_DTYP;
                    ha->id[i][j].lock = 0;
                }
            }
            restore_flags(flags);

            if (!gdth_search_drives(hanum)) {
                printk("GDT-PCI: Error during device scan\n");
                --gdth_ctr_count;
		--gdth_ctr_vcount;
                save_flags(flags);
                cli();
#if LINUX_VERSION_CODE >= 0x010346 
                free_irq(ha->irq,NULL);
#else
                free_irq(ha->irq);
#endif
                restore_flags(flags);
                scsi_unregister(shp);
                continue;
            }

#if LINUX_VERSION_CODE >= 0x020000
            shp->max_id      = 8;
            shp->max_lun     = 8;
            shp->max_channel = ha->bus_cnt - 1;
#else
            /* register addit. SCSI channels as virtual controllers */
            for (b=1; b<ha->bus_cnt; ++b) {
                shp = scsi_register(shtp,sizeof(gdth_num_str));
                shp->unchecked_isa_dma = 0;
                shp->irq = ha->irq;
                shp->dma_channel = 0xff;
                for (i=0; i<MAXID; ++i) {
                    if (ha->id[b][i].type==SIOP_DTYP) {
                        shp->this_id = i;
                        break;
                    }
                }
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum = b;
            }
#endif

            gdth_enable_int(hanum);
        }
    }

    TRACE2(("gdth_detect() %d controller detected\n",gdth_ctr_count));
    if (gdth_ctr_count > 0) {
#ifdef GDTH_STATISTICS
	TRACE2(("gdth_detect(): Initializing timer !\n"));
	timer_table[GDTH_TIMER].fn = gdth_timeout;
	timer_table[GDTH_TIMER].expires = jiffies + HZ;
	timer_active |= 1<<GDTH_TIMER;
#endif
#if LINUX_VERSION_CODE >= 0x020100
	register_reboot_notifier(&gdth_notifier);
#endif
    }
    gdth_polling = FALSE;
    return gdth_ctr_vcount;
}


int gdth_release(struct Scsi_Host *shp)
{
    unsigned long flags;

    TRACE2(("gdth_release()\n"));

    save_flags(flags);
    cli();
    if (NUMDATA(shp)->busnum == 0) {
        if (shp->irq) {
#if LINUX_VERSION_CODE >= 0x010346
            free_irq(shp->irq,NULL);
#else
            free_irq(shp->irq);
#endif
        }
        if (shp->dma_channel != 0xff) {
            free_dma(shp->dma_channel);
        }
    }

    restore_flags(flags);
    scsi_unregister(shp);
    return 0;
}
            

static const char *gdth_ctr_name(int hanum)
{
    gdth_ha_str *ha;

    TRACE2(("gdth_ctr_name()\n"));

    ha    = HADATA(gdth_ctr_tab[hanum]);

    if (ha->type == GDT_EISA) {
        switch (ha->stype) {
          case GDT3_ID:
            return("GDT3000/3020 (EISA)");
          case GDT3A_ID:
            return("GDT3000A/3020A/3050A (EISA)");
          case GDT3B_ID:
            return("GDT3000B/3010A (EISA)");
        }
    } else if (ha->type == GDT_ISA) {
        return("GDT2000/2020 (ISA)");
    } else if (ha->type == GDT_PCI) {
        switch (ha->stype) {
          case PCI_DEVICE_ID_VORTEX_GDT60x0:
            return("GDT6000/6020/6050 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6000B:
            return("GDT6000B/6010 (PCI)");
        }
    } else if (ha->type == GDT_PCINEW) {
        switch (ha->stype) {
          case PCI_DEVICE_ID_VORTEX_GDT6x10:
            return("GDT6110/6510 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x20:
            return("GDT6120/6520 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6530:
            return("GDT6530 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6550:
            return("GDT6550 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x17:
            return("GDT6117/6517 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x27:
            return("GDT6127/6527 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6537:
            return("GDT6537 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6557:
            return("GDT6557/6557-ECC (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x15:
            return("GDT6115/6515 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x25:
            return("GDT6125/6525 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6535:
            return("GDT6535 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6555:
            return("GDT6555/6555-ECC (PCI)");
        }
    } else if (ha->type == GDT_PCIMPR) {
        switch (ha->stype) {
          case PCI_DEVICE_ID_VORTEX_GDT6x17RP:
            return("GDT6117RP/GDT6517RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x27RP:
            return("GDT6127RP/GDT6527RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6537RP:
            return("GDT6537RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6557RP:
            return("GDT6557RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x11RP:
            return("GDT6111RP/GDT6511RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x21RP:
            return("GDT6121RP/GDT6521RP (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x17RP1:
            return("GDT6117RP1/GDT6517RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x27RP1:
            return("GDT6127RP1/GDT6527RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6537RP1:
            return("GDT6537RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6557RP1:
            return("GDT6557RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x11RP1:
            return("GDT6111RP1/GDT6511RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x21RP1:
            return("GDT6121RP1/GDT6521RP1 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x17RP2:
            return("GDT6117RP2/GDT6517RP2 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x27RP2:
            return("GDT6127RP2/GDT6527RP2 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6537RP2:
            return("GDT6537RP2 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6557RP2:
            return("GDT6557RP2 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x11RP2:
            return("GDT6111RP2/GDT6511RP2 (PCI)");
          case PCI_DEVICE_ID_VORTEX_GDT6x21RP2:
            return("GDT6121RP2/GDT6521RP2 (PCI)");
        }
    }
    return("");
}

const char *gdth_info(struct Scsi_Host *shp)
{
    int hanum;
    
    TRACE2(("gdth_info()\n"));
    hanum = NUMDATA(shp)->hanum;

    return (gdth_ctr_name(hanum));
}


int gdth_abort(Scsi_Cmnd *scp)
{
    TRACE2(("gdth_abort() reason %d\n",scp->abort_reason));
    return SCSI_ABORT_SNOOZE;
}

#if LINUX_VERSION_CODE >= 0x010346
int gdth_reset(Scsi_Cmnd *scp, unsigned int reset_flags)
#else
int gdth_reset(Scsi_Cmnd *scp)
#endif
{
    TRACE2(("gdth_reset()\n"));
    return SCSI_RESET_PUNT;
}


#if LINUX_VERSION_CODE >= 0x010300
int gdth_bios_param(Disk *disk,kdev_t dev,int *ip)
#else
int gdth_bios_param(Disk *disk,int dev,int *ip)
#endif
{
    unchar b, t;
    int hanum;
    gdth_ha_str *ha;

    hanum = NUMDATA(disk->device->host)->hanum;
    b = disk->device->channel;
    t = disk->device->id;
    TRACE2(("gdth_bios_param() ha %d bus %d target %d\n", hanum, b, t));
    ha = HADATA(gdth_ctr_tab[hanum]);

    ip[0] = ha->id[b][t].heads;
    ip[1] = ha->id[b][t].secs;
    ip[2] = disk->capacity / ip[0] / ip[1];

    TRACE2(("gdth_bios_param(): %d heads, %d secs, %d cyls\n",
            ip[0],ip[1],ip[2]));
    return 0;
}


static void internal_done(Scsi_Cmnd *scp)
{
    scp->SCp.sent_command++;
}

int gdth_command(Scsi_Cmnd *scp)
{
    TRACE2(("gdth_command()\n"));

    scp->SCp.sent_command = 0;
    gdth_queuecommand(scp,internal_done);

    while (!scp->SCp.sent_command)
        barrier();
    return scp->result;
}


int gdth_queuecommand(Scsi_Cmnd *scp,void (*done)(Scsi_Cmnd *))
{
    int hanum;
    int priority;

    TRACE(("gdth_queuecommand() cmd 0x%x id %d lun %d\n",
                  scp->cmnd[0],scp->target,scp->lun));
    
    scp->scsi_done = (void *)done;
    scp->SCp.have_data_in = 0;
    hanum = NUMDATA(scp->host)->hanum;
#ifdef GDTH_STATISTICS
    ++act_ios;
#endif

    priority = DEFAULT_PRI;
#if LINUX_VERSION_CODE >= 0x010300
    if (scp->done == gdth_scsi_done)
        priority = scp->SCp.this_residual;
#endif
    gdth_putq( hanum, scp, priority );
    gdth_next( hanum );
    return 0;
}


/* shutdown routine */
#if LINUX_VERSION_CODE >= 0x020100
static int gdth_halt(struct notifier_block *nb, ulong event, void *buf)
#else
void gdth_halt(void)
#endif
{
    int             hanum, i, j;
    gdth_ha_str     *ha;
    Scsi_Cmnd       scp;
    Scsi_Device     sdev;
    gdth_cmd_str    gdtcmd;
    char            cmnd[12];

#if LINUX_VERSION_CODE >= 0x020100
    TRACE2(("gdth_halt() event %d\n",event));
    if (event != SYS_RESTART && event != SYS_HALT && event != SYS_POWER_OFF)
	return NOTIFY_DONE;
#else
    TRACE2(("gdth_halt()\n"));
#endif
    printk("GDT: Flushing all host drives .. ");
    for (hanum = 0; hanum < gdth_ctr_count; ++hanum) {
        ha = HADATA(gdth_ctr_tab[hanum]);
        memset(&sdev,0,sizeof(Scsi_Device));
        memset(&scp, 0,sizeof(Scsi_Cmnd));
        sdev.host = gdth_ctr_tab[hanum];
        sdev.id = sdev.host->this_id;
        scp.cmd_len = 12;
        scp.host = gdth_ctr_tab[hanum];
        scp.target = sdev.host->this_id;
        scp.device = &sdev;
        scp.use_sg = 0;

        /* flush */
        for (i = 0; i < MAXBUS; ++i) {
            for (j = 0; j < MAXID; ++j) {
                if (ha->id[i][j].type == CACHE_DTYP) {
                    gdtcmd.BoardNode = LOCALBOARD;
                    gdtcmd.Service = CACHESERVICE;
                    gdtcmd.OpCode = GDT_FLUSH;
                    gdtcmd.u.cache.DeviceNo = ha->id[i][j].hostdrive;
                    gdtcmd.u.cache.BlockNo = 1;
                    gdtcmd.u.cache.sg_canz = 0;
                    TRACE2(("gdth_halt(): flush ha %d drive %d\n",
                        hanum, ha->id[i][j].hostdrive));
                    {
                        struct semaphore sem = MUTEX_LOCKED;
                        scp.request.rq_status = RQ_SCSI_BUSY;
                        scp.request.sem = &sem;
                        scsi_do_cmd(&scp, cmnd, &gdtcmd,
                                    sizeof(gdth_cmd_str), gdth_scsi_done,
                                    30*HZ, 1);
                        down(&sem);
                    }
                }
            }
        }

        /* controller reset */
        gdtcmd.BoardNode = LOCALBOARD;
        gdtcmd.Service = CACHESERVICE;
        gdtcmd.OpCode = GDT_RESET;
        TRACE2(("gdth_halt(): reset controller %d\n", hanum));
        {
            struct semaphore sem = MUTEX_LOCKED;
            scp.request.rq_status = RQ_SCSI_BUSY;
            scp.request.sem = &sem;
            scsi_do_cmd(&scp, cmnd, &gdtcmd,
                sizeof(gdth_cmd_str), gdth_scsi_done,
                10*HZ, 1);
            down(&sem);
        }
    }
    printk("Done.\n");

#ifdef GDTH_STATISTICS
    timer_active &= ~(1<<GDTH_TIMER);
#endif
#if LINUX_VERSION_CODE >= 0x020100
    unregister_reboot_notifier(&gdth_notifier);
    return NOTIFY_OK;
#endif
}


/* called from init/main.c */
void gdth_setup(char *str,int *ints)
{
    static size_t setup_idx = 0;

    TRACE2(("gdth_setup() str %s ints[0] %d ints[1] %d\n",
                  str ? str:"NULL", ints[0],
                  ints[0] ? ints[1]:0));

    if (setup_idx >= MAXHA) {
        printk("GDT: gdth_setup() called too many times. Bad LILO params ?\n");
        return;
    }
    if (ints[0] != 1) {
        printk("GDT: Illegal command line !\n");
        printk("Usage: gdth=<IRQ>\n");
        printk("Where: <IRQ>: valid EISA controller IRQ (10,11,12,14)\n");
        printk("              or 0 to disable controller driver\n");
        return;
    }
    if (ints[1] == 10 || ints[1] == 11 || ints[1] == 12 || ints[1] == 14) {
        irqs[setup_idx++] = ints[1];
        irqs[setup_idx]   = 0xff;
        return;
    }
    if (ints[1] == 0) {
        disable_gdth_scan = TRUE;
        return;
    }
    printk("GDT: Invalid IRQ (%d) specified\n",ints[1]);
}


#ifdef MODULE
Scsi_Host_Template driver_template = GDTH;
#include "scsi_module.c"
#endif

