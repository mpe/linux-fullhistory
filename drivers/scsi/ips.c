/*****************************************************************************/
/* ips.c -- driver for the IBM ServeRAID controller                          */
/*                                                                           */
/* Written By: Keith Mitchell, IBM Corporation                               */
/*                                                                           */
/* Copyright (C) 1999 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* NO WARRANTY                                                               */
/* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        */
/* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      */
/* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      */
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    */
/* solely responsible for determining the appropriateness of using and       */
/* distributing the Program and assumes all risks associated with its        */
/* exercise of rights under this Agreement, including but not limited to     */
/* the risks and costs of program errors, damage to or loss of data,         */
/* programs or equipment, and unavailability or interruption of operations.  */
/*                                                                           */
/* DISCLAIMER OF LIABILITY                                                   */
/* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   */
/* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        */
/* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   */
/* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     */
/* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    */
/* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  */
/* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/* Bugs/Comments/Suggestions should be mailed to:                            */
/*      ipslinux@us.ibm.com                                                  */
/*                                                                           */
/*****************************************************************************/

/*****************************************************************************/
/* Change Log                                                                */
/*                                                                           */
/* 0.99.02  - Breakup commands that are bigger than 8 * the stripe size      */
/* 0.99.03  - Make interrupt routine handle all completed request on the     */
/*            adapter not just the first one                                 */
/*          - Make sure passthru commands get woken up if we run out of      */
/*            SCBs                                                           */
/*          - Send all of the commands on the queue at once rather than      */
/*            one at a time since the card will support it.                  */
/* 0.99.04  - Fix race condition in the passthru mechanism -- this required  */
/*            the interface to the utilities to change                       */
/*          - Fix error recovery code                                        */
/* 0.99.05  - Fix an oops when we get certain passthru commands              */
/* 1.00.00  - Initial Public Release                                         */
/*            Functionally equivalent to 0.99.05                             */
/* 3.60.00  - Bump max commands to 128 for use with ServeRAID firmware 3.60  */
/*          - Change version to 3.60 to coincide with ServeRAID release      */
/*            numbering.                                                     */
/* 3.60.01  - Remove bogus error check in passthru routine                   */
/* 3.60.02  - Make DCDB direction based on lookup table                      */
/*          - Only allow one DCDB command to a SCSI ID at a time             */
/*                                                                           */
/*****************************************************************************/

/*
 * Conditional Compilation directives for this driver:
 *
 * NO_IPS_RESET         - Don't reset the controller (no matter what)
 * IPS_DEBUG            - More verbose error messages
 * IPS_PCI_PROBE_DEBUG  - Print out more detail on the PCI probe
 *
 */

#if defined (MODULE)
   #include <linux/module.h>
#endif /* MODULE */

#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/stddef.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>

#include <linux/blk.h>
#include <linux/types.h>

#ifndef NO_IPS_CMDLINE
#include <scsi/sg.h>
#endif

#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "ips.h"

#include <linux/stat.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/smp.h>

/*
 * DRIVER_VER
 */
#define IPS_VERSION_HIGH        "3.60"  /* MUST be 4 chars */
#define IPS_VERSION_LOW         ".02 "  /* MUST be 4 chars */

#if !defined(__i386__)
   #error "This driver has only been tested on the x86 platform"
#endif

#if LINUX_VERSION_CODE < LinuxVersionCode(2,2,0)
   #error "This driver only works with kernel 2.2.0 and later"
#endif

#if !defined(NO_IPS_CMDLINE) && ((SG_BIG_BUFF < 8192) || !defined(SG_BIG_BUFF))
   #error "To use the command-line interface you need to define SG_BIG_BUFF"
#endif

#if IPS_DEBUG >= 12
   #define DBG(s)       printk(KERN_NOTICE s "\n"); MDELAY(2*ONE_SEC)
#elif IPS_DEBUG >= 11
   #define DBG(s)       printk(KERN_NOTICE s "\n")
#else
   #define DBG(s)
#endif

/*
 * global variables
 */
static const char * ips_name = "ips";
static struct Scsi_Host * ips_sh[IPS_MAX_ADAPTERS];  /* Array of host controller structures */
static ips_ha_t * ips_ha[IPS_MAX_ADAPTERS];          /* Array of HA structures */
static unsigned int ips_num_controllers = 0;
static int          ips_cmd_timeout = 60;
static int          ips_reset_timeout = 60 * 5;

#define MAX_ADAPTER_NAME 6

static char ips_adapter_name[][30] = {
   "ServeRAID",
   "ServeRAID II",
   "ServeRAID on motherboard",
   "ServeRAID on motherboard",
   "ServeRAID 3H",
   "ServeRAID 3L"
};

/*
 * Direction table
 */
static char ips_command_direction[] = {
IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT,
IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK,
IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT,
IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_OUT,
IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_OUT,
IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_IN,
IPS_DATA_UNK, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_UNK,
IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT,
IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_NONE,
IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT,
IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT,
IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_NONE,
IPS_DATA_UNK, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_UNK,
IPS_DATA_NONE, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_NONE,
IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_OUT,
IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_NONE,
IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_IN,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_OUT,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK
};

/*
 * Function prototypes
 */
int ips_detect(Scsi_Host_Template *);
int ips_release(struct Scsi_Host *);
int ips_abort(Scsi_Cmnd *);
int ips_reset(Scsi_Cmnd *, unsigned int);
int ips_eh_abort(Scsi_Cmnd *);
int ips_eh_reset(Scsi_Cmnd *);
int ips_queue(Scsi_Cmnd *, void (*) (Scsi_Cmnd *));
int ips_biosparam(Disk *, kdev_t, int *);
const char * ips_info(struct Scsi_Host *);
void do_ipsintr(int, void *, struct pt_regs *);
static int ips_hainit(ips_ha_t *);
static int ips_map_status(ips_scb_t *, ips_stat_t *);
static int ips_send(ips_ha_t *, ips_scb_t *, scb_callback);
static int ips_send_wait(ips_ha_t *, ips_scb_t *, int);
static int ips_send_cmd(ips_ha_t *, ips_scb_t *);
static int ips_chkstatus(ips_ha_t *);
static int ips_online(ips_ha_t *, ips_scb_t *);
static int ips_inquiry(ips_ha_t *, ips_scb_t *);
static int ips_rdcap(ips_ha_t *, ips_scb_t *);
static int ips_msense(ips_ha_t *, ips_scb_t *);
static int ips_reqsen(ips_ha_t *, ips_scb_t *);
static int ips_allocatescbs(ips_ha_t *);
static int ips_reset_adapter(ips_ha_t *);
static int ips_statupd(ips_ha_t *);
static int ips_issue(ips_ha_t *, ips_scb_t *);
static int ips_isintr(ips_ha_t *);
static int ips_wait(ips_ha_t *, int, int);
static int ips_write_driver_status(ips_ha_t *);
static int ips_read_adapter_status(ips_ha_t *);
static int ips_read_subsystem_parameters(ips_ha_t *);
static int ips_read_config(ips_ha_t *);
static int ips_clear_adapter(ips_ha_t *);
static int ips_readwrite_page5(ips_ha_t *, int);
static void ips_intr(ips_ha_t *);
static void ips_next(ips_ha_t *);
static void ipsintr_blocking(ips_ha_t *, struct ips_scb *);
static void ipsintr_done(ips_ha_t *, struct ips_scb *);
static void ips_done(ips_ha_t *, ips_scb_t *);
static void ips_free(ips_ha_t *);
static void ips_init_scb(ips_ha_t *, ips_scb_t *);
static void ips_freescb(ips_ha_t *, ips_scb_t *);
static void ips_statinit(ips_ha_t *);
static ips_scb_t * ips_getscb(ips_ha_t *);
static inline void ips_putq_scb_head(ips_scb_queue_t *, ips_scb_t *);
static inline void ips_putq_scb_tail(ips_scb_queue_t *, ips_scb_t *);
static inline ips_scb_t * ips_removeq_scb_head(ips_scb_queue_t *);
static inline ips_scb_t * ips_removeq_scb(ips_scb_queue_t *, ips_scb_t *);
static inline void ips_putq_wait_head(ips_wait_queue_t *, Scsi_Cmnd *);
static inline void ips_putq_wait_tail(ips_wait_queue_t *, Scsi_Cmnd *);
static inline Scsi_Cmnd * ips_removeq_wait_head(ips_wait_queue_t *);
static inline Scsi_Cmnd * ips_removeq_wait(ips_wait_queue_t *, Scsi_Cmnd *);

#ifndef NO_IPS_CMDLINE
static int ips_is_passthru(Scsi_Cmnd *);
static int ips_make_passthru(ips_ha_t *, Scsi_Cmnd *, ips_scb_t *);
static int ips_usrcmd(ips_ha_t *, ips_passthru_t *, ips_scb_t *);
#endif

int  ips_proc_info(char *, char **, off_t, int, int, int);
static int ips_host_info(ips_ha_t *, char *, off_t, int);
static void copy_mem_info(INFOSTR *, char *, int);
static int copy_info(INFOSTR *, char *, ...);

/*--------------------------------------------------------------------------*/
/* Exported Functions                                                       */
/*--------------------------------------------------------------------------*/

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_detect                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Detect and initialize the driver                                       */
/*                                                                          */
/****************************************************************************/
int
ips_detect(Scsi_Host_Template *SHT) {
   struct Scsi_Host *sh;
   ips_ha_t         *ha;
   u32               io_addr;
   u16               planer;
   u8                bus;
   u8                func;
   u8                irq;
   int               index;
   struct pci_dev   *dev = NULL;

   DBG("ips_detect");

   SHT->proc_info = ips_proc_info;
   SHT->proc_name = "ips";

#if defined(CONFIG_PCI)

   /* initalize number of controllers */
   ips_num_controllers = 0;

   if (!pci_present())
      return (0);

   for (index = 0; index < IPS_MAX_ADAPTERS; index++) {

      if (!(dev = pci_find_device(IPS_VENDORID, IPS_DEVICEID, dev)))
         break;
      if (pci_enable_device(dev))
      	 break;
      /* stuff that we get in dev */
      irq = dev->irq;
      bus = dev->bus->number;
      func = dev->devfn;
      io_addr = pci_resource_start(dev, 0);

      /* get planer status */
      if (pci_read_config_word(dev, 0x04, &planer)) {
         printk(KERN_WARNING "(%s%d) can't get planer status.\n",
                ips_name, index);

         continue;
      }

      /* check I/O address */
      if (pci_resource_flags(dev, 0) & IORESOURCE_MEM)
         continue;

      /* check to see if an onboard planer controller is disabled */
      if (!(planer & 0x000C)) {

   #ifdef IPS_PCI_PROBE_DEBUG
         printk(KERN_NOTICE "(%s%d) detect, Onboard ServeRAID disabled by BIOS\n",
                ips_name, index);
   #endif

         continue;
      }

   #ifdef IPS_PCI_PROBE_DEBUG
      printk(KERN_NOTICE "(%s%d) detect bus %d, func %x, irq %d, io %x\n",
             ips_name, index, bus, func, irq, io_addr);
   #endif

      /* found a controller */
      sh = scsi_register(SHT, sizeof(ips_ha_t));

      if (sh == NULL) {
         printk(KERN_WARNING "(%s%d) Unable to register controller with SCSI subsystem - skipping controller\n",
                ips_name, index);

         continue;
      }

      ha = HA(sh);
      memset(ha, 0, sizeof(ips_ha_t));

      /* Initialize spin lock */
      spin_lock_init(&ha->scb_lock);
      spin_lock_init(&ha->copp_lock);

      ips_sh[ips_num_controllers] = sh;
      ips_ha[ips_num_controllers] = ha;
      ips_num_controllers++;
      ha->active = 1;

      ha->enq = kmalloc(sizeof(ENQCMD), GFP_KERNEL|GFP_DMA);

      if (!ha->enq) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host inquiry structure - skipping contoller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->adapt = kmalloc(sizeof(ADAPTER_AREA), GFP_KERNEL|GFP_DMA);

      if (!ha->adapt) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host adapt structure - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->conf = kmalloc(sizeof(CONFCMD), GFP_KERNEL|GFP_DMA);

      if (!ha->conf) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host conf structure - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->nvram = kmalloc(sizeof(NVRAM_PAGE5), GFP_KERNEL|GFP_DMA);

      if (!ha->nvram) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host nvram structure - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->subsys = kmalloc(sizeof(SUBSYS_PARAM), GFP_KERNEL|GFP_DMA);

      if (!ha->subsys) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host subsystem structure - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->dummy = kmalloc(sizeof(BASIC_IO_CMD), GFP_KERNEL|GFP_DMA);

      if (!ha->dummy) {
         printk(KERN_WARNING "(%s%d) Unable to allocate host dummy structure - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      /* Store away needed values for later use */
      sh->io_port = io_addr;
      sh->n_io_port = 255;
      sh->unique_id = io_addr;
      sh->irq = irq;
      sh->select_queue_depths = NULL;
      sh->sg_tablesize = sh->hostt->sg_tablesize;
      sh->can_queue = sh->hostt->can_queue;
      sh->cmd_per_lun = sh->hostt->cmd_per_lun;
      sh->unchecked_isa_dma = sh->hostt->unchecked_isa_dma;
      sh->use_clustering = sh->hostt->use_clustering;

      /* Store info in HA structure */
      ha->io_addr = io_addr;
      ha->irq = irq;
      ha->host_num = index;

      /* install the interrupt handler */
      if (request_irq(irq, do_ipsintr, SA_SHIRQ, ips_name, ha)) {
         printk(KERN_WARNING "(%s%d) unable to install interrupt handler - skipping controller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      /*
       * Allocate a temporary SCB for initialization
       */
      ha->scbs = (ips_scb_t *) kmalloc(sizeof(ips_scb_t), GFP_KERNEL|GFP_DMA);
      if (!ha->scbs) {
         /* couldn't allocate a temp SCB */
         printk(KERN_WARNING "(%s%d) unable to allocate CCBs - skipping contoller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      memset(ha->scbs, 0, sizeof(ips_scb_t));
      ha->scbs->sg_list = (SG_LIST *) kmalloc(sizeof(SG_LIST) * MAX_SG_ELEMENTS, GFP_KERNEL|GFP_DMA);
      if (!ha->scbs->sg_list) {
         /* couldn't allocate a temp SCB S/G list */
         printk(KERN_WARNING "(%s%d) unable to allocate CCBs - skipping contoller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      ha->max_cmds = 1;

      if (!ips_hainit(ha)) {
         printk(KERN_WARNING "(%s%d) unable to initialize controller - skipping\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      /*
       * Free the temporary SCB
       */
      kfree(ha->scbs->sg_list);
      kfree(ha->scbs);
      ha->scbs = NULL;

      /* allocate CCBs */
      if (!ips_allocatescbs(ha)) {
         printk(KERN_WARNING "(%s%d) unable to allocate CCBs - skipping contoller\n",
                ips_name, index);

         ha->active = 0;

         continue;
      }

      /* finish setting values */
      sh->max_id = ha->ntargets;
      sh->max_lun = ha->nlun;
      sh->max_channel = ha->nbus;
      sh->can_queue = ha->max_cmds-1;
   } /* end for */

   return (ips_num_controllers);

#else

   /* No PCI -- No ServeRAID */
   return (0);
#endif /* CONFIG_PCI */
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_release                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove a driver                                                        */
/*                                                                          */
/****************************************************************************/
int
ips_release(struct Scsi_Host *sh) {
   ips_scb_t *scb;
   ips_ha_t  *ha;
   int        i;

   DBG("ips_release");

   for (i = 0; i < IPS_MAX_ADAPTERS && ips_sh[i] != sh; i++);

   if (i == IPS_MAX_ADAPTERS)
      panic("(%s) release, invalid Scsi_Host pointer.\n",
            ips_name);

   ha = HA(sh);

   if (!ha)
      return (FALSE);

   /* flush the cache on the controller */
   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_cmd_timeout;
   scb->cdb[0] = FLUSH_CACHE;

   scb->cmd.flush_cache.op_code = FLUSH_CACHE;
   scb->cmd.flush_cache.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.flush_cache.state = NORM_STATE;
   scb->cmd.flush_cache.reserved = 0;
   scb->cmd.flush_cache.reserved2 = 0;
   scb->cmd.flush_cache.reserved3 = 0;
   scb->cmd.flush_cache.reserved4 = 0;

   printk("(%s%d) Flushing Cache.\n", ips_name, ha->host_num);

   /* send command */
   if (ips_send_wait(ha, scb, ips_cmd_timeout) == IPS_FAILURE)
      printk("(%s%d) Incomplete Flush.\n", ips_name, ha->host_num);

   printk("(%s%d) Flushing Complete.\n", ips_name, ha->host_num);

   ips_sh[i] = NULL;
   ips_ha[i] = NULL;

   /* free extra memory */
   ips_free(ha);

   /* free IRQ */
   free_irq(ha->irq, ha);

   /* unregister with SCSI sub system */
   scsi_unregister(sh);

   return (FALSE);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_eh_abort                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Abort a command (using the new error code stuff)                       */
/*                                                                          */
/****************************************************************************/
int
ips_eh_abort(Scsi_Cmnd *SC) {
   ips_ha_t         *ha;

   DBG("ips_eh_abort");

   if (!SC)
      return (FAILED);

   ha = (ips_ha_t *) SC->host->hostdata;

   if (!ha)
      return (FAILED);

   if (!ha->active)
      return (FAILED);

   if (SC->serial_number != SC->serial_number_at_timeout) {
      /* HMM, looks like a bogus command */
#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Abort called with bogus scsi command\n");
#endif

      return (FAILED);
   }

   if (test_and_set_bit(IPS_IN_ABORT, &ha->flags))
      return (FAILED);

   /* See if the command is on the wait queue */
   if (ips_removeq_wait(&ha->scb_waitlist, SC) ||
       ips_removeq_wait(&ha->copp_waitlist, SC)) {
      /* command not sent yet */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (SUCCESS);
   } else {
      /* command must have already been sent */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (FAILED);
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_abort                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Abort a command                                                        */
/*                                                                          */
/****************************************************************************/
int
ips_abort(Scsi_Cmnd *SC) {
   ips_ha_t         *ha;

   DBG("ips_abort");

   if (!SC)
      return (SCSI_ABORT_SNOOZE);

   ha = (ips_ha_t *) SC->host->hostdata;

   if (!ha)
      return (SCSI_ABORT_SNOOZE);

   if (!ha->active)
      return (SCSI_ABORT_SNOOZE);

   if (SC->serial_number != SC->serial_number_at_timeout) {
      /* HMM, looks like a bogus command */
#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Abort called with bogus scsi command\n");
#endif

      return (SCSI_ABORT_NOT_RUNNING);
   }

   if (test_and_set_bit(IPS_IN_ABORT, &ha->flags))
      return (SCSI_ABORT_SNOOZE);

   /* See if the command is on the wait queue */
   if (ips_removeq_wait(&ha->scb_waitlist, SC) ||
       ips_removeq_wait(&ha->copp_waitlist, SC)) {
      /* command not sent yet */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (SCSI_ABORT_PENDING);
   } else {
      /* command must have already been sent */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (SCSI_ABORT_SNOOZE);
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_eh_reset                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller (with new eh error code)                          */
/*                                                                          */
/****************************************************************************/
int
ips_eh_reset(Scsi_Cmnd *SC) {
   ips_ha_t         *ha;
   ips_scb_t        *scb;

   DBG("ips_eh_reset");

#ifdef NO_IPS_RESET
   return (FAILED);
#else

   if (!SC) {

#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Reset called with NULL scsi command\n");
#endif

      return (FAILED);
   }

   ha = (ips_ha_t *) SC->host->hostdata;

   if (!ha) {

#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Reset called with NULL ha struct\n");
#endif

      return (FAILED);
   }

   if (!ha->active)
      return (FAILED);

   if (test_and_set_bit(IPS_IN_RESET, &ha->flags))
      return (FAILED);

   /* See if the command is on the waiting queue */
   if (ips_removeq_wait(&ha->scb_waitlist, SC) ||
       ips_removeq_wait(&ha->copp_waitlist, SC)) {
      /* command not sent yet */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (SUCCESS);
   }

   /*
    * command must have already been sent
    * reset the controller
    */
   if (!ips_reset_adapter(ha)) {
      clear_bit(IPS_IN_RESET, &ha->flags);

      return (FAILED);
   }

   if (!ips_clear_adapter(ha)) {
      clear_bit(IPS_IN_RESET, &ha->flags);

      return (FAILED);
   }

   /* Now fail all of the active commands */
#if IPS_DEBUG >= 1
   printk(KERN_WARNING "(%s%d) Failing active commands\n",
          ips_name, ha->host_num);
#endif
   while ((scb = ips_removeq_scb_head(&ha->scb_activelist))) {
      scb->scsi_cmd->result = DID_RESET << 16;
      scb->scsi_cmd->scsi_done(scb->scsi_cmd);
      ips_freescb(ha, scb);
   }

   /* Reset the number of active IOCTLs */
   ha->num_ioctl = 0;

   clear_bit(IPS_IN_RESET, &ha->flags);

   if (!test_bit(IPS_IN_INTR, &ha->flags)) {
      /*
       * Only execute the next command when
       * we are not being called from the
       * interrupt handler.  The interrupt
       * handler wants to do this and since
       * interrupts are turned off here....
       */
      ips_next(ha);
   }

   return (SUCCESS);

#endif /* NO_IPS_RESET */

}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reset                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller                                                   */
/*                                                                          */
/****************************************************************************/
int
ips_reset(Scsi_Cmnd *SC, unsigned int flags) {
   ips_ha_t         *ha;
   ips_scb_t        *scb;

   DBG("ips_reset");

#ifdef NO_IPS_RESET
   return (SCSI_RESET_SNOOZE);
#else

   if (!SC) {

#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Reset called with NULL scsi command\n");
#endif

      return (SCSI_RESET_SNOOZE);
   }

   ha = (ips_ha_t *) SC->host->hostdata;

   if (!ha) {

#if IPS_DEBUG >= 1
      printk(KERN_NOTICE "Reset called with NULL ha struct\n");
#endif

      return (SCSI_RESET_SNOOZE);
   }

   if (!ha->active)
      return (SCSI_RESET_SNOOZE);

   if (test_and_set_bit(IPS_IN_RESET, &ha->flags))
      return (SCSI_RESET_SNOOZE);

   /* See if the command is on the waiting queue */
   if (ips_removeq_wait(&ha->scb_waitlist, SC) ||
       ips_removeq_wait(&ha->copp_waitlist, SC)) {
      /* command not sent yet */
      clear_bit(IPS_IN_ABORT, &ha->flags);

      return (SCSI_RESET_SNOOZE);
   }

   /* reset the controller */
   if (!ips_reset_adapter(ha)) {
      clear_bit(IPS_IN_RESET, &ha->flags);

      return (SCSI_RESET_ERROR);
   }

   if (!ips_clear_adapter(ha)) {
      clear_bit(IPS_IN_RESET, &ha->flags);

      return (SCSI_RESET_ERROR);
   }

   /* Now fail all of the active commands */
#if IPS_DEBUG >= 1
   printk(KERN_WARNING "(%s%d) Failing active commands\n",
          ips_name, ha->host_num);
#endif
   while ((scb = ips_removeq_scb_head(&ha->scb_activelist))) {
      scb->scsi_cmd->result = DID_RESET << 16;
      scb->scsi_cmd->scsi_done(scb->scsi_cmd);
      ips_freescb(ha, scb);
   }

   /* Reset the number of active IOCTLs */
   ha->num_ioctl = 0;

   clear_bit(IPS_IN_RESET, &ha->flags);

   if (!test_bit(IPS_IN_INTR, &ha->flags)) {
      /*
       * Only execute the next command when
       * we are not being called from the
       * interrupt handler.  The interrupt
       * handler wants to do this and since
       * interrupts are turned off here....
       */
      ips_next(ha);
   }

   return (SCSI_RESET_SUCCESS);

#endif /* NO_IPS_RESET */

}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_queue                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command to the controller                                       */
/*                                                                          */
/****************************************************************************/
int
ips_queue(Scsi_Cmnd *SC, void (*done) (Scsi_Cmnd *)) {
   ips_ha_t         *ha;

   DBG("ips_queue");

   ha = (ips_ha_t *) SC->host->hostdata;

   if (!ha)
      return (1);

   if (!ha->active)
      return (1);

#ifndef NO_IPS_CMDLINE
   if (ips_is_passthru(SC)) {
      if (ha->copp_waitlist.count == IPS_MAX_IOCTL_QUEUE) {
         SC->result = DID_BUS_BUSY << 16;
         done(SC);

         return (0);
      }
   } else {
#endif
      if (ha->scb_waitlist.count == IPS_MAX_QUEUE) {
         SC->result = DID_BUS_BUSY << 16;
         done(SC);

         return (0);
      }

#ifndef NO_IPS_CMDLINE
   }
#endif

   SC->scsi_done = done;

#if IPS_DEBUG >= 10
   printk(KERN_NOTICE "%s: ips_queue: cmd 0x%X (%d %d %d)\n",
          ips_name,
          SC->cmnd[0],
          SC->channel,
          SC->target,
          SC->lun);
#if IPS_DEBUG >= 11
        MDELAY(2*ONE_SEC);
#endif
#endif

#ifndef NO_IPS_CMDLINE
   if (ips_is_passthru(SC))
      ips_putq_wait_tail(&ha->copp_waitlist, SC);
   else
#endif
      ips_putq_wait_tail(&ha->scb_waitlist, SC);

   if ((!test_bit(IPS_IN_INTR, &ha->flags)) &&
       (!test_bit(IPS_IN_ABORT, &ha->flags)) &&
       (!test_bit(IPS_IN_RESET, &ha->flags)))
      ips_next(ha);

   return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_biosparam                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Set bios geometry for the controller                                   */
/*                                                                          */
/****************************************************************************/
int
ips_biosparam(Disk *disk, kdev_t dev, int geom[]) {
   ips_ha_t         *ha;
   int               heads;
   int               sectors;
   int               cylinders;

   DBG("ips_biosparam");

   ha = (ips_ha_t *) disk->device->host->hostdata;

   if (!ha)
      /* ?!?! host adater info invalid */
      return (0);

   if (!ha->active)
      return (0);

   if (!ips_read_adapter_status(ha))
      /* ?!?! Enquiry command failed */
      return (0);

   if ((disk->capacity > 0x400000) &&
       ((ha->enq->ucMiscFlag & 0x8) == 0)) {
      heads = NORM_MODE_HEADS;
      sectors = NORM_MODE_SECTORS;
   } else {
      heads = COMP_MODE_HEADS;
      sectors = COMP_MODE_SECTORS;
   }

   cylinders = disk->capacity / (heads * sectors);

#if IPS_DEBUG >= 2
   printk(KERN_NOTICE "Geometry: heads: %d, sectors: %d, cylinders: %d\n",
          heads, sectors, cylinders);
#endif

   geom[0] = heads;
   geom[1] = sectors;
   geom[2] = cylinders;

   return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: do_ipsintr                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Wrapper for the interrupt handler                                      */
/*                                                                          */
/****************************************************************************/
void
do_ipsintr(int irq, void *dev_id, struct pt_regs *regs) {
   ips_ha_t         *ha;
   unsigned int      cpu_flags;

   DBG("do_ipsintr");

   ha = (ips_ha_t *) dev_id;

   spin_lock_irqsave(&io_request_lock, cpu_flags);

   if (test_and_set_bit(IPS_IN_INTR, &ha->flags)) {
      spin_unlock_irqrestore(&io_request_lock, cpu_flags);

      return ;
   }

   if (!ha) {
      clear_bit(IPS_IN_INTR, &ha->flags);
      spin_unlock_irqrestore(&io_request_lock, cpu_flags);

      return;
   }

   if (!ha->active) {
      clear_bit(IPS_IN_INTR, &ha->flags);
      spin_unlock_irqrestore(&io_request_lock, cpu_flags);

      return;
   }

   ips_intr(ha);

   clear_bit(IPS_IN_INTR, &ha->flags);

   spin_unlock_irqrestore(&io_request_lock, cpu_flags);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_intr                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Polling interrupt handler                                              */
/*                                                                          */
/*   ASSUMES interrupts are disabled                                        */
/*                                                                          */
/****************************************************************************/
void
ips_intr(ips_ha_t *ha) {
   ips_stat_t       *sp;
   ips_scb_t        *scb;
   int               status;

   DBG("ips_intr");

   if (!ha)
      return;

   if (!ha->active)
      return;

   while (ips_isintr(ha)) {
      sp = &ha->sp;

      if ((status = ips_chkstatus(ha)) < 0) {
         /* unexpected interrupt - no ccb */
         printk(KERN_WARNING "(%s%d) Spurious interrupt; no ccb.\n",
                ips_name, ha->host_num);
         continue ;
      }

      scb = (ips_scb_t *) sp->scb_addr;

      /*
       * use the callback function to finish things up
       * NOTE: interrupts are OFF for this
       */
      (*scb->callback) (ha, scb);
   }

   clear_bit(IPS_IN_INTR, &ha->flags);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_info                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Return info about the driver                                           */
/*                                                                          */
/****************************************************************************/
const char *
ips_info(struct Scsi_Host *SH) {
   static char  buffer[256];
   char        *bp;
   ips_ha_t    *ha;

   DBG("ips_info");

   ha = HA(SH);

   if (!ha)
      return (NULL);

   bp = &buffer[0];
   memset(bp, 0, sizeof(buffer));

   strcpy(bp, "IBM PCI ServeRAID ");
   strcat(bp, IPS_VERSION_HIGH);
   strcat(bp, IPS_VERSION_LOW);

   if (ha->ad_type > 0 &&
       ha->ad_type <= MAX_ADAPTER_NAME) {
      strcat(bp, " <");
      strcat(bp, ips_adapter_name[ha->ad_type-1]);
      strcat(bp, ">");
   }

   return (bp);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_proc_info                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   The passthru interface for the driver                                  */
/*                                                                          */
/****************************************************************************/
int
ips_proc_info(char *buffer, char **start, off_t offset,
              int length, int hostno, int func) {
   int           i;
   int           ret;
   ips_ha_t     *ha = NULL;

   DBG("ips_proc_info");

   /* Find our host structure */
   for (i = 0; i < ips_num_controllers; i++) {
      if (ips_sh[i] && ips_sh[i]->host_no == hostno) {
         ha = (ips_ha_t *) ips_sh[i]->hostdata;

         break;
      }
   }

   if (!ha)
      return (-EINVAL);

   if (func) {
      /* write */
      return (0);
   } else {
      /* read */
      if (start)
         *start = buffer;

      ret = ips_host_info(ha, buffer, offset, length);

      return (ret);
   }
}

/*--------------------------------------------------------------------------*/
/* Helper Functions                                                         */
/*--------------------------------------------------------------------------*/

#ifndef NO_IPS_CMDLINE

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_is_passthru                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Determine if the specified SCSI command is really a passthru command   */
/*                                                                          */
/****************************************************************************/
static int
ips_is_passthru(Scsi_Cmnd *SC) {
   DBG("ips_is_passthru");

   if (!SC)
      return (0);

   if ((SC->channel == 0) &&
       (SC->target == IPS_ADAPTER_ID) &&
       (SC->lun == 0) &&
       (SC->cmnd[0] == 0x0d) &&
       (SC->request_bufflen) &&
       (!SC->use_sg) &&
       (((char *) SC->request_buffer)[0] == 'C') &&
       (((char *) SC->request_buffer)[1] == 'O') &&
       (((char *) SC->request_buffer)[2] == 'P') &&
       (((char *) SC->request_buffer)[3] == 'P')) {
      return (1);
   } else {
      return (0);
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_is_passthru                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Make a passthru command out of the info in the Scsi block              */
/*                                                                          */
/****************************************************************************/
static int
ips_make_passthru(ips_ha_t *ha, Scsi_Cmnd *SC, ips_scb_t *scb) {
   ips_passthru_t *pt;

   DBG("ips_make_passthru");

   if (!SC->request_bufflen || !SC->request_buffer) {
      /* no data */
#if IPS_DEBUG_PT >= 1
      printk(KERN_NOTICE "(%s%d) No passthru structure\n",
             ips_name, ha->host_num);
#endif

      return (IPS_FAILURE);
   }

   if (SC->request_bufflen < sizeof(ips_passthru_t)) {
      /* wrong size */
#if IPS_DEBUG_PT >= 1
      printk(KERN_NOTICE "(%s%d) Passthru structure wrong size\n",
             ips_name, ha->host_num);
#endif

      return (IPS_FAILURE);
   }

   if ((((char *) SC->request_buffer)[0] != 'C') ||
       (((char *) SC->request_buffer)[1] != 'O') ||
       (((char *) SC->request_buffer)[2] != 'P') ||
       (((char *) SC->request_buffer)[3] != 'P')) {
      /* signature doesn't match */
#if IPS_DEBUG_PT >= 1
      printk(KERN_NOTICE "(%s%d) Wrong signature on passthru structure.\n",
             ips_name, ha->host_num);
#endif

      return (IPS_FAILURE);
   }

   pt = (ips_passthru_t *) SC->request_buffer;
   scb->scsi_cmd = SC;

   if (SC->request_bufflen < (sizeof(ips_passthru_t) + pt->CmdBSize)) {
      /* wrong size */
#if IPS_DEBUG_PT >= 1
      printk(KERN_NOTICE "(%s%d) Passthru structure wrong size\n",
             ips_name, ha->host_num);
#endif

      return (IPS_FAILURE);
   }

   switch (pt->CoppCmd) {
   case IPS_NUMCTRLS:
      memcpy(SC->request_buffer + sizeof(ips_passthru_t),
             &ips_num_controllers, sizeof(int));
      SC->result = DID_OK << 16;

      return (IPS_SUCCESS_IMM);
   case IPS_CTRLINFO:
      memcpy(SC->request_buffer + sizeof(ips_passthru_t),
             ha, sizeof(ips_ha_t));
      SC->result = DID_OK << 16;

      return (IPS_SUCCESS_IMM);
   case COPPUSRCMD:
      if (ips_usrcmd(ha, pt, scb))
         return (IPS_SUCCESS);
      else
         return (IPS_FAILURE);
      break;
   }

   return (IPS_FAILURE);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_usrcmd                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Process a user command and make it ready to send                       */
/*                                                                          */
/****************************************************************************/
static int
ips_usrcmd(ips_ha_t *ha, ips_passthru_t *pt, ips_scb_t *scb) {
   SG_LIST        *sg_list;

   DBG("ips_usrcmd");

   if ((!scb) || (!pt) || (!ha))
      return (0);

   /* Save the S/G list pointer so it doesn't get clobbered */
   sg_list = scb->sg_list;

   /* copy in the CP */
   memcpy(&scb->cmd, &pt->CoppCP.cmd, sizeof(IOCTL_INFO));
   memcpy(&scb->dcdb, &pt->CoppCP.dcdb, sizeof(DCDB_TABLE));

   /* FIX stuff that might be wrong */
   scb->sg_list = sg_list;
   scb->scb_busaddr = VIRT_TO_BUS(scb);
   scb->bus = 0;
   scb->target_id = 0;
   scb->lun = 0;
   scb->sg_len = 0;
   scb->data_len = 0;
   scb->flags = 0;
   scb->op_code = 0;
   scb->callback = ipsintr_done;
   scb->timeout = ips_cmd_timeout;
   scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);

   /* we don't support DCDB/READ/WRITE Scatter Gather */
   if ((scb->cmd.basic_io.op_code == READ_SCATTER_GATHER) ||
       (scb->cmd.basic_io.op_code == WRITE_SCATTER_GATHER) ||
       (scb->cmd.basic_io.op_code == DIRECT_CDB_SCATTER_GATHER))
      return (0);

   if (pt->CmdBSize) {
      scb->data_busaddr = VIRT_TO_BUS(scb->scsi_cmd->request_buffer + sizeof(ips_passthru_t));
   } else {
      scb->data_busaddr = 0L;
   }

   if (pt->CmdBSize) {
      if (scb->cmd.dcdb.op_code == DIRECT_CDB) {
         scb->cmd.dcdb.dcdb_address = VIRT_TO_BUS(&scb->dcdb);
         scb->dcdb.buffer_pointer = scb->data_busaddr;
      } else {
         scb->cmd.basic_io.sg_addr = scb->data_busaddr;
      }
   }

   /* set timeouts */
   if (pt->TimeOut) {
      scb->timeout = pt->TimeOut;

      if (pt->TimeOut <= 10)
         scb->dcdb.cmd_attribute |= TIMEOUT_10;
      else if (pt->TimeOut <= 60)
         scb->dcdb.cmd_attribute |= TIMEOUT_60;
      else
         scb->dcdb.cmd_attribute |= TIMEOUT_20M;
   }

   /* assume error */
   scb->scsi_cmd->result = DID_ERROR << 16;

   /* success */
   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_cleanup_passthru                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Cleanup after a passthru command                                       */
/*                                                                          */
/****************************************************************************/
static void
ips_cleanup_passthru(ips_scb_t *scb) {
   ips_passthru_t *pt;

   DBG("ips_cleanup_passthru");

   if ((!scb) || (!scb->scsi_cmd) || (!scb->scsi_cmd->request_buffer)) {
#if IPS_DEBUG_PT >= 1
      printk(KERN_NOTICE "IPS couldn't cleanup\n");
#endif

      return ;
   }

   pt = (ips_passthru_t *) scb->scsi_cmd->request_buffer;

   /* Copy data back to the user */
   pt->BasicStatus = scb->basic_status;
   pt->ExtendedStatus = scb->extended_status;

   scb->scsi_cmd->result = DID_OK << 16;
}

#endif

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_host_info                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   The passthru interface for the driver                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_host_info(ips_ha_t *ha, char *ptr, off_t offset, int len) {
   INFOSTR info;

   DBG("ips_host_info");

   info.buffer = ptr;
   info.length = len;
   info.offset = offset;
   info.pos = 0;

   copy_info(&info, "\nIBM ServeRAID General Information:\n\n");

   if ((ha->nvram->signature == NVRAM_PAGE5_SIGNATURE) &&
       (ha->nvram->adapter_type != 0))
      copy_info(&info, "\tController Type                   : %s\n", ips_adapter_name[ha->ad_type-1]);
   else
      copy_info(&info, "\tController Type                   : Unknown\n");

   copy_info(&info, "\tIO port address                   : 0x%lx\n", ha->io_addr);
   copy_info(&info, "\tIRQ number                        : %d\n", ha->irq);

   if (ha->nvram->signature == NVRAM_PAGE5_SIGNATURE)
      copy_info(&info, "\tBIOS Version                      : %c%c%c%c%c%c%c%c\n",
                ha->nvram->bios_high[0], ha->nvram->bios_high[1],
                ha->nvram->bios_high[2], ha->nvram->bios_high[3],
                ha->nvram->bios_low[0], ha->nvram->bios_low[1],
                ha->nvram->bios_low[2], ha->nvram->bios_low[3]);

   copy_info(&info, "\tFirmware Version                  : %c%c%c%c%c%c%c%c\n",
             ha->enq->CodeBlkVersion[0], ha->enq->CodeBlkVersion[1],
             ha->enq->CodeBlkVersion[2], ha->enq->CodeBlkVersion[3],
             ha->enq->CodeBlkVersion[4], ha->enq->CodeBlkVersion[5],
             ha->enq->CodeBlkVersion[6], ha->enq->CodeBlkVersion[7]);

   copy_info(&info, "\tBoot Block Version                : %c%c%c%c%c%c%c%c\n",
             ha->enq->BootBlkVersion[0], ha->enq->BootBlkVersion[1],
             ha->enq->BootBlkVersion[2], ha->enq->BootBlkVersion[3],
             ha->enq->BootBlkVersion[4], ha->enq->BootBlkVersion[5],
             ha->enq->BootBlkVersion[6], ha->enq->BootBlkVersion[7]);

   copy_info(&info, "\tDriver Version                    : %s%s\n",
             IPS_VERSION_HIGH, IPS_VERSION_LOW);

   copy_info(&info, "\tMax Physical Devices              : %d\n",
             ha->enq->ucMaxPhysicalDevices);
   copy_info(&info, "\tMax Active Commands               : %d\n",
             ha->max_cmds);
   copy_info(&info, "\tCurrent Queued Commands           : %d\n",
             ha->scb_waitlist.count);
   copy_info(&info, "\tCurrent Active Commands           : %d\n",
             ha->scb_activelist.count - ha->num_ioctl);
   copy_info(&info, "\tCurrent Queued PT Commands        : %d\n",
             ha->copp_waitlist.count);
   copy_info(&info, "\tCurrent Active PT Commands        : %d\n",
             ha->num_ioctl);

   copy_info(&info, "\n");

   return (info.pos > info.offset ? info.pos - info.offset : 0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: copy_mem_info                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Copy data into an INFOSTR structure                                    */
/*                                                                          */
/****************************************************************************/
static void
copy_mem_info(INFOSTR *info, char *data, int len) {
   DBG("copy_mem_info");

   if (info->pos + len > info->length)
      len = info->length - info->pos;

   if (info->pos + len < info->offset) {
      info->pos += len;
      return;
   }

   if (info->pos < info->offset) {
      data += (info->offset - info->pos);
      len  -= (info->offset - info->pos);
   }

   if (len > 0) {
      memcpy(info->buffer + info->pos, data, len);
      info->pos += len;
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: copy_info                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   printf style wrapper for an info structure                             */
/*                                                                          */
/****************************************************************************/
static int
copy_info(INFOSTR *info, char *fmt, ...) {
   va_list args;
   char buf[81];
   int len;

   DBG("copy_info");

   va_start(args, fmt);
   len = vsprintf(buf, fmt, args);
   va_end(args);

   copy_mem_info(info, buf, len);

   return (len);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_hainit                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize the controller                                              */
/*                                                                          */
/* NOTE: Assumes to be called from with a lock                              */
/*                                                                          */
/****************************************************************************/
static int
ips_hainit(ips_ha_t *ha) {
   int       i;

   DBG("ips_hainit");

   if (!ha)
      return (0);

   /* initialize status queue */
   ips_statinit(ha);

   /* Setup HBA ID's */
   if (!ips_read_config(ha)) {

#ifndef NO_IPS_RESET

      /* Try to reset the controller and try again */
      if (!ips_reset_adapter(ha)) {
         printk(KERN_WARNING "(%s%d) unable to reset controller.\n",
                ips_name, ha->host_num);

         return (0);
      }

      if (!ips_clear_adapter(ha)) {
         printk(KERN_WARNING "(%s%d) unable to initialize controller.\n",
                ips_name, ha->host_num);

         return (0);
      }

#endif

      if (!ips_read_config(ha)) {
         printk(KERN_WARNING "(%s%d) unable to read config from controller.\n",
                ips_name, ha->host_num);

         return (0);
      }
   } /* end if */

   /* write driver version */
   if (!ips_write_driver_status(ha)) {
      printk(KERN_WARNING "(%s%d) unable to write driver info to controller.\n",
             ips_name, ha->host_num);

      return (0);
   }

   if (!ips_read_adapter_status(ha)) {
      printk(KERN_WARNING "(%s%d) unable to read controller status.\n",
             ips_name, ha->host_num);

      return (0);
   }

   if (!ips_read_subsystem_parameters(ha)) {
      printk(KERN_WARNING "(%s%d) unable to read subsystem parameters.\n",
             ips_name, ha->host_num);

      return (0);
   }

   /* set limits on SID, LUN, BUS */
   ha->ntargets = MAX_TARGETS + 1;
   ha->nlun = 1;
   ha->nbus = (ha->enq->ucMaxPhysicalDevices / MAX_TARGETS);

   switch (ha->conf->logical_drive[0].ucStripeSize) {
   case 4:
      ha->max_xfer = 0x10000;
      break;

   case 5:
      ha->max_xfer = 0x20000;
      break;

   case 6:
      ha->max_xfer = 0x40000;
      break;

   case 7:
   default:
      ha->max_xfer = 0x80000;
      break;
   }

   /* setup max concurrent commands */
   if (ha->subsys->param[4] & 0x1) {
      /* Use the new method */
      ha->max_cmds = ha->enq->ucConcurrentCmdCount;
   } else {
      /* use the old method */
      switch (ha->conf->logical_drive[0].ucStripeSize) {
      case 4:
         ha->max_cmds = 32;
         break;

      case 5:
         ha->max_cmds = 16;
         break;

      case 6:
         ha->max_cmds = 8;
         break;

      case 7:
      default:
         ha->max_cmds = 4;
         break;
      }
   }

   /* set controller IDs */
   ha->ha_id[0] = IPS_ADAPTER_ID;
   for (i = 1; i < ha->nbus; i++) {
      ha->ha_id[i] = ha->conf->init_id[i-1] & 0x1f;
      ha->dcdb_active[i-1] = 0;
   }

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_next                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Take the next command off the queue and send it to the controller      */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static void
ips_next(ips_ha_t *ha) {
   ips_scb_t    *scb;
   Scsi_Cmnd    *SC;
   Scsi_Cmnd    *p;
   int           ret;

   DBG("ips_next");

   if (!ha)
      return ;

#ifndef NO_IPS_CMDLINE
   /*
    * Send passthru commands
    * These have priority over normal I/O
    * but shouldn't affect performance too much
    * since we limit the number that can be active
    * on the card at any one time
    */
   while ((ha->num_ioctl < IPS_MAX_IOCTL) &&
          (ha->copp_waitlist.head) &&
          (scb = ips_getscb(ha))) {
      SC = ips_removeq_wait_head(&ha->copp_waitlist);

      ret = ips_make_passthru(ha, SC, scb);

      switch (ret) {
      case IPS_FAILURE:
         if (scb->scsi_cmd) {
            scb->scsi_cmd->result = DID_ERROR << 16;
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);
         }
         ips_freescb(ha, scb);
         break;
      case IPS_SUCCESS_IMM:
         if (scb->scsi_cmd)
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);
         ips_freescb(ha, scb);
         break;
      default:
         break;
      } /* end case */

      if (ret != IPS_SUCCESS)
         continue;

      ret = ips_send_cmd(ha, scb);

      if (ret == IPS_SUCCESS) {
         ips_putq_scb_head(&ha->scb_activelist, scb);
         ha->num_ioctl++;
      }

      switch(ret) {
      case IPS_FAILURE:
         if (scb->scsi_cmd) {
            scb->scsi_cmd->result = DID_ERROR << 16;
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);
         }

         ips_freescb(ha, scb);
         break;
      case IPS_SUCCESS_IMM:
         if (scb->scsi_cmd)
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);
         ips_freescb(ha, scb);
         break;
      default:
         break;
      } /* end case */
   }
#endif

   /*
    * Send "Normal" I/O commands
    */
   p = ha->scb_waitlist.head;
   while ((p) && (scb = ips_getscb(ha))) {
      if ((p->channel > 0) && (ha->dcdb_active[p->channel-1] & (1 << p->target))) {
         ips_freescb(ha, scb);
         p = (Scsi_Cmnd *) p->host_scribble;
         continue;
      }

      SC = ips_removeq_wait(&ha->scb_waitlist, p);

      SC->result = DID_OK;
      SC->host_scribble = NULL;

      memset(SC->sense_buffer, 0, sizeof(SC->sense_buffer));

      scb->target_id = SC->target;
      scb->lun = SC->lun;
      scb->bus = SC->channel;
      scb->scsi_cmd = SC;
      scb->breakup = 0;
      scb->data_len = 0;
      scb->callback = ipsintr_done;
      scb->timeout = ips_cmd_timeout;
      memset(&scb->cmd, 0, 16);

      /* copy in the CDB */
      memcpy(scb->cdb, SC->cmnd, SC->cmd_len);

      /* Now handle the data buffer */
      if (SC->use_sg) {
         struct scatterlist *sg;
         int                 i;

         sg = SC->request_buffer;

         for (i = 0; i < SC->use_sg; i++) {
            scb->sg_list[i].address = VIRT_TO_BUS(sg[i].address);
            scb->sg_list[i].length = sg[i].length;

            if (scb->data_len + sg[i].length > ha->max_xfer) {
               /*
                * Data Breakup required
                */
               scb->breakup = i;
               break;
            }

            scb->data_len += sg[i].length;
         }

         if (!scb->breakup)
            scb->sg_len = SC->use_sg;
         else
            scb->sg_len = scb->breakup;

         scb->dcdb.transfer_length = scb->data_len;
         scb->data_busaddr = VIRT_TO_BUS(scb->sg_list);
      } else {
         if (SC->request_bufflen) {
            if (SC->request_bufflen > ha->max_xfer) {
               /*
                * Data breakup required
                */
               scb->breakup = 1;
               scb->data_len = ha->max_xfer;
            } else {
               scb->data_len = SC->request_bufflen;
            }

            scb->dcdb.transfer_length = scb->data_len;
            scb->data_busaddr = VIRT_TO_BUS(SC->request_buffer);
            scb->sg_len = 0;
         } else {
            scb->data_busaddr = 0L;
            scb->sg_len = 0;
            scb->data_len = 0;
            scb->dcdb.transfer_length = 0;
         }

      }

      scb->dcdb.cmd_attribute |=
         ips_command_direction[scb->scsi_cmd->cmnd[0]];

      if (!scb->dcdb.cmd_attribute & 0x3)
         scb->dcdb.transfer_length = 0;

      if (scb->data_len >= IPS_MAX_XFER) {
         scb->dcdb.cmd_attribute |= TRANSFER_64K;
         scb->dcdb.transfer_length = 0;
      }

      ret = ips_send_cmd(ha, scb);

      if (ret == IPS_SUCCESS)
         ips_putq_scb_head(&ha->scb_activelist, scb);

      switch(ret) {
      case IPS_FAILURE:
         if (scb->scsi_cmd) {
            scb->scsi_cmd->result = DID_ERROR << 16;
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);
         }

         if (scb->bus)
            ha->dcdb_active[scb->bus-1] &= ~(1 << scb->target_id);

         ips_freescb(ha, scb);
         break;
      case IPS_SUCCESS_IMM:
         if (scb->scsi_cmd)
            scb->scsi_cmd->scsi_done(scb->scsi_cmd);

         if (scb->bus)
            ha->dcdb_active[scb->bus-1] &= ~(1 << scb->target_id);

         ips_freescb(ha, scb);
         break;
      default:
         break;
      } /* end case */

      p = (Scsi_Cmnd *) p->host_scribble;
   } /* end while */
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_scb_head                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the head of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline void
ips_putq_scb_head(ips_scb_queue_t *queue, ips_scb_t *item) {
   DBG("ips_putq_scb_head");

   if (!item)
      return ;

   item->q_next = queue->head;
   queue->head = item;

   if (!queue->tail)
      queue->tail = item;

   queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_scb_tail                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the tail of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline void
ips_putq_scb_tail(ips_scb_queue_t *queue, ips_scb_t *item) {
   DBG("ips_putq_scb_tail");

   if (!item)
      return ;

   item->q_next = NULL;

   if (queue->tail)
      queue->tail->q_next = item;

   queue->tail = item;

   if (!queue->head)
      queue->head = item;

   queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_scb_head                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove the head of the queue                                           */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline ips_scb_t *
ips_removeq_scb_head(ips_scb_queue_t *queue) {
   ips_scb_t  *item;

   DBG("ips_removeq_scb_head");

   item = queue->head;

   if (!item)
      return (NULL);

   queue->head = item->q_next;
   item->q_next = NULL;

   if (queue->tail == item)
      queue->tail = NULL;

   queue->count--;

   return (item);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_scb                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an item from a queue                                            */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline ips_scb_t *
ips_removeq_scb(ips_scb_queue_t *queue, ips_scb_t *item) {
   ips_scb_t  *p;

   DBG("ips_removeq_scb");

   if (!item)
      return (NULL);

   if (item == queue->head)
      return (ips_removeq_scb_head(queue));

   p = queue->head;

   while ((p) && (item != p->q_next))
      p = p->q_next;

   if (p) {
      /* found a match */
      p->q_next = item->q_next;

      if (!item->q_next)
         queue->tail = p;

      item->q_next = NULL;
      queue->count--;

      return (item);
   }

   return (NULL);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_wait_head                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the head of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline void
ips_putq_wait_head(ips_wait_queue_t *queue, Scsi_Cmnd *item) {
   DBG("ips_putq_wait_head");

   if (!item)
      return ;

   item->host_scribble = (char *) queue->head;
   queue->head = item;

   if (!queue->tail)
      queue->tail = item;

   queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_wait_tail                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the tail of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline void
ips_putq_wait_tail(ips_wait_queue_t *queue, Scsi_Cmnd *item) {
   DBG("ips_putq_wait_tail");

   if (!item)
      return ;

   item->host_scribble = NULL;

   if (queue->tail)
      queue->tail->host_scribble = (char *)item;

   queue->tail = item;

   if (!queue->head)
      queue->head = item;

   queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_wait_head                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove the head of the queue                                           */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline Scsi_Cmnd *
ips_removeq_wait_head(ips_wait_queue_t *queue) {
   Scsi_Cmnd  *item;

   DBG("ips_removeq_wait_head");

   item = queue->head;

   if (!item)
      return (NULL);

   queue->head = (Scsi_Cmnd *) item->host_scribble;
   item->host_scribble = NULL;

   if (queue->tail == item)
      queue->tail = NULL;

   queue->count--;

   return (item);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_wait                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an item from a queue                                            */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static inline Scsi_Cmnd *
ips_removeq_wait(ips_wait_queue_t *queue, Scsi_Cmnd *item) {
   Scsi_Cmnd  *p;

   DBG("ips_removeq_wait");

   if (!item)
      return (NULL);

   if (item == queue->head)
      return (ips_removeq_wait_head(queue));

   p = queue->head;

   while ((p) && (item != (Scsi_Cmnd *) p->host_scribble))
      p = (Scsi_Cmnd *) p->host_scribble;

   if (p) {
      /* found a match */
      p->host_scribble = item->host_scribble;

      if (!item->host_scribble)
         queue->tail = p;

      item->host_scribble = NULL;
      queue->count--;

      return (item);
   }

   return (NULL);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ipsintr_blocking                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Finalize an interrupt for internal commands                            */
/*                                                                          */
/****************************************************************************/
static void
ipsintr_blocking(ips_ha_t *ha, ips_scb_t *scb) {
   DBG("ipsintr_blocking");

   if ((ha->waitflag == TRUE) &&
       (ha->cmd_in_progress == scb->cdb[0])) {
      ha->waitflag = FALSE;

      return ;
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ipsintr_done                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Finalize an interrupt for non-internal commands                        */
/*                                                                          */
/****************************************************************************/
static void
ipsintr_done(ips_ha_t *ha, ips_scb_t *scb) {
   DBG("ipsintr_done");

   if (scb->scsi_cmd == NULL) {
      /* unexpected interrupt */
      printk(KERN_WARNING "(%s%d) Spurious interrupt; scsi_cmd not set.\n",
             ips_name, ha->host_num);

      return;
   }

   ips_done(ha, scb);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_done                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Do housekeeping on completed commands                                  */
/*                                                                          */
/****************************************************************************/
static void
ips_done(ips_ha_t *ha, ips_scb_t *scb) {
   int ret;

   DBG("ips_done");

   if (!scb)
      return ;

#ifndef NO_IPS_CMDLINE
   if ((scb->scsi_cmd) && (ips_is_passthru(scb->scsi_cmd))) {
      ips_cleanup_passthru(scb);
      ha->num_ioctl--;
   } else {
#endif
      /*
       * Check to see if this command had too much
       * data and had to be broke up.  If so, queue
       * the rest of the data and continue.
       */
      if (scb->breakup) {
         /* we had a data breakup */
         u16 bk_save;

         bk_save = scb->breakup;
         scb->breakup = 0;

         if (scb->scsi_cmd->use_sg) {
            /* S/G request */
            struct scatterlist *sg;
            int                 i;

            sg = scb->scsi_cmd->request_buffer;

            scb->data_len = 0;

            for (i = bk_save; i < scb->scsi_cmd->use_sg; i++) {
               scb->sg_list[i - bk_save].address = VIRT_TO_BUS(sg[i].address);
               scb->sg_list[i - bk_save].length = sg[i].length;

               if (scb->data_len + sg[i].length > ha->max_xfer) {
                  /*
                   * Data Breakup required
                   */
                  scb->breakup = i;
                  break;
               }

               scb->data_len += sg[i].length;
            }

            if (!scb->breakup)
               scb->sg_len = scb->scsi_cmd->use_sg - bk_save;
            else
               scb->sg_len = scb->breakup - bk_save;

            scb->dcdb.transfer_length = scb->data_len;
            scb->data_busaddr = VIRT_TO_BUS(scb->sg_list);
         } else {
            /* Non S/G Request */
            if (scb->scsi_cmd->request_bufflen - (bk_save * ha->max_xfer)) {
               /* Further breakup required */
               scb->data_len = ha->max_xfer;
               scb->data_busaddr = VIRT_TO_BUS(scb->scsi_cmd->request_buffer + (bk_save * ha->max_xfer));
               scb->breakup = bk_save + 1;
            } else {
               scb->data_len = scb->scsi_cmd->request_bufflen - (bk_save * ha->max_xfer);
               scb->data_busaddr = VIRT_TO_BUS(scb->scsi_cmd->request_buffer + (bk_save * ha->max_xfer));
            }

            scb->dcdb.transfer_length = scb->data_len;
            scb->sg_len = 0;
         }

         scb->dcdb.cmd_attribute |=
            ips_command_direction[scb->scsi_cmd->cmnd[0]];
  
         if (!scb->dcdb.cmd_attribute & 0x3)
            scb->dcdb.transfer_length = 0;
         if (scb->data_len >= IPS_MAX_XFER) {
            scb->dcdb.cmd_attribute |= TRANSFER_64K;
            scb->dcdb.transfer_length = 0;
         }

         ret = ips_send_cmd(ha, scb);

         switch(ret) {
         case IPS_FAILURE:
            if (scb->scsi_cmd) {
               scb->scsi_cmd->result = DID_ERROR << 16;
               scb->scsi_cmd->scsi_done(scb->scsi_cmd);
            }

            ips_freescb(ha, scb);
            break;
         case IPS_SUCCESS_IMM:
            if (scb->scsi_cmd) {
               scb->scsi_cmd->result = DID_ERROR << 16;
               scb->scsi_cmd->scsi_done(scb->scsi_cmd);
            }

            ips_freescb(ha, scb);
            break;
         default:
            break;
         } /* end case */

         return ;
      }
#ifndef NO_IPS_CMDLINE
   } /* end if passthru */
#endif

   if (scb->bus)
      ha->dcdb_active[scb->bus-1] &= ~(1 << scb->target_id);

   /* call back to SCSI layer */
   scb->scsi_cmd->scsi_done(scb->scsi_cmd);
   ips_freescb(ha, scb);

   /* do the next command */
   ips_next(ha);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_map_status                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Map ServeRAID error codes to Linux Error Codes                         */
/*                                                                          */
/****************************************************************************/
static int
ips_map_status(ips_scb_t *scb, ips_stat_t *sp) {
   int       errcode;

   DBG("ips_map_status");

   if (scb->bus) {
#if IPS_DEBUG >= 10
      printk(KERN_NOTICE "(%s) Physical device error: %x %x, Sense Key: %x, ASC: %x, ASCQ: %x\n",
             ips_name,
             scb->basic_status,
             scb->extended_status,
             scb->dcdb.sense_info[2] & 0xf,
             scb->dcdb.sense_info[12],
             scb->dcdb.sense_info[13]);
#endif

      /* copy SCSI status and sense data for DCDB commands */
      memcpy(scb->scsi_cmd->sense_buffer, scb->dcdb.sense_info,
             sizeof(scb->scsi_cmd->sense_buffer));
      scb->scsi_cmd->result = scb->dcdb.scsi_status;
   } else
      scb->scsi_cmd->result = 0;

   /* default driver error */
   errcode = DID_ERROR;

   switch (scb->basic_status & GSC_STATUS_MASK) {
   case CMD_TIMEOUT:
      errcode = DID_TIME_OUT;
      break;

   case INVAL_OPCO:
   case INVAL_CMD_BLK:
   case INVAL_PARM_BLK:
   case LOG_DRV_ERROR:
   case CMD_CMPLT_WERROR:
      break;

   case PHYS_DRV_ERROR:
      /*
       * For physical drive errors that
       * are not on a logical drive should
       * be DID_OK.  The SCSI errcode will
       * show what the real error is.
       */
      if (scb->bus)
         errcode = DID_OK;

      switch (scb->extended_status) {
      case SELECTION_TIMEOUT:
         if (scb->bus) {
            scb->scsi_cmd->result |= DID_TIME_OUT << 16;

            return (0);
         }
         break;
      case DATA_OVER_UNDER_RUN:
         if ((scb->bus) && (scb->dcdb.transfer_length < scb->data_len)) {
            if ((scb->scsi_cmd->cmnd[0] == INQUIRY) &&
                ((((char *) scb->scsi_cmd->buffer)[0] & 0x1f) == TYPE_DISK)) {
               /* underflow -- no error               */
               /* restrict access to physical DASD    */
               errcode = DID_TIME_OUT;
               break;
            }

            /* normal underflow Occured */
            if (scb->dcdb.transfer_length >= scb->scsi_cmd->underflow) {
               scb->scsi_cmd->result |= DID_OK << 16;

               return (0);
            }
         }

         break;
      case EXT_RECOVERY:
         /* don't fail recovered errors */
         if (scb->bus) {
            scb->scsi_cmd->result |= DID_OK << 16;

            return (0);
         }
         break;

      case EXT_HOST_RESET:
      case EXT_DEVICE_RESET:
         errcode = DID_RESET;
         break;

      case EXT_CHECK_CONDITION:
         break;
      } /* end switch */
   } /* end switch */

   scb->scsi_cmd->result |= (errcode << 16);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_send                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Wrapper for ips_send_cmd                                               */
/*                                                                          */
/****************************************************************************/
static int
ips_send(ips_ha_t *ha, ips_scb_t *scb, scb_callback callback) {
   int ret;

   DBG("ips_send");

   scb->callback = callback;

   ret = ips_send_cmd(ha, scb);

   return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_send_wait                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command to the controller and wait for it to return             */
/*                                                                          */
/****************************************************************************/
static int
ips_send_wait(ips_ha_t *ha, ips_scb_t *scb, int timeout) {
   int       ret;

   DBG("ips_send_wait");

   ha->waitflag = TRUE;
   ha->cmd_in_progress = scb->cdb[0];

   ret = ips_send(ha, scb, ipsintr_blocking);

   if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
      return (ret);

   ret = ips_wait(ha, timeout, IPS_INTR_OFF);

   return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_send_cmd                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Map SCSI commands to ServeRAID commands for logical drives             */
/*                                                                          */
/****************************************************************************/
static int
ips_send_cmd(ips_ha_t *ha, ips_scb_t *scb) {
   int       ret;

   DBG("ips_send_cmd");

   ret = IPS_SUCCESS;

   if (!scb->scsi_cmd) {
      /* internal command */

      if (scb->bus > 0) {
         /* ServeRAID commands can't be issued */
         /* to real devices -- fail them       */
         if ((ha->waitflag == TRUE) &&
             (ha->cmd_in_progress == scb->cdb[0])) {
            ha->waitflag = FALSE;
         }

         return (1);
      }
#ifndef NO_IPS_CMDLINE
   } else if ((scb->bus == 0) && (!ips_is_passthru(scb->scsi_cmd))) {
#else
   } else if (scb->bus == 0) {
#endif
      /* command to logical bus -- interpret */
      ret = IPS_SUCCESS_IMM;

      switch (scb->scsi_cmd->cmnd[0]) {
      case ALLOW_MEDIUM_REMOVAL:
      case REZERO_UNIT:
      case ERASE:
      case WRITE_FILEMARKS:
      case SPACE:
         scb->scsi_cmd->result = DID_ERROR << 16;
         break;

      case START_STOP:
         scb->scsi_cmd->result = DID_OK << 16;

      case TEST_UNIT_READY:
      case INQUIRY:
         if (scb->target_id == IPS_ADAPTER_ID) {
            /*
             * Either we have a TUR
             * or we have a SCSI inquiry
             */
            if (scb->scsi_cmd->cmnd[0] == TEST_UNIT_READY)
               scb->scsi_cmd->result = DID_OK << 16;

            if (scb->scsi_cmd->cmnd[0] == INQUIRY) {
               IPS_INQUIRYDATA inq;

               memset(&inq, 0, sizeof(IPS_INQUIRYDATA));

               inq.DeviceType = TYPE_PROCESSOR;
               inq.DeviceTypeQualifier = 0;
               inq.RemoveableMedia = 0;
               inq.Versions = 0x1;  /* SCSI I */
               inq.AdditionalLength = 31;
               strncpy(inq.VendorId, "IBM     ", 8);
               strncpy(inq.ProductId, "SERVERAID       ", 16);
               strncpy(inq.ProductRevisionLevel, "1.00", 4);

               memcpy(scb->scsi_cmd->request_buffer, &inq, scb->scsi_cmd->request_bufflen);

               scb->scsi_cmd->result = DID_OK << 16;
            }
         } else {
            scb->cmd.logical_info.op_code = GET_LOGICAL_DRIVE_INFO;
            scb->cmd.logical_info.command_id = IPS_COMMAND_ID(ha, scb);
            scb->cmd.logical_info.buffer_addr = VIRT_TO_BUS(&ha->adapt->logical_drive_info);
            scb->cmd.logical_info.reserved = 0;
            scb->cmd.logical_info.reserved2 = 0;
            ret = IPS_SUCCESS;
         }

         break;

      case REQUEST_SENSE:
         ips_reqsen(ha, scb);
         scb->scsi_cmd->result = DID_OK << 16;
         break;

      case READ_6:
      case WRITE_6:
         if (!scb->sg_len) {
            scb->cmd.basic_io.op_code =
            (scb->scsi_cmd->cmnd[0] == READ_6) ? IPS_READ : IPS_WRITE;
         } else {
            scb->cmd.basic_io.op_code =
            (scb->scsi_cmd->cmnd[0] == READ_6) ? READ_SCATTER_GATHER : WRITE_SCATTER_GATHER;
         }

         scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
         scb->cmd.basic_io.log_drv = scb->target_id;
         scb->cmd.basic_io.sg_count = scb->sg_len;
         scb->cmd.basic_io.sg_addr = scb->data_busaddr;

         if (scb->cmd.basic_io.lba)
            scb->cmd.basic_io.lba += scb->cmd.basic_io.sector_count;
         else
            scb->cmd.basic_io.lba = (((scb->scsi_cmd->cmnd[1] & 0x1f) << 16) |
                                     (scb->scsi_cmd->cmnd[2] << 8) |
                                     (scb->scsi_cmd->cmnd[3]));

         scb->cmd.basic_io.sector_count = scb->data_len / IPS_BLKSIZE;

         if (scb->cmd.basic_io.sector_count == 0)
            scb->cmd.basic_io.sector_count = 256;

         scb->cmd.basic_io.reserved = 0;
         ret = IPS_SUCCESS;
         break;

      case READ_10:
      case WRITE_10:
         if (!scb->sg_len) {
            scb->cmd.basic_io.op_code =
            (scb->scsi_cmd->cmnd[0] == READ_10) ? IPS_READ : IPS_WRITE;
         } else {
            scb->cmd.basic_io.op_code =
            (scb->scsi_cmd->cmnd[0] == READ_10) ? READ_SCATTER_GATHER : WRITE_SCATTER_GATHER;
         }

         scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
         scb->cmd.basic_io.log_drv = scb->target_id;
         scb->cmd.basic_io.sg_count = scb->sg_len;
         scb->cmd.basic_io.sg_addr = scb->data_busaddr;

         if (scb->cmd.basic_io.lba)
            scb->cmd.basic_io.lba += scb->cmd.basic_io.sector_count;
         else
            scb->cmd.basic_io.lba = ((scb->scsi_cmd->cmnd[2] << 24) |
                                     (scb->scsi_cmd->cmnd[3] << 16) |
                                     (scb->scsi_cmd->cmnd[4] << 8) |
                                     scb->scsi_cmd->cmnd[5]);

         scb->cmd.basic_io.sector_count = scb->data_len / IPS_BLKSIZE;

         scb->cmd.basic_io.reserved = 0;

         if (scb->cmd.basic_io.sector_count == 0) {
            /*
             * This is a null condition
             * we don't have to do anything
             * so just return
             */
            scb->scsi_cmd->result = DID_OK << 16;
         } else
            ret = IPS_SUCCESS;

         break;

      case RESERVE:
      case RELEASE:
         scb->scsi_cmd->result = DID_OK << 16;
         break;

      case MODE_SENSE:
         scb->cmd.basic_io.op_code = ENQUIRY;
         scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
         scb->cmd.basic_io.sg_addr = VIRT_TO_BUS(ha->enq);
         ret = IPS_SUCCESS;
         break;

      case READ_CAPACITY:
         scb->cmd.logical_info.op_code = GET_LOGICAL_DRIVE_INFO;
         scb->cmd.logical_info.command_id = IPS_COMMAND_ID(ha, scb);
         scb->cmd.logical_info.buffer_addr = VIRT_TO_BUS(&ha->adapt->logical_drive_info);
         scb->cmd.logical_info.reserved = 0;
         scb->cmd.logical_info.reserved2 = 0;
         scb->cmd.logical_info.reserved3 = 0;
         ret = IPS_SUCCESS;
         break;

      case SEND_DIAGNOSTIC:
      case REASSIGN_BLOCKS:
      case FORMAT_UNIT:
      case SEEK_10:
      case VERIFY:
      case READ_DEFECT_DATA:
      case READ_BUFFER:
      case WRITE_BUFFER:
         scb->scsi_cmd->result = DID_OK << 16;
         break;

      default:
         scb->scsi_cmd->result = DID_ERROR << 16;
         break;
      } /* end switch */
   } /* end if */

   if (ret == IPS_SUCCESS_IMM)
      return (ret);

   /* setup DCDB */
   if (scb->bus > 0) {
      if (!scb->sg_len)
         scb->cmd.dcdb.op_code = DIRECT_CDB;
      else
         scb->cmd.dcdb.op_code = DIRECT_CDB_SCATTER_GATHER;

      ha->dcdb_active[scb->bus-1] |= (1 << scb->target_id);
      scb->cmd.dcdb.command_id = IPS_COMMAND_ID(ha, scb);
      scb->cmd.dcdb.dcdb_address = VIRT_TO_BUS(&scb->dcdb);
      scb->cmd.dcdb.reserved = 0;
      scb->cmd.dcdb.reserved2 = 0;
      scb->cmd.dcdb.reserved3 = 0;

      scb->dcdb.device_address = ((scb->bus - 1) << 4) | scb->target_id;
      scb->dcdb.cmd_attribute |= DISCONNECT_ALLOWED;

      if (scb->timeout) {
         if (scb->timeout <= 10)
            scb->dcdb.cmd_attribute |= TIMEOUT_10;
         else if (scb->timeout <= 60)
            scb->dcdb.cmd_attribute |= TIMEOUT_60;
         else
            scb->dcdb.cmd_attribute |= TIMEOUT_20M;
      }

      if (!(scb->dcdb.cmd_attribute & TIMEOUT_20M))
         scb->dcdb.cmd_attribute |= TIMEOUT_20M;

      scb->dcdb.sense_length = sizeof(scb->scsi_cmd->sense_buffer);
      scb->dcdb.buffer_pointer = scb->data_busaddr;
      scb->dcdb.sg_count = scb->sg_len;
      scb->dcdb.cdb_length = scb->scsi_cmd->cmd_len;
      memcpy(scb->dcdb.scsi_cdb, scb->scsi_cmd->cmnd, scb->scsi_cmd->cmd_len);
   }

   return (ips_issue(ha, scb));
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_chk_status                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Check the status of commands to logical drives                         */
/*                                                                          */
/****************************************************************************/
static int
ips_chkstatus(ips_ha_t *ha) {
   ips_scb_t  *scb;
   ips_stat_t *sp = &ha->sp;
   u8          basic_status;
   u8          ext_status;
   int         command_id;
   int         errcode;
   int         ret;

   DBG("ips_chkstatus");

   command_id = ips_statupd(ha);

   if (command_id > (MAX_CMDS-1)) {
      printk(KERN_NOTICE "(%s%d) invalid command id received: %d\n",
             ips_name, ha->host_num, command_id);

      return (-1);
   }

   scb = &ha->scbs[command_id];
   sp->scb_addr = (u32) scb;
   sp->residue_len = 0;
   scb->basic_status = basic_status = ha->adapt->p_status_tail->basic_status & BASIC_STATUS_MASK;
   scb->extended_status = ext_status = ha->adapt->p_status_tail->extended_status;

   /* Remove the item from the active queue */
   ips_removeq_scb(&ha->scb_activelist, scb);

   if (!scb->scsi_cmd)
      /* internal commands are handled in do_ipsintr */
      return (0);

#ifndef NO_IPS_CMDLINE
   if ((scb->scsi_cmd) && (ips_is_passthru(scb->scsi_cmd)))
      /* passthru - just returns the raw result */
      return (0);
#endif

   errcode = DID_OK;
   ret = 0;

   if (((basic_status & GSC_STATUS_MASK) == SSUCCESS) ||
       ((basic_status & GSC_STATUS_MASK) == RECOVERED_ERROR)) {

      if (scb->bus == 0) {
#if IPS_DEBUG >= 1
         if ((basic_status & GSC_STATUS_MASK) == RECOVERED_ERROR) {
            printk(KERN_NOTICE "(%s%d) Recovered Logical Drive Error OpCode: %x, BSB: %x, ESB: %x\n",
                   ips_name, ha->host_num,
                   scb->cmd.basic_io.op_code, basic_status, ext_status);
         }
#endif

         switch (scb->scsi_cmd->cmnd[0]) {
         case ALLOW_MEDIUM_REMOVAL:
         case REZERO_UNIT:
         case ERASE:
         case WRITE_FILEMARKS:
         case SPACE:
            errcode = DID_ERROR;
            ret = 1;
            break;

         case START_STOP:
            break;

         case TEST_UNIT_READY:
            if (!ips_online(ha, scb)) {
               errcode = DID_TIME_OUT;
               ret = 1;
            }
            break;

         case INQUIRY:
            if (ips_online(ha, scb)) {
               ips_inquiry(ha, scb);
            } else {
               errcode = DID_TIME_OUT;
               ret = 1;
            }
            break;

         case REQUEST_SENSE:
            ips_reqsen(ha, scb);
            break;

         case READ_6:
         case WRITE_6:
         case READ_10:
         case WRITE_10:
         case RESERVE:
         case RELEASE:
            break;

         case MODE_SENSE:
            if (!ips_online(ha, scb) || !ips_msense(ha, scb)) {
               errcode = DID_ERROR;
               ret = 1;
            }
            break;

         case READ_CAPACITY:
            if (ips_online(ha, scb))
               ips_rdcap(ha, scb);
            else {
               errcode = DID_TIME_OUT;
               ret = 1;
            }
            break;

         case SEND_DIAGNOSTIC:
         case REASSIGN_BLOCKS:
            break;

         case FORMAT_UNIT:
            errcode = DID_ERROR;
            ret = 1;
            break;

         case SEEK_10:
         case VERIFY:
         case READ_DEFECT_DATA:
         case READ_BUFFER:
         case WRITE_BUFFER:
            break;

         default:
            errcode = DID_ERROR;
            ret = 1;
         } /* end switch */

         scb->scsi_cmd->result = errcode << 16;
      } else { /* bus == 0 */
         /* restrict access to physical drives */
         if ((scb->scsi_cmd->cmnd[0] == INQUIRY) &&
             ((((char *) scb->scsi_cmd->buffer)[0] & 0x1f) == TYPE_DISK)) {

            scb->scsi_cmd->result = DID_TIME_OUT << 16;

            ret = 1;
         }
      } /* else */
   } else { /* recovered error / success */
#if IPS_DEBUG >= 1
      if (scb->bus == 0) {
         printk(KERN_NOTICE "(%s%d) Unrecovered Logical Drive Error OpCode: %x, BSB: %x, ESB: %x\n",
                ips_name, ha->host_num,
                scb->cmd.basic_io.op_code, basic_status, ext_status);
      }
#endif

      ret = ips_map_status(scb, sp);
   } /* else */

   return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_online                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Determine if a logical drive is online                                 */
/*                                                                          */
/****************************************************************************/
static int
ips_online(ips_ha_t *ha, ips_scb_t *scb) {
   DBG("ips_online");

   if (scb->target_id >= MAX_LOGICAL_DRIVES)
      return (0);

   if ((scb->basic_status & GSC_STATUS_MASK) > 1) {
      memset(&ha->adapt->logical_drive_info, 0, sizeof(ha->adapt->logical_drive_info));

      return (0);
   }

   if (scb->target_id < ha->adapt->logical_drive_info.no_of_log_drive &&
       ha->adapt->logical_drive_info.drive_info[scb->target_id].state != OFF_LINE &&
       ha->adapt->logical_drive_info.drive_info[scb->target_id].state != FREE &&
       ha->adapt->logical_drive_info.drive_info[scb->target_id].state != CRS &&
       ha->adapt->logical_drive_info.drive_info[scb->target_id].state != SYS)
      return (1);
   else
      return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_inquiry                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate an inquiry command to a logical drive                         */
/*                                                                          */
/****************************************************************************/
static int
ips_inquiry(ips_ha_t *ha, ips_scb_t *scb) {
   IPS_INQUIRYDATA inq;

   DBG("ips_inquiry");

   memset(&inq, 0, sizeof(IPS_INQUIRYDATA));

   inq.DeviceType = TYPE_DISK;
   inq.DeviceTypeQualifier = 0;
   inq.RemoveableMedia = 0;
   inq.Versions = 0x1;  /* SCSI I */
   inq.AdditionalLength = 31;
   strncpy(inq.VendorId, "IBM     ", 8);
   strncpy(inq.ProductId, "SERVERAID       ", 16);
   strncpy(inq.ProductRevisionLevel, "1.00", 4);

   memcpy(scb->scsi_cmd->request_buffer, &inq, scb->scsi_cmd->request_bufflen);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_rdcap                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a read capacity command to a logical drive                    */
/*                                                                          */
/****************************************************************************/
static int
ips_rdcap(ips_ha_t *ha, ips_scb_t *scb) {
   CAPACITY_T *cap;

   DBG("ips_rdcap");

   if (scb->scsi_cmd->bufflen < 8)
      return (0);

   cap = (CAPACITY_T *) scb->scsi_cmd->request_buffer;

   cap->lba = htonl(ha->adapt->logical_drive_info.drive_info[scb->target_id].sector_count - 1);
   cap->len = htonl((u32) IPS_BLKSIZE);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_msense                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a mode sense command to a logical drive                       */
/*                                                                          */
/****************************************************************************/
static int
ips_msense(ips_ha_t *ha, ips_scb_t *scb) {
   u16          heads;
   u16          sectors;
   u32          cylinders;
   ips_mdata_t  mdata;

   DBG("ips_msense");

   if (ha->enq->ulDriveSize[scb->target_id] > 0x400000 &&
       (ha->enq->ucMiscFlag & 0x8) == 0) {
      heads = NORM_MODE_HEADS;
      sectors = NORM_MODE_SECTORS;
   } else {
      heads = COMP_MODE_HEADS;
      sectors = COMP_MODE_SECTORS;
   }

   cylinders = ha->enq->ulDriveSize[scb->target_id] / (heads * sectors);

   mdata.plh.plh_type = 0;
   mdata.plh.plh_wp = 0;
   mdata.plh.plh_bdl = 8;

   switch (scb->scsi_cmd->cmnd[2] & 0x3f) {
   case 0x03: /* page 3 */
      mdata.pdata.pg3.pg_pc = 0x3;
      mdata.pdata.pg3.pg_res1 = 0;
      mdata.pdata.pg3.pg_len = sizeof(DADF_T);
      mdata.plh.plh_len = 3 + mdata.plh.plh_bdl + mdata.pdata.pg3.pg_len;
      mdata.pdata.pg3.pg_trk_z = 0;
      mdata.pdata.pg3.pg_asec_z = 0;
      mdata.pdata.pg3.pg_atrk_z = 0;
      mdata.pdata.pg3.pg_atrk_v = 0;
      mdata.pdata.pg3.pg_sec_t = htons(sectors);
      mdata.pdata.pg3.pg_bytes_s = htons(IPS_BLKSIZE);
      mdata.pdata.pg3.pg_intl = htons(1);
      mdata.pdata.pg3.pg_trkskew = 0;
      mdata.pdata.pg3.pg_cylskew = 0;
      mdata.pdata.pg3.pg_res2 = 0;
      mdata.pdata.pg3.pg_ins = 0;
      mdata.pdata.pg3.pg_surf = 0;
      mdata.pdata.pg3.pg_rmb = 0;
      mdata.pdata.pg3.pg_hsec = 0;
      mdata.pdata.pg3.pg_ssec = 1;
      break;

   case 0x4:
      mdata.pdata.pg4.pg_pc = 0x4;
      mdata.pdata.pg4.pg_res1 = 0;
      mdata.pdata.pg4.pg_len = sizeof(RDDG_T);
      mdata.plh.plh_len = 3 + mdata.plh.plh_bdl + mdata.pdata.pg4.pg_len;
      mdata.pdata.pg4.pg_cylu = (cylinders >> 8) & 0xffff;
      mdata.pdata.pg4.pg_cyll = cylinders & 0xff;
      mdata.pdata.pg4.pg_head = heads;
      mdata.pdata.pg4.pg_wrpcompu = 0;
      mdata.pdata.pg4.pg_wrpcompl = 0;
      mdata.pdata.pg4.pg_redwrcur = 0;
      mdata.pdata.pg4.pg_drstep = htons(1);
      mdata.pdata.pg4.pg_landu = 0;
      mdata.pdata.pg4.pg_landl = 0;
      mdata.pdata.pg4.pg_res2 = 0;
      break;
   default:
      return (0);
   } /* end switch */

   memcpy(scb->scsi_cmd->request_buffer, &mdata, scb->scsi_cmd->request_bufflen);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reqsen                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a request sense command to a logical drive                    */
/*                                                                          */
/****************************************************************************/
static int
ips_reqsen(ips_ha_t *ha, ips_scb_t *scb) {
   char *sp;

   DBG("ips_reqsen");

   sp = (char *) scb->scsi_cmd->sense_buffer;
   memset(sp, 0, sizeof(scb->scsi_cmd->sense_buffer));

   sp[0] = 0x70;
   sp[3] = NO_SENSE;
   sp[7] = 0xe;
   sp[12] = NO_SENSE;

   return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_free                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Free any allocated space for this controller                           */
/*                                                                          */
/****************************************************************************/
static void
ips_free(ips_ha_t *ha) {
   int i;

   DBG("ips_free");

   if (ha) {
      if (ha->enq) {
         kfree(ha->enq);
         ha->enq = NULL;
      }

      if (ha->conf) {
         kfree(ha->conf);
         ha->conf = NULL;
      }

      if (ha->adapt) {
         kfree(ha->adapt);
         ha->adapt = NULL;
      }

      if (ha->nvram) {
         kfree(ha->nvram);
         ha->nvram = NULL;
      }

      if (ha->subsys) {
         kfree(ha->subsys);
         ha->subsys = NULL;
      }

      if (ha->dummy) {
         kfree(ha->dummy);
         ha->dummy = NULL;
      }

      if (ha->scbs) {
         for (i = 0; i < ha->max_cmds; i++) {
            if (ha->scbs[i].sg_list)
               kfree(ha->scbs[i].sg_list);
         }

         kfree(ha->scbs);
         ha->scbs = NULL;
      } /* end if */
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_allocatescbs                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Allocate the command blocks                                            */
/*                                                                          */
/****************************************************************************/
static int
ips_allocatescbs(ips_ha_t *ha) {
   ips_scb_t *scb_p;
   int        i;

   DBG("ips_allocatescbs");

   /* Allocate memory for the CCBs */
   ha->scbs = (ips_scb_t *) kmalloc(ha->max_cmds * sizeof(ips_scb_t), GFP_KERNEL|GFP_DMA);

   memset(ha->scbs, 0, ha->max_cmds * sizeof(ips_scb_t));

   for (i = 0; i < ha->max_cmds; i++) {
      scb_p = &ha->scbs[i];

      /* allocate S/G list */
      scb_p->sg_list = (SG_LIST *) kmalloc(sizeof(SG_LIST) * MAX_SG_ELEMENTS, GFP_KERNEL|GFP_DMA);

      if (! scb_p->sg_list)
         return (0);

      /* add to the free list */
      if (i < ha->max_cmds - 1) {
         scb_p->q_next = ha->scb_freelist;
         ha->scb_freelist = scb_p;
      }
   }

   /* success */
   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_init_scb                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a CCB to default values                                     */
/*                                                                          */
/****************************************************************************/
static void
ips_init_scb(ips_ha_t *ha, ips_scb_t *scb) {
   SG_LIST *sg_list;

   DBG("ips_init_scb");

   if (scb == NULL)
      return ;

   sg_list = scb->sg_list;

   /* zero fill */
   memset(scb, 0, sizeof(ips_scb_t));
   memset(ha->dummy, 0, sizeof(BASIC_IO_CMD));

   /* Initialize dummy command bucket */
   ha->dummy->op_code = 0xFF;
   ha->dummy->ccsar = VIRT_TO_BUS(ha->dummy);
   ha->dummy->command_id = MAX_CMDS;

   /* set bus address of scb */
   scb->scb_busaddr = VIRT_TO_BUS(scb);
   scb->sg_list = sg_list;

   /* Neptune Fix */
   scb->cmd.basic_io.cccr = ILE;
   scb->cmd.basic_io.ccsar = VIRT_TO_BUS(ha->dummy);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_get_scb                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a CCB to default values                                     */
/*                                                                          */
/* ASSUMED to be callled from within a lock                                 */
/*                                                                          */
/****************************************************************************/
static ips_scb_t *
ips_getscb(ips_ha_t *ha) {
   ips_scb_t     *scb;
   unsigned int   cpu_flags;

   DBG("ips_getscb");

   spin_lock_irqsave(&ha->scb_lock, cpu_flags);
   if ((scb = ha->scb_freelist) == NULL) {
      spin_unlock_irqrestore(&ha->scb_lock, cpu_flags);

      return (NULL);
   }

   ha->scb_freelist = scb->q_next;
   scb->q_next = NULL;

   spin_unlock_irqrestore(&ha->scb_lock, cpu_flags);

   ips_init_scb(ha, scb);

   return (scb);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_free_scb                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Return an unused CCB back to the free list                             */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static void
ips_freescb(ips_ha_t *ha, ips_scb_t *scb) {
   unsigned int cpu_flags;

   DBG("ips_freescb");

   /* check to make sure this is not our "special" scb */
   if (IPS_COMMAND_ID(ha, scb) < (ha->max_cmds - 1)) {
      spin_lock_irqsave(&ha->scb_lock, cpu_flags);
      scb->q_next = ha->scb_freelist;
      ha->scb_freelist = scb;
      spin_unlock_irqrestore(&ha->scb_lock, cpu_flags);
   }
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reset_adapter                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller                                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_reset_adapter(ips_ha_t *ha) {
   u8  Isr;
   u8  Cbsp;
   u8  PostByte[MAX_POST_BYTES];
   u8  ConfigByte[MAX_CONFIG_BYTES];
   int i, j;
   int reset_counter;

   DBG("ips_reset_adapter");

#if IPS_DEBUG >= 1
   printk(KERN_WARNING "ips_reset_adapter: io addr: %x, irq: %d\n",
          ha->io_addr, ha->irq);
#endif

   reset_counter = 0;

   while (reset_counter < 2) {
      reset_counter++;

      outb(RST, ha->io_addr + SCPR);
      MDELAY(ONE_SEC);
      outb(0, ha->io_addr + SCPR);
      MDELAY(ONE_SEC);

      for (i = 0; i < MAX_POST_BYTES; i++) {
         for (j = 0; j < 45; j++) {
            Isr = inb(ha->io_addr + HISR);
            if (Isr & GHI)
               break;

            MDELAY(ONE_SEC);
         }

         if (j >= 45) {
            /* error occured */
            if (reset_counter < 2)
               continue;
            else
               /* reset failed */
               return (0);
         }

         PostByte[i] = inb(ha->io_addr + ISPR);
         outb(Isr, ha->io_addr + HISR);
      }

      if (PostByte[0] < GOOD_POST_BASIC_STATUS) {
         printk("(%s%d) reset controller fails (post status %x %x).\n",
                ips_name, ha->host_num, PostByte[0], PostByte[1]);

         return (0);
      }

      for (i = 0; i < MAX_CONFIG_BYTES; i++) {
         for (j = 0; j < 240; j++) {
            Isr = inb(ha->io_addr + HISR);
            if (Isr & GHI)
               break;

            MDELAY(ONE_SEC); /* 100 msec */
         }

         if (j >= 240) {
            /* error occured */
            if (reset_counter < 2)
               continue;
            else
               /* reset failed */
               return (0);
         }

         ConfigByte[i] = inb(ha->io_addr + ISPR);
         outb(Isr, ha->io_addr + HISR);
      }

      if (ConfigByte[0] == 0 && ConfigByte[1] == 2) {
         printk("(%s%d) reset controller fails (status %x %x).\n",
                ips_name, ha->host_num, ConfigByte[0], ConfigByte[1]);

         return (0);
      }

      for (i = 0; i < 240; i++) {
         Cbsp = inb(ha->io_addr + CBSP);

         if ((Cbsp & OP) == 0)
            break;

         MDELAY(ONE_SEC);
      }

      if (i >= 240) {
         /* error occured */
         if (reset_counter < 2)
            continue;
         else
            /* reset failed */
            return (0);
      }

      /* setup CCCR */
      outw(0x1010, ha->io_addr + CCCR);

      /* Enable busmastering */
      outb(EBM, ha->io_addr + SCPR);

      /* setup status queues */
      ips_statinit(ha);

      /* Enable interrupts */
      outb(EI, ha->io_addr + HISR);

      /* if we get here then everything went OK */
      break;
   }

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statinit                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize the status queues on the controller                         */
/*                                                                          */
/****************************************************************************/
static void
ips_statinit(ips_ha_t *ha) {
   u32        phys_status_start;

   DBG("ips_statinit");

   ha->adapt->p_status_start = ha->adapt->status;
   ha->adapt->p_status_end = ha->adapt->status + MAX_CMDS;
   ha->adapt->p_status_tail = ha->adapt->status;

   phys_status_start = VIRT_TO_BUS(ha->adapt->status);
   outl(phys_status_start, ha->io_addr + SQSR);
   outl(phys_status_start + STATUS_Q_SIZE, ha->io_addr + SQER);
   outl(phys_status_start + STATUS_SIZE, ha->io_addr + SQHR);
   outl(phys_status_start, ha->io_addr + SQTR);

   ha->adapt->hw_status_start = phys_status_start;
   ha->adapt->hw_status_tail = phys_status_start;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statupd                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an element from the status queue                                */
/*                                                                          */
/****************************************************************************/
static int
ips_statupd(ips_ha_t *ha) {
   int       command_id;

   DBG("ips_statupd");

   if (ha->adapt->p_status_tail != ha->adapt->p_status_end) {
      ha->adapt->p_status_tail++;
      ha->adapt->hw_status_tail += sizeof(STATUS);
   } else {
      ha->adapt->p_status_tail = ha->adapt->p_status_start;
      ha->adapt->hw_status_tail = ha->adapt->hw_status_start;
   }

   outl(ha->adapt->hw_status_tail, ha->io_addr + SQTR);

   command_id = ha->adapt->p_status_tail->command_id;

   return (command_id);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_issue                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command down to the controller                                  */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_issue(ips_ha_t *ha, ips_scb_t *scb) {
   u32       TimeOut;
   u16       val;

   DBG("ips_issue");

#if IPS_DEBUG >= 10
   if (scb->scsi_cmd)
      printk(KERN_NOTICE "%s: ips_issue: cmd 0x%X id %d (%d %d %d)\n",
             ips_name,
             scb->cdb[0],
             scb->cmd.basic_io.command_id,
             scb->bus,
             scb->target_id,
             scb->lun);
   else
      printk(KERN_NOTICE "%s: ips_issue: logical cmd id %d\n",
             ips_name,
             scb->cmd.basic_io.command_id);
#if IPS_DEBUG >= 11
      MDELAY(ONE_SEC);
#endif
#endif

   TimeOut = 0;

   while ((val = inw(ha->io_addr + CCCR)) & SEMAPHORE) {
      UDELAY(1000);

      if (++TimeOut >= SEMAPHORE_TIMEOUT) {
         if (!(val & START_STOP_BIT))
            break;

         printk(KERN_WARNING "(%s%d) ips_issue val [0x%x].\n",
                ips_name, ha->host_num, val);
         printk(KERN_WARNING "(%s%d) ips_issue semaphore chk timeout.\n",
                ips_name, ha->host_num);

         return (IPS_FAILURE);
      } /* end if */
   } /* end while */

   outl(scb->scb_busaddr, ha->io_addr + CCSAR);
   outw(START_COMMAND, ha->io_addr + CCCR);

   return (IPS_SUCCESS);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isintr                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Test to see if an interrupt is for us                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_isintr(ips_ha_t *ha) {
   u8 Isr;

   DBG("ips_isintr");

   Isr = inb(ha->io_addr + HISR);

   if (Isr == 0xFF)
      /* ?!?! Nothing really there */
      return (0);

   if (Isr & SCE)
      return (1);
   else if (Isr & (SQO | GHI)) {
      /* status queue overflow or GHI */
      /* just clear the interrupt */
      outb(Isr, ha->io_addr + HISR);
   }

   return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_wait                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Wait for a command to complete                                         */
/*                                                                          */
/****************************************************************************/
static int
ips_wait(ips_ha_t *ha, int time, int intr) {
   int        ret;

   DBG("ips_wait");

   ret = IPS_FAILURE;

   time *= ONE_SEC; /* convert seconds to milliseconds */

   while (time > 0) {
      if (intr == IPS_INTR_OFF) {
         if (ha->waitflag == FALSE) {
            /*
             * controller generated an interupt to
             * acknowledge completion of the command
             * and ips_intr() has serviced the interrupt.
             */
            ret = IPS_SUCCESS;
            break;
         }

         /*
          * NOTE: Interrupts are disabled here
          * On an SMP system interrupts will only
          * be disabled on one processor.
          * So, ultimately we still need to set the
          * "I'm in the interrupt handler flag"
          */
         while (test_and_set_bit(IPS_IN_INTR, &ha->flags))
            UDELAY(1000);

         ips_intr(ha);

         clear_bit(IPS_IN_INTR, &ha->flags);

      } else {
         if (ha->waitflag == FALSE) {
            ret = IPS_SUCCESS;
            break;
         }
      }

      UDELAY(1000); /* 1 milisecond */
      time--;
   }

   return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_write_driver_status                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Write OS/Driver version to Page 5 of the nvram on the controller       */
/*                                                                          */
/****************************************************************************/
static int
ips_write_driver_status(ips_ha_t *ha) {
   DBG("ips_write_driver_status");

   if (!ips_readwrite_page5(ha, FALSE)) {
      printk(KERN_WARNING "(%s%d) unable to read NVRAM page 5.\n",
             ips_name, ha->host_num);

      return (0);
   }

   /* check to make sure the page has a valid */
   /* signature */
   if (ha->nvram->signature != NVRAM_PAGE5_SIGNATURE) {
#if IPS_DEBUG >= 1
      printk("(%s%d) NVRAM page 5 has an invalid signature: %X.\n",
             ips_name, ha->host_num, ha->nvram->signature);
#endif
      return (1);
   }

#if IPS_DEBUG >= 2
   printk("(%s%d) Ad Type: %d, Ad Slot: %d, BIOS: %c%c%c%c %c%c%c%c.\n",
          ips_name, ha->host_num, ha->nvram->adapter_type, ha->nvram->adapter_slot,
          ha->nvram->bios_high[0], ha->nvram->bios_high[1],
          ha->nvram->bios_high[2], ha->nvram->bios_high[3],
          ha->nvram->bios_low[0], ha->nvram->bios_low[1],
          ha->nvram->bios_low[2], ha->nvram->bios_low[3]);
#endif

   /* save controller type */
   ha->ad_type = ha->nvram->adapter_type;

   /* change values (as needed) */
   ha->nvram->operating_system = OS_LINUX;
   strncpy((char *) ha->nvram->driver_high, IPS_VERSION_HIGH, 4);
   strncpy((char *) ha->nvram->driver_low, IPS_VERSION_LOW, 4);

   /* now update the page */
   if (!ips_readwrite_page5(ha, TRUE)) {
      printk(KERN_WARNING "(%s%d) unable to write NVRAM page 5.\n",
             ips_name, ha->host_num);

      return (0);
   }

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_adapter_status                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Do an Inquiry command to the adapter                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_read_adapter_status(ips_ha_t *ha) {
   ips_scb_t *scb;
   int        ret;

   DBG("ips_read_adapter_status");

   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_cmd_timeout;
   scb->cdb[0] = ENQUIRY;

   scb->cmd.basic_io.op_code = ENQUIRY;
   scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.basic_io.sg_count = 0;
   scb->cmd.basic_io.sg_addr = VIRT_TO_BUS(ha->enq);
   scb->cmd.basic_io.lba = 0;
   scb->cmd.basic_io.sector_count = 0;
   scb->cmd.basic_io.log_drv = 0;
   scb->cmd.basic_io.reserved = 0;

   /* send command */
   ret = ips_send_wait(ha, scb, ips_cmd_timeout);
   if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
      return (0);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_subsystem_parameters                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read subsystem parameters from the adapter                             */
/*                                                                          */
/****************************************************************************/
static int
ips_read_subsystem_parameters(ips_ha_t *ha) {
   ips_scb_t *scb;
   int        ret;

   DBG("ips_read_subsystem_parameters");

   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_cmd_timeout;
   scb->cdb[0] = GET_SUBSYS_PARAM;

   scb->cmd.basic_io.op_code = GET_SUBSYS_PARAM;
   scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.basic_io.sg_count = 0;
   scb->cmd.basic_io.sg_addr = VIRT_TO_BUS(ha->subsys);
   scb->cmd.basic_io.lba = 0;
   scb->cmd.basic_io.sector_count = 0;
   scb->cmd.basic_io.log_drv = 0;
   scb->cmd.basic_io.reserved = 0;

   /* send command */
   ret = ips_send_wait(ha, scb, ips_cmd_timeout);
   if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
      return (0);

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_config                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read the configuration on the adapter                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_read_config(ips_ha_t *ha) {
   ips_scb_t *scb;
   int        i;
   int        ret;

   DBG("ips_read_config");

   /* set defaults for initiator IDs */
   ha->conf->init_id[0] = IPS_ADAPTER_ID;
   for (i = 1; i < 4; i++)
      ha->conf->init_id[i] = 7;

   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_cmd_timeout;
   scb->cdb[0] = READ_NVRAM_CONFIGURATION;

   scb->cmd.basic_io.op_code = READ_NVRAM_CONFIGURATION;
   scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.basic_io.sg_addr = VIRT_TO_BUS(ha->conf);

   /* send command */
   if (((ret = ips_send_wait(ha, scb, ips_cmd_timeout)) == IPS_FAILURE) ||
       (ret == IPS_SUCCESS_IMM) ||
       ((scb->basic_status & GSC_STATUS_MASK) > 1)) {

      memset(ha->conf, 0, sizeof(CONFCMD));

      /* reset initiator IDs */
      ha->conf->init_id[0] = IPS_ADAPTER_ID;
      for (i = 1; i < 4; i++)
         ha->conf->init_id[i] = 7;

      return (0);
   }

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_readwrite_page5                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read the configuration on the adapter                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_readwrite_page5(ips_ha_t *ha, int write) {
   ips_scb_t *scb;
   int        ret;

   DBG("ips_readwrite_page5");

   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_cmd_timeout;
   scb->cdb[0] = RW_NVRAM_PAGE;

   scb->cmd.nvram.op_code = RW_NVRAM_PAGE;
   scb->cmd.nvram.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.nvram.page = 5;
   scb->cmd.nvram.write = write;
   scb->cmd.nvram.buffer_addr = VIRT_TO_BUS(ha->nvram);
   scb->cmd.nvram.reserved = 0;
   scb->cmd.nvram.reserved2 = 0;

   /* issue the command */
   if (((ret = ips_send_wait(ha, scb, ips_cmd_timeout)) == IPS_FAILURE) ||
       (ret == IPS_SUCCESS_IMM) ||
       ((scb->basic_status & GSC_STATUS_MASK) > 1)) {

      memset(ha->nvram, 0, sizeof(NVRAM_PAGE5));

      return (0);
   }

   return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_clear_adapter                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Clear the stripe lock tables                                           */
/*                                                                          */
/****************************************************************************/
static int
ips_clear_adapter(ips_ha_t *ha) {
   ips_scb_t *scb;
   int        ret;

   DBG("ips_clear_adapter");

   scb = &ha->scbs[ha->max_cmds-1];

   ips_init_scb(ha, scb);

   scb->timeout = ips_reset_timeout;
   scb->cdb[0] = CONFIG_SYNC;

   scb->cmd.config_sync.op_code = CONFIG_SYNC;
   scb->cmd.config_sync.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.config_sync.channel = 0;
   scb->cmd.config_sync.source_target = POCL;
   scb->cmd.config_sync.reserved = 0;
   scb->cmd.config_sync.reserved2 = 0;
   scb->cmd.config_sync.reserved3 = 0;

   /* issue command */
   ret = ips_send_wait(ha, scb, ips_reset_timeout);
   if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
      return (0);

   /* send unlock stripe command */
   ips_init_scb(ha, scb);

   scb->cdb[0] = GET_ERASE_ERROR_TABLE;
   scb->timeout = ips_reset_timeout;

   scb->cmd.unlock_stripe.op_code = GET_ERASE_ERROR_TABLE;
   scb->cmd.unlock_stripe.command_id = IPS_COMMAND_ID(ha, scb);
   scb->cmd.unlock_stripe.log_drv = 0;
   scb->cmd.unlock_stripe.control = CSL;
   scb->cmd.unlock_stripe.reserved = 0;
   scb->cmd.unlock_stripe.reserved2 = 0;
   scb->cmd.unlock_stripe.reserved3 = 0;

   /* issue command */
   ret = ips_send_wait(ha, scb, ips_reset_timeout);
   if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
      return (0);

   return (1);
}

#if defined (MODULE)

Scsi_Host_Template driver_template = IPS;

   #include "scsi_module.c"

#endif


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
