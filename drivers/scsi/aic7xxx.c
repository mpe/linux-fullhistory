/*+M*************************************************************************
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Sources include the Adaptec 1740 driver (aha1740.c), the Ultrastor 24F
 * driver (ultrastor.c), various Linux kernel source, the Adaptec EISA
 * config file (!adp7771.cfg), the Adaptec AHA-2740A Series User's Guide,
 * the Linux Kernel Hacker's Guide, Writing a SCSI Device Driver for Linux,
 * the Adaptec 1542 driver (aha1542.c), the Adaptec EISA overlay file
 * (adp7770.ovl), the Adaptec AHA-2740 Series Technical Reference Manual,
 * the Adaptec AIC-7770 Data Book, the ANSI SCSI specification, the
 * ANSI SCSI-2 specification (draft 10c), ...
 *
 * --------------------------------------------------------------------------
 *
 *  Modifications by Daniel M. Eischen (deischen@iworks.InterWorks.org):
 *
 *  Substantially modified to include support for wide and twin bus
 *  adapters, DMAing of SCBs, tagged queueing, IRQ sharing, bug fixes,
 *  SCB paging, and other rework of the code.
 *
 *  Parts of this driver were also based on the FreeBSD driver by
 *  Justin T. Gibbs.  His copyright follows:
 *
 * --------------------------------------------------------------------------  
 * Copyright (c) 1994-1997 Justin Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $Id: aic7xxx.c,v 1.119 1997/06/27 19:39:18 gibbs Exp $
 *---------------------------------------------------------------------------
 *
 *  Thanks also go to (in alphabetical order) the following:
 *
 *    Rory Bolt     - Sequencer bug fixes
 *    Jay Estabrook - Initial DEC Alpha support
 *    Doug Ledford  - Much needed abort/reset bug fixes
 *    Kai Makisara  - DMAing of SCBs
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended
 *           aic7xxx=no_reset
 *           aic7xxx=ultra
 *           aic7xxx=irq_trigger:[0,1]  # 0 edge, 1 level
 *           aic7xxx=verbose
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 1/23/97
 *
 *  $Id: aic7xxx.c,v 4.1 1997/06/12 08:23:42 deang Exp $
 *-M*************************************************************************/

/*+M**************************************************************************
 *
 * Further driver modifications made by Doug Ledford <dledford@redhat.com>
 *
 * Copyright (c) 1997-1998 Doug Ledford
 *
 * These changes are released under the same licensing terms as the FreeBSD
 * driver written by Justin Gibbs.  Please see his Copyright notice above
 * for the exact terms and conditions covering my changes as well as the
 * warranty statement.
 *
 * Modifications made to the aic7xxx.c,v 4.1 driver from Dan Eischen include
 * but are not limited to:
 *
 *  1: Import of the latest FreeBSD sequencer code for this driver
 *  2: Modification of kernel code to accomodate different sequencer semantics
 *  3: Extensive changes throughout kernel portion of driver to improve
 *     abort/reset processing and error hanndling
 *  4: Other work contributed by various people on the Internet
 *  5: Changes to printk information and verbosity selection code
 *  6: General reliability related changes, especially in IRQ management
 *  7: Modifications to the default probe/attach order for supported cards
 *  8: SMP friendliness has been improved
 *
 * Overall, this driver represents a significant departure from the official
 * aic7xxx driver released by Dan Eischen in two ways.  First, in the code
 * itself.  A diff between the two version of the driver is now a several
 * thousand line diff.  Second, in approach to solving the same problem.  The
 * problem is importing the FreeBSD aic7xxx driver code to linux can be a
 * difficult and time consuming process, that also can be error prone.  Dan
 * Eischen's official driver uses the approach that the linux and FreeBSD
 * drivers should be as identical as possible.  To that end, his next version
 * of this driver will be using a mid-layer code library that he is developing
 * to moderate communications between the linux mid-level SCSI code and the
 * low level FreeBSD driver.  He intends to be able to essentially drop the
 * FreeBSD driver into the linux kernel with only a few minor tweaks to some
 * include files and the like and get things working, making for fast easy
 * imports of the FreeBSD code into linux.
 *
 * I disagree with Dan's approach.  Not that I don't think his way of doing
 * things would be nice, easy to maintain, and create a more uniform driver
 * between FreeBSD and Linux.  I have no objection to those issues.  My
 * disagreement is on the needed functionality.  There simply are certain
 * things that are done differently in FreeBSD than linux that will cause
 * problems for this driver regardless of any middle ware Dan implements.
 * The biggest example of this at the moment is interrupt semantics.  Linux
 * doesn't provide the same protection techniques as FreeBSD does, nor can
 * they be easily implemented in any middle ware code since they would truly
 * belong in the kernel proper and would effect all drivers.  For the time
 * being, I see issues such as these as major stumbling blocks to the 
 * reliability of code based upon such middle ware.  Therefore, I choose to
 * use a different approach to importing the FreeBSD code that doesn't
 * involve any middle ware type code.  My approach is to import the sequencer
 * code from FreeBSD wholesale.  Then, to only make changes in the kernel
 * portion of the driver as they are needed for the new sequencer semantics.
 * In this way, the portion of the driver that speaks to the rest of the
 * linux kernel is fairly static and can be changed/modified to solve
 * any problems one might encounter without concern for the FreeBSD driver.
 *
 * Note: If time and experience should prove me wrong that the middle ware
 * code Dan writes is reliable in its operation, then I'll retract my above
 * statements.  But, for those that don't know, I'm from Missouri (in the US)
 * and our state motto is "The Show-Me State".  Well, before I will put
 * faith into it, you'll have to show me that it works :)
 *
 *_M*************************************************************************/

/*
 * The next three defines are user configurable.  These should be the only
 * defines a user might need to get in here and change.  There are other
 * defines buried deeper in the code, but those really shouldn't need touched
 * under normal conditions.
 */

/*
 * AIC7XXX_FAKE_NEGOTIATION_CMDS
 *   We now have two distinctly different methods of device negotiation
 *   in this code.  The two methods are selected by either defining or not
 *   defining this option.  The difference is as follows:
 *
 *   With AIC7XXX_FAKE_NEGOTIATION_CMDS not set (commented out)
 *     When the driver is in need of issuing a negotiation command for any
 *     given device, it will add the negotiation message on to part of a
 *     regular SCSI command for the device.  In the process, if the device
 *     is configured for and using tagged queueing, then the code will
 *     also issue that single command as a non-tagged command, attach the
 *     negotiation message to that one command, and use a temporary
 *     queue depth of one to keep the untagged and tagged commands from
 *     overlapping.
 *       Pros: This doesn't use any extra SCB structures, it's simple, it
 *         works most of the time (if not all of the time now), and
 *         since we get the device capability info frmo the INQUIRY data
 *         now, shouldn't cause any problems.
 *       Cons: When we need to send a negotiation command to a device, we
 *         must use a command that is being sent to LUN 0 of the device.
 *         If we try sending one to high LUN numbers, then some devices
 *         get noticeably upset.  Since we have to wait for a command with
 *         LUN == 0 to come along, we may not be able to renegotiate when
 *         we want if the user is actually using say LUN 1 of a CD Changer
 *         instead of using LUN 0 for an extended period of time.
 *
 *   With AIC7XXX_FAKE_NEGOTIATION_CMDS defined
 *     When we need to negotiate with a device, instead of attaching our
 *     negotiation message to an existing command, we insert our own
 *     fictional Scsi_Cmnd into the chain that has the negotiation message
 *     attached to it.  We send this one command as untagged regardless
 *     of the device type, and we fiddle with the queue depth the same as
 *     we would with the option unset to avoid overlapping commands.  The
 *     primary difference between this and the unset option is that the
 *     negotiation message is no longer attached to a specific command,
 *     instead it is its own command and is merely triggered by a
 *     combination of both A) We need to negotiate and B) The mid level
 *     SCSI code has sent us a command.  We still don't do any negotiation
 *     unless there is a valid SCSI command to be processed.
 *       Pros: This fixes the problem above in the Cons section.  Since we
 *         issue our own fake command, we can set the LUN to 0 regardless
 *         of what the LUN is in the real command.  It also means that if
 *         the device get's nasty over negotiation issues, it won't be
 *         showing up on a regular command, so we won't get any SENSE buffer
 *         data or STATUS_BYTE returns to the mid level code that are caused
 *         by snits in the negotiation code.
 *       Cons: We add more code, and more complexity.  This means more ways
 *         in which things could break.  It means a larger driver.  It means
 *         more resource consumption for the fake commands.  However, the
 *         biggest problem is this.  Take a system where there is a CD-ROM
 *         on the SCSI bus.  Someone has a CD in the CD-ROM and is using it.
 *         For some reason the SCSI bus gets reset.  We don't touch the
 *         CD-ROM again for quite a period of time (so we don't renegotiate
 *         after the reset until we do touch the CD-ROM again).  In the
 *         time while we aren't using the CD-ROM, the current disc is
 *         removed and a new one put in.  When we go to check that disc, we
 *         will first have to renegotiate.  In so doing, we issue our fake
 *         SCSI command, which happens to be TEST_UNIT_READY.  The CD-ROM
 *         negotiates with us, then responds to our fake command with a
 *         CHECK_CONDITION status.  We REQUEST_SENSE from the CD-ROM, it
 *         then sends the SENSE data to our fake command to tell it that
 *         it has been through a disc change.  There, now we've cleared out
 *         the SENSE data along with our negotiation command, and when the
 *         real command executes, it won't pick up that the CD was changed.
 *         That's the biggest Con to this approach.  In the future, I could
 *         probably code around this problem though, so this option is still
 *         viable.
 *
 *  So, which command style should you use?  I would appreciate it if people
 *  could try out both types.  I want to know about any cases where one
 *  method works and the other doesn't.  If one method works on significantly
 *  more systems than another, then it will become the default.  If the second
 *  option turns out to work best, then I'll find a way to work around that
 *  big con I listed.
 *
 *  -- July 7, 02:33
 *    OK...I just added some code that should make the Con listed for the
 *    fake commands a non issue now.  However, it needs testing.  For now,
 *    I'm going to make the default to use the fake commands, we'll see how
 *    it goes.
 */
 
#define AIC7XXX_FAKE_NEGOTIATION_CMDS

/*
 * AIC7XXX_STRICT_PCI_SETUP
 *   Should we assume the PCI config options on our controllers are set with
 *   sane and proper values, or should we be anal about our PCI config
 *   registers and force them to what we want?  The main advantage to
 *   defining this option is on non-Intel hardware where the BIOS may not
 *   have been run to set things up, or if you have one of the BIOSless
 *   Adaptec controllers, such as a 2910, that don't get set up by the
 *   BIOS.  However, keep in mind that we really do set the most important
 *   items in the driver regardless of this setting, this only controls some
 *   of the more esoteric PCI options on these cards.  In that sense, I
 *   would default to leaving this off.  However, if people wish to try
 *   things both ways, that would also help me to know if there are some
 *   machines where it works one way but not another.
 *
 *   -- July 7, 17:09
 *     OK...I need this on my machine for testing, so the default is to
 *     leave it defined.
 *
 *   -- July 7, 18:49
 *     I needed it for testing, but it didn't make any difference, so back
 *     off she goes.
 *
 *   -- July 16, 23:04
 *     I turned it back on to try and compensate for the 2.1.x PCI code
 *     which no longer relies solely on the BIOS and now tries to set
 *     things itself.
 */

#define AIC7XXX_STRICT_PCI_SETUP

/*
 * AIC7XXX_VERBOSE_DEBUGGING
 *   This option enables a lot of extra printk();s in the code, surrounded
 *   by if (aic7xxx_verbose ...) statements.  Executing all of those if
 *   statements and the extra checks can get to where it actually does have
 *   an impact on CPU usage and such, as well as code size.  Disabling this
 *   define will keep some of those from becoming part of the code.
 *
 *   NOTE:  Currently, this option has no real effect, I will be adding the
 *   various #ifdef's in the code later when I've decided a section is
 *   complete and no longer needs debugging.  OK...a lot of things are now
 *   surrounded by this define, so turning this off does have an impact.
 */
 
/*
 * #define AIC7XXX_VERBOSE_DEBUGGING
 */
 
#if defined(MODULE) || defined(PCMCIA)
#include <linux/module.h>
#endif

#if defined(PCMCIA)
#  undef MODULE
#endif

#include <stdarg.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/tqueue.h>
#include <linux/tasks.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "aic7xxx.h"

#include "aic7xxx/sequencer.h"
#include "aic7xxx/scsi_message.h"
#include "aic7xxx_reg.h"

#include <linux/stat.h>
#include <linux/malloc.h>        /* for kmalloc() */

#include <linux/config.h>        /* for CONFIG_PCI */

/*
 * To generate the correct addresses for the controller to issue
 * on the bus.  Originally added for DEC Alpha support.
 */
#define VIRT_TO_BUS(a) (unsigned int)virt_to_bus((void *)(a))

struct proc_dir_entry proc_scsi_aic7xxx = {
    PROC_SCSI_AIC7XXX, 7, "aic7xxx",
    S_IFDIR | S_IRUGO | S_IXUGO, 2,
    0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

#define AIC7XXX_C_VERSION  "5.1.4"

#define NUMBER(arr)     (sizeof(arr) / sizeof(arr[0]))
#define MIN(a,b)        (((a) < (b)) ? (a) : (b))
#define MAX(a,b)        (((a) > (b)) ? (a) : (b))
#define ALL_TARGETS -1
#define ALL_CHANNELS -1
#define ALL_LUNS -1
#define MAX_TARGETS  16
#define MAX_LUNS     8
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

#ifndef KERNEL_VERSION
#  define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

/*
 * We need the bios32.h file if we are kernel version 2.1.92 or less.  The
 * full set of pci_* changes wasn't in place until 2.1.93
 */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,1,92)
#  if defined(__sparc_v9__) || defined(__powerpc__)
#    error "PPC and Sparc platforms are only support under 2.1.92 and above"
#  endif
#  include <linux/bios32.h>
#endif

#if defined(__powerpc__)
#  define MMAPIO
#  ifdef mb
#    undef mb
#  endif
#  define mb() \
     __asm__ __volatile__("eieio" ::: "memory")
#elif defined(__i386__)
#  define MMAPIO
#  ifdef mb
#    undef mb
#  endif
#  define mb() \
     __asm__ __volatile__("lock ; addl $0,0(%%esp)": : :"memory")
#elif defined(__alpha__)
#  ifdef mb
#    undef mb
#  endif
#  define mb() \
     __asm__ __volatile__("mb": : :"memory")
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,0)
#  include <asm/spinlock.h>
#  include <linux/smp.h>
#  define cpuid smp_processor_id()
#  if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
#    define DRIVER_LOCK_INIT \
       spin_lock_init(&p->spin_lock);
#    define DRIVER_LOCK \
       if(!p->cpu_lock_count[cpuid]) { \
         spin_lock_irqsave(&p->spin_lock, cpu_flags); \
         p->cpu_lock_count[cpuid]++; \
       } else { \
         p->cpu_lock_count[cpuid]++; \
       }
#    define DRIVER_UNLOCK \
       if(--p->cpu_lock_count[cpuid] == 0) \
         spin_unlock_irqrestore(&p->spin_lock, cpu_flags);
#  else
#    define DRIVER_LOCK_INIT
#    define DRIVER_LOCK
#    define DRIVER_UNLOCK
#  endif
#else
#  define cpuid 0
#  define DRIVER_LOCK_INIT
#  define DRIVER_LOCK \
       save_flags(cpu_flags); \
       cli();
#  define DRIVER_UNLOCK \
       restore_flags(cpu_flags);
#  define le32_to_cpu(x) (x)
#  define cpu_to_le32(x) (x)
#endif

/*
 * You can try raising me if tagged queueing is enabled, or lowering
 * me if you only have 4 SCBs.
 */
#ifdef CONFIG_AIC7XXX_CMDS_PER_LUN
#define AIC7XXX_CMDS_PER_LUN CONFIG_AIC7XXX_CMDS_PER_LUN
#endif

/* Set this to the delay in seconds after SCSI bus reset. */
#ifdef CONFIG_AIC7XXX_RESET_DELAY
#define AIC7XXX_RESET_DELAY CONFIG_AIC7XXX_RESET_DELAY
#else
#define AIC7XXX_RESET_DELAY 5
#endif

/*
 * Control collection of SCSI transfer statistics for the /proc filesystem.
 *
 * NOTE: Do NOT enable this when running on kernels version 1.2.x and below.
 * NOTE: This does affect performance since it has to maintain statistics.
 */
#ifdef CONFIG_AIC7XXX_PROC_STATS
#define AIC7XXX_PROC_STATS
#endif

/*
 * NOTE: Uncommenting the define below no longer has any effect, the
 *       tagged queue value array is always active now.  I've added
 *       a setup option to set this particular array and I'm hoping
 *       insmod will be smart enough to set it properly as well.  It's
 *       by use of this array that a person can enable tagged queueing.
 *       The DEFAULT_TAG_COMMANDS define has been changed to disable
 *       tagged queueing by default, so if your devices can handle tagged
 *       queueing you will need to add a line to their lilo.conf file like:
 *       append="aic7xxx=verbose,tag_info:{{32,32,32,32},{32,32,32,32}}"
 *       which will result in the first four devices on the first two
 *       controllers being set to a tagged queue depth of 32.
 *
 * Set this for defining the number of tagged commands on a device
 * by device, and controller by controller basis.  The first set
 * of tagged commands will be used for the first detected aic7xxx
 * controller, the second set will be used for the second detected
 * aic7xxx controller, and so on.  These values will *only* be used
 * for targets that are tagged queueing capable; these values will
 * be ignored in all other cases.  The tag_commands is an array of
 * 16 to allow for wide and twin adapters.  Twin adapters will use
 * indexes 0-7 for channel 0, and indexes 8-15 for channel 1.
 *
 * *** Determining commands per LUN ***
 * 
 * When AIC7XXX_CMDS_PER_LUN is not defined, the driver will use its
 * own algorithm to determine the commands/LUN.  If SCB paging is
 * enabled, which is always now, the default is 8 commands per lun
 * that indicates it supports tagged queueing.  All non-tagged devices
 * use an internal queue depth of 3, with no more than one of those
 * three commands active at one time.
 */
/* #define AIC7XXX_TAGGED_QUEUEING_BY_DEVICE */

typedef struct
{
  unsigned char tag_commands[16];   /* Allow for wide/twin adapters. */
} adapter_tag_info_t;

/*
 * Make a define that will tell the driver not to use tagged queueing
 * by default.
 */
#define DEFAULT_TAG_COMMANDS {255, 255, 255, 255, 255, 255, 255, 255,\
                              255, 255, 255, 255, 255, 255, 255, 255}

/*
 * Modify this as you see fit for your system.  By setting tag_commands
 * to 0, the driver will use it's own algorithm for determining the
 * number of commands to use (see above).  When 255, the driver will
 * not enable tagged queueing for that particular device.  When positive
 * (> 0) and (< 255) the values in the array are used for the queue_depth.
 * Note that the maximum value for an entry is 254, but you're insane if
 * you try to use that many commands on one device.
 *
 * In this example, the first line will disable tagged queueing for all
 * the devices on the first probed aic7xxx adapter.
 *
 * The second line enables tagged queueing with 4 commands/LUN for IDs
 * (1, 2-11, 13-15), disables tagged queueing for ID 12, and tells the
 * driver to use its own algorithm for ID 1.
 *
 * The third line is the same as the first line.
 *
 * The fourth line disables tagged queueing for devices 0 and 3.  It
 * enables tagged queueing for the other IDs, with 16 commands/LUN
 * for IDs 1 and 4, 127 commands/LUN for ID 8, and 4 commands/LUN for
 * IDs 2, 5-7, and 9-15.
 */

/*
 * NOTE: The below structure is for reference only, the actual structure
 *       to modify in order to change things is located around line
 *       number 1305
adapter_tag_info_t aic7xxx_tag_info[] =
{
  {DEFAULT_TAG_COMMANDS},
  {{4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 255, 4, 4, 4}},
  {DEFAULT_TAG_COMMANDS},
  {{255, 16, 4, 255, 16, 4, 4, 4, 127, 4, 4, 4, 4, 4, 4, 4}}
};
*/

/*
 * Define an array of board names that can be indexed by aha_type.
 * Don't forget to change this when changing the types!
 */
static const char *board_names[] = {
  "AIC-7xxx Unknown",                                   /* AIC_NONE */
  "Adaptec AIC-7810 Hardware RAID Controller",          /* AIC_7810 */
  "Adaptec AIC-7770 SCSI host adapter",                 /* AIC_7770 */
  "Adaptec AHA-274X SCSI host adapter",                 /* AIC_7771 */
  "Adaptec AHA-284X SCSI host adapter",                 /* AIC_284x */
  "Adaptec AIC-7850 SCSI host adapter",                 /* AIC_7850 */
  "Adaptec AIC-7855 SCSI host adapter",                 /* AIC_7855 */
  "Adaptec AIC-7860 Ultra SCSI host adapter",           /* AIC_7860 */
  "Adaptec AHA-2940A Ultra SCSI host adapter",          /* AIC_7861 */
  "Adaptec AIC-7870 SCSI host adapter",                 /* AIC_7870 */
  "Adaptec AHA-294X SCSI host adapter",                 /* AIC_7871 */
  "Adaptec AHA-394X SCSI host adapter",                 /* AIC_7872 */
  "Adaptec AHA-398X SCSI host adapter",                 /* AIC_7873 */
  "Adaptec AHA-2944 SCSI host adapter",                 /* AIC_7874 */
  "Adaptec AIC-7880 Ultra SCSI host adapter",           /* AIC_7880 */
  "Adaptec AHA-294X Ultra SCSI host adapter",           /* AIC_7881 */
  "Adaptec AHA-394X Ultra SCSI host adapter",           /* AIC_7882 */
  "Adaptec AHA-398X Ultra SCSI host adapter",           /* AIC_7883 */
  "Adaptec AHA-2944 Ultra SCSI host adapter",           /* AIC_7884 */
  "Adaptec AIC-7895 Ultra SCSI host adapter",           /* AIC_7895 */
  "Adaptec AIC-7890/1 Ultra2 SCSI host adapter",        /* AIC_7890 */
  "Adaptec AHA-294X Ultra2 SCSI host adapter",          /* AIC_7890 */
  "Adaptec AIC-7896/7 Ultra2 SCSI host adapter",        /* AIC_7896 */
  "Adaptec AHA-394X Ultra2 SCSI host adapter"           /* AIC_7897 */
};

/*
 * There should be a specific return value for this in scsi.h, but
 * it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW   DID_ERROR

/*
 *  What we want to do is have the higher level scsi driver requeue
 *  the command to us. There is no specific driver status for this
 *  condition, but the higher level scsi driver will requeue the
 *  command on a DID_BUS_BUSY error.
 *
 *  Upon further inspection and testing, it seems that DID_BUS_BUSY
 *  will *always* retry the command.  We can get into an infinite loop
 *  if this happens when we really want some sort of counter that
 *  will automatically abort/reset the command after so many retries.
 *  Using DID_ERROR will do just that.  (Made by a suggestion by
 *  Doug Ledford 8/1/96)
 */
#define DID_RETRY_COMMAND DID_ERROR

#define HSCSIID        0x07
#define SCSI_RESET     0x040

/*
 * EISA/VL-bus stuff
 */
#define MINSLOT                1
#define MAXSLOT                15
#define SLOTBASE(x)        ((x) << 12)
#define BASE_TO_SLOT(x) ((x) >> 12)

/*
 * Standard EISA Host ID regs  (Offset from slot base)
 */
#define AHC_HID0              0x80   /* 0,1: msb of ID2, 2-7: ID1      */
#define AHC_HID1              0x81   /* 0-4: ID3, 5-7: LSB ID2         */
#define AHC_HID2              0x82   /* product                        */
#define AHC_HID3              0x83   /* firmware revision              */

/*
 * AIC-7770 I/O range to reserve for a card
 */
#define MINREG                0xC00
#define MAXREG                0xCBF

#define INTDEF                0x5C      /* Interrupt Definition Register */

/*
 * AIC-78X0 PCI registers
 */
#define        CLASS_PROGIF_REVID        0x08
#define                DEVREVID        0x000000FFul
#define                PROGINFC        0x0000FF00ul
#define                SUBCLASS        0x00FF0000ul
#define                BASECLASS        0xFF000000ul

#define        CSIZE_LATTIME                0x0C
#define                CACHESIZE        0x0000003Ful        /* only 5 bits */
#define                LATTIME                0x0000FF00ul

#define        DEVCONFIG                0x40
#define                SCBSIZE32        0x00010000ul        /* aic789X only */
#define                MPORTMODE        0x00000400ul        /* aic7870 only */
#define                RAMPSM           0x00000200ul        /* aic7870 only */
#define                RAMPSM_ULTRA2    0x00000004
#define                VOLSENSE         0x00000100ul
#define                SCBRAMSEL        0x00000080ul
#define                SCBRAMSEL_ULTRA2 0x00000008
#define                MRDCEN           0x00000040ul
#define                EXTSCBTIME       0x00000020ul        /* aic7870 only */
#define                EXTSCBPEN        0x00000010ul        /* aic7870 only */
#define                BERREN           0x00000008ul
#define                DACEN            0x00000004ul
#define                STPWLEVEL        0x00000002ul
#define                DIFACTNEGEN      0x00000001ul        /* aic7870 only */

#define        SCAMCTL                  0x1a                /* Ultra2 only  */
#define        CCSCBBADDR               0xf0                /* aic7895/6/7  */

/*
 * Define the different types of SEEPROMs on aic7xxx adapters
 * and make it also represent the address size used in accessing
 * its registers.  The 93C46 chips have 1024 bits organized into
 * 64 16-bit words, while the 93C56 chips have 2048 bits organized
 * into 128 16-bit words.  The C46 chips use 6 bits to address
 * each word, while the C56 and C66 (4096 bits) use 8 bits to
 * address each word.
 */
typedef enum {C46 = 6, C56_66 = 8} seeprom_chip_type;

/*
 *
 * Define the format of the SEEPROM registers (16 bits).
 *
 */
struct seeprom_config {

/*
 * SCSI ID Configuration Flags
 */
#define CFXFER                0x0007      /* synchronous transfer rate */
#define CFSYNCH               0x0008      /* enable synchronous transfer */
#define CFDISC                0x0010      /* enable disconnection */
#define CFWIDEB               0x0020      /* wide bus device (wide card) */
#define CFSYNCHISULTRA        0x0040      /* CFSYNC is an ultra offset */
#define CFNEWULTRAFORMAT      0x0080      /* Use the Ultra2 SEEPROM format */
#define CFSTART               0x0100      /* send start unit SCSI command */
#define CFINCBIOS             0x0200      /* include in BIOS scan */
#define CFRNFOUND             0x0400      /* report even if not found */
#define CFMULTILUN            0x0800      /* probe mult luns in BIOS scan */
#define CFWBCACHEYES          0x4000      /* Enable W-Behind Cache on drive */
#define CFWBCACHENC           0xc000      /* Don't change W-Behind Cache */
/* UNUSED                0x3000 */
  unsigned short device_flags[16];        /* words 0-15 */

/*
 * BIOS Control Bits
 */
#define CFSUPREM        0x0001  /* support all removable drives */
#define CFSUPREMB       0x0002  /* support removable drives for boot only */
#define CFBIOSEN        0x0004  /* BIOS enabled */
/* UNUSED                0x0008 */
#define CFSM2DRV        0x0010  /* support more than two drives */
#define CF284XEXTEND    0x0020  /* extended translation (284x cards) */
/* UNUSED                0x0040 */
#define CFEXTEND        0x0080  /* extended translation enabled */
/* UNUSED                0xFF00 */
  unsigned short bios_control;  /* word 16 */

/*
 * Host Adapter Control Bits
 */
#define CFAUTOTERM      0x0001  /* Perform Auto termination */
#define CFULTRAEN       0x0002  /* Ultra SCSI speed enable (Ultra cards) */
#define CF284XSELTO     0x0003  /* Selection timeout (284x cards) */
#define CF284XFIFO      0x000C  /* FIFO Threshold (284x cards) */
#define CFSTERM         0x0004  /* SCSI low byte termination */
#define CFWSTERM        0x0008  /* SCSI high byte termination (wide card) */
#define CFSPARITY       0x0010  /* SCSI parity */
#define CF284XSTERM     0x0020  /* SCSI low byte termination (284x cards) */
#define CFRESETB        0x0040  /* reset SCSI bus at boot */
#define CFBPRIMARY      0x0100  /* Channel B primary on 7895 chipsets */
#define CFSEAUTOTERM    0x0400  /* aic7890 Perform SE Auto Term */
#define CFLVDSTERM      0x0800  /* aic7890 LVD Termination */
/* UNUSED                0xF280 */
  unsigned short adapter_control;        /* word 17 */

/*
 * Bus Release, Host Adapter ID
 */
#define CFSCSIID        0x000F                /* host adapter SCSI ID */
/* UNUSED                0x00F0 */
#define CFBRTIME        0xFF00                /* bus release time */
  unsigned short brtime_id;                /* word 18 */

/*
 * Maximum targets
 */
#define CFMAXTARG        0x00FF        /* maximum targets */
/* UNUSED                0xFF00 */
  unsigned short max_targets;                /* word 19 */

  unsigned short res_1[11];                /* words 20-30 */
  unsigned short checksum;                /* word 31 */
};

#define SELBUS_MASK                0x0a
#define         SELNARROW        0x00
#define         SELBUSB                0x08
#define SINGLE_BUS                0x00

#define SCB_TARGET(scb)         \
       (((scb)->hscb->target_channel_lun & TID) >> 4)
#define SCB_LUN(scb)            \
       ((scb)->hscb->target_channel_lun & LID)
#define SCB_IS_SCSIBUS_B(scb)   \
       (((scb)->hscb->target_channel_lun & SELBUSB) != 0)

/*
 * If an error occurs during a data transfer phase, run the command
 * to completion - it's easier that way - making a note of the error
 * condition in this location. This then will modify a DID_OK status
 * into an appropriate error for the higher-level SCSI code.
 */
#define aic7xxx_error(cmd)        ((cmd)->SCp.Status)

/*
 * Keep track of the targets returned status.
 */
#define aic7xxx_status(cmd)        ((cmd)->SCp.sent_command)

/*
 * The position of the SCSI commands scb within the scb array.
 */
#define aic7xxx_position(cmd)        ((cmd)->SCp.have_data_in)

/*
 * So we can keep track of our host structs
 */
static struct aic7xxx_host *first_aic7xxx = NULL;

/*
 * As of Linux 2.1, the mid-level SCSI code uses virtual addresses
 * in the scatter-gather lists.  We need to convert the virtual
 * addresses to physical addresses.
 */
struct hw_scatterlist {
  unsigned int address;
  unsigned int length;
};

/*
 * Maximum number of SG segments these cards can support.
 */
#define        AIC7XXX_MAX_SG 128

/*
 * The maximum number of SCBs we could have for ANY type
 * of card. DON'T FORGET TO CHANGE THE SCB MASK IN THE
 * SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB        255


struct aic7xxx_hwscb {
/* ------------    Begin hardware supported fields    ---------------- */
/* 0*/  unsigned char control;
/* 1*/  unsigned char target_channel_lun;       /* 4/1/3 bits */
/* 2*/  unsigned char target_status;
/* 3*/  unsigned char SG_segment_count;
/* 4*/  unsigned int  SG_list_pointer;
/* 8*/  unsigned char residual_SG_segment_count;
/* 9*/  unsigned char residual_data_count[3];
/*12*/  unsigned int  data_pointer;
/*16*/  unsigned int  data_count;
/*20*/  unsigned int  SCSI_cmd_pointer;
/*24*/  unsigned char SCSI_cmd_length;
/*25*/  unsigned char tag;          /* Index into our kernel SCB array.
                                     * Also used as the tag for tagged I/O
                                     */
#define SCB_PIO_TRANSFER_SIZE  26   /* amount we need to upload/download
                                     * via PIO to initialize a transaction.
                                     */
/*26*/  unsigned char next;         /* Used to thread SCBs awaiting selection
                                     * or disconnected down in the sequencer.
                                     */
/*27*/  unsigned char prev;
/*28*/  unsigned int pad;           /*
                                     * Unused by the kernel, but we require
                                     * the padding so that the array of
                                     * hardware SCBs is alligned on 32 byte
                                     * boundaries so the sequencer can index
                                     */
};

typedef enum {
        SCB_FREE                = 0x0000,
        SCB_WAITINGQ            = 0x0002,
        SCB_ACTIVE              = 0x0004,
        SCB_SENSE               = 0x0008,
        SCB_ABORT               = 0x0010,
        SCB_DEVICE_RESET        = 0x0020,
        SCB_RESET               = 0x0040,
        SCB_RECOVERY_SCB        = 0x0080,
        SCB_WAS_BUSY            = 0x0100,
        SCB_MSGOUT_SENT         = 0x0200,
        SCB_MSGOUT_SDTR         = 0x0400,
        SCB_MSGOUT_WDTR         = 0x0800,
        SCB_MSGOUT_BITS         = SCB_MSGOUT_SENT | 
                                  SCB_MSGOUT_SDTR |
                                  SCB_MSGOUT_WDTR,
        SCB_QUEUED_ABORT        = 0x1000,
        SCB_QUEUED_FOR_DONE     = 0x2000
} scb_flag_type;

typedef enum {
        AHC_FNONE                 = 0x00000000,
        AHC_PAGESCBS              = 0x00000001,
        AHC_CHANNEL_B_PRIMARY     = 0x00000002,
        AHC_USEDEFAULTS           = 0x00000004,
        AHC_INDIRECT_PAGING       = 0x00000008,
        AHC_CHNLB                 = 0x00000020,
        AHC_CHNLC                 = 0x00000040,
        AHC_EXTEND_TRANS_A        = 0x00000100,
        AHC_EXTEND_TRANS_B        = 0x00000200,
        AHC_TERM_ENB_A            = 0x00000400,
        AHC_TERM_ENB_SE_LOW       = 0x00000400,
        AHC_TERM_ENB_B            = 0x00000800,
        AHC_TERM_ENB_SE_HIGH      = 0x00000800,
        AHC_HANDLING_REQINITS     = 0x00001000,
        AHC_TARGETMODE            = 0x00002000,
        AHC_NEWEEPROM_FMT         = 0x00004000,
 /*
  *  Here ends the FreeBSD defined flags and here begins the linux defined
  *  flags.  NOTE: I did not preserve the old flag name during this change
  *  specifically to force me to evaluate what flags were being used properly
  *  and what flags weren't.  This way, I could clean up the flag usage on
  *  a use by use basis.  Doug Ledford
  */
        AHC_A_SCANNED             = 0x00100000,
        AHC_B_SCANNED             = 0x00200000,
        AHC_MULTI_CHANNEL         = 0x00400000,
        AHC_BIOS_ENABLED          = 0x00800000,
        AHC_SEEPROM_FOUND         = 0x01000000,
        AHC_TERM_ENB_LVD          = 0x02000000,
        AHC_ABORT_PENDING         = 0x04000000,
        AHC_RESET_PENDING         = 0x08000000,
#define AHC_IN_ISR_BIT              28
        AHC_IN_ISR                = 0x10000000,
        AHC_IN_ABORT              = 0x20000000,
        AHC_IN_RESET              = 0x40000000,
        AHC_EXTERNAL_SRAM         = 0x80000000
} ahc_flag_type;

typedef enum {
  AHC_NONE             = 0x0000,
  AHC_CHIPID_MASK      = 0x00ff,
  AHC_AIC7770          = 0x0001,
  AHC_AIC7850          = 0x0002,
  AHC_AIC7860          = 0x0003,
  AHC_AIC7870          = 0x0004,
  AHC_AIC7880          = 0x0005,
  AHC_AIC7890          = 0x0006,
  AHC_AIC7895          = 0x0007,
  AHC_AIC7896          = 0x0008,
  AHC_VL               = 0x0100,
  AHC_EISA             = 0x0200,
  AHC_PCI              = 0x0400,
} ahc_chip;

typedef enum {
  AHC_FENONE           = 0x0000,
  AHC_ULTRA            = 0x0001,
  AHC_ULTRA2           = 0x0002,
  AHC_WIDE             = 0x0004,
  AHC_TWIN             = 0x0008,
  AHC_MORE_SRAM        = 0x0010,
  AHC_CMD_CHAN         = 0x0020,
  AHC_QUEUE_REGS       = 0x0040,
  AHC_SG_PRELOAD       = 0x0080,
  AHC_SPIOCAP          = 0x0100,
  AHC_AIC7770_FE       = AHC_FENONE,
  AHC_AIC7850_FE       = AHC_SPIOCAP,
  AHC_AIC7860_FE       = AHC_ULTRA|AHC_SPIOCAP,
  AHC_AIC7870_FE       = AHC_FENONE,
  AHC_AIC7880_FE       = AHC_ULTRA,
  AHC_AIC7890_FE       = AHC_MORE_SRAM|AHC_CMD_CHAN|AHC_ULTRA2|
                         AHC_QUEUE_REGS|AHC_SG_PRELOAD,
  AHC_AIC7895_FE       = AHC_MORE_SRAM|AHC_CMD_CHAN|AHC_ULTRA,
  AHC_AIC7896_FE       = AHC_AIC7890_FE,
} ahc_feature;

struct aic7xxx_scb {
        struct aic7xxx_hwscb  *hscb;          /* corresponding hardware scb */
        Scsi_Cmnd             *cmd;              /* Scsi_Cmnd for this scb */
        struct aic7xxx_scb    *q_next;        /* next scb in queue */
        volatile scb_flag_type flags;         /* current state of scb */
        struct hw_scatterlist *sg_list;       /* SG list in adapter format */
        void                  *kmalloc_ptr;
        unsigned char          tag_action;
        unsigned char          sg_count;
        unsigned char          sense_cmd[6];  /*
                                               * Allocate 6 characters for
                                               * sense command.
                                               */
        unsigned int           sg_length; /* We init this during buildscb so we
                                           * don't have to calculate anything
                                           * during underflow/overflow/stat code
                                           */
};

/*
 * Define a linked list of SCBs.
 */
typedef struct {
  struct aic7xxx_scb *head;
  struct aic7xxx_scb *tail;
} scb_queue_type;

static struct {
  unsigned char errno;
  const char *errmesg;
} hard_error[] = {
  { ILLHADDR,  "Illegal Host Access" },
  { ILLSADDR,  "Illegal Sequencer Address referenced" },
  { ILLOPCODE, "Illegal Opcode in sequencer program" },
  { SQPARERR,  "Sequencer Ram Parity Error" },
  { DPARERR,   "Data-Path Ram Parity Error" },
  { MPARERR,   "Scratch Ram/SCB Array Ram Parity Error" },
  { PCIERRSTAT,"PCI Error detected" },
  { CIOPARERR, "CIOBUS Parity Error" }
};

static unsigned char
generic_sense[] = { REQUEST_SENSE, 0, 0, 0, 255, 0 };

typedef struct {
  scb_queue_type free_scbs;        /*
                                    * SCBs assigned to free slot on
                                    * card (no paging required)
                                    */
  struct aic7xxx_scb   *scb_array[AIC7XXX_MAXSCB];
  struct aic7xxx_hwscb *hscbs;
  unsigned char  numscbs;          /* current number of scbs */
  unsigned char  maxhscbs;         /* hardware scbs */
  unsigned char  maxscbs;          /* max scbs including pageable scbs */
  void          *hscb_kmalloc_ptr;
} scb_data_type;

struct target_cmd {
  unsigned char mesg_bytes[4];
  unsigned char command[28];
};

#define AHC_TRANS_CUR    0x0001
#define AHC_TRANS_ACTIVE 0x0002
#define AHC_TRANS_GOAL   0x0004
#define AHC_TRANS_USER   0x0008
#define AHC_TRANS_QUITE  0x0010
typedef struct {
  unsigned char cur_width;
  unsigned char goal_width;
  unsigned char cur_period;
  unsigned char goal_period;
  unsigned char cur_offset;
  unsigned char goal_offset;
  unsigned char user_width;
  unsigned char user_period;
  unsigned char user_offset;
} transinfo_type;

/*
 * Define a structure used for each host adapter.  Note, in order to avoid
 * problems with architectures I can't test on (because I don't have one,
 * such as the Alpha based systems) which happen to give faults for
 * non-aligned memory accesses, care was taken to align this structure
 * in a way that gauranteed all accesses larger than 8 bits were aligned
 * on the appropriate boundary.  It's also organized to try and be more
 * cache line efficient.  Be careful when changing this lest you might hurt
 * overall performance and bring down the wrath of the masses.
 */
struct aic7xxx_host {
  /*
   *  This is the first 64 bytes in the host struct
   */

  struct Scsi_Host        *host;             /* pointer to scsi host */
  struct aic7xxx_host     *next;             /* allow for multiple IRQs */
  int                      host_no;          /* SCSI host number */
  unsigned long            base;             /* card base address */
  volatile unsigned char  *maddr;            /* memory mapped address */
  unsigned long            mbase;            /* I/O memory address */
  volatile ahc_flag_type   flags;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,0)
  spinlock_t               spin_lock;
#endif
  volatile unsigned char   cpu_lock_count[NR_CPUS];
  ahc_chip                 chip;             /* chip type */
  ahc_feature              features;         /* chip features */
  unsigned long            last_reset;
  unsigned long            isr_count;        /* Interrupt count */
  unsigned long            spurious_int;
  struct target_cmd       *targetcmds;
  unsigned int             num_targetcmds;
  unsigned short           discenable;       /* Targets allowed to disconnect */
  unsigned short           tagenable;        /* Targets using tagged I/O */
  unsigned short           orderedtag;       /* Ordered Q tags allowed */
  volatile unsigned char   activescbs;       /* active scbs */
  volatile unsigned char   max_activescbs;
  unsigned char            unpause;          /* unpause value for HCNTRL */
  unsigned char            pause;            /* pause value for HCNTRL */
  volatile unsigned char   qoutfifonext;
  volatile unsigned char   qinfifonext;

  /*
   * MAX_TARGETS is currently == 16, so that makes these entries the next
   * 64 bytes
   */

#define  DEVICE_PRESENT                 0x01
#define  BUS_DEVICE_RESET_PENDING       0x02
#define  DEVICE_TIMEOUT                 0x04
#define  DEVICE_PRINT_SDTR              0x08
#define  DEVICE_PRINT_WDTR              0x10
#define  DEVICE_SUCCESS                 0x20
#define  DEVICE_TAGGED_SUCCESS          0x40
#define  DEVICE_SCANNED                 0x80
  volatile unsigned char   dev_flags[MAX_TARGETS];
  volatile unsigned char   dev_active_cmds[MAX_TARGETS];
  volatile unsigned char   dev_temp_queue_depth[MAX_TARGETS];
  unsigned char            dev_commands_sent[MAX_TARGETS];

  /*
   * The next 128 (or 256 on 64 bit machines)....
   */
  Scsi_Cmnd               *dev_wdtr_cmnd[MAX_TARGETS];
  Scsi_Cmnd               *dev_sdtr_cmnd[MAX_TARGETS];

  /*
   * The next 64.... 
   */

  long                     dev_last_reset[MAX_TARGETS];

  /*
   * The next 64....
   */

  unsigned char            dev_mid_level_queue_depth[MAX_TARGETS];
  unsigned char            dev_last_queue_full[MAX_TARGETS];
  unsigned char            dev_last_queue_full_count[MAX_TARGETS];
  unsigned char            dev_max_queue_depth[MAX_TARGETS];

  /*
   * The next 128....
   */

  volatile scb_queue_type  delayed_scbs[MAX_TARGETS];

  /*
   *
   */

  struct timer_list        dev_timer[MAX_TARGETS];

  /*
   * The next 64....
   */

  unsigned char            msg_buf[9];       /* The message for the target */
  unsigned char            msg_type;
#define MSG_TYPE_NONE              0x00
#define MSG_TYPE_INITIATOR_MSGOUT  0x01
#define MSG_TYPE_INITIATOR_MSGIN   0x02
  unsigned char            msg_len;          /* Length of message */
  unsigned char            msg_index;        /* Index into msg_buf array */
  transinfo_type           transinfo[MAX_TARGETS];
  volatile scb_queue_type  waiting_scbs;     /*
                                              * SCBs waiting for space in
                                              * the QINFIFO.
                                              */
  scb_data_type           *scb_data;

  struct aic7xxx_cmd_queue {
    Scsi_Cmnd *head;
    Scsi_Cmnd *tail;
  } completeq;


  /*
   * We put the less frequently used host structure items after the more
   * frequently used items to try and ease the burden on the cache subsystem.
   * These entries are not *commonly* accessed, whereas the preceding entries
   * are accessed very often.  The only exceptions are the qinfifo, qoutfifo,
   * and untagged_scbs array.  But, they are often accessed only once and each
   * access into these arrays is likely to blow a cache line, so they are put
   * down here so we can minimize the number of cache lines required to hold
   * the preceeding entries.
   */

  volatile unsigned char   untagged_scbs[256];
  volatile unsigned char   qoutfifo[256];
  volatile unsigned char   qinfifo[256];
  unsigned int             irq;              /* IRQ for this adapter */
  volatile unsigned short  needsdtr;
  volatile unsigned short  sdtr_pending;
  volatile unsigned short  needwdtr;
  volatile unsigned short  wdtr_pending;
  int                      instance;         /* aic7xxx instance number */
  int                      scsi_id;          /* host adapter SCSI ID */
  int                      scsi_id_b;        /* channel B for twin adapters */
  unsigned int             bios_address;
  int                      board_name_index;
  unsigned long            reset_start;
  unsigned short           needsdtr_copy;    /* default config */
  unsigned short           needwdtr_copy;    /* default config */
  unsigned short           ultraenb;         /* Ultra mode target list */
  unsigned short           bios_control;     /* bios control - SEEPROM */
  unsigned short           adapter_control;  /* adapter control - SEEPROM */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
  struct pci_dev          *pdev;
#endif
  unsigned char            pci_bus;
  unsigned char            pci_device_fn;
  struct seeprom_config    sc;
  unsigned short           sc_type;
  unsigned short           sc_size;

  /*
   * Statistics Kept:
   *
   * Total Xfers (count for each command that has a data xfer),
   * broken down further by reads && writes.
   *
   * Binned sizes, writes && reads:
   *    < 512, 512, 1-2K, 2-4K, 4-8K, 8-16K, 16-32K, 32-64K, 64K-128K, > 128K
   *
   * Total amounts read/written above 512 bytes (amts under ignored)
   *
   * NOTE: Enabling this feature is likely to cause a noticeable performance
   * decrease as the accesses into the stats structures blows apart multiple
   * cache lines and is CPU time consuming.  We keep the xfer count always
   * for use by the aic7xxx_proc.c code, but only do the bins if the
   * proc stats code is enabled.
   */
  struct aic7xxx_xferstats {
    long xfers;                              /* total xfer count */
    long w_total;                            /* total writes */
    long w_total512;                         /* 512 byte blocks written */
    long r_total;                            /* total reads */
    long r_total512;                         /* 512 byte blocks read */
#ifdef AIC7XXX_PROC_STATS
    long w_bins[10];                         /* binned write */
    long r_bins[10];                         /* binned reads */
#endif /* AIC7XXX_PROC_STATS */
  } stats[MAX_TARGETS][MAX_LUNS];            /* [(channel << 3)|target][lun] */
};

/*
 * Valid SCSIRATE values. (p. 3-17)
 * Provides a mapping of transfer periods in ns/4 to the proper value to
 * stick in the SCSIRATE reg to use that transfer rate.
 */
#define AHC_SYNCRATE_ULTRA2 0
#define AHC_SYNCRATE_ULTRA  2
#define AHC_SYNCRATE_FAST   5
static struct aic7xxx_syncrate {
  /* Rates in Ultra mode have bit 8 of sxfr set */
#define                ULTRA_SXFR 0x100
  int sxfr_ultra2;
  int sxfr;
  unsigned char period;
  const char *rate[2];
} aic7xxx_syncrates[] = {
  { 0x13,  0x000,  10,  {"40.0", "80.0"} },
  { 0x14,  0x000,  11,  {"33.0", "66.6"} },
  { 0x15,  0x100,  12,  {"20.0", "40.0"} },
  { 0x16,  0x110,  15,  {"16.0", "32.0"} },
  { 0x17,  0x120,  18,  {"13.4", "26.8"} },
  { 0x18,  0x000,  25,  {"10.0", "20.0"} },
  { 0x19,  0x010,  31,  {"8.0",  "16.0"} },
  { 0x1a,  0x020,  37,  {"6.67", "13.3"} },
  { 0x1b,  0x030,  43,  {"5.7",  "11.4"} },
  { 0x10,  0x040,  50,  {"5.0",  "10.0"} },
  { 0x00,  0x050,  56,  {"4.4",  "8.8" } },
  { 0x00,  0x060,  62,  {"4.0",  "8.0" } },
  { 0x00,  0x070,  68,  {"3.6",  "7.2" } },
  { 0x00,  0x000,  0,   {NULL, NULL}   },
};

#define CTL_OF_SCB(scb) (((scb->hscb)->target_channel_lun >> 3) & 0x1),  \
                        (((scb->hscb)->target_channel_lun >> 4) & 0xf), \
                        ((scb->hscb)->target_channel_lun & 0x07)

#define CTL_OF_CMD(cmd) ((cmd->channel) & 0x01),  \
                        ((cmd->target) & 0x0f), \
                        ((cmd->lun) & 0x07)

#define TARGET_INDEX(cmd)  ((cmd)->target | ((cmd)->channel << 3))

/*
 * A nice little define to make doing our printks a little easier
 */

#define WARN_LEAD KERN_WARNING "(scsi%d:%d:%d:%d) "
#define INFO_LEAD KERN_INFO "(scsi%d:%d:%d:%d) "

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system.  This should be fixed.  Exceptions to this
 *       rule are noted in the comments.
 */


/*
 * Skip the scsi bus reset.  Non 0 make us skip the reset at startup.  This
 * has no effect on any later resets that might occur due to things like
 * SCSI bus timeouts.
 */
static unsigned int aic7xxx_no_reset = 0;
/*
 * Certain PCI motherboards will scan PCI devices from highest to lowest,
 * others scan from lowest to highest, and they tend to do all kinds of
 * strange things when they come into contact with PCI bridge chips.  The
 * net result of all this is that the PCI card that is actually used to boot
 * the machine is very hard to detect.  Most motherboards go from lowest
 * PCI slot number to highest, and the first SCSI controller found is the
 * one you boot from.  The only exceptions to this are when a controller
 * has its BIOS disabled.  So, we by default sort all of our SCSI controllers
 * from lowest PCI slot number to highest PCI slot number.  We also force
 * all controllers with their BIOS disabled to the end of the list.  This
 * works on *almost* all computers.  Where it doesn't work, we have this
 * option.  Setting this option to non-0 will reverse the order of the sort
 * to highest first, then lowest, but will still leave cards with their BIOS
 * disabled at the very end.  That should fix everyone up unless there are
 * really strange cirumstances.
 */
static int aic7xxx_reverse_scan = 0;
/*
 * This setting enables a hack to fix the IRQ settings on buggy 7895
 * MB controller setups:
 *    -1 == Disable this hack
 *     0 == Use the Channel A IRQ for both channels
 *     1 == Use the Channel B IRQ for both channels
 */
static unsigned int aic7xxx_extended = 0;
/*
 * The IRQ trigger method used on EISA controllers. Does not effect PCI cards.
 *   -1 = Use detected settings.
 *    0 = Force Edge triggered mode.
 *    1 = Force Level triggered mode.
 */
static int aic7xxx_irq_trigger = -1;
/*
 * This variable is used to override the termination settings on a controller.
 * This should not be used under normal conditions.  However, in the case
 * that a controller does not have a readable SEEPROM (so that we can't
 * read the SEEPROM settings directly) and that a controller has a buggered
 * version of the cable detection logic, this can be used to force the 
 * correct termination.  It is preferable to use the manual termination
 * settings in the BIOS if possible, but some motherboard controllers store
 * those settings in a format we can't read.  In other cases, auto term
 * should also work, but the chipset was put together with no auto term
 * logic (common on motherboard controllers).  In those cases, we have
 * 32 bits here to work with.  That's good for 8 controllers/channels.  The
 * bits are organized as 4 bits per channel, with scsi0 getting the lowest
 * 4 bits in the int.  A 1 in a bit position indicates the termination setting
 * that corresponds to that bit should be enabled, a 0 is disabled.
 * It looks something like this:
 *
 *    0x0f =  1111-Single Ended Low Byte Termination on/off
 *            ||\-Single Ended High Byte Termination on/off
 *            |\-LVD Low Byte Termination on/off
 *            \-LVD High Byte Termination on/off
 *
 * For non-Ultra2 controllers, the upper 2 bits are not important.  So, to
 * enable both high byte and low byte termination on scsi0, I would need to
 * make sure that the override_term variable was set to 0x03 (bits 0011).
 * To make sure that all termination is enabled on an Ultra2 controller at
 * scsi2 and only high byte termination on scsi1 and high and low byte
 * termination on scsi0, I would set override_term=0xf23 (bits 1111 0010 0011)
 *
 * For the most part, users should never have to use this, that's why I
 * left it fairly cryptic instead of easy to understand.  If you need it,
 * most likely someone will be telling you what your's needs to be set to.
 */
static int aic7xxx_override_term = -1;
/*
 * Certain motherboard chipset controllers tend to screw
 * up the polarity of the term enable output pin.  Use this variable
 * to force the correct polarity for your system.  This is a bitfield variable
 * similar to the previous one, but this one has one bit per channel instead
 * of four.
 *    0 = Force the setting to active low.
 *    1 = Force setting to active high.
 * Most Adaptec cards are active high, several motherboards are active low.
 * To force a 2940 card at SCSI 0 to active high and a motherboard 7895
 * controller at scsi1 and scsi2 to active low, and a 2910 card at scsi3
 * to active high, you would need to set stpwlev=0x9 (bits 1001).
 *
 * People shouldn't need to use this, but if you are experiencing lots of
 * SCSI timeout problems, this may help.  There is one sure way to test what
 * this option needs to be.  Using a boot floppy to boot the system, configure
 * your system to enable all SCSI termination (in the Adaptec SCSI BIOS) and
 * if needed then also pass a value to override_term to make sure that the
 * driver is enabling SCSI termination, then set this variable to either 0
 * or 1.  When the driver boots, make sure there are *NO* SCSI cables
 * connected to your controller.  If it finds and inits the controller
 * without problem, then the setting you passed to stpwlev was correct.  If
 * the driver goes into a reset loop and hangs the system, then you need the
 * other setting for this variable.  If neither setting lets the machine
 * boot then you have definite termination problems that may not be fixable.
 */
static int aic7xxx_stpwlev = -1;
/*
 * Set this to non-0 in order to force the driver to panic the kernel
 * and print out debugging info on a SCSI abort or reset cycle.
 */
static int aic7xxx_panic_on_abort = 0;
/*
 * PCI bus parity checking of the Adaptec controllers.  This is somewhat
 * dubious at best.  To my knowledge, this option has never actually
 * solved a PCI parity problem, but on certain machines with broken PCI
 * chipset configurations, it can generate tons of false error messages.
 * It's included in the driver for completeness.
 *   0 = Shut off PCI parity check
 *  -1 = Normal polarity pci parity checking
 *   1 = reverse polarity pci parity checking
 *
 * NOTE: you can't actually pass -1 on the lilo prompt.  So, to set this
 * variable to -1 you would actually want to simply pass the variable
 * name without a number.  That will invert the 0 which will result in
 * -1.
 */
static int aic7xxx_pci_parity = 0;
/*
 * Set this to any non-0 value to cause us to dump the contents of all
 * the card's registers in a hex dump format tailored to each model of
 * controller.
 * 
 * NOTE: THE CONTROLLER IS LEFT IN AN UNUSEABLE STATE BY THIS OPTION.
 *       YOU CANNOT BOOT UP WITH THIS OPTION, IT IS FOR DEBUGGING PURPOSES
 *       ONLY
 */
static int aic7xxx_dump_card = 0;
/*
 * Set this to a non-0 value to make us dump out the 32 bit instruction
 * registers on the card after completing the sequencer download.  This
 * allows the actual sequencer download to be verified.  It is possible
 * to use this option and still boot up and run your system.  This is
 * only intended for debugging purposes.
 */
static int aic7xxx_dump_sequencer = 0;
/*
 * Certain newer motherboards have put new PCI based devices into the
 * IO spaces that used to typically be occupied by VLB or EISA cards.
 * This overlap can cause these newer motherboards to lock up when scanned
 * for older EISA and VLB devices.  Setting this option to non-0 will
 * cause the driver to skip scanning for any VLB or EISA controllers and
 * only support the PCI controllers.  NOTE: this means that if the kernel
 * os compiled with PCI support disabled, then setting this to non-0
 * would result in never finding any devices :)
 */
static int aic7xxx_no_probe = 0;

/*
 * So that insmod can find the variable and make it point to something
 */
#ifdef MODULE
static char * aic7xxx = NULL;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,18)
MODULE_PARM(aic7xxx, "s");
#endif

/*
 * Just in case someone uses commas to separate items on the insmod
 * command line, we define a dummy buffer here to avoid having insmod
 * write wild stuff into our code segment
 */
static char dummy_buffer[60] = "Please don't trounce on me insmod!!\n";

#endif

/*
 * See the comments earlier in the file for what this item is all about
 * If you have more than 4 controllers, you will need to increase the
 * the number of items in the array below.  Additionally, if you don't
 * want to have lilo pass a humongous config line to the aic7xxx driver,
 * then you can get in and manually adjust these instead of leaving them
 * at the default.  Pay attention to the comments earlier in this file
 * concerning this array if you are going to hand modify these values.
 */
static adapter_tag_info_t aic7xxx_tag_info[] =
{
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS}
};

#define VERBOSE_NORMAL         0x0000
#define VERBOSE_NEGOTIATION    0x0001
#define VERBOSE_SEQINT         0x0002
#define VERBOSE_SCSIINT        0x0004
#define VERBOSE_PROBE          0x0008
#define VERBOSE_PROBE2         0x0010
#define VERBOSE_NEGOTIATION2   0x0020
#define VERBOSE_MINOR_ERROR    0x0040
#define VERBOSE_TRACING        0x0080
#define VERBOSE_ABORT          0x0f00
#define VERBOSE_ABORT_MID      0x0100
#define VERBOSE_ABORT_FIND     0x0200
#define VERBOSE_ABORT_PROCESS  0x0400
#define VERBOSE_ABORT_RETURN   0x0800
#define VERBOSE_RESET          0xf000
#define VERBOSE_RESET_MID      0x1000
#define VERBOSE_RESET_FIND     0x2000
#define VERBOSE_RESET_PROCESS  0x4000
#define VERBOSE_RESET_RETURN   0x8000
static int aic7xxx_verbose = VERBOSE_NORMAL | VERBOSE_NEGOTIATION |
           VERBOSE_PROBE;                     /* verbose messages */


/****************************************************************************
 *
 * We're going to start putting in function declarations so that order of
 * functions is no longer important.  As needed, they are added here.
 *
 ***************************************************************************/

static void aic7xxx_panic_abort(struct aic7xxx_host *p, Scsi_Cmnd *cmd);
static void aic7xxx_print_card(struct aic7xxx_host *p);
static void aic7xxx_print_scratch_ram(struct aic7xxx_host *p);
static void aic7xxx_print_sequencer(struct aic7xxx_host *p, int downloaded);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
static void aic7xxx_check_scbs(struct aic7xxx_host *p, char *buffer);
#endif

/****************************************************************************
 *
 * These functions are now used.  They happen to be wrapped in useless
 * inb/outb port read/writes around the real reads and writes because it
 * seems that certain very fast CPUs have a problem dealing with us when
 * going at full speed.
 *
 ***************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
static inline void
mdelay(int milliseconds)
{
  int i;

  for(i=0; i<milliseconds; i++)
    udelay(1000);
}
#endif

static inline unsigned char
aic_inb(struct aic7xxx_host *p, long port)
{
#ifdef MMAPIO
  unsigned char x;
  if(p->maddr)
  {
    x = p->maddr[port];
  }
  else
  {
    x = inb(p->base + port);
  }
  mb();
  return(x);
#else
  return(inb(p->base + port));
#endif
}

static inline void
aic_outb(struct aic7xxx_host *p, unsigned char val, long port)
{
#ifdef MMAPIO
  if(p->maddr)
  {
    p->maddr[port] = val;
  }
  else
  {
    outb(val, p->base + port);
  }
  mb();
#else
  outb(val, p->base + port);
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_setup
 *
 * Description:
 *   Handle Linux boot parameters. This routine allows for assigning a value
 *   to a parameter with a ':' between the parameter and the value.
 *   ie. aic7xxx=unpause:0x0A,extended
 *-F*************************************************************************/
void
aic7xxx_setup(char *s, int *dummy)
{
  int   i, n;
  char *p;
  char *end;

  static struct {
    const char *name;
    unsigned int *flag;
  } options[] = {
    { "extended",    &aic7xxx_extended },
    { "no_reset",    &aic7xxx_no_reset },
    { "irq_trigger", &aic7xxx_irq_trigger },
    { "verbose",     &aic7xxx_verbose },
    { "reverse_scan",&aic7xxx_reverse_scan },
    { "override_term", &aic7xxx_override_term },
    { "stpwlev", &aic7xxx_stpwlev },
    { "no_probe", &aic7xxx_no_probe },
    { "panic_on_abort", &aic7xxx_panic_on_abort },
    { "pci_parity", &aic7xxx_pci_parity },
    { "dump_card", &aic7xxx_dump_card },
    { "dump_sequencer", &aic7xxx_dump_sequencer },
    { "tag_info",    NULL }
  };

  end = strchr(s, '\0');

  for (p = strtok(s, ",."); p; p = strtok(NULL, ",."))
  {
    for (i = 0; i < NUMBER(options); i++)
    {
      n = strlen(options[i].name);
      if (!strncmp(options[i].name, p, n))
      {
        if (!strncmp(p, "tag_info", n))
        {
          if (p[n] == ':')
          {
            char *base;
            char *tok, *tok_end, *tok_end2;
            char tok_list[] = { '.', ',', '{', '}', '\0' };
            int i, instance = -1, device = -1;
            unsigned char done = FALSE;

            base = p;
            tok = base + n + 1;  /* Forward us just past the ':' */
            tok_end = strchr(tok, '\0');
            if (tok_end < end)
              *tok_end = ',';
            while(!done)
            {
              switch(*tok)
              {
                case '{':
                  if (instance == -1)
                    instance = 0;
                  else if (device == -1)
                    device = 0;
                  tok++;
                  break;
                case '}':
                  if (device != -1)
                    device = -1;
                  else if (instance != -1)
                    instance = -1;
                  tok++;
                  break;
                case ',':
                case '.':
                  if (instance == -1)
                    done = TRUE;
                  else if (device >= 0)
                    device++;
                  else if (instance >= 0)
                    instance++;
                  if ( (device >= MAX_TARGETS) || 
                       (instance >= NUMBER(aic7xxx_tag_info)) )
                    done = TRUE;
                  tok++;
                  if (!done)
                  {
                    base = tok;
                  }
                  break;
                case '\0':
                  done = TRUE;
                  break;
                default:
                  done = TRUE;
                  tok_end = strchr(tok, '\0');
                  for(i=0; tok_list[i]; i++)
                  {
                    tok_end2 = strchr(tok, tok_list[i]);
                    if ( (tok_end2) && (tok_end2 < tok_end) )
                    {
                      tok_end = tok_end2;
                      done = FALSE;
                    }
                  }
                  if ( (instance >= 0) && (device >= 0) &&
                       (instance < NUMBER(aic7xxx_tag_info)) &&
                       (device < MAX_TARGETS) )
                    aic7xxx_tag_info[instance].tag_commands[device] =
                      simple_strtoul(tok, NULL, 0) & 0xff;
                  tok = tok_end;
                  break;
              }
            }
            while((p != base) && (p != NULL))
              p = strtok(NULL, ",.");
          }
        }
        else if (p[n] == ':')
        {
          *(options[i].flag) = simple_strtoul(p + n + 1, NULL, 0);
        }
        else if (!strncmp(p, "verbose", n))
        {
          *(options[i].flag) = 0xff09;
        }
        else
        {
          *(options[i].flag) = ~(*(options[i].flag));
        }
      }
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   pause_sequencer
 *
 * Description:
 *   Pause the sequencer and wait for it to actually stop - this
 *   is important since the sequencer can disable pausing for critical
 *   sections.
 *-F*************************************************************************/
static inline void
pause_sequencer(struct aic7xxx_host *p)
{
  aic_outb(p, p->pause, HCNTRL);
  while ((aic_inb(p, HCNTRL) & PAUSE) == 0)
  {
    ;
  }
}

/*+F*************************************************************************
 * Function:
 *   unpause_sequencer
 *
 * Description:
 *   Unpause the sequencer. Unremarkable, yet done often enough to
 *   warrant an easy way to do it.
 *-F*************************************************************************/
static inline void
unpause_sequencer(struct aic7xxx_host *p, int unpause_always)
{
  if (unpause_always ||
      ( !(aic_inb(p, INTSTAT) & (SCSIINT | SEQINT | BRKADRINT)) &&
        !(p->flags & AHC_HANDLING_REQINITS) ) )
  {
    aic_outb(p, p->unpause, HCNTRL);
  }
}

/*+F*************************************************************************
 * Function:
 *   restart_sequencer
 *
 * Description:
 *   Restart the sequencer program from address zero.  This assumes
 *   that the sequencer is already paused.
 *-F*************************************************************************/
static inline void
restart_sequencer(struct aic7xxx_host *p)
{
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE, SEQCTL);
}

/*
 * We include the aic7xxx_seq.c file here so that the other defines have
 * already been made, and so that it comes before the code that actually
 * downloads the instructions (since we don't typically use function
 * prototype, our code has to be ordered that way, it's a left-over from
 * the original driver days.....I should fix it some time DL).
 */
#include "aic7xxx_seq.c"

/*+F*************************************************************************
 * Function:
 *   aic7xxx_check_patch
 *
 * Description:
 *   See if the next patch to download should be downloaded.
 *-F*************************************************************************/
static int
aic7xxx_check_patch(struct aic7xxx_host *p,
  struct sequencer_patch **start_patch, int start_instr, int *skip_addr)
{
  struct sequencer_patch *cur_patch;
  struct sequencer_patch *last_patch;
  int num_patches;

  num_patches = sizeof(sequencer_patches)/sizeof(struct sequencer_patch);
  last_patch = &sequencer_patches[num_patches];
  cur_patch = *start_patch;

  while ((cur_patch < last_patch) && (start_instr == cur_patch->begin))
  {
    if (cur_patch->patch_func(p) == 0)
    {
      /*
       * Start rejecting code.
       */
      *skip_addr = start_instr + cur_patch->skip_instr;
      cur_patch += cur_patch->skip_patch;
    }
    else
    {
      /*
       * Found an OK patch.  Advance the patch pointer to the next patch
       * and wait for our instruction pointer to get here.
       */
      cur_patch++;
    }
  }

  *start_patch = cur_patch;
  if (start_instr < *skip_addr)
    /*
     * Still skipping
     */
    return (0);
  return(1);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_download_instr
 *
 * Description:
 *   Find the next patch to download.
 *-F*************************************************************************/
static void
aic7xxx_download_instr(struct aic7xxx_host *p, int instrptr,
  unsigned char *dconsts)
{
  union ins_formats instr;
  struct ins_format1 *fmt1_ins;
  struct ins_format3 *fmt3_ins;
  unsigned char opcode;

  instr = *(union ins_formats*) &seqprog[instrptr * 4];

  instr.integer = le32_to_cpu(instr.integer);
  
  fmt1_ins = &instr.format1;
  fmt3_ins = NULL;

  /* Pull the opcode */
  opcode = instr.format1.opcode;
  switch (opcode)
  {
    case AIC_OP_JMP:
    case AIC_OP_JC:
    case AIC_OP_JNC:
    case AIC_OP_CALL:
    case AIC_OP_JNE:
    case AIC_OP_JNZ:
    case AIC_OP_JE:
    case AIC_OP_JZ:
    {
      struct sequencer_patch *cur_patch;
      int address_offset;
      unsigned int address;
      int skip_addr;
      int i;

      fmt3_ins = &instr.format3;
      address_offset = 0;
      address = fmt3_ins->address;
      cur_patch = sequencer_patches;
      skip_addr = 0;

      for (i = 0; i < address;)
      {
        aic7xxx_check_patch(p, &cur_patch, i, &skip_addr);
        if (skip_addr > i)
        {
          int end_addr;

          end_addr = MIN(address, skip_addr);
          address_offset += end_addr - i;
          i = skip_addr;
        }
        else
        {
          i++;
        }
      }
      address -= address_offset;
      fmt3_ins->address = address;
      /* Fall Through to the next code section */
    }
    case AIC_OP_OR:
    case AIC_OP_AND:
    case AIC_OP_XOR:
    case AIC_OP_ADD:
    case AIC_OP_ADC:
    case AIC_OP_BMOV:
      if (fmt1_ins->parity != 0)
      {
        fmt1_ins->immediate = dconsts[fmt1_ins->immediate];
      }
      fmt1_ins->parity = 0;
      /* Fall Through to the next code section */
    case AIC_OP_ROL:
      if ((p->features & AHC_ULTRA2) != 0)
      {
        int i, count;

        /* Calculate odd parity for the instruction */
        for ( i=0, count=0; i < 31; i++)
        {
          unsigned int mask;

          mask = 0x01 << i;
          if ((instr.integer & mask) != 0)
            count++;
        }
        if (!(count & 0x01))
          instr.format1.parity = 1;
      }
      else
      {
        if (fmt3_ins != NULL)
        {
          instr.integer =  fmt3_ins->immediate |
                          (fmt3_ins->source << 8) |
                          (fmt3_ins->address << 16) |
                          (fmt3_ins->opcode << 25);
        }
        else
        {
          instr.integer =  fmt1_ins->immediate |
                          (fmt1_ins->source << 8) |
                          (fmt1_ins->destination << 16) |
                          (fmt1_ins->ret << 24) |
                          (fmt1_ins->opcode << 25);
        }
      }
      aic_outb(p, (instr.integer & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 8) & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 16) & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 24) & 0xff), SEQRAM);
      break;

    default:
      panic("aic7xxx: Unknown opcode encountered in sequencer program.");
      break;
  }
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_loadseq
 *
 * Description:
 *   Load the sequencer code into the controller memory.
 *-F*************************************************************************/
static void
aic7xxx_loadseq(struct aic7xxx_host *p)
{
  struct sequencer_patch *cur_patch;
  int i;
  int downloaded;
  int skip_addr;
  unsigned char download_consts[4] = {0, 0, 0, 0};

  if (aic7xxx_verbose & VERBOSE_PROBE)
  {
    printk(KERN_INFO "(scsi%d) Downloading sequencer code...", p->host_no);
  }
  download_consts[TMODE_NUMCMDS] = p->num_targetcmds;
  cur_patch = &sequencer_patches[0];
  downloaded = 0;
  skip_addr = 0;

  aic_outb(p, PERRORDIS|LOADRAM|FAILDIS|FASTMODE, SEQCTL);
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);

  for (i = 0; i < sizeof(seqprog) / 4;  i++)
  {
    if (aic7xxx_check_patch(p, &cur_patch, i, &skip_addr) == 0)
    {
      /* Skip this instruction for this configuration. */
      continue;
    }
    aic7xxx_download_instr(p, i, &download_consts[0]);
    downloaded++;
  }

  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE | FAILDIS, SEQCTL);
  unpause_sequencer(p, TRUE);
  mdelay(1);
  pause_sequencer(p);
  aic_outb(p, FASTMODE, SEQCTL);
  if (aic7xxx_verbose & VERBOSE_PROBE)
  {
    printk(" %d instructions downloaded\n", downloaded);
  }
  if (aic7xxx_dump_sequencer)
    aic7xxx_print_sequencer(p, downloaded);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_sequencer
 *
 * Description:
 *   Print the contents of the sequencer memory to the screen.
 *-F*************************************************************************/
static void
aic7xxx_print_sequencer(struct aic7xxx_host *p, int downloaded)
{
  int i, k, temp;
  
  aic_outb(p, PERRORDIS|LOADRAM|FAILDIS|FASTMODE, SEQCTL);
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);

  k = 0;
  for (i=0; i < downloaded; i++)
  {
    if ( k == 0 )
      printk("%03x: ", i);
    temp = aic_inb(p, SEQRAM);
    temp |= (aic_inb(p, SEQRAM) << 8);
    temp |= (aic_inb(p, SEQRAM) << 16);
    temp |= (aic_inb(p, SEQRAM) << 24);
    printk("%08x", temp);
    if ( ++k == 8 )
    {
      printk("\n");
      k = 0;
    }
    else
      printk(" ");
  }
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE | FAILDIS, SEQCTL);
  unpause_sequencer(p, TRUE);
  mdelay(1);
  pause_sequencer(p);
  aic_outb(p, FASTMODE, SEQCTL);
  printk("\n");
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_delay
 *
 * Description:
 *   Delay for specified amount of time.  We use mdelay because the timer
 *   interrupt is not guaranteed to be enabled.  This will cause an
 *   infinite loop since jiffies (clock ticks) is not updated.
 *-F*************************************************************************/
static void
aic7xxx_delay(int seconds)
{
  mdelay(seconds * 1000);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_info
 *
 * Description:
 *   Return a string describing the driver.
 *-F*************************************************************************/
const char *
aic7xxx_info(struct Scsi_Host *dooh)
{
  static char buffer[256];
  char *bp;
  struct aic7xxx_host *p;

  bp = &buffer[0];
  p = (struct aic7xxx_host *)dooh->hostdata;
  memset(bp, 0, sizeof(buffer));
  strcpy(bp, "Adaptec AHA274x/284x/294x (EISA/VLB/PCI-Fast SCSI) ");
  strcat(bp, AIC7XXX_C_VERSION);
  strcat(bp, "/");
  strcat(bp, AIC7XXX_H_VERSION);
  strcat(bp, "\n");
  strcat(bp, "       <");
  strcat(bp, board_names[p->board_name_index]);
  strcat(bp, ">");

  return(bp);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_syncrate
 *
 * Description:
 *   Look up the valid period to SCSIRATE conversion in our table
 *-F*************************************************************************/
static struct aic7xxx_syncrate *
aic7xxx_find_syncrate(struct aic7xxx_host *p, unsigned int *period,
  unsigned int maxsync)
{
  struct aic7xxx_syncrate *syncrate;

  syncrate = &aic7xxx_syncrates[maxsync];
  while ( (syncrate->rate[0] != NULL) &&
         (!(p->features & AHC_ULTRA2) || syncrate->sxfr_ultra2) )
  {
    if ( *period <= syncrate->period )
    {
      /*
       * When responding to a target that requests sync, the requested rate
       * may fall between two rates that we can output, but still be a rate
       * that we can receive.  Because of this, we want to respond with the
       * same rate that it sent to us even if the persiod we use to send
       * data to it is lower.  Only lower the response period if we must.
       */
      if(syncrate == &aic7xxx_syncrates[maxsync])
      {
        *period = syncrate->period;
      }
      break;
    }
    syncrate++;
  }
  if ( (*period == 0) || (syncrate->rate[0] == NULL) ||
       ((p->features & AHC_ULTRA2) && (syncrate->sxfr_ultra2 == 0)) )
  {
    /*
     * Use async transfers for this target
     */
    *period = 0;
    syncrate = NULL;
  }
  return (syncrate);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_period
 *
 * Description:
 *   Look up the valid SCSIRATE to period conversion in our table
 *-F*************************************************************************/
static unsigned int
aic7xxx_find_period(struct aic7xxx_host *p, unsigned int scsirate,
  unsigned int maxsync)
{
  struct aic7xxx_syncrate *syncrate;

  if ((p->features & AHC_ULTRA2) != 0)
  {
    scsirate &= SXFR_ULTRA2;
  }
  else
  {
    scsirate &= SXFR;
  }

  syncrate = &aic7xxx_syncrates[maxsync];
  while (syncrate->rate[0] != NULL)
  {
    if ((p->features & AHC_ULTRA2) != 0)
    {
      if (syncrate->sxfr_ultra2 == 0)
        break;
      else if (scsirate == syncrate->sxfr_ultra2)
        return (syncrate->period);
    }
    else if (scsirate == (syncrate->sxfr & ~ULTRA_SXFR))
    {
      return (syncrate->period);
    }
    syncrate++;
  }
  return (0); /* async */
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_validate_offset
 *
 * Description:
 *   Set a valid offset value for a particular card in use and transfer
 *   settings in use.
 *-F*************************************************************************/
static void
aic7xxx_validate_offset(struct aic7xxx_host *p,
  struct aic7xxx_syncrate *syncrate, unsigned int *offset, int wide)
{
  unsigned int maxoffset;

  /* Limit offset to what the card (and device) can do */
  if (syncrate == NULL)
  {
    maxoffset = 0;
  }
  else if (p->features & AHC_ULTRA2)
  {
    maxoffset = MAX_OFFSET_ULTRA2;
  }
  else
  {
    if (wide)
      maxoffset = MAX_OFFSET_16BIT;
    else
      maxoffset = MAX_OFFSET_8BIT;
  }
  *offset = MIN(*offset, maxoffset);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_syncrate
 *
 * Description:
 *   Set the actual syncrate down in the card and in our host structs
 *-F*************************************************************************/
static void
aic7xxx_set_syncrate(struct aic7xxx_host *p, struct aic7xxx_syncrate *syncrate,
    int target, int channel, unsigned int period, unsigned int offset,
    unsigned int type)
{
  unsigned char tindex;
  unsigned short target_mask;
  unsigned char lun;
  unsigned int old_period, old_offset;

  tindex = target | (channel << 3);
  target_mask = 0x01 << tindex;
  lun = aic_inb(p, SCB_TCL) & 0x07;

  if (syncrate == NULL)
  {
    period = 0;
    offset = 0;
  }

  old_period = p->transinfo[tindex].cur_period;
  old_offset = p->transinfo[tindex].cur_offset;

  
  if (type & AHC_TRANS_CUR)
  {
    unsigned int scsirate;

    scsirate = aic_inb(p, TARG_SCSIRATE + tindex);
    if (p->features & AHC_ULTRA2)
    {
      scsirate &= ~SXFR_ULTRA2;
      if (syncrate != NULL)
      {
        scsirate |= syncrate->sxfr_ultra2;
      }
      if (type & AHC_TRANS_ACTIVE)
      {
        aic_outb(p, offset, SCSIOFFSET);
      }
      aic_outb(p, offset, TARG_OFFSET + tindex);
    }
    else /* Not an Ultra2 controller */
    {
      scsirate &= ~(SXFR|SOFS);
      p->ultraenb &= ~target_mask;
      if (syncrate != NULL)
      {
        if (syncrate->sxfr & ULTRA_SXFR)
        {
          p->ultraenb |= target_mask;
        }
        scsirate |= (syncrate->sxfr & SXFR);
        scsirate |= (offset & SOFS);
      }
      if (type & AHC_TRANS_ACTIVE)
      {
        unsigned char sxfrctl0;

        sxfrctl0 = aic_inb(p, SXFRCTL0);
        sxfrctl0 &= ~FAST20;
        if (p->ultraenb & target_mask)
          sxfrctl0 |= FAST20;
        aic_outb(p, sxfrctl0, SXFRCTL0);
      }
      aic_outb(p, p->ultraenb & 0xff, ULTRA_ENB);
      aic_outb(p, (p->ultraenb >> 8) & 0xff, ULTRA_ENB + 1 );
    }
    if (type & AHC_TRANS_ACTIVE)
    {
      aic_outb(p, scsirate, SCSIRATE);
    }
    aic_outb(p, scsirate, TARG_SCSIRATE + tindex);
    p->transinfo[tindex].cur_period = period;
    p->transinfo[tindex].cur_offset = offset;
    if ( !(type & AHC_TRANS_QUITE) &&
         (aic7xxx_verbose & VERBOSE_NEGOTIATION) &&
         (p->dev_flags[tindex] & DEVICE_PRINT_SDTR) )
    {
      if (offset)
      {
        int rate_mod = (scsirate & WIDEXFER) ? 1 : 0;
      
        printk(INFO_LEAD "Synchronous at %s Mbyte/sec, "
               "offset %d.\n", p->host_no, channel, target, lun,
               syncrate->rate[rate_mod], offset);
      }
      else
      {
        printk(INFO_LEAD "Using asynchronous transfers.\n",
               p->host_no, channel, target, lun);
      }
      p->dev_flags[tindex] &= ~DEVICE_PRINT_SDTR;
    }
  }

  if (type & AHC_TRANS_GOAL)
  {
    p->transinfo[tindex].goal_period = period;
    p->transinfo[tindex].goal_offset = offset;
  }

  if (type & AHC_TRANS_USER)
  {
    p->transinfo[tindex].user_period = period;
    p->transinfo[tindex].user_offset = offset;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_width
 *
 * Description:
 *   Set the actual width down in the card and in our host structs
 *-F*************************************************************************/
static void
aic7xxx_set_width(struct aic7xxx_host *p, int target, int channel, int lun,
    unsigned int width, unsigned int type)
{
  unsigned char tindex;
  unsigned short target_mask;
  unsigned int old_width, new_offset;

  tindex = target | (channel << 3);
  target_mask = 1 << tindex;
  
  old_width = p->transinfo[tindex].cur_width;

  if (p->features & AHC_ULTRA2)
    new_offset = MAX_OFFSET_ULTRA2;
  else if (width == MSG_EXT_WDTR_BUS_16_BIT)
    new_offset = MAX_OFFSET_16BIT;
  else
    new_offset = MAX_OFFSET_8BIT;
  
  if (type & AHC_TRANS_CUR) 
  {
    unsigned char scsirate;

    scsirate = aic_inb(p, TARG_SCSIRATE + tindex);

    scsirate &= ~WIDEXFER;
    if (width == MSG_EXT_WDTR_BUS_16_BIT)
      scsirate |= WIDEXFER;

    aic_outb(p, scsirate, TARG_SCSIRATE + tindex);

    if (type & AHC_TRANS_ACTIVE)
      aic_outb(p, scsirate, SCSIRATE);

    p->transinfo[tindex].cur_width = width;

    if ((aic7xxx_verbose & VERBOSE_NEGOTIATION2) && 
        (p->dev_flags[tindex] & DEVICE_PRINT_WDTR))
    {
      printk(INFO_LEAD "Using %s transfers\n", p->host_no, channel, target,
        lun, (scsirate & WIDEXFER) ? "Wide(16bit)" : "Narrow(8bit)" );
      p->dev_flags[tindex] &= ~DEVICE_PRINT_WDTR;
    }
  }

  if (type & AHC_TRANS_GOAL)
    p->transinfo[tindex].goal_width = width;
  if (type & AHC_TRANS_USER)
    p->transinfo[tindex].user_width = width;

  /*
   * Having just set the width, the SDTR should come next, and we need a valid
   * offset for the SDTR.  So, we make sure we put a valid one in here now as
   * the goal_offset.
   */
  if (p->transinfo[tindex].goal_offset)
    p->transinfo[tindex].goal_offset = new_offset;

}
      
/*+F*************************************************************************
 * Function:
 *   scbq_init
 *
 * Description:
 *   SCB queue initialization.
 *
 *-F*************************************************************************/
static void
scbq_init(volatile scb_queue_type *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_head
 *
 * Description:
 *   Add an SCB to the head of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_head(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags;
#endif

  DRIVER_LOCK
  scb->q_next = queue->head;
  queue->head = scb;
  if (queue->tail == NULL)       /* If list was empty, update tail. */
    queue->tail = queue->head;
  DRIVER_UNLOCK
}

/*+F*************************************************************************
 * Function:
 *   scbq_remove_head
 *
 * Description:
 *   Remove an SCB from the head of the list.
 *
 *-F*************************************************************************/
static inline struct aic7xxx_scb *
scbq_remove_head(volatile scb_queue_type *queue)
{
  struct aic7xxx_scb * scbp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags;
#endif

  DRIVER_LOCK
  scbp = queue->head;
  if (queue->head != NULL)
    queue->head = queue->head->q_next;
  if (queue->head == NULL)       /* If list is now empty, update tail. */
    queue->tail = NULL;
  DRIVER_UNLOCK
  return(scbp);
}

/*+F*************************************************************************
 * Function:
 *   scbq_remove
 *
 * Description:
 *   Removes an SCB from the list.
 *
 *-F*************************************************************************/
static inline void
scbq_remove(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags;
#endif

  DRIVER_LOCK
  if (queue->head == scb)
  {
    /* At beginning of queue, remove from head. */
    scbq_remove_head(queue);
  }
  else
  {
    struct aic7xxx_scb *curscb = queue->head;

    /*
     * Search until the next scb is the one we're looking for, or
     * we run out of queue.
     */
    while ((curscb != NULL) && (curscb->q_next != scb))
    {
      curscb = curscb->q_next;
    }
    if (curscb != NULL)
    {
      /* Found it. */
      curscb->q_next = scb->q_next;
      if (scb->q_next == NULL)
      {
        /* Update the tail when removing the tail. */
        queue->tail = curscb;
      }
    }
  }
  DRIVER_UNLOCK
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_tail
 *
 * Description:
 *   Add an SCB at the tail of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_tail(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags;
#endif

  DRIVER_LOCK
  scb->q_next = NULL;
  if (queue->tail != NULL)       /* Add the scb at the end of the list. */
    queue->tail->q_next = scb;
  queue->tail = scb;             /* Update the tail. */
  if (queue->head == NULL)       /* If list was empty, update head. */
    queue->head = queue->tail;
  DRIVER_UNLOCK
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_match_scb
 *
 * Description:
 *   Checks to see if an scb matches the target/channel as specified.
 *   If target is ALL_TARGETS (-1), then we're looking for any device
 *   on the specified channel; this happens when a channel is going
 *   to be reset and all devices on that channel must be aborted.
 *-F*************************************************************************/
static int
aic7xxx_match_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb,
    int target, int channel, int lun, unsigned char tag)
{
  int targ = (scb->hscb->target_channel_lun >> 4) & 0x0F;
  int chan = (scb->hscb->target_channel_lun >> 3) & 0x01;
  int slun = scb->hscb->target_channel_lun & 0x07;
  int match;

  match = ((chan == channel) || (channel == ALL_CHANNELS));
  if (match != 0)
    match = ((targ == target) || (target == ALL_TARGETS));
  if (match != 0)
    match = ((lun == slun) || (lun == ALL_LUNS));
  if (match != 0)
    match = ((tag == scb->hscb->tag) || (tag == SCB_LIST_NULL));

  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
  {
    printk(KERN_INFO "(scsi%d:%d:%d:%d:tag%d) %s search criteria"
      " (scsi%d:%d:%d:%d:tag%d)\n", p->host_no, CTL_OF_SCB(scb),
      scb->hscb->tag, (match) ? "matches" : "doesn't match",
      p->host_no, channel, target, lun, tag);
  }

  return (match);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_add_curscb_to_free_list
 *
 * Description:
 *   Adds the current scb (in SCBPTR) to the list of free SCBs.
 *-F*************************************************************************/
static void
aic7xxx_add_curscb_to_free_list(struct aic7xxx_host *p)
{
  /*
   * Invalidate the tag so that aic7xxx_find_scb doesn't think
   * it's active
   */
  aic_outb(p, SCB_LIST_NULL, SCB_TAG);
  aic_outb(p, 0, SCB_CONTROL);

  aic_outb(p, aic_inb(p, FREE_SCBH), SCB_NEXT);
  aic_outb(p, aic_inb(p, SCBPTR), FREE_SCBH);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_rem_scb_from_disc_list
 *
 * Description:
 *   Removes the current SCB from the disconnected list and adds it
 *   to the free list.
 *-F*************************************************************************/
static unsigned char
aic7xxx_rem_scb_from_disc_list(struct aic7xxx_host *p, unsigned char scbptr)
{
  unsigned char next;
  unsigned char prev;

  aic_outb(p, scbptr, SCBPTR);
  next = aic_inb(p, SCB_NEXT);
  prev = aic_inb(p, SCB_PREV);
  aic7xxx_add_curscb_to_free_list(p);

  if (prev != SCB_LIST_NULL)
  {
    aic_outb(p, prev, SCBPTR);
    aic_outb(p, next, SCB_NEXT);
  }
  else
  {
    aic_outb(p, next, DISCONNECTED_SCBH);
  }

  if (next != SCB_LIST_NULL)
  {
    aic_outb(p, next, SCBPTR);
    aic_outb(p, prev, SCB_PREV);
  }
  return next;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_busy_target
 *
 * Description:
 *   Set the specified target busy.
 *-F*************************************************************************/
static inline void
aic7xxx_busy_target(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  p->untagged_scbs[scb->hscb->target_channel_lun] = scb->hscb->tag;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_index_busy_target
 *
 * Description:
 *   Returns the index of the busy target, and optionally sets the
 *   target inactive.
 *-F*************************************************************************/
static inline unsigned char
aic7xxx_index_busy_target(struct aic7xxx_host *p, unsigned char tcl,
    int unbusy)
{
  unsigned char busy_scbid;

  busy_scbid = p->untagged_scbs[tcl];
  if (unbusy)
  {
    p->untagged_scbs[tcl] = SCB_LIST_NULL;
  }
  return (busy_scbid);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_scb
 *
 * Description:
 *   Look through the SCB array of the card and attempt to find the
 *   hardware SCB that corresponds to the passed in SCB.  Return
 *   SCB_LIST_NULL if unsuccessful.  This routine assumes that the
 *   card is already paused.
 *-F*************************************************************************/
static unsigned char
aic7xxx_find_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  unsigned char saved_scbptr;
  unsigned char curindex;

  saved_scbptr = aic_inb(p, SCBPTR);
  curindex = 0;
  for (curindex = 0; curindex < p->scb_data->maxhscbs; curindex++)
  {
    aic_outb(p, curindex, SCBPTR);
    if (aic_inb(p, SCB_TAG) == scb->hscb->tag)
    {
      break;
    }
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  if (curindex >= p->scb_data->maxhscbs)
  {
    curindex = SCB_LIST_NULL;
  }

  return (curindex);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_allocate_scb
 *
 * Description:
 *   Get an SCB from the free list or by allocating a new one.
 *-F*************************************************************************/
static int
aic7xxx_allocate_scb(struct aic7xxx_host *p)
{
  struct aic7xxx_scb   *scbp = NULL;
  int scb_size = sizeof(struct aic7xxx_scb) +
                 sizeof (struct hw_scatterlist) * AIC7XXX_MAX_SG;
  int i;
  int step = PAGE_SIZE / 1024;
  unsigned long scb_count = 0;
  struct hw_scatterlist *hsgp;
  struct aic7xxx_scb *scb_ap;
  unsigned long temp;


  if (p->scb_data->numscbs < p->scb_data->maxscbs)
  {
    /*
     * Calculate the optimal number of SCBs to allocate.
     *
     * NOTE: This formula works because the sizeof(sg_array) is always
     * 1024.  Therefore, scb_size * i would always be > PAGE_SIZE *
     * (i/step).  The (i-1) allows the left hand side of the equation
     * to grow into the right hand side to a point of near perfect
     * efficiency since scb_size * (i -1) is growing slightly faster
     * than the right hand side.  If the number of SG array elements
     * is changed, this function may not be near so efficient any more.
     */
    for ( i=step;; i *= 2 )
    {
      if ( (scb_size * (i-1)) >= ( (PAGE_SIZE * (i/step)) - 64 ) )
      {
        i /= 2;
        break;
      }
    }
    scb_count = MIN( (i-1), p->scb_data->maxscbs - p->scb_data->numscbs);
    scb_ap = (struct aic7xxx_scb *)kmalloc(scb_size * scb_count, GFP_ATOMIC);
    if (scb_ap != NULL)
    {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
      {
        if (p->scb_data->numscbs == 0)
          printk(INFO_LEAD "Allocating initial %ld SCB structures.\n",
            p->host_no, -1, -1, -1, scb_count);
        else
          printk(INFO_LEAD "Allocating %ld additional SCB structures.\n",
            p->host_no, -1, -1, -1, scb_count);
      }
#endif
      memset(scb_ap, 0, scb_count * scb_size);
      temp = (unsigned long) &scb_ap[scb_count];
      temp += 1023;
      temp &= ~1023;
      hsgp = (struct hw_scatterlist *)temp;
      for (i=0; i < scb_count; i++)
      {
        scbp = &scb_ap[i];
        scbp->hscb = &p->scb_data->hscbs[p->scb_data->numscbs];
        scbp->sg_list = &hsgp[i * AIC7XXX_MAX_SG];
        memset(scbp->hscb, 0, sizeof(struct aic7xxx_hwscb));
        scbp->hscb->tag = p->scb_data->numscbs;
        /*
         * Place in the scb array; never is removed
         */
        p->scb_data->scb_array[p->scb_data->numscbs++] = scbp;
        scbq_insert_head(&p->scb_data->free_scbs, scbp);
      }
      scbp->kmalloc_ptr = scb_ap;
    }
    else
    {
      return(0);
    }
  }
  return(scb_count);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue_cmd_complete
 *
 * Description:
 *   Due to race conditions present in the SCSI subsystem, it is easier
 *   to queue completed commands, then call scsi_done() on them when
 *   we're finished.  This function queues the completed commands.
 *-F*************************************************************************/
static void
aic7xxx_queue_cmd_complete(struct aic7xxx_host *p, Scsi_Cmnd *cmd)
{
  cmd->host_scribble = (char *)p->completeq.head;
  p->completeq.head = cmd;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done_cmds_complete
 *
 * Description:
 *   Process the completed command queue.
 *-F*************************************************************************/
static void
aic7xxx_done_cmds_complete(struct aic7xxx_host *p)
{
  Scsi_Cmnd *cmd;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned int cpu_flags = 0;
  
  DRIVER_LOCK
  while (p->completeq.head != NULL)
  {
    cmd = p->completeq.head;
    p->completeq.head = (Scsi_Cmnd *)cmd->host_scribble;
    cmd->host_scribble = NULL;
    sti();
    cmd->scsi_done(cmd);
    cli();
  }
  DRIVER_UNLOCK
#else
  while (p->completeq.head != NULL)
  {
    cmd = p->completeq.head;
    p->completeq.head = (Scsi_Cmnd *)cmd->host_scribble;
    cmd->host_scribble = NULL;
    cmd->scsi_done(cmd);
  }
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free_scb
 *
 * Description:
 *   Free the scb and insert into the free scb list.
 *-F*************************************************************************/
static void
aic7xxx_free_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{

  scb->flags = SCB_FREE;
  scb->cmd = NULL;
  scb->sg_count = 0;
  scb->sg_length = 0;
  scb->tag_action = 0;
  scb->hscb->control = 0;
  scb->hscb->target_status = 0;
  scb->hscb->target_channel_lun = SCB_LIST_NULL;

  scbq_insert_head(&p->scb_data->free_scbs, scb);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done
 *
 * Description:
 *   Calls the higher level scsi done function and frees the scb.
 *-F*************************************************************************/
static void
aic7xxx_done(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  Scsi_Cmnd *cmd = scb->cmd;
  int tindex = TARGET_INDEX(cmd);
  struct aic7xxx_scb *scbp;
  unsigned char queue_depth;

  if (scb->flags & SCB_RECOVERY_SCB)
  {
    p->flags &= ~AHC_ABORT_PENDING;
  }
  if (scb->flags & SCB_RESET)
  {
      cmd->result = (DID_RESET << 16) | (cmd->result & 0xffff);
  }
  else if (scb->flags & SCB_ABORT)
  {
      cmd->result = (DID_RESET << 16) | (cmd->result & 0xffff);
  }
  else if (!(p->dev_flags[tindex] & DEVICE_SCANNED))
  {
    if ( (cmd->cmnd[0] == INQUIRY) && (cmd->result == DID_OK) )
    {
      char *buffer;
      
      if(cmd->use_sg)
      {
        struct scatterlist *sg;

        sg = (struct scatterlist *)cmd->request_buffer;
        buffer = (char *)sg[0].address;
      }
      else
      {
        buffer = (char *)cmd->request_buffer;
      }
#define WIDE_INQUIRY_BITS 0x60
#define SYNC_INQUIRY_BITS 0x10
      if ( (buffer[7] & WIDE_INQUIRY_BITS) &&
           (p->features & AHC_WIDE) )
      {
        p->needwdtr |= (1<<tindex);
        p->needwdtr_copy |= (1<<tindex);
        if ( (p->flags & AHC_SEEPROM_FOUND) &&
             (p->transinfo[tindex].user_width != MSG_EXT_WDTR_BUS_16_BIT) )
          p->transinfo[tindex].goal_width = MSG_EXT_WDTR_BUS_8_BIT;
        else
          p->transinfo[tindex].goal_width = MSG_EXT_WDTR_BUS_16_BIT;
      }
      else
      {
        p->needwdtr &= ~(1<<tindex);
        p->needwdtr_copy &= ~(1<<tindex);
        pause_sequencer(p);
        aic7xxx_set_width(p, cmd->target, cmd->channel, cmd->lun,
                          MSG_EXT_WDTR_BUS_8_BIT, (AHC_TRANS_ACTIVE |
                                                   AHC_TRANS_GOAL |
                                                   AHC_TRANS_CUR) );
        unpause_sequencer(p, FALSE);
      }
      if (buffer[7] & SYNC_INQUIRY_BITS)
      {
        p->needsdtr |= (1<<tindex);
        p->needsdtr_copy |= (1<<tindex);

        if (p->flags & AHC_SEEPROM_FOUND)
          p->transinfo[tindex].goal_period = p->transinfo[tindex].user_period;
        else if (p->features & AHC_ULTRA2)
          p->transinfo[tindex].goal_period =
            aic7xxx_syncrates[AHC_SYNCRATE_ULTRA2].period;
        else if (p->features & AHC_ULTRA)
          p->transinfo[tindex].goal_period =
            aic7xxx_syncrates[AHC_SYNCRATE_ULTRA].period;
        else
          p->transinfo[tindex].goal_period =
            aic7xxx_syncrates[AHC_SYNCRATE_FAST].period;

        if (p->features & AHC_ULTRA2)
          p->transinfo[tindex].goal_offset = MAX_OFFSET_ULTRA2;
        else if (p->transinfo[tindex].goal_width == MSG_EXT_WDTR_BUS_16_BIT)
          p->transinfo[tindex].goal_offset = MAX_OFFSET_16BIT;
        else
          p->transinfo[tindex].goal_offset = MAX_OFFSET_8BIT;
      }
      else
      {
        p->needsdtr &= ~(1<<tindex);
        p->needsdtr_copy &= ~(1<<tindex);
        p->transinfo[tindex].goal_period = 0;
        p->transinfo[tindex].goal_offset = 0;
      }
      p->dev_flags[tindex] |= DEVICE_SCANNED;
      p->dev_flags[tindex] |= DEVICE_PRINT_WDTR | DEVICE_PRINT_SDTR;
#undef WIDE_INQUIRY_BITS
#undef SYNC_INQUIRY_BITS
    }
  }
  else if ((scb->flags & (SCB_MSGOUT_WDTR | SCB_MSGOUT_SDTR)) != 0)
  {
    unsigned short mask;
    int message_error = FALSE;

    mask = 0x01 << tindex;
 
    /*
     * Check to see if we get an invalid message or a message error
     * after failing to negotiate a wide or sync transfer message.
     */
    if ((scb->flags & SCB_SENSE) && 
          ((scb->cmd->sense_buffer[12] == 0x43) ||  /* INVALID_MESSAGE */
          (scb->cmd->sense_buffer[12] == 0x49))) /* MESSAGE_ERROR  */
    {
      message_error = TRUE;
    }

    if (scb->flags & SCB_MSGOUT_WDTR)
    {
      p->wdtr_pending &= ~mask;
      if (message_error)
      {
        if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
             (p->dev_flags[tindex] & DEVICE_PRINT_WDTR) )
        {
          printk(INFO_LEAD "Device failed to complete Wide Negotiation "
            "processing and\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "returned a sense error code for invalid message, "
            "disabling future\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "Wide negotiation to this device.\n", p->host_no,
            CTL_OF_SCB(scb));
          p->dev_flags[tindex] &= ~DEVICE_PRINT_WDTR;
        }
        p->needwdtr &= ~mask;
        p->needwdtr_copy &= ~mask;
      }
    }
    if (scb->flags & SCB_MSGOUT_SDTR)
    {
      p->sdtr_pending &= ~mask;
      if (message_error)
      {
        if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
             (p->dev_flags[tindex] & DEVICE_PRINT_SDTR) )
        {
          printk(INFO_LEAD "Device failed to complete Sync Negotiation "
            "processing and\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "returned a sense error code for invalid message, "
            "disabling future\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "Sync negotiation to this device.\n", p->host_no,
            CTL_OF_SCB(scb));
          p->dev_flags[tindex] &= ~DEVICE_PRINT_SDTR;
        }
        p->needsdtr &= ~mask;
        p->needsdtr_copy &= ~mask;
      }
    }
  }
  queue_depth = p->dev_temp_queue_depth[tindex];
  if (queue_depth >= p->dev_active_cmds[tindex])
  {
    scbp = scbq_remove_head(&p->delayed_scbs[tindex]);
    if (scbp)
    {
      if (queue_depth == 1)
      {
        /*
         * Give extra preference to untagged devices, such as CD-R devices
         * This makes it more likely that a drive *won't* stuff up while
         * waiting on data at a critical time, such as CD-R writing and
         * audio CD ripping operations.  Should also benefit tape drives.
         */
        scbq_insert_head(&p->waiting_scbs, scbp);
      }
      else
      {
        scbq_insert_tail(&p->waiting_scbs, scbp);
      }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Moving SCB from delayed to waiting queue.\n",
               p->host_no, CTL_OF_SCB(scbp));
#endif
      if (queue_depth > p->dev_active_cmds[tindex])
      {
        scbp = scbq_remove_head(&p->delayed_scbs[tindex]);
        if (scbp)
          scbq_insert_tail(&p->waiting_scbs, scbp);
      }
    }
  }
  if ( !(scb->tag_action) && (p->tagenable & (1<<tindex)) )
  {
    p->dev_temp_queue_depth[tindex] = p->dev_max_queue_depth[tindex];
  }
  p->dev_active_cmds[tindex]--;
  p->activescbs--;

  /*
   * If this was an untagged I/O, unbusy the target so the sequencer won't
   * mistake things later
   */
  if (aic7xxx_index_busy_target(p, scb->hscb->target_channel_lun, FALSE) ==
      scb->hscb->tag)
  {
    aic7xxx_index_busy_target(p, scb->hscb->target_channel_lun, TRUE);
  }

  {
    int actual;

    /*
     * XXX: we should actually know how much actually transferred
     * XXX: for each command, but apparently that's too difficult.
     * 
     * We set a lower limit of 512 bytes on the transfer length.  We
     * ignore anything less than this because we don't have a real
     * reason to count it.  Read/Writes to tapes are usually about 20K
     * and disks are a minimum of 512 bytes unless you want to count
     * non-read/write commands (such as TEST_UNIT_READY) which we don't
     */
    actual = scb->sg_length;
    if ((actual >= 512) && (((cmd->result >> 16) & 0xf) == DID_OK))
    {
      struct aic7xxx_xferstats *sp;
#ifdef AIC7XXX_PROC_STATS
      long *ptr;
      int x;
#endif /* AIC7XXX_PROC_STATS */

      sp = &p->stats[TARGET_INDEX(cmd)][cmd->lun & 0x7];
      sp->xfers++;
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if ( (sp->xfers > 16) && (aic7xxx_verbose > 0xffff) )
        aic7xxx_verbose &= 0xffff;
#endif

      /*
       * For block devices, cmd->request.cmd is always == either READ or
       * WRITE.  For character devices, this isn't always set properly, so
       * we check data_cmnd[0].  This catches the conditions for st.c, but
       * I'm still not sure if request.cmd is valid for sg devices.
       */
      if ( (cmd->request.cmd == WRITE) || (cmd->data_cmnd[0] == WRITE_6) ||
           (cmd->data_cmnd[0] == WRITE_FILEMARKS) )
      {
        sp->w_total++;
        sp->w_total512 += (actual >> 9);
#ifdef AIC7XXX_PROC_STATS
        ptr = sp->w_bins;
#endif /* AIC7XXX_PROC_STATS */
      }
      else
      {
        sp->r_total++;
        sp->r_total512 += (actual >> 9);
#ifdef AIC7XXX_PROC_STATS
        ptr = sp->r_bins;
#endif /* AIC7XXX_PROC_STATS */
      }
#ifdef AIC7XXX_PROC_STATS
      for (x = 9; x <= 17; x++)
      {
        if (actual < (1 << x))
        {
          ptr[x - 9]++;
          break;
        }
      }
      if (x > 17)
      {
        ptr[x - 9]++;
      }
#endif /* AIC7XXX_PROC_STATS */
    }
  }
  aic7xxx_free_scb(p, scb);
  aic7xxx_queue_cmd_complete(p, cmd);

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_done_queue
 *
 * Description:
 *   Calls the aic7xxx_done() for the Scsi_Cmnd of each scb in the
 *   aborted list, and adds each scb to the free list.  If complete
 *   is TRUE, we also process the commands complete list.
 *-F*************************************************************************/
static void
aic7xxx_run_done_queue(struct aic7xxx_host *p, /*complete*/ int complete)
{
  struct aic7xxx_scb *scb;
  int i, found = 0;

  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scb = p->scb_data->scb_array[i];
    if (scb->flags & SCB_QUEUED_FOR_DONE)
    {
      if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
        printk(INFO_LEAD "Aborting scb %d\n",
             p->host_no, CTL_OF_SCB(scb), scb->hscb->tag);
      found++;
      aic7xxx_done(p, scb);
    }
  }
  if (aic7xxx_verbose & (VERBOSE_ABORT_RETURN | VERBOSE_RESET_RETURN))
  {
    printk(INFO_LEAD "%d commands found and queued for "
        "completion.\n", p->host_no, -1, -1, -1, found);
  }
  if (complete)
  {
    aic7xxx_done_cmds_complete(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort_waiting_scb
 *
 * Description:
 *   Manipulate the waiting for selection list and return the
 *   scb that follows the one that we remove.
 *-F*************************************************************************/
static unsigned char
aic7xxx_abort_waiting_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb,
    unsigned char scbpos, unsigned char prev)
{
  unsigned char curscb, next;

  /*
   * Select the SCB we want to abort and pull the next pointer out of it.
   */
  curscb = aic_inb(p, SCBPTR);
  aic_outb(p, scbpos, SCBPTR);
  next = aic_inb(p, SCB_NEXT);

  aic7xxx_add_curscb_to_free_list(p);

  /*
   * Update the waiting list
   */
  if (prev == SCB_LIST_NULL)
  {
    /*
     * First in the list
     */
    aic_outb(p, next, WAITING_SCBH);
  }
  else
  {
    /*
     * Select the scb that pointed to us and update its next pointer.
     */
    aic_outb(p, prev, SCBPTR);
    aic_outb(p, next, SCB_NEXT);
  }
  /*
   * Point us back at the original scb position and inform the SCSI
   * system that the command has been aborted.
   */
  aic_outb(p, curscb, SCBPTR);
  return (next);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_search_qinfifo
 *
 * Description:
 *   Search the queue-in FIFO for matching SCBs and conditionally
 *   requeue.  Returns the number of matching SCBs.
 *-F*************************************************************************/
static int
aic7xxx_search_qinfifo(struct aic7xxx_host *p, int target, int channel,
    int lun, unsigned char tag, int flags, int requeue,
    volatile scb_queue_type *queue)
{
  int      found;
  unsigned char qinpos, qintail;
  struct aic7xxx_scb *scbp;

  found = 0;
  qinpos = aic_inb(p, QINPOS);
  qintail = p->qinfifonext;

  p->qinfifonext = qinpos;

  while (qinpos != qintail)
  {
    scbp = p->scb_data->scb_array[p->qinfifo[qinpos++]];
    if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
    {
       /*
        * We found an scb that needs to be removed.
        */
       if (requeue && (queue != NULL))
       {
         if (scbp->flags & SCB_WAITINGQ)
         {
           scbq_remove(queue, scbp);
           scbq_remove(&p->waiting_scbs, scbp);
           scbq_remove(&p->delayed_scbs[TARGET_INDEX(scbp->cmd)], scbp);
           p->dev_active_cmds[TARGET_INDEX(scbp->cmd)]++;
           p->activescbs++;
         }
         scbq_insert_tail(queue, scbp);
         p->dev_active_cmds[TARGET_INDEX(scbp->cmd)]--;
         p->activescbs--;
         scbp->flags |= SCB_WAITINGQ;
         if ( !(scbp->tag_action & TAG_ENB) )
         {
           aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
             TRUE);
         }
       }
       else if (requeue)
       {
         p->qinfifo[p->qinfifonext++] = scbp->hscb->tag;
       }
       else
       {
        /*
         * Preserve any SCB_RECOVERY_SCB flags on this scb then set the
         * flags we were called with, presumeably so aic7xxx_run_done_queue
         * can find this scb
         */
         scbp->flags = flags | (scbp->flags & SCB_RECOVERY_SCB);
         if (aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
                                       FALSE) == scbp->hscb->tag)
         {
           aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
             TRUE);
         }
       }
       found++;
    }
    else
    {
      p->qinfifo[p->qinfifonext++] = scbp->hscb->tag;
    }
  }
  /*
   * Now that we've done the work, clear out any left over commands in the
   * qinfifo and update the KERNEL_QINPOS down on the card.
   *
   *  NOTE: This routine expect the sequencer to already be paused when
   *        it is run....make sure it's that way!
   */
  qinpos = p->qinfifonext;
  while(qinpos != qintail)
  {
    p->qinfifo[qinpos++] = SCB_LIST_NULL;
  }
  if (p->features & AHC_QUEUE_REGS)
    aic_outb(p, p->qinfifonext, HNSCB_QOFF);
  else
    aic_outb(p, p->qinfifonext, KERNEL_QINPOS);

  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_scb_on_qoutfifo
 *
 * Description:
 *   Is the scb that was passed to us currently on the qoutfifo?
 *-F*************************************************************************/
static int
aic7xxx_scb_on_qoutfifo(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int i=0;

  while(p->qoutfifo[(p->qoutfifonext + i) & 0xff ] != SCB_LIST_NULL)
  {
    if(p->qoutfifo[(p->qoutfifonext + i) & 0xff ] == scb->hscb->tag)
      return TRUE;
    else
      i++;
  }
  return FALSE;
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_device
 *
 * Description:
 *   The device at the given target/channel has been reset.  Abort
 *   all active and queued scbs for that target/channel.  This function
 *   need not worry about linked next pointers because if was a MSG_ABORT_TAG
 *   then we had a tagged command (no linked next), if it was MSG_ABORT or
 *   MSG_BUS_DEV_RESET then the device won't know about any commands any more
 *   and no busy commands will exist, and if it was a bus reset, then nothing
 *   knows about any linked next commands any more.  In all cases, we don't
 *   need to worry about the linked next or busy scb, we just need to clear
 *   them.
 *-F*************************************************************************/
static void
aic7xxx_reset_device(struct aic7xxx_host *p, int target, int channel,
                     int lun, unsigned char tag)
{
  struct aic7xxx_scb *scbp;
  unsigned char active_scb, tcl;
  int i = 0, j, init_lists = FALSE;

  /*
   * Restore this when we're done
   */
  active_scb = aic_inb(p, SCBPTR);

  if (aic7xxx_verbose & (VERBOSE_RESET_PROCESS | VERBOSE_ABORT_PROCESS))
    printk(INFO_LEAD "Reset device, active_scb %d\n",
         p->host_no, channel, target, lun, active_scb);
  /*
   * Deal with the busy target and linked next issues.
   */
  {
    int min_target, max_target;
    struct aic7xxx_scb *scbp, *prev_scbp;

    /* Make all targets 'relative' to bus A. */
    if (target == ALL_TARGETS)
    {
      switch (channel)
      {
        case 0:
                 min_target = 0;
                 max_target = (p->features & AHC_WIDE) ? 15 : 7;
                 break;
        case 1:
                 min_target = 8;
                 max_target = 15;
                 break;
        case ALL_CHANNELS:
        default:
                 min_target = 0;
                 max_target = (p->features & (AHC_TWIN|AHC_WIDE)) ? 15 : 7;
                 break;
      }
    }
    else
    { 
      min_target = target | (channel << 3);
      max_target = min_target;
    }


    for (i = min_target; i <= max_target; i++)
    {
      if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
        printk(INFO_LEAD "Cleaning up status information "
          "and delayed_scbs.\n", p->host_no, channel, i, lun);
      if ( !(p->dev_flags[i] & DEVICE_TAGGED_SUCCESS) &&
            (p->dev_active_cmds[i]) &&
            (p->tagenable & (0x01 << i)) )
      {
        printk(INFO_LEAD "Device appears to be choking on tagged commands.\n",
          p->host_no, channel, i, lun);
        printk(INFO_LEAD "Will use untagged I/O instead.\n", p->host_no,
          channel, i, lun);
        p->dev_max_queue_depth[i] = 1;
        p->dev_temp_queue_depth[i] = 1;
        p->tagenable &= ~(0x01 << i);
        p->orderedtag &= ~(0x01 << i);
      }
      p->dev_flags[i] &= ~BUS_DEVICE_RESET_PENDING;
      if ( tag == SCB_LIST_NULL )
      {
        p->dev_flags[i] |= DEVICE_PRINT_WDTR | DEVICE_PRINT_SDTR;
        p->dev_last_reset[i] = jiffies;
        p->dev_last_queue_full_count[i] = 0;
        p->dev_last_queue_full[i] = 0;
        p->dev_temp_queue_depth[i] =
          p->dev_max_queue_depth[i];
  /*
   * In case this isn't a full bus reset, we want to add a 4 second timer in
   * here so that we can delay all re-sent commands for this device for the
   * 4 seconds and then have our timer routine pick them back up.
   */
        del_timer(&p->dev_timer[i]);
        p->dev_timer[i].expires = jiffies + (4 * HZ);
        add_timer(&p->dev_timer[i]);
      }
      for(j=0; j<MAX_LUNS; j++)
      {
        if (channel == 1)
          tcl = ((i << 4) & 0x70) | (channel << 3) | j;
        else
          tcl = (i << 4) | (channel << 3) | j;
        if ( (aic7xxx_index_busy_target(p, tcl, FALSE) == tag) ||
             (tag == SCB_LIST_NULL) )
          aic7xxx_index_busy_target(p, tcl, /* unbusy */ TRUE);
      }
      j = 0; 
      prev_scbp = NULL; 
      scbp = p->delayed_scbs[i].head;
      while ( (scbp != NULL) && (j++ <= (p->scb_data->numscbs + 1)) )
      {
        prev_scbp = scbp;
        scbp = scbp->q_next;
        if ( prev_scbp == scbp )
        {
          if (aic7xxx_verbose & (VERBOSE_ABORT | VERBOSE_RESET))
            printk(WARN_LEAD "Yikes!! scb->q_next == scb "
              "in the delayed_scbs queue!\n", p->host_no, channel, i, lun);
          scbp = NULL;
          prev_scbp->q_next = NULL;
          p->delayed_scbs[i].tail = prev_scbp;
        }
        if (aic7xxx_match_scb(p, prev_scbp, target, channel, lun, tag))
        {
          scbq_remove(&p->delayed_scbs[i], prev_scbp);
          if (prev_scbp->flags & SCB_WAITINGQ)
          {
            p->dev_active_cmds[i]++;
            p->activescbs++;
          }
          prev_scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
          prev_scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
        }
      }
      if ( j > (p->scb_data->maxscbs + 1) )
      {
        if (aic7xxx_verbose & (VERBOSE_ABORT | VERBOSE_RESET))
          printk(WARN_LEAD "Yikes!! There's a loop in the "
            "delayed_scbs queue!\n", p->host_no, channel, i, lun);
        scbq_init(&p->delayed_scbs[i]);
      }
      if ( p->delayed_scbs[i].head == NULL )
        del_timer(&p->dev_timer[i]);
    }
  }

  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning QINFIFO.\n", p->host_no, channel, target, lun );
  aic7xxx_search_qinfifo(p, target, channel, lun, tag,
      SCB_RESET | SCB_QUEUED_FOR_DONE, /* requeue */ FALSE, NULL);

/*
 *  Search the waiting_scbs queue for matches, this catches any SCB_QUEUED
 *  ABORT/RESET commands.
 */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning waiting_scbs.\n", p->host_no, channel,
      target, lun );
  {
    struct aic7xxx_scb *scbp, *prev_scbp;

    j = 0; 
    prev_scbp = NULL; 
    scbp = p->waiting_scbs.head;
    while ( (scbp != NULL) && (j++ <= (p->scb_data->numscbs + 1)) )
    {
      prev_scbp = scbp;
      scbp = scbp->q_next;
      if ( prev_scbp == scbp )
      {
        if (aic7xxx_verbose & (VERBOSE_ABORT | VERBOSE_RESET))
          printk(WARN_LEAD "Yikes!! scb->q_next == scb "
            "in the waiting_scbs queue!\n", p->host_no, CTL_OF_SCB(scbp));
        scbp = NULL;
        prev_scbp->q_next = NULL;
        p->waiting_scbs.tail = prev_scbp;
      }
      if (aic7xxx_match_scb(p, prev_scbp, target, channel, lun, tag))
      {
        scbq_remove(&p->waiting_scbs, prev_scbp);
        if (prev_scbp->flags & SCB_WAITINGQ)
        {
          p->dev_active_cmds[TARGET_INDEX(prev_scbp->cmd)]++;
          p->activescbs++;
        }
        prev_scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
        prev_scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
      }
    }
    if ( j > (p->scb_data->maxscbs + 1) )
    {
      if (aic7xxx_verbose & (VERBOSE_ABORT | VERBOSE_RESET))
        printk(WARN_LEAD "Yikes!! There's a loop in the "
          "waiting_scbs queue!\n", p->host_no, channel, target, lun);
      scbq_init(&p->waiting_scbs);
    }
  }


  /*
   * Search waiting for selection list.
   */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning waiting for selection "
      "list.\n", p->host_no, channel, target, lun);
  {
    unsigned char next, prev, scb_index;

    next = aic_inb(p, WAITING_SCBH);  /* Start at head of list. */
    prev = SCB_LIST_NULL;
    j = 0;
    while ( (next != SCB_LIST_NULL) && (j++ <= (p->scb_data->maxscbs + 1)) )
    {
      aic_outb(p, next, SCBPTR);
      scb_index = aic_inb(p, SCB_TAG);
      if (scb_index >= p->scb_data->numscbs)
      {
       /*
        * No aic7xxx_verbose check here.....we want to see this since it
        * means either the kernel driver or the sequencer screwed things up
        */
        printk(WARN_LEAD "Waiting List inconsistency; SCB index=%d, "
          "numscbs=%d\n", p->host_no, channel, target, lun, scb_index,
          p->scb_data->numscbs);
        next = aic_inb(p, SCB_NEXT);
        aic7xxx_add_curscb_to_free_list(p);
      }
      else
      {
        scbp = p->scb_data->scb_array[scb_index];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          next = aic7xxx_abort_waiting_scb(p, scbp, next, prev);
          if (scbp->flags & SCB_WAITINGQ)
          {
            p->dev_active_cmds[TARGET_INDEX(scbp->cmd)]++;
            p->activescbs++;
          }
          scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
          scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
          if (prev == SCB_LIST_NULL)
          {
            /*
             * This is either the first scb on the waiting list, or we
             * have already yanked the first and haven't left any behind.
             * Either way, we need to turn off the selection hardware if
             * it isn't already off.
             */
            aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
            aic_outb(p, CLRSELTIMEO, CLRSINT1);
          }
        }
        else
        {
          prev = next;
          next = aic_inb(p, SCB_NEXT);
        }
      }
    }
    if ( j > (p->scb_data->maxscbs + 1) )
    {
      printk(WARN_LEAD "Yikes!!  There is a loop in the waiting for "
        "selection list!\n", p->host_no, channel, target, lun);
      init_lists = TRUE;
    }
  }

  /*
   * Go through disconnected list and remove any entries we have queued
   * for completion, zeroing their control byte too.
   */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning disconnected scbs "
      "list.\n", p->host_no, channel, target, lun);
  if (p->features & AHC_PAGESCBS)
  {
    unsigned char next, prev, scb_index;

    next = aic_inb(p, DISCONNECTED_SCBH);
    prev = SCB_LIST_NULL;
    j = 0;
    while ( (next != SCB_LIST_NULL) && (j++ <= (p->scb_data->maxscbs + 1)) )
    {
      aic_outb(p, next, SCBPTR);
      scb_index = aic_inb(p, SCB_TAG);
      if (scb_index > p->scb_data->numscbs)
      {
        printk(WARN_LEAD "Disconnected List inconsistency; SCB index=%d, "
          "numscbs=%d\n", p->host_no, channel, target, lun, scb_index,
          p->scb_data->numscbs);
        next = aic7xxx_rem_scb_from_disc_list(p, next);
      }
      else
      {
        scbp = p->scb_data->scb_array[scb_index];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          next = aic7xxx_rem_scb_from_disc_list(p, next);
          if (scbp->flags & SCB_WAITINGQ)
          {
            p->dev_active_cmds[TARGET_INDEX(scbp->cmd)]++;
            p->activescbs++;
          }
          scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
          scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
          scbp->hscb->control = 0;
        }
        else
        {
          prev = next;
          next = aic_inb(p, SCB_NEXT);
        }
      }
    }
    if ( j > (p->scb_data->maxscbs + 1) )
    {
      printk(WARN_LEAD "Yikes!!  There is a loop in the disconnected list!\n",
        p->host_no, channel, target, lun);
      init_lists = TRUE;
    }
  }

  /*
   * Walk the free list making sure no entries on the free list have
   * a valid SCB_TAG value or SCB_CONTROL byte.
   */
  if (p->features & AHC_PAGESCBS)
  {
    unsigned char next;

    j = 0;
    next = aic_inb(p, FREE_SCBH);
    if ( (next >= p->scb_data->maxhscbs) && (next != SCB_LIST_NULL) )
    {
      printk(WARN_LEAD "Bogus FREE_SCBH!.\n", p->host_no, channel,
        target, lun);
      init_lists = TRUE;
      next = SCB_LIST_NULL;
    }
    while ( (next != SCB_LIST_NULL) && (j++ <= (p->scb_data->maxscbs + 1)) )
    {
      aic_outb(p, next, SCBPTR);
      if (aic_inb(p, SCB_TAG) < p->scb_data->numscbs)
      {
        printk(WARN_LEAD "Free list inconsistency!.\n", p->host_no, channel,
          target, lun);
        init_lists = TRUE;
        next = SCB_LIST_NULL;
      }
      else
      {
        aic_outb(p, SCB_LIST_NULL, SCB_TAG);
        aic_outb(p, 0, SCB_CONTROL);
        next = aic_inb(p, SCB_NEXT);
      }
    }
    if ( j > (p->scb_data->maxscbs + 1) )
    {
      printk(WARN_LEAD "Yikes!!  There is a loop in the free list!\n",
        p->host_no, channel, target, lun);
      init_lists = TRUE;
    }
  }

  /*
   * Go through the hardware SCB array looking for commands that
   * were active but not on any list.
   */
  if (init_lists)
  {
    aic_outb(p, SCB_LIST_NULL, FREE_SCBH);
    aic_outb(p, SCB_LIST_NULL, WAITING_SCBH);
    aic_outb(p, SCB_LIST_NULL, DISCONNECTED_SCBH);
  }
  for (i = p->scb_data->maxhscbs - 1; i >= 0; i--)
  {
    unsigned char scbid;

    aic_outb(p, i, SCBPTR);
    if (init_lists)
    {
      aic_outb(p, SCB_LIST_NULL, SCB_TAG);
      aic_outb(p, SCB_LIST_NULL, SCB_NEXT);
      aic_outb(p, SCB_LIST_NULL, SCB_PREV);
      aic_outb(p, 0, SCB_CONTROL);
      aic7xxx_add_curscb_to_free_list(p);
    }
    else
    {
      scbid = aic_inb(p, SCB_TAG);
      if (scbid < p->scb_data->numscbs)
      {
        scbp = p->scb_data->scb_array[scbid];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          aic_outb(p, 0, SCB_CONTROL);
          aic_outb(p, SCB_LIST_NULL, SCB_TAG);
          aic7xxx_add_curscb_to_free_list(p);
        }
      }
    }
  }

  /*
   * Go through the entire SCB array now and look for commands for
   * for this target that are stillactive.  These are other (most likely
   * tagged) commands that were disconnected when the reset occurred.
   * Any commands we find here we know this about, it wasn't on any queue,
   * it wasn't in the qinfifo, it wasn't in the disconnected or waiting
   * lists, so it really must have been a paged out SCB.  In that case,
   * we shouldn't need to bother with updating any counters, just mark
   * the correct flags and go on.
   */
  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scbp = p->scb_data->scb_array[i];
    if ((scbp->flags & SCB_ACTIVE) &&
        aic7xxx_match_scb(p, scbp, target, channel, lun, tag) &&
        !aic7xxx_scb_on_qoutfifo(p, scbp))
    {
      if (scbp->flags & SCB_WAITINGQ)
      {
        scbq_remove(&p->waiting_scbs, scbp);
        scbq_remove(&p->delayed_scbs[TARGET_INDEX(scbp->cmd)], scbp);
        p->dev_active_cmds[TARGET_INDEX(scbp->cmd)]++;
        p->activescbs++;
      }
      scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
      scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
    }
  }

  aic_outb(p, active_scb, SCBPTR);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_clear_intstat
 *
 * Description:
 *   Clears the interrupt status.
 *-F*************************************************************************/
static void
aic7xxx_clear_intstat(struct aic7xxx_host *p)
{
  /* Clear any interrupt conditions this may have caused. */
  aic_outb(p, CLRSELDO | CLRSELDI | CLRSELINGO, CLRSINT0);
  aic_outb(p, CLRSELTIMEO | CLRATNO | CLRSCSIRSTI | CLRBUSFREE | CLRSCSIPERR |
       CLRPHASECHG | CLRREQINIT, CLRSINT1);
  aic_outb(p, CLRSCSIINT | CLRSEQINT | CLRBRKADRINT | CLRPARERR, CLRINT);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_current_bus
 *
 * Description:
 *   Reset the current SCSI bus.
 *-F*************************************************************************/
static void
aic7xxx_reset_current_bus(struct aic7xxx_host *p)
{

  /* Disable reset interrupts. */
  aic_outb(p, aic_inb(p, SIMODE1) & ~ENSCSIRST, SIMODE1);

  /* Turn off the bus' current operations, after all, we shouldn't have any
   * valid commands left to cause a RSELI and SELO once we've tossed the
   * bus away with this reset, so we might as well shut down the sequencer
   * until the bus is restarted as oppossed to saving the current settings
   * and restoring them (which makes no sense to me). */

  /* Turn on the bus reset. */
  aic_outb(p, aic_inb(p, SCSISEQ) | SCSIRSTO, SCSISEQ);
  while ( (aic_inb(p, SCSISEQ) & SCSIRSTO) == 0)
    mdelay(5);

  mdelay(10);

  /* Turn off the bus reset. */
  aic_outb(p, 0, SCSISEQ);
  mdelay(5);

  aic7xxx_clear_intstat(p);
  /* Re-enable reset interrupts. */
  aic_outb(p, aic_inb(p, SIMODE1) | ENSCSIRST, SIMODE1);

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_channel
 *
 * Description:
 *   Reset the channel.
 *-F*************************************************************************/
static void
aic7xxx_reset_channel(struct aic7xxx_host *p, int channel, int initiate_reset)
{
  unsigned long offset_min, offset_max;
  unsigned char sblkctl;
  int cur_channel;

  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    printk(INFO_LEAD "Reset channel called, %s initiate reset.\n",
      p->host_no, channel, -1, -1, (initiate_reset==TRUE) ? "will" : "won't" );


  if (channel == 1)
  {
    p->needsdtr |= (p->needsdtr_copy & 0xFF00);
    p->sdtr_pending &= 0x00FF;
    offset_min = 8;
    offset_max = 16;
  }
  else
  {
    if (p->features & AHC_WIDE)
    {
      p->needsdtr = p->needsdtr_copy;
      p->needwdtr = p->needwdtr_copy;
      p->sdtr_pending = 0x0;
      p->wdtr_pending = 0x0;
      offset_min = 0;
      offset_max = 16;
    }
    else
    {
      /* Channel A */
      p->needsdtr |= (p->needsdtr_copy & 0x00FF);
      p->sdtr_pending &= 0xFF00;
      offset_min = 0;
      offset_max = 8;
    }
  }

  while (offset_min < offset_max)
  {
    /*
     * Revert to async/narrow transfers until we renegotiate.
     */
    aic_outb(p, 0, TARG_SCSIRATE + offset_min);
    if (p->features & AHC_ULTRA2)
    {
      aic_outb(p, 0, TARG_OFFSET + offset_min);
    }
    offset_min++;
  }

  /*
   * Reset the bus and unpause/restart the controller
   */
  sblkctl = aic_inb(p, SBLKCTL);
  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
    cur_channel = (sblkctl & SELBUSB) >> 3;
  else
    cur_channel = 0;
  if ( (cur_channel != channel) && (p->features & AHC_TWIN) )
  {
    /*
     * Case 1: Command for another bus is active
     */
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Stealthily resetting idle channel.\n", p->host_no,
        channel, -1, -1);
    /*
     * Stealthily reset the other bus without upsetting the current bus.
     */
    aic_outb(p, sblkctl ^ SELBUSB, SBLKCTL);
    aic_outb(p, aic_inb(p, SIMODE1) & ~ENBUSFREE, SIMODE1);
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
    }
    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP), SCSISEQ);
    aic7xxx_clear_intstat(p);
    aic_outb(p, sblkctl, SBLKCTL);
  }
  else
  {
    /*
     * Case 2: A command from this bus is active or we're idle.
     */
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Resetting currently active channel.\n", p->host_no,
        channel, -1, -1);
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENBUSFREE|ENREQINIT),
      SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    p->msg_type = MSG_TYPE_NONE;
    p->msg_len = 0;
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
    }
    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP), SCSISEQ);
    aic7xxx_clear_intstat(p);
  }
  if (aic7xxx_verbose & VERBOSE_RESET_RETURN)
    printk(INFO_LEAD "Channel reset\n", p->host_no, channel, -1, -1);
  /*
   * Clean up all the state information for the pending transactions
   * on this bus.
   */
  aic7xxx_reset_device(p, ALL_TARGETS, channel, ALL_LUNS, SCB_LIST_NULL);

  /*
   * Convince Mid Level SCSI code to leave us be for a little bit...
   */
  p->last_reset = jiffies;
  p->host->last_reset = (jiffies + (HZ * AIC7XXX_RESET_DELAY));

  if ( !(p->features & AHC_TWIN) )
  {
    restart_sequencer(p);
  }

  return;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_waiting_queues
 *
 * Description:
 *   Scan the awaiting_scbs queue downloading and starting as many
 *   scbs as we can.
 *-F*************************************************************************/
static void
aic7xxx_run_waiting_queues(struct aic7xxx_host *p)
{
  struct aic7xxx_scb *scb;
  int tindex;
  int sent;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags = 0;
#endif


  if (p->waiting_scbs.head == NULL)
    return;

  sent = 0;

  /*
   * First handle SCBs that are waiting but have been assigned a slot.
   */
  DRIVER_LOCK
  while ((scb = scbq_remove_head(&p->waiting_scbs)) != NULL)
  {
    tindex = TARGET_INDEX(scb->cmd);
    if ( !scb->tag_action && (p->tagenable & (1<<tindex)) )
    {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Reducing Queue depth for untagged command.\n",
               p->host_no, CTL_OF_SCB(scb));
#endif
      p->dev_temp_queue_depth[tindex] = 1;
    }
    if ( (p->dev_active_cmds[tindex] >=
          p->dev_temp_queue_depth[tindex]) ||
         time_after_eq(p->dev_last_reset[tindex], jiffies - 4 * HZ) )
    {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Moving SCB to Delayed Queue.\n",
               p->host_no, CTL_OF_SCB(scb));
#endif
      scbq_insert_tail(&p->delayed_scbs[tindex], scb);
      if ( !timer_pending(&p->dev_timer[tindex]) &&
           !(p->dev_active_cmds[tindex]) )
      {
        p->dev_timer[tindex].expires = p->dev_last_reset[tindex] + (4 * HZ);
        add_timer(&p->dev_timer[tindex]);
      }
    }
    else
    {
        scb->flags &= ~SCB_WAITINGQ;
#ifdef AIC7XXX_VERBOSE_DEBUGGING
        if (aic7xxx_verbose > 0xffff)
          printk(INFO_LEAD "Sending command %d/0x%x to QINFIFO\n", p->host_no,
                 CTL_OF_SCB(scb), scb->hscb->tag, scb->flags);
#endif
        p->dev_active_cmds[tindex]++;
        p->activescbs++;
        if ( !(scb->tag_action) )
        {
          aic7xxx_busy_target(p, scb);
        }
        p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
        sent++;
    }
  }
  if (sent)
  {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (aic7xxx_verbose > 0xffff)
    {
      printk(INFO_LEAD "Sending commands to QINFIFO\n", p->host_no,
             -1, -1, -1);
      if ( (p->isr_count < 16) && (aic7xxx_panic_on_abort) &&
           (p->flags & AHC_PAGESCBS) )
        aic7xxx_check_scbs(p, "While sending commands to QINFIFO");
    }
#endif
    if (p->features & AHC_QUEUE_REGS)
      aic_outb(p, p->qinfifonext, HNSCB_QOFF);
    else
    {
      pause_sequencer(p);
      aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
      unpause_sequencer(p, FALSE);
    }
    if (p->activescbs > p->max_activescbs)
      p->max_activescbs = p->activescbs;
  }
  DRIVER_UNLOCK
}

#ifdef CONFIG_PCI

#define  DPE 0x80
#define  SSE 0x40
#define  RMA 0x20
#define  RTA 0x10
#define  STA 0x08
#define  DPR 0x01

/*+F*************************************************************************
 * Function:
 *   aic7xxx_pci_intr
 *
 * Description:
 *   Check the scsi card for PCI errors and clear the interrupt
 *
 *   NOTE: If you don't have this function and a 2940 card encounters
 *         a PCI error condition, the machine will end up locked as the
 *         interrupt handler gets slammed with non-stop PCI error interrupts
 *-F*************************************************************************/
static void
aic7xxx_pci_intr(struct aic7xxx_host *p)
{
  unsigned char status1;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
  pci_read_config_byte(p->pdev, PCI_STATUS + 1, &status1);
#else
  pcibios_read_config_byte(p->pci_bus, p->pci_device_fn,
                           PCI_STATUS + 1, &status1);
#endif

  if ( (status1 & DPE) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Data Parity Error during PCI address or PCI write"
      "phase.\n", p->host_no, -1, -1, -1);
  if ( (status1 & SSE) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Signal System Error Detected\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & RMA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Received a PCI Master Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & RTA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Received a PCI Target Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & STA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Signaled a PCI Target Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & DPR) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Data Parity Error has been reported via PCI pin "
      "PERR#\n", p->host_no, -1, -1, -1);
  
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
  pci_write_config_byte(p->pdev, PCI_STATUS + 1, status1);
#else
  pcibios_write_config_byte(p->pci_bus, p->pci_device_fn,
                            PCI_STATUS + 1, status1);
#endif
  if (status1 & (DPR|RMA|RTA))
    aic_outb(p,  CLRPARERR, CLRINT);

  if ( (aic7xxx_panic_on_abort) && (p->spurious_int > 500) )
    aic7xxx_panic_abort(p, NULL);

}
#endif /* CONFIG_PCI */

/*+F*************************************************************************
 * Function:
 *   aic7xxx_timer
 *
 * Description:
 *   Take expired extries off of delayed queues and place on waiting queue
 *   then run waiting queue to start commands.
 ***************************************************************************/
static void
aic7xxx_timer(struct aic7xxx_host *p)
{
  int i, j;
  unsigned long cpu_flags = 0;
  struct aic7xxx_scb *scb;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)   
  DRIVER_LOCK
#else
  spin_lock_irqsave(&io_request_lock, cpu_flags);
#endif
  for(i=0; i<MAX_TARGETS; i++)
  {
    if ( timer_pending(&p->dev_timer[i]) && 
         time_before_eq(p->dev_timer[i].expires, jiffies) )
    {
      del_timer(&p->dev_timer[i]);
      p->dev_temp_queue_depth[i] =  p->dev_max_queue_depth[i];
      j = 0;
      while ( ((scb = scbq_remove_head(&p->delayed_scbs[i])) != NULL) &&
              (j++ < p->scb_data->numscbs) )
      {
        scbq_insert_tail(&p->waiting_scbs, scb);
      }
      if (j == p->scb_data->numscbs)
      {
        printk(INFO_LEAD "timer: Yikes, loop in delayed_scbs list.\n",
          p->host_no, 0, i, -1);
        scbq_init(&p->delayed_scbs[i]);
        scbq_init(&p->waiting_scbs);
        /*
         * Well, things are screwed now, wait for a reset to clean the junk
         * out.
         */
      }
    }
  }
  aic7xxx_run_waiting_queues(p);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)   
  DRIVER_UNLOCK
#else
  spin_unlock_irqrestore(&io_request_lock, cpu_flags);
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_sdtr
 *
 * Description:
 *   Constucts a synchronous data transfer message in the message
 *   buffer on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_sdtr(struct aic7xxx_host *p, unsigned char period,
        unsigned char offset)
{
  p->msg_buf[p->msg_index++] = MSG_EXTENDED;
  p->msg_buf[p->msg_index++] = MSG_EXT_SDTR_LEN;
  p->msg_buf[p->msg_index++] = MSG_EXT_SDTR;
  p->msg_buf[p->msg_index++] = period;
  p->msg_buf[p->msg_index++] = offset;
  p->msg_len += 5;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_wdtr
 *
 * Description:
 *   Constucts a wide data transfer message in the message buffer
 *   on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_wdtr(struct aic7xxx_host *p, unsigned char bus_width)
{
  p->msg_buf[p->msg_index++] = MSG_EXTENDED;
  p->msg_buf[p->msg_index++] = MSG_EXT_WDTR_LEN;
  p->msg_buf[p->msg_index++] = MSG_EXT_WDTR;
  p->msg_buf[p->msg_index++] = bus_width;
  p->msg_len += 4;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_calc_residual
 *
 * Description:
 *   Calculate the residual data not yet transferred.
 *-F*************************************************************************/
static void
aic7xxx_calculate_residual (struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  struct aic7xxx_hwscb *hscb;
  Scsi_Cmnd *cmd;
  int actual, i;

  cmd = scb->cmd;
  hscb = scb->hscb;

  /*
   *  Don't destroy valid residual information with
   *  residual coming from a check sense operation.
   */
  if (((scb->hscb->control & DISCONNECTED) == 0) &&
      (scb->flags & SCB_SENSE) == 0)
  {
    /*
     *  We had an underflow. At this time, there's only
     *  one other driver that bothers to check for this,
     *  and cmd->underflow seems to be set rather half-
     *  heartedly in the higher-level SCSI code.
     */
    actual = scb->sg_length;
    for (i=1; i < hscb->residual_SG_segment_count; i++)
    {
      actual -= scb->sg_list[scb->sg_count - i].length;
    }
    actual -= (hscb->residual_data_count[2] << 16) |
              (hscb->residual_data_count[1] <<  8) |
              hscb->residual_data_count[0];

    if (actual < cmd->underflow)
    {
      if (aic7xxx_verbose & VERBOSE_MINOR_ERROR)
        printk(INFO_LEAD "Underflow - Wanted %u, %s %u, residual SG "
          "count %d.\n", p->host_no, CTL_OF_SCB(scb), cmd->underflow,
          (cmd->request.cmd == WRITE) ? "wrote" : "read", actual,
          hscb->residual_SG_segment_count);
      aic7xxx_error(cmd) = DID_RETRY_COMMAND;
      aic7xxx_status(cmd) = hscb->target_status;
    }
  }

  /*
   * Clean out the residual information in the SCB for the
   * next consumer.
   */
  hscb->residual_data_count[2] = 0;
  hscb->residual_data_count[1] = 0;
  hscb->residual_data_count[0] = 0;
  hscb->residual_SG_segment_count = 0;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_device_reset
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_device_reset(struct aic7xxx_host *p, int target, int channel)
{
  unsigned short targ_mask;
  unsigned char tindex = target;

  tindex |= ((channel & 0x01) << 3);

  targ_mask = (0x01 << tindex);
  /*
   * Go back to async/narrow transfers and renegotiate.
   */
  p->needsdtr |= (p->needsdtr_copy & targ_mask);
  p->needwdtr |= (p->needwdtr_copy & targ_mask);
  p->sdtr_pending &= ~targ_mask;
  p->wdtr_pending &= ~targ_mask;
  aic_outb(p, 0, TARG_SCSIRATE + tindex);
  if (p->features & AHC_ULTRA2)
    aic_outb(p, 0, TARG_OFFSET + tindex);
  aic7xxx_reset_device(p, target, channel, ALL_LUNS, SCB_LIST_NULL);
  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    printk(INFO_LEAD "Bus Device Reset delivered.\n", p->host_no, channel,
      target, -1);
  aic7xxx_run_done_queue(p, /*complete*/ FALSE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_seqint
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_seqint(struct aic7xxx_host *p, unsigned char intstat)
{
  struct aic7xxx_scb *scb;
  unsigned short target_mask;
  unsigned char target, lun, tindex;
  unsigned char queue_flag = FALSE;
  char channel;

  target = ((aic_inb(p, SAVED_TCL) >> 4) & 0x0f);
  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
    channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
  else
    channel = 0;
  tindex = target + (channel << 3);
  lun = aic_inb(p, SAVED_TCL) & 0x07;
  target_mask = (0x01 << tindex);

  /*
   * Go ahead and clear the SEQINT now, that avoids any interrupt race
   * conditions later on in case we enable some other interrupt.
   */
  aic_outb(p, CLRSEQINT, CLRINT);
  switch (intstat & SEQINT_MASK)
  {
    case NO_MATCH:
      {
        aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP),
                 SCSISEQ);
        printk(WARN_LEAD "No active SCB for reconnecting target - Issuing "
               "BUS DEVICE RESET.\n", p->host_no, channel, target, lun);
        printk(WARN_LEAD "      SAVED_TCL=0x%x, ARG_1=0x%x, SEQADDR=0x%x\n",
               p->host_no, channel, target, lun,
               aic_inb(p, SAVED_TCL), aic_inb(p, ARG_1),
               (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
      }
      break;

    case SEND_REJECT:
      {
        if (aic7xxx_verbose & VERBOSE_MINOR_ERROR)
          printk(INFO_LEAD "Rejecting unknown message (0x%x) received from "
            "target, SEQ_FLAGS=0x%x\n", p->host_no, channel, target, lun,
            aic_inb(p, ACCUM), aic_inb(p, SEQ_FLAGS));
      }
      break;

    case NO_IDENT:
      {
        /*
         * The reconnecting target either did not send an identify
         * message, or did, but we didn't find an SCB to match and
         * before it could respond to our ATN/abort, it hit a dataphase.
         * The only safe thing to do is to blow it away with a bus
         * reset.
         */
        if (aic7xxx_verbose & (VERBOSE_SEQINT | VERBOSE_RESET_MID))
          printk(INFO_LEAD "Target did not send an IDENTIFY message; "
            "LASTPHASE 0x%x, SAVED_TCL 0x%x\n", p->host_no, channel, target,
            lun, aic_inb(p, LASTPHASE), aic_inb(p, SAVED_TCL));

        aic7xxx_reset_channel(p, channel, /*initiate reset*/ TRUE);
        aic7xxx_run_done_queue(p, FALSE);

      }
      break;

    case BAD_PHASE:
      if (aic_inb(p, LASTPHASE) == P_BUSFREE)
      {
        if (aic7xxx_verbose & VERBOSE_SEQINT)
          printk(INFO_LEAD "Missed busfree.\n", p->host_no, channel,
            target, lun);
        restart_sequencer(p);
      }
      else
      {
        if (aic7xxx_verbose & VERBOSE_SEQINT)
          printk(INFO_LEAD "Unknown scsi bus phase, continuing\n", p->host_no,
            channel, target, lun);
      }
      break;

    case EXTENDED_MSG:
      {
        p->msg_type = MSG_TYPE_INITIATOR_MSGIN;
        p->msg_len = 0;
        p->msg_index = 0;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
        if (aic7xxx_verbose > 0xffff)
          printk(INFO_LEAD "Enabling REQINITs for MSG_IN\n", p->host_no,
                 channel, target, lun);
#endif

       /*      
        * To actually receive the message, simply turn on
        * REQINIT interrupts and let our interrupt handler
        * do the rest (REQINIT should already be true).
        */
        p->flags |= AHC_HANDLING_REQINITS;
        aic_outb(p, aic_inb(p, SIMODE1) | ENREQINIT, SIMODE1);

       /*
        * We don't want the sequencer unpaused yet so we return early
        */
        return;
      }

    case REJECT_MSG:
      {
        /*
         * What we care about here is if we had an outstanding SDTR
         * or WDTR message for this target. If we did, this is a
         * signal that the target is refusing negotiation.
         */
        unsigned char scb_index;
        unsigned char last_msg;

        scb_index = aic_inb(p, SCB_TAG);
        scb = p->scb_data->scb_array[scb_index];
        last_msg = aic_inb(p, LAST_MSG);

        if ( (last_msg == MSG_IDENTIFYFLAG) &&
             (scb->tag_action) &&
            !(scb->flags & SCB_MSGOUT_BITS) )
        {
          if ((scb->tag_action == MSG_ORDERED_Q_TAG) &&
              (p->dev_flags[tindex] & DEVICE_TAGGED_SUCCESS))
          {
            /*
             * OK...the device seems able to accept tagged commands, but
             * not ordered tag commands, only simple tag commands.  So, we
             * disable ordered tag commands and go on with life just like
             * normal.
             */
            p->orderedtag &= ~target_mask;
            scb->tag_action = MSG_SIMPLE_Q_TAG;
            scb->hscb->control &= ~SCB_TAG_TYPE;
            scb->hscb->control |= MSG_SIMPLE_Q_TAG;
            aic_outb(p, scb->hscb->control, SCB_CONTROL);
            /*
             * OK..we set the tag type to simple tag command, now we re-assert
             * ATNO and hope this will take us into the identify phase again
             * so we can resend the tag type and info to the device.
             */
            aic_outb(p, MSG_IDENTIFYFLAG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
          }
          else if ( (scb->tag_action == MSG_SIMPLE_Q_TAG) &&
                   !(p->dev_flags[tindex] & DEVICE_TAGGED_SUCCESS) )
          {
            unsigned char i, reset = 0;
            struct aic7xxx_scb *scbp;
            int old_verbose;
            /*
             * Hmmmm....the device is flaking out on tagged commands.  The
             * bad thing is that we already have tagged commands enabled in
             * the device struct in the mid level code.  We also have a queue
             * set according to the tagged queue depth.  Gonna have to live
             * with it by controlling our queue depth internally and making
             * sure we don't set the tagged command flag any more.
             */
            p->tagenable &= ~target_mask;
            p->orderedtag &= ~target_mask;
            p->dev_max_queue_depth[tindex] =
               p->dev_temp_queue_depth[tindex] = 1;
            /*
             * We set this command up as a bus device reset.  However, we have
             * to clear the tag type as it's causing us problems.  We shouldnt
             * have to worry about any other commands being active, since if
             * the device is refusing tagged commands, this should be the
             * first tagged command sent to the device, however, we do have
             * to worry about any other tagged commands that may already be
             * in the qinfifo.  The easiest way to do this, is to issue a BDR,
             * send all the commands back to the mid level code, then let them
             * come back and get rebuilt as untagged commands.
             */
            scb->tag_action = 0;
            scb->hscb->control &= ~(TAG_ENB | SCB_TAG_TYPE);
            aic_outb(p,  scb->hscb->control, SCB_CONTROL);

            old_verbose = aic7xxx_verbose;
            aic7xxx_verbose &= ~(VERBOSE_RESET|VERBOSE_ABORT);
            for (i=0; i!=p->scb_data->numscbs; i++)
            {
              scbp = p->scb_data->scb_array[i];
              if ((scbp->flags & SCB_ACTIVE) && (scbp != scb))
              {
                if (aic7xxx_match_scb(p, scbp, target, channel, lun, i))
                {
                  aic7xxx_reset_device(p, target, channel, lun, i);
                  reset++;
                }
                aic7xxx_run_done_queue(p, FALSE);
              }
            }
            aic7xxx_verbose = old_verbose;
            /*
             * Wait until after the for loop to set the busy index since
             * aic7xxx_reset_device will clear the busy index during its
             * operation.
             */
            aic7xxx_busy_target(p, scb);
            printk(INFO_LEAD "Device is refusing tagged commands, using "
              "untagged I/O.\n", p->host_no, channel, target, lun);
            aic_outb(p, MSG_IDENTIFYFLAG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
          }
        }
        else if (scb->flags & SCB_MSGOUT_WDTR)
        {
          /*
           * note 8bit xfers and clear flag
           */
          p->needwdtr &= ~target_mask;
          p->needwdtr_copy &= ~target_mask;
          p->wdtr_pending &= ~target_mask;
          scb->flags &= ~SCB_MSGOUT_BITS;
          aic7xxx_set_width(p, target, channel, lun, MSG_EXT_WDTR_BUS_8_BIT,
            (AHC_TRANS_ACTIVE|AHC_TRANS_GOAL|AHC_TRANS_CUR));
          aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0,
                               AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE);
          if ( (p->needsdtr_copy & target_mask) &&
               !(p->sdtr_pending & target_mask) )
          {
            p->sdtr_pending |= target_mask;
            scb->flags |= SCB_MSGOUT_SDTR;
            aic_outb(p, HOST_MSG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
          }
        }
        else if (scb->flags & SCB_MSGOUT_SDTR)
        {
         /*
          * note asynch xfers and clear flag
          */
          p->needsdtr &= ~target_mask;
          p->needsdtr_copy &= ~target_mask;
          p->sdtr_pending &= ~target_mask;
          scb->flags &= ~SCB_MSGOUT_SDTR;
          aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0,
            (AHC_TRANS_CUR|AHC_TRANS_ACTIVE|AHC_TRANS_GOAL));
        }
        else if (aic7xxx_verbose & VERBOSE_SEQINT)
        {
          /*
           * Otherwise, we ignore it.
           */
          printk(INFO_LEAD "Received MESSAGE_REJECT for unknown cause.  "
            "Ignoring.\n", p->host_no, channel, target, lun);
        }
      }
      break;

    case BAD_STATUS:
      {
        unsigned char scb_index;
        struct aic7xxx_hwscb *hscb;
        Scsi_Cmnd *cmd;

        /* The sequencer will notify us when a command has an error that
         * would be of interest to the kernel.  This allows us to leave
         * the sequencer running in the common case of command completes
         * without error.  The sequencer will have DMA'd the SCB back
         * up to us, so we can reference the drivers SCB array.
         *
         * Set the default return value to 0 indicating not to send
         * sense.  The sense code will change this if needed and this
         * reduces code duplication.
         */
        aic_outb(p, 0, RETURN_1);
        scb_index = aic_inb(p, SCB_TAG);
        if (scb_index > p->scb_data->numscbs)
        {
          printk(WARN_LEAD "Invalid SCB during SEQINT 0x%02x, SCB_TAG %d.\n",
            p->host_no, channel, target, lun, intstat, scb_index);
          break;
        }
        scb = p->scb_data->scb_array[scb_index];
        hscb = scb->hscb;

        if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
        {
          printk(WARN_LEAD "Invalid SCB during SEQINT 0x%x, scb %d, flags 0x%x,"
            " cmd 0x%lx.\n", p->host_no, channel, target, lun, intstat,
            scb_index, scb->flags, (unsigned long) scb->cmd);
        }
        else
        {
          cmd = scb->cmd;
          hscb->target_status = aic_inb(p, SCB_TARGET_STATUS);
          aic7xxx_status(cmd) = hscb->target_status;

          cmd->result = hscb->target_status;

          switch (status_byte(hscb->target_status))
          {
            case GOOD:
              if (aic7xxx_verbose & VERBOSE_SEQINT)
                printk(INFO_LEAD "Interrupted for status of GOOD???\n",
                  p->host_no, CTL_OF_SCB(scb));
              break;

            case COMMAND_TERMINATED:
            case CHECK_CONDITION:
              if ( !(scb->flags & SCB_SENSE) )
              {
                /*
                 * XXX - How do we save the residual (if there is one).
                 */
                if ( hscb->residual_SG_segment_count != 0 )
                  aic7xxx_calculate_residual(p, scb);

                /*
                   * Send a sense command to the requesting target.
                 * XXX - revisit this and get rid of the memcopys.
                   */
                memcpy(&scb->sense_cmd[0], &generic_sense[0],
                       sizeof(generic_sense));

                scb->sense_cmd[1] = (cmd->lun << 5);
                scb->sense_cmd[4] = sizeof(cmd->sense_buffer);

                scb->sg_list[0].address = 
                  cpu_to_le32(VIRT_TO_BUS(&cmd->sense_buffer[0]));
                scb->sg_list[0].length = 
                  cpu_to_le32(sizeof(cmd->sense_buffer));

                /*
                 * XXX - We should allow disconnection, but can't as it
                 * might allow overlapped tagged commands.
                 */
                  /* hscb->control &= DISCENB; */
                hscb->control = 0;
                hscb->target_status = 0;
                hscb->SG_list_pointer = 
                  cpu_to_le32(VIRT_TO_BUS(&scb->sg_list[0]));
                hscb->data_pointer = scb->sg_list[0].address;
                hscb->data_count = scb->sg_list[0].length;
                hscb->SCSI_cmd_pointer = 
                  cpu_to_le32(VIRT_TO_BUS(&scb->sense_cmd[0]));
                hscb->SCSI_cmd_length = COMMAND_SIZE(scb->sense_cmd[0]);
                hscb->residual_SG_segment_count = 0;
                hscb->residual_data_count[0] = 0;
                hscb->residual_data_count[1] = 0;
                hscb->residual_data_count[2] = 0;

                scb->sg_count = hscb->SG_segment_count = 1;
                scb->sg_length = sizeof(cmd->sense_buffer);
                scb->tag_action = 0;
                /*
                 * This problem could be caused if the target has lost power
                 * or found some other way to loose the negotiation settings,
                 * so if needed, we'll re-negotiate while doing the sense cmd.
                 * However, if this SCB already was attempting to negotiate,
                 * then we assume this isn't the problem and skip this part.
                 */
#ifdef AIC7XXX_FAKE_NEGOTIATION_CMDS
                if ( (scb->cmd->cmnd[0] != TEST_UNIT_READY) &&
                     (p->dev_flags[tindex] & DEVICE_SCANNED) &&
                     !(p->wdtr_pending & target_mask) && 
                     !(p->sdtr_pending & target_mask) )
                {
                  p->needwdtr |= (p->needwdtr_copy & target_mask);
                  p->needsdtr |= (p->needsdtr_copy & target_mask);
                }
                else if ( (scb->cmd == p->dev_wdtr_cmnd[tindex]) ||
                          (scb->cmd == p->dev_sdtr_cmnd[tindex]) )
                {
                  /*
                   * This is already a negotiation command, so we must have
                   * already done either WDTR or SDTR (or maybe both).  So
                   * we simply check sdtr_pending and needsdtr to see if we
                   * should throw out SDTR on this command.
                   *
                   * Note: Don't check the needsdtr_copy here, instead just
                   * check to see if WDTR wiped out our SDTR and set needsdtr.
                   * Even if WDTR did wipe out SDTR and set needsdtr, if
                   * parse_msg() then turned around and started our SDTR
                   * in back to back fasion, then conclusion of that should
                   * have negated any needsdtr setting.  That's why we only
                   * check needsdtr and sdtr_pending.
                   */
                  scb->flags &= ~SCB_MSGOUT_BITS;
                  if ( (scb->cmd == p->dev_wdtr_cmnd[tindex]) &&
                       !(p->sdtr_pending & target_mask) &&
                       (p->needsdtr & target_mask) )
                  {
                    p->sdtr_pending |= target_mask;
                    hscb->control |= MK_MESSAGE;
                    scb->flags |= SCB_MSGOUT_SDTR;
                  }

                  /*
                   * This is the important part though.  We are getting sense
                   * info back from this device.  It's going into a fake
                   * command.  We need to put that into the real command
                   * instead so that the mid level SCSI code can act upon it.
                   * So, when we set up these fake commands, the next pointer
                   * is used to point to the real command.  Use that to change
                   * the address of our sense_buffer[] to the real command.
                   * However, don't do this if the real command is also a
                   * TEST_UNIT_READY as it will most likely pull down its own
                   * SENSE information anyway.
                   */
                  if (cmd->next->cmnd[0] != TEST_UNIT_READY)
                  {
                    scb->sg_list[0].address = 
                      cpu_to_le32(VIRT_TO_BUS(&cmd->next->sense_buffer[0]));
                    hscb->data_pointer = scb->sg_list[0].address;
                  }
                }
#else
                if (  (scb->cmd->cmnd[0] != TEST_UNIT_READY) &&
                     !(scb->flags & SCB_MSGOUT_BITS) && 
                      (scb->cmd->lun == 0) &&
                      (p->dev_flags[TARGET_INDEX(scb->cmd)] & DEVICE_SCANNED) )
                {
                  if ( (p->needwdtr_copy & target_mask) &&
                      !(p->wdtr_pending & target_mask) &&
                      !(p->sdtr_pending & target_mask) )
                  {
                    p->needwdtr |= target_mask;
                    p->wdtr_pending |= target_mask;
                    hscb->control |= MK_MESSAGE;
                    scb->flags |= SCB_MSGOUT_WDTR;
                  }
                  if ( p->needsdtr_copy & target_mask )
                  {
                    p->needsdtr |= target_mask;
                    if ( !(p->wdtr_pending & target_mask) &&
                         !(p->sdtr_pending & target_mask) )
                    {
                      p->sdtr_pending |= target_mask;
                      hscb->control |= MK_MESSAGE;
                      scb->flags |= SCB_MSGOUT_SDTR;
                    }
                  }
                }
                else
                  scb->flags &= ~SCB_MSGOUT_BITS;
#endif /* AIC7XXX_FAKE_NEGOTIATION_CMDS */
                scb->flags |= SCB_SENSE;
                /*
                 * Ensure the target is busy since this will be an
                 * an untagged request.
                 */
#ifdef AIC7XXX_VERBOSE_DEBUGGING
                if (aic7xxx_verbose > 0xffff)
                {
                  if (scb->flags & SCB_MSGOUT_BITS)
                    printk(INFO_LEAD "Requesting SENSE with %s\n", p->host_no,
                           CTL_OF_SCB(scb), (scb->flags & SCB_MSGOUT_SDTR) ?
                           "SDTR" : "WDTR");
                  else
                    printk(INFO_LEAD "Requesting SENSE, no MSG\n", p->host_no,
                           CTL_OF_SCB(scb));
                }
#endif
                aic7xxx_busy_target(p, scb);
                aic_outb(p, SEND_SENSE, RETURN_1);
                aic7xxx_error(cmd) = DID_OK;
                break;
              }  /* first time sense, no errors */
              aic7xxx_error(cmd) = DID_OK;
              scb->flags &= ~SCB_SENSE;
              break;

            case QUEUE_FULL:
              queue_flag = TRUE;    /* Mark that this is a QUEUE_FULL and */
            case BUSY:              /* drop through to here */
            {
              struct aic7xxx_scb *next_scbp, *prev_scbp;
              unsigned char active_hscb, next_hscb, prev_hscb, scb_index;
              /*
               * We have to look three places for queued commands:
               *  1: QINFIFO
               *  2: p->waiting_scbs queue
               *  3: WAITING_SCBS list on card (for commands that are started
               *     but haven't yet made it to the device)
               */
              aic7xxx_search_qinfifo(p, target, channel, lun,
                SCB_LIST_NULL, 0, TRUE,
                &p->delayed_scbs[tindex]);
              next_scbp = p->waiting_scbs.head;
              while ( next_scbp != NULL )
              {
                prev_scbp = next_scbp;
                next_scbp = next_scbp->q_next;
                if ( aic7xxx_match_scb(p, prev_scbp, target, channel, lun,
                     SCB_LIST_NULL) )
                {
                  scbq_remove(&p->waiting_scbs, prev_scbp);
                  scbq_insert_tail(&p->delayed_scbs[tindex],
                    prev_scbp);
                }
              }
              next_scbp = NULL;
              active_hscb = aic_inb(p, SCBPTR);
              prev_hscb = next_hscb = scb_index = SCB_LIST_NULL;
              next_hscb = aic_inb(p, WAITING_SCBH);
              while (next_hscb != SCB_LIST_NULL)
              {
                aic_outb(p, next_hscb, SCBPTR);
                scb_index = aic_inb(p, SCB_TAG);
                if (scb_index < p->scb_data->numscbs)
                {
                  next_scbp = p->scb_data->scb_array[scb_index];
                  if (aic7xxx_match_scb(p, next_scbp, target, channel, lun,
                      SCB_LIST_NULL) )
                  {
                    if (next_scbp->flags & SCB_WAITINGQ)
                    {
                      p->dev_active_cmds[tindex]++;
                      p->activescbs--;
                      scbq_remove(&p->delayed_scbs[tindex], next_scbp);
                      scbq_remove(&p->waiting_scbs, next_scbp);
                    }
                    scbq_insert_head(&p->delayed_scbs[tindex],
                      next_scbp);
                    next_scbp->flags |= SCB_WAITINGQ;
                    p->dev_active_cmds[tindex]--;
                    p->activescbs--;
                    next_hscb = aic_inb(p, SCB_NEXT);
                    aic_outb(p, 0, SCB_CONTROL);
                    aic_outb(p, SCB_LIST_NULL, SCB_TAG);
                    aic7xxx_add_curscb_to_free_list(p);
                    if (prev_hscb == SCB_LIST_NULL)
                    {
                      /* We were first on the list,
                       * so we kill the selection
                       * hardware.  Let the sequencer
                       * re-init the hardware itself
                       */
                      aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
                      aic_outb(p, CLRSELTIMEO, CLRSINT1);
                      aic_outb(p, next_hscb, WAITING_SCBH);
                    }
                    else
                    {
                      aic_outb(p, prev_hscb, SCBPTR);
                      aic_outb(p, next_hscb, SCB_NEXT);
                    }
                  }
                  else
                  {
                    prev_hscb = next_hscb;
                    next_hscb = aic_inb(p, SCB_NEXT);
                  }
                } /* scb_index >= p->scb_data->numscbs */
              }
              aic_outb(p, active_hscb, SCBPTR);
              if (scb->flags & SCB_WAITINGQ)
              {
                scbq_remove(&p->delayed_scbs[tindex], scb);
                scbq_remove(&p->waiting_scbs, scb);
                p->dev_active_cmds[tindex]++;
                p->activescbs++;
              }
              scbq_insert_head(&p->delayed_scbs[tindex], scb);
              p->dev_active_cmds[tindex]--;
              p->activescbs--;
              scb->flags |= SCB_WAITINGQ | SCB_WAS_BUSY;
                  
              if ( !timer_pending(&p->dev_timer[tindex]) ) 
              {
                if ( p->dev_active_cmds[tindex] )
                {
                  p->dev_timer[tindex].expires = jiffies + (HZ * 2);
                  add_timer(&p->dev_timer[tindex]);
                }
                else
                {
                  p->dev_timer[tindex].expires = jiffies + (HZ / 2);
                  add_timer(&p->dev_timer[tindex]);
                }
              }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
              if (aic7xxx_verbose & VERBOSE_MINOR_ERROR)
              {
                if (queue_flag)
                  printk(INFO_LEAD "Queue full received; queue depth %d, "
                    "active %d\n", p->host_no, CTL_OF_SCB(scb),
                    p->dev_max_queue_depth[tindex],
                    p->dev_active_cmds[tindex]);
                else
                  printk(INFO_LEAD "Target busy\n", p->host_no, CTL_OF_SCB(scb));

              }
#endif
              if (queue_flag)
              {
                p->dev_temp_queue_depth[tindex] = 
                  p->dev_active_cmds[tindex];
                if ( p->dev_last_queue_full[tindex] !=
                     p->dev_active_cmds[tindex] )
                {
                  p->dev_last_queue_full[tindex] = 
                      p->dev_active_cmds[tindex];
                  p->dev_last_queue_full_count[tindex] = 0;
                }
                else
                {
                  p->dev_last_queue_full_count[tindex]++;
                }
                if ( (p->dev_last_queue_full_count[tindex] > 14) &&
                     (p->dev_active_cmds[tindex] > 4) )
                {
                  if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
                    printk(INFO_LEAD "Queue depth reduced to %d\n", p->host_no,
                      CTL_OF_SCB(scb), p->dev_active_cmds[tindex]);
                  p->dev_max_queue_depth[tindex] = 
                      p->dev_active_cmds[tindex];
                  p->dev_last_queue_full[tindex] = 0;
                  p->dev_last_queue_full_count[tindex] = 0;
                }
              }
              break;
            }
            
            default:
              if (aic7xxx_verbose & VERBOSE_SEQINT)
                printk(INFO_LEAD "Unexpected target status 0x%x.\n", p->host_no,
                     CTL_OF_SCB(scb), scb->hscb->target_status);
              if (!aic7xxx_error(cmd))
              {
                aic7xxx_error(cmd) = DID_RETRY_COMMAND;
              }
              break;
          }  /* end switch */
        }  /* end else of */
      }
      break;

    case AWAITING_MSG:
      {
        unsigned char scb_index, msg_out;

        scb_index = aic_inb(p, SCB_TAG);
        msg_out = aic_inb(p, MSG_OUT);
        scb = p->scb_data->scb_array[scb_index];
        p->msg_index = p->msg_len = 0;
        /*
         * This SCB had a MK_MESSAGE set in its control byte informing
         * the sequencer that we wanted to send a special message to
         * this target.
         */

        if ( !(scb->flags & SCB_DEVICE_RESET) &&
              (aic_inb(p, MSG_OUT) == MSG_IDENTIFYFLAG) &&
              (scb->hscb->control & TAG_ENB) )
        {
          p->msg_buf[p->msg_index++] = scb->tag_action;
          p->msg_buf[p->msg_index++] = scb->hscb->tag;
          p->msg_len += 2;
        }

        if (scb->flags & SCB_DEVICE_RESET)
        {
          p->msg_buf[p->msg_index++] = MSG_BUS_DEV_RESET;
          p->msg_len++;
          if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
            printk(INFO_LEAD "Bus device reset mailed.\n",
                 p->host_no, CTL_OF_SCB(scb));
        }
        else if (scb->flags & SCB_ABORT)
        {
          if (scb->tag_action)
          {
            p->msg_buf[p->msg_index++] = MSG_ABORT_TAG;
          }
          else
          {
            p->msg_buf[p->msg_index++] = MSG_ABORT;
          }
          p->msg_len++;
          if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
            printk(INFO_LEAD "Abort message mailed.\n", p->host_no,
              CTL_OF_SCB(scb));
        }
        else if (scb->flags & SCB_MSGOUT_WDTR)
        {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
          if (aic7xxx_verbose > 0xffff)
            printk(INFO_LEAD "Sending WDTR message.\n", p->host_no,
                   CTL_OF_SCB(scb));
#endif
          aic7xxx_construct_wdtr(p,
            p->transinfo[TARGET_INDEX(scb->cmd)].goal_width);
        }
        else if (scb->flags & SCB_MSGOUT_SDTR)
        {
          unsigned int max_sync, period;
          /*
           * We need to set an accurate goal_offset instead of
           * the ridiculously high one we default to.  We should
           * now know if we are wide.  Plus, the WDTR code will 
           * set our goal_offset for us as well.
           */
          if (p->transinfo[tindex].goal_offset)
          {
            if (p->features & AHC_ULTRA2)
              p->transinfo[tindex].goal_offset = MAX_OFFSET_ULTRA2;
            else if (p->transinfo[tindex].cur_width == MSG_EXT_WDTR_BUS_16_BIT)
              p->transinfo[tindex].goal_offset = MAX_OFFSET_16BIT;
            else
              p->transinfo[tindex].goal_offset = MAX_OFFSET_8BIT;
          }
          /*
           * Now that the device is selected, use the bits in SBLKCTL and
           * SSTAT2 to determine the max sync rate for this device.
           */
          if (p->features & AHC_ULTRA2)
          {
            if ( (aic_inb(p, SBLKCTL) & ENAB40) &&
                !(aic_inb(p, SSTAT2) & EXP_ACTIVE) )
            {
              max_sync = AHC_SYNCRATE_ULTRA2;
            }
            else
            {
              max_sync = AHC_SYNCRATE_ULTRA;
            }
          }
          else if (p->features & AHC_ULTRA)
          {
            max_sync = AHC_SYNCRATE_ULTRA;
          }
          else
          {
            max_sync = AHC_SYNCRATE_FAST;
          }
          period = p->transinfo[tindex].goal_period;
          aic7xxx_find_syncrate(p, &period, max_sync);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
          if (aic7xxx_verbose > 0xffff)
            printk(INFO_LEAD "Sending SDTR %d/%d message.\n", p->host_no,
                   CTL_OF_SCB(scb),
                   p->transinfo[tindex].goal_period,
                   p->transinfo[tindex].goal_offset);
#endif
          aic7xxx_construct_sdtr(p, period,
            p->transinfo[tindex].goal_offset);
        }
        else 
        {
          sti();
          panic("aic7xxx: AWAITING_MSG for an SCB that does "
                "not have a waiting message.\n");
        }
        /*
         * We've set everything up to send our message, now to actually do
         * so we need to enable reqinit interrupts and let the interrupt
         * handler do the rest.  We don't want to unpause the sequencer yet
         * though so we'll return early.  We also have to make sure that
         * we clear the SEQINT *BEFORE* we set the REQINIT handler active
         * or else it's possible on VLB cards to loose the first REQINIT
         * interrupt.  Edge triggered EISA cards could also loose this
         * interrupt, although PCI and level triggered cards should not
         * have this problem since they continually interrupt the kernel
         * until we take care of the situation.
         */
        scb->flags |= SCB_MSGOUT_SENT;
        p->msg_index = 0;
        p->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
        p->flags |= AHC_HANDLING_REQINITS;
        aic_outb(p, aic_inb(p, SIMODE1) | ENREQINIT, SIMODE1);
        return;
      }
      break;

    case DATA_OVERRUN:
      {
        unsigned char scb_index = aic_inb(p, SCB_TAG);
        unsigned char lastphase = aic_inb(p, LASTPHASE);
        unsigned int i;

        scb = (p->scb_data->scb_array[scb_index]);
        /*
         * XXX - What do we really want to do on an overrun?  The
         *       mid-level SCSI code should handle this, but for now,
         *       we'll just indicate that the command should retried.
         *    If we retrieved sense info on this target, then the 
         *    base SENSE info should have been saved prior to the
         *    overrun error.  In that case, we return DID_OK and let
         *    the mid level code pick up on the sense info.  Otherwise
         *    we return DID_ERROR so the command will get retried.
         */
        if ( !(scb->flags & SCB_SENSE) )
        {
          printk(WARN_LEAD "Data overrun detected in %s phase, tag %d;\n",
            p->host_no, CTL_OF_SCB(scb), 
            (lastphase == P_DATAIN) ? "Data-In" : "Data-Out", scb->hscb->tag);
          printk(KERN_WARNING "  %s seen Data Phase. Length=%d, NumSGs=%d.\n",
            (aic_inb(p, SEQ_FLAGS) & DPHASE) ? "Have" : "Haven't",
            scb->sg_length, scb->sg_count);
          for (i = 0; i < scb->sg_count; i++)
          {
            printk(KERN_WARNING "     sg[%d] - Addr 0x%x : Length %d\n",
                 i, 
                 le32_to_cpu(scb->sg_list[i].address),
                 le32_to_cpu(scb->sg_list[i].length) );
          }
          aic7xxx_error(scb->cmd) = DID_ERROR;
        }
        else
          printk(INFO_LEAD "Data Overrun during SEND_SENSE operation.\n",
            p->host_no, CTL_OF_SCB(scb));
      }
      break;

#if AIC7XXX_NOT_YET 
    case TRACEPOINT:
      {
        printk(INFO_LEAD "Tracepoint #1 reached.\n", p->host_no, channel,
          target, lun);
      }
      break;

    case TRACEPOINT2:
      {
        printk(INFO_LEAD "Tracepoint #2 reached.\n", p->host_no, channel,
          target, lun);
      }
      break;

    /* XXX Fill these in later */
    case MSG_BUFFER_BUSY:
      printk("aic7xxx: Message buffer busy.\n");
      break;
    case MSGIN_PHASEMIS:
      printk("aic7xxx: Message-in phasemis.\n");
      break;
#endif

    default:                   /* unknown */
      printk(WARN_LEAD "Unknown SEQINT, INTSTAT 0x%x, SCSISIGI 0x%x.\n",
             p->host_no, channel, target, lun, intstat,
             aic_inb(p, SCSISIGI));
      break;
  }

  /*
   * Clear the sequencer interrupt and unpause the sequencer.
   */
  unpause_sequencer(p, /* unpause always */ TRUE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_parse_msg
 *
 * Description:
 *   Parses incoming messages into actions on behalf of
 *   aic7xxx_handle_reqinit
 *_F*************************************************************************/
static int
aic7xxx_parse_msg(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int reject, reply, done;
  unsigned char target_scsirate, tindex;
  unsigned short target_mask;
  unsigned char target, channel, lun;

  target = scb->cmd->target;
  channel = scb->cmd->channel;
  lun = scb->cmd->lun;
  reply = reject = done = FALSE;
  tindex = TARGET_INDEX(scb->cmd);
  target_scsirate = aic_inb(p, TARG_SCSIRATE + tindex);
  target_mask = (0x01 << tindex);

  /*
   * Parse as much of the message as is availible,
   * rejecting it if we don't support it.  When
   * the entire message is availible and has been
   * handled, return TRUE indicating that we have
   * parsed an entire message.
   */

  if (p->msg_buf[0] != MSG_EXTENDED)
  {
    reject = TRUE;
  }

  /*
   * Just accept the length byte outright and perform
   * more checking once we know the message type.
   */

  if ( !reject && (p->msg_len > 2) )
  {
    switch(p->msg_buf[2])
    {
      case MSG_EXT_SDTR:
      {
        unsigned int period, offset;
        unsigned char maxsync, saved_offset;
        struct aic7xxx_syncrate *syncrate;
        
        if (p->msg_buf[1] != MSG_EXT_SDTR_LEN)
        {
          reject = TRUE;
          break;
        }

        if (p->msg_len < (MSG_EXT_SDTR_LEN + 2))
        {
          break;
        }

        period = p->msg_buf[3];
        saved_offset = offset = p->msg_buf[4];

        if (p->features & AHC_ULTRA2)
        {
          if ( (aic_inb(p, SBLKCTL) & ENAB40) &&
               !(aic_inb(p, SSTAT2) & EXP_ACTIVE) )
          {
            maxsync = AHC_SYNCRATE_ULTRA2;
          }
          else
          {
            maxsync = AHC_SYNCRATE_ULTRA;
          }
        }
        else if (p->features & AHC_ULTRA)
        {
          maxsync = AHC_SYNCRATE_ULTRA;
        }
        else
        {
          maxsync = AHC_SYNCRATE_FAST;
        }
  
#ifdef AIC7XXX_VERBOSE_DEBUGGING
        if (aic7xxx_verbose > 0xffff)
        {
          printk(INFO_LEAD "Finished receipt of SDTR, parsing %d/%d\n",
                 p->host_no, CTL_OF_SCB(scb), period, offset);
          syncrate = aic7xxx_find_syncrate(p, &period, maxsync);
          printk(INFO_LEAD "After find_syncrate() %d/%d\n",
                 p->host_no, CTL_OF_SCB(scb), period, offset);
          aic7xxx_validate_offset(p, syncrate, &offset,
                                  target_scsirate & WIDEXFER);
          printk(INFO_LEAD "After validate_offset() %d/%d\n",
                 p->host_no, CTL_OF_SCB(scb), period, offset);
          aic7xxx_set_syncrate(p, syncrate, target, channel, period,
                               offset, AHC_TRANS_ACTIVE|AHC_TRANS_CUR);
          printk(INFO_LEAD "Final values of Period/Offset as set: %d/%d\n",
                 p->host_no, CTL_OF_SCB(scb), period, offset);
        }
        else
        {
          syncrate = aic7xxx_find_syncrate(p, &period, maxsync);
          aic7xxx_validate_offset(p, syncrate, &offset,
                                  target_scsirate & WIDEXFER);
          aic7xxx_set_syncrate(p, syncrate, target, channel, period,
                               offset, AHC_TRANS_ACTIVE|AHC_TRANS_CUR);
        }
#else
        syncrate = aic7xxx_find_syncrate(p, &period, maxsync);
        aic7xxx_validate_offset(p, syncrate, &offset,
                                target_scsirate & WIDEXFER);
        aic7xxx_set_syncrate(p, syncrate, target, channel, period,
                             offset, AHC_TRANS_ACTIVE|AHC_TRANS_CUR);
#endif

        if (offset == 0)
        {
          /*
           * Uhh ohh, things fell through to async....update the goal
           * items and the needsdtr_copy to reflect this...
           */
          aic7xxx_set_syncrate(p, syncrate, target, channel, period,
                 offset, AHC_TRANS_GOAL|AHC_TRANS_QUITE);
          p->needsdtr_copy &= ~target_mask;
        }
        /*
         * Did we start this, if not, or if we went to low and had to
         * go async, then send an SDTR back to the target
         */
        p->needsdtr &= ~target_mask;
        p->sdtr_pending &= ~target_mask;
        if ( ((scb->flags & (SCB_MSGOUT_SENT|SCB_MSGOUT_SDTR)) ==
              (SCB_MSGOUT_SENT|SCB_MSGOUT_SDTR)) &&
             (offset == saved_offset) )
        {
          scb->flags &= ~SCB_MSGOUT_BITS;
        }
        else
        {
          /*
           * Send a reply SDTR back.  Even if we sent the first one, it
           * is valid to send another one out immediately to re-negotiate
           * things, and a few devices don't like getting rejects after
           * we already sent them one SDTR.  Just send an SDTR for async
           * this time if need be (or for the correct params if we didn't
           * start all of this).  If this is a Reject Reply type message,
           * then we've put the async settings into the goal area for
           * future reference (when we get the AWAITING_MSG interrupt).
           * If this is a case where we are responding to the target's
           * initiated SDTR, then leave our own goal and user values in
           * place (unless the device hasn't been scanned yet, in which
           * case, put the user values into the goal values so we don't
           * send out an Async message).
           */
          if ( !(p->dev_flags[tindex] & DEVICE_SCANNED) )
          {
            p->transinfo[tindex].goal_width =
              p->transinfo[tindex].user_width;
            p->transinfo[tindex].goal_period =
              p->transinfo[tindex].user_period;
            p->transinfo[tindex].goal_offset =
              p->transinfo[tindex].user_offset;
            p->needwdtr_copy |= target_mask;
            p->needsdtr_copy |= target_mask;
          }
          scb->flags &= ~SCB_MSGOUT_BITS;
          scb->flags |= SCB_MSGOUT_SDTR;
          aic_outb(p, HOST_MSG, MSG_OUT);
          aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
        }
        done = TRUE;
        break;
      }
      case MSG_EXT_WDTR:
      {
        unsigned char bus_width;
          
        if (p->msg_buf[1] != MSG_EXT_WDTR_LEN)
        {
          reject = TRUE;
          break;
        }

        if (p->msg_len < (MSG_EXT_WDTR_LEN + 2))
        {
          break;
        }

        bus_width = p->msg_buf[3];
        if ( (scb->flags & (SCB_MSGOUT_SENT|SCB_MSGOUT_WDTR)) ==
             (SCB_MSGOUT_SENT|SCB_MSGOUT_WDTR) )
        {
          switch(bus_width)
          {
            default:
            {
              reject = TRUE;
              if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
                   ((p->dev_flags[tindex] & DEVICE_PRINT_WDTR) ||
                    (aic7xxx_verbose > 0xffff)) )
              {
                printk(INFO_LEAD "Requesting %d bit transfers, rejecting.\n",
                  p->host_no, CTL_OF_SCB(scb), 8 * (0x01 << bus_width));
                p->dev_flags[tindex] &= ~DEVICE_PRINT_WDTR;
              }
            } /* We fall through on purpose */
            case MSG_EXT_WDTR_BUS_8_BIT:
            {
              bus_width = MSG_EXT_WDTR_BUS_8_BIT;
              p->needwdtr_copy &= ~target_mask;
              break;
            }
            case MSG_EXT_WDTR_BUS_16_BIT:
            {
              break;
            }
          }
          scb->flags &= ~SCB_MSGOUT_BITS;
          p->wdtr_pending &= ~target_mask;
          p->needwdtr &= ~target_mask;
        }
        else
        {
          scb->flags &= ~SCB_MSGOUT_BITS;
          scb->flags |= SCB_MSGOUT_WDTR;
          reply = TRUE;
          if ( !(p->dev_flags[tindex] & DEVICE_SCANNED) )
          {
            /* 
             * Well, we now know the WDTR and SYNC caps of this device since
             * it contacted us first, mark it as such and copy the user stuff
             * over to the goal stuff.
             */
            p->transinfo[tindex].goal_width =
              p->transinfo[tindex].user_width;
            p->transinfo[tindex].goal_period =
              p->transinfo[tindex].user_period;
            p->transinfo[tindex].goal_offset =
              p->transinfo[tindex].user_offset;
            p->needwdtr_copy |= target_mask;
            p->needsdtr_copy |= target_mask;
          }
          switch(bus_width)
          {
            default:
            {
              if ( (p->features & AHC_WIDE) &&
                   (p->transinfo[tindex].goal_width ==
                    MSG_EXT_WDTR_BUS_16_BIT) )
              {
                bus_width = MSG_EXT_WDTR_BUS_16_BIT;
                break;
              }
            } /* Fall through if we aren't a wide card */
            case MSG_EXT_WDTR_BUS_8_BIT:
            {
              p->needwdtr_copy &= ~target_mask;
              bus_width = MSG_EXT_WDTR_BUS_8_BIT;
              break;
            }
          }
          p->needwdtr &= ~target_mask;
          p->wdtr_pending &= ~target_mask;
          aic_outb(p, HOST_MSG, MSG_OUT);
          aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
        }
        aic7xxx_set_width(p, target, channel, lun, bus_width,
                          AHC_TRANS_ACTIVE|AHC_TRANS_CUR);
        
        /*
         * By virtue of the SCSI spec, a WDTR message negates any existing
         * SDTR negotiations.  So, even if needsdtr isn't marked for this
         * device, we still have to do a new SDTR message if the device
         * supports SDTR at all.  Therefore, we check needsdtr_copy instead
         * of needstr.
         */
        aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0,
                             AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE);
        if ( (p->needsdtr_copy & target_mask) &&
            !(p->sdtr_pending & target_mask))
        {
          p->needsdtr |= target_mask;
          if ( !reject && !reply )
          {
            scb->flags &= ~SCB_MSGOUT_WDTR;
            if (p->transinfo[tindex].goal_period)
            {
              p->sdtr_pending |= target_mask;
              scb->flags |= SCB_MSGOUT_SDTR;
              aic_outb(p, HOST_MSG, MSG_OUT);
              aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
            }
          }
        }
        done = TRUE;
        break;
      }
      default:
      {
        reject = TRUE;
        break;
      }
    } /* end of switch(p->msg_type) */
  } /* end of if (!reject && (p->msg_len > 2)) */

  if (reject)
  {
    aic_outb(p, MSG_MESSAGE_REJECT, MSG_OUT);
    aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
    done = TRUE;
  }
  return(done);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_reqinit
 *
 * Description:
 *   Interrupt handler for REQINIT interrupts (used to transfer messages to
 *    and from devices).
 *_F*************************************************************************/
static void
aic7xxx_handle_reqinit(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  unsigned char lastbyte;
  unsigned char phasemis;
  int done = FALSE;

  switch(p->msg_type)
  {
    case MSG_TYPE_INITIATOR_MSGOUT:
      {
        if (p->msg_len == 0)
          panic("aic7xxx: REQINIT with no active message!\n");

        lastbyte = (p->msg_index == (p->msg_len - 1));
        phasemis = ( aic_inb(p, SCSISIGI) & PHASE_MASK) != P_MESGOUT;

        if (lastbyte || phasemis)
        {
          /* Time to end the message */
          p->msg_len = 0;
          p->msg_type = MSG_TYPE_NONE;
          /*
           * NOTE-TO-MYSELF: If you clear the REQINIT after you
           * disable REQINITs, then cases of REJECT_MSG stop working
           * and hang the bus
           */
          aic_outb(p, aic_inb(p, SIMODE1) & ~ENREQINIT, SIMODE1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          p->flags &= ~AHC_HANDLING_REQINITS;

          if (phasemis == 0)
          {
            aic_outb(p, p->msg_buf[p->msg_index], SINDEX);
            aic_outb(p, 0, RETURN_1);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
            if (aic7xxx_verbose > 0xffff)
              printk(INFO_LEAD "Completed sending of REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
#endif
          }
          else
          {
            aic_outb(p, MSGOUT_PHASEMIS, RETURN_1);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
            if (aic7xxx_verbose > 0xffff)
              printk(INFO_LEAD "PHASEMIS while sending REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
#endif
          }
          unpause_sequencer(p, TRUE);
        }
        else
        {
          /*
           * Present the byte on the bus (clearing REQINIT) but don't
           * unpause the sequencer.
           */
          aic_outb(p, CLRREQINIT, CLRSINT1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          aic_outb(p,  p->msg_buf[p->msg_index++], SCSIDATL);
        }
        break;
      }
    case MSG_TYPE_INITIATOR_MSGIN:
      {
        phasemis = ( aic_inb(p, SCSISIGI) & PHASE_MASK ) != P_MESGIN;

        if (phasemis == 0)
        {
          p->msg_len++;
          /* Pull the byte in without acking it */
          p->msg_buf[p->msg_index] = aic_inb(p, SCSIBUSL);
          done = aic7xxx_parse_msg(p, scb);
          /* Ack the byte */
          aic_outb(p, CLRREQINIT, CLRSINT1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          aic_inb(p, SCSIDATL);
          p->msg_index++;
        }
        if (phasemis || done)
        {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
          if (aic7xxx_verbose > 0xffff)
          {
            if (phasemis)
              printk(INFO_LEAD "PHASEMIS while receiving REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
            else
              printk(INFO_LEAD "Completed receipt of REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
          }
#endif
          /* Time to end our message session */
          p->msg_len = 0;
          p->msg_type = MSG_TYPE_NONE;
          aic_outb(p, aic_inb(p, SIMODE1) & ~ENREQINIT, SIMODE1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          p->flags &= ~AHC_HANDLING_REQINITS;
          unpause_sequencer(p, TRUE);
        }
        break;
      }
    default:
      {
        panic("aic7xxx: Unknown REQINIT message type.\n");
        break;
      }
  } /* End of switch(p->msg_type) */
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_scsiint
 *
 * Description:
 *   Interrupt handler for SCSI interrupts (SCSIINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_scsiint(struct aic7xxx_host *p, unsigned char intstat)
{
  unsigned char scb_index;
  unsigned char status;
  struct aic7xxx_scb *scb;

  scb_index = aic_inb(p, SCB_TAG);
  status = aic_inb(p, SSTAT1);

  if (scb_index < p->scb_data->numscbs)
  {
    scb = p->scb_data->scb_array[scb_index];
    if ((scb->flags & SCB_ACTIVE) == 0)
    {
      scb = NULL;
    }
  }
  else
  {
    scb = NULL;
  }


  if ((status & SCSIRSTI) != 0)
  {
    int channel;

    if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
      channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
    else
      channel = 0;

    if (aic7xxx_verbose & VERBOSE_RESET)
      printk(WARN_LEAD "Someone else reset the channel!!\n",
           p->host_no, channel, -1, -1);
    /*
     * Go through and abort all commands for the channel, but do not
     * reset the channel again.
     */
    aic7xxx_reset_channel(p, channel, /* Initiate Reset */ FALSE);
    aic7xxx_run_done_queue(p, FALSE);
    scb = NULL;
  }
  else if ( ((status & BUSFREE) != 0) && ((status & SELTO) == 0) )
  {
    /*
     * First look at what phase we were last in.  If it's message-out,
     * chances are pretty good that the bus free was in response to
     * one of our abort requests.
     */
    unsigned char lastphase = aic_inb(p, LASTPHASE);
    unsigned char saved_tcl = aic_inb(p, SAVED_TCL);
    unsigned char target = (saved_tcl >> 4) & 0x0F;
    int channel;
    int printerror = TRUE;

    if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
      channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
    else
      channel = 0;

    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP),
             SCSISEQ);
    if (lastphase == P_MESGOUT)
    {
      unsigned char message;

      message = aic_inb(p, SINDEX);

      if ((message == MSG_ABORT) || (message == MSG_ABORT_TAG))
      {
        if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
          printk(INFO_LEAD "SCB %d abort delivered.\n", p->host_no,
            CTL_OF_SCB(scb), scb->hscb->tag);
        aic7xxx_reset_device(p, target, channel, ALL_LUNS,
                (message == MSG_ABORT) ? SCB_LIST_NULL : scb->hscb->tag );
        aic7xxx_run_done_queue(p, FALSE);
        scb = NULL;
        printerror = 0;
      }
      else if (message == MSG_BUS_DEV_RESET)
      {
        aic7xxx_handle_device_reset(p, target, channel);
        scb = NULL;
        printerror = 0;
      }
    }
    if (printerror != 0)
    {
      if (scb != NULL)
      {
        unsigned char tag;

        if ((scb->hscb->control & TAG_ENB) != 0)
        {
          tag = scb->hscb->tag;
        }
        else
        {
          tag = SCB_LIST_NULL;
        }
        aic7xxx_reset_device(p, target, channel, ALL_LUNS, tag);
        aic7xxx_run_done_queue(p, FALSE);
      }
      printk(INFO_LEAD "Unexpected busfree, LASTPHASE = 0x%x, "
             "SEQADDR = 0x%x\n", p->host_no, channel, target, -1, lastphase,
             (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
      scb = NULL;
    }
    aic_outb(p, MSG_NOOP, MSG_OUT);
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENBUSFREE|ENREQINIT),
      SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    aic_outb(p, CLRBUSFREE, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    restart_sequencer(p);
    unpause_sequencer(p, TRUE);
  }
  else if ((status & SELTO) != 0)
  {
    unsigned char scbptr;
    unsigned char nextscb;
    Scsi_Cmnd *cmd;

    scbptr = aic_inb(p, WAITING_SCBH);
    if (scbptr > p->scb_data->maxhscbs)
    {
      /*
       * I'm still trying to track down exactly how this happens, but until
       * I find it, this code will make sure we aren't passing bogus values
       * into the SCBPTR register, even if that register will just wrap
       * things around, we still don't like having out of range variables.
       *
       * NOTE: Don't check the aic7xxx_verbose variable, I want this message
       * to always be displayed.
       */
      printk(INFO_LEAD "Invalid WAITING_SCBH value %d, improvising.\n",
             p->host_no, -1, -1, -1, scbptr);
      if (p->scb_data->maxhscbs > 4)
        scbptr &= (p->scb_data->maxhscbs - 1);
      else
        scbptr &= 0x03;
    }
    aic_outb(p, scbptr, SCBPTR);
    scb_index = aic_inb(p, SCB_TAG);

    scb = NULL;
    if (scb_index < p->scb_data->numscbs)
    {
      scb = p->scb_data->scb_array[scb_index];
      if ((scb->flags & SCB_ACTIVE) == 0)
      {
        scb = NULL;
      }
    }
    if (scb == NULL)
    {
      printk(WARN_LEAD "Referenced SCB %d not valid during SELTO.\n",
             p->host_no, -1, -1, -1, scb_index);
      printk(KERN_WARNING "        SCSISEQ = 0x%x SEQADDR = 0x%x SSTAT0 = 0x%x "
             "SSTAT1 = 0x%x\n", aic_inb(p, SCSISEQ),
             aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
             aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
      if (aic7xxx_panic_on_abort)
        aic7xxx_panic_abort(p, NULL);
    }
    else
    {
      cmd = scb->cmd;
      cmd->result = (DID_TIME_OUT << 16);

      /*
       * Clear out this hardware SCB
       */
      aic_outb(p, 0, SCB_CONTROL);

      /*
       * Clear out a few values in the card that are in an undetermined
       * state.
       */
      aic_outb(p, MSG_NOOP, MSG_OUT);

      /*
       * Shift the waiting for selection queue forward
       */
      nextscb = aic_inb(p, SCB_NEXT);
      aic_outb(p, nextscb, WAITING_SCBH);

      /*
       * Put this SCB back on the free list.
       */
      aic7xxx_add_curscb_to_free_list(p);
      /*
       * XXX - If we queued an abort tag, go clean up the disconnected list.
       * We know that this particular SCB had to be the queued abort since
       * the disconnected SCB would have gotten a reconnect instead.
       * However, if this is an abort command, then DID_TIMEOUT isn't
       * appropriate, neither is returning the command for that matter.
       * What we need to do then is to let the command timeout again so
       * we get a reset since this abort just failed.
       */
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Selection Timeout.\n", p->host_no, CTL_OF_SCB(scb));
#endif
      if (p->flags & SCB_QUEUED_ABORT)
      {
        cmd->result = 0;
        scb->flags &= ~SCB_QUEUED_ABORT;
        scb = NULL;
      }
    }
    /*
     * Restarting the sequencer will stop the selection and make sure devices
     * are allowed to reselect in.
     */
    aic_outb(p, 0, SCSISEQ);
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENREQINIT|ENBUSFREE), SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    aic_outb(p, CLRSELTIMEO | CLRBUSFREE, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    restart_sequencer(p);
    unpause_sequencer(p, TRUE);
  }
  else if (scb == NULL)
  {
    printk(WARN_LEAD "aic7xxx_isr - referenced scb not valid "
           "during scsiint 0x%x scb(%d)\n"
           "      SIMODE0 0x%x, SIMODE1 0x%x, SSTAT0 0x%x, SEQADDR 0x%x\n",
           p->host_no, -1, -1, -1, status, scb_index, aic_inb(p, SIMODE0),
           aic_inb(p, SIMODE1), aic_inb(p, SSTAT0),
           (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
    /*
     * Turn off the interrupt and set status to zero, so that it
     * falls through the rest of the SCSIINT code.
     */
    aic_outb(p, status, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  else if (status & SCSIPERR)
  {
    /*
     * Determine the bus phase and queue an appropriate message.
     */
    char  *phase;
    Scsi_Cmnd *cmd;
    unsigned char mesg_out = MSG_NOOP;
    unsigned char lastphase = aic_inb(p, LASTPHASE);

    cmd = scb->cmd;
    switch (lastphase)
    {
      case P_DATAOUT:
        phase = "Data-Out";
        break;
      case P_DATAIN:
        phase = "Data-In";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_COMMAND:
        phase = "Command";
        break;
      case P_MESGOUT:
        phase = "Message-Out";
        break;
      case P_STATUS:
        phase = "Status";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_MESGIN:
        phase = "Message-In";
        mesg_out = MSG_PARITY_ERROR;
        break;
      default:
        phase = "unknown";
        break;
    }

    /*
     * A parity error has occurred during a data
     * transfer phase. Flag it and continue.
     */
    printk(WARN_LEAD "Parity error during %s phase.\n",
           p->host_no, CTL_OF_SCB(scb), phase);

    /*
     * We've set the hardware to assert ATN if we get a parity
     * error on "in" phases, so all we need to do is stuff the
     * message buffer with the appropriate message.  "In" phases
     * have set mesg_out to something other than MSG_NOP.
     */
    if (mesg_out != MSG_NOOP)
    {
      aic_outb(p, mesg_out, MSG_OUT);
      scb = NULL;
    }
    aic_outb(p, CLRSCSIPERR, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause_always */ TRUE);
  }
  else if ( (status & REQINIT) &&
            (p->flags & AHC_HANDLING_REQINITS) )
  {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (aic7xxx_verbose > 0xffff)
      printk(INFO_LEAD "Handling REQINIT, SSTAT1=0x%x.\n", p->host_no,
             CTL_OF_SCB(scb), aic_inb(p, SSTAT1));
#endif
    aic7xxx_handle_reqinit(p, scb);
    return;
  }
  else
  {
    /*
     * We don't know what's going on. Turn off the
     * interrupt source and try to continue.
     */
    if (aic7xxx_verbose & VERBOSE_SCSIINT)
      printk(INFO_LEAD "Unknown SCSIINT status, SSTAT1(0x%x).\n",
        p->host_no, -1, -1, -1, status);
    aic_outb(p, status, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  if (scb != NULL)
  {
    aic7xxx_done(p, scb);
  }
}

#ifdef AIC7XXX_VERBOSE_DEBUGGING
static void
aic7xxx_check_scbs(struct aic7xxx_host *p, char *buffer)
{
  unsigned char saved_scbptr, free_scbh, dis_scbh, wait_scbh, temp;
  int i, bogus, lost;
  static unsigned char scb_status[AIC7XXX_MAXSCB];

#define SCB_NO_LIST 0
#define SCB_FREE_LIST 1
#define SCB_WAITING_LIST 2
#define SCB_DISCONNECTED_LIST 4
#define SCB_CURRENTLY_ACTIVE 8

  /*
   * Note, these checks will fail on a regular basis once the machine moves
   * beyond the bus scan phase.  The problem is race conditions concerning
   * the scbs and where they are linked in.  When you have 30 or so commands
   * outstanding on the bus, and run this twice with every interrupt, the
   * chances get pretty good that you'll catch the sequencer with an SCB
   * only partially linked in.  Therefore, once we pass the scan phase
   * of the bus, we really should disable this function.
   */
  bogus = FALSE;
  memset(&scb_status[0], 0, sizeof(scb_status));
  pause_sequencer(p);
  saved_scbptr = aic_inb(p, SCBPTR);
  if (saved_scbptr >= p->scb_data->maxhscbs)
  {
    printk("Bogus SCBPTR %d\n", saved_scbptr);
    bogus = TRUE;
  }
  scb_status[saved_scbptr] = SCB_CURRENTLY_ACTIVE;
  free_scbh = aic_inb(p, FREE_SCBH);
  if ( (free_scbh != SCB_LIST_NULL) &&
       (free_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus FREE_SCBH %d\n", free_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = free_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_FREE_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_FREE_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }

  dis_scbh = aic_inb(p, DISCONNECTED_SCBH);
  if ( (dis_scbh != SCB_LIST_NULL) &&
       (dis_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus DISCONNECTED_SCBH %d\n", dis_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = dis_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_DISCONNECTED_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_DISCONNECTED_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }
  
  wait_scbh = aic_inb(p, WAITING_SCBH);
  if ( (wait_scbh != SCB_LIST_NULL) &&
       (wait_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus WAITING_SCBH %d\n", wait_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = wait_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_WAITING_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_WAITING_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }

  lost=0;
  for(i=0; i < p->scb_data->maxhscbs; i++)
  {
    aic_outb(p, i, SCBPTR);
    temp = aic_inb(p, SCB_NEXT);
    if ( ((temp != SCB_LIST_NULL) &&
          (temp >= p->scb_data->maxhscbs)) )
    {
      printk("HSCB %d bad, SCB_NEXT invalid(%d).\n", i, temp);
      bogus = TRUE;
    }
    if ( temp == i )
    {
      printk("HSCB %d bad, SCB_NEXT points to self.\n", i);
      bogus = TRUE;
    }
    temp = aic_inb(p, SCB_PREV);
    if ((temp != SCB_LIST_NULL) &&
        (temp >= p->scb_data->maxhscbs))
    {
      printk("HSCB %d bad, SCB_PREV invalid(%d).\n", i, temp);
      bogus = TRUE;
    }
    if (scb_status[i] == 0)
      lost++;
    if (lost > 1)
    {
      printk("Too many lost scbs.\n");
      bogus=TRUE;
    }
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  unpause_sequencer(p, FALSE);
  if (bogus)
  {
    printk("Bogus parameters found in card SCB array structures.\n");
    printk("%s\n", buffer);
    aic7xxx_panic_abort(p, NULL);
  }
  return;
}
#endif

/*+F*************************************************************************
 * Function:
 *   aic7xxx_isr
 *
 * Description:
 *   SCSI controller interrupt handler.
 *-F*************************************************************************/
static void
aic7xxx_isr(int irq, void *dev_id, struct pt_regs *regs)
{
  struct aic7xxx_host *p;
  unsigned char intstat;

  p = (struct aic7xxx_host *)dev_id;

  /*
   * Just a few sanity checks.  Make sure that we have an int pending.
   * Also, if PCI, then we are going to check for a PCI bus error status
   * should we get too many spurious interrupts.
   */
  if (!((intstat = aic_inb(p, INTSTAT)) & INT_PEND))
  {
#ifdef CONFIG_PCI
    if ( (p->chip & AHC_PCI) && (p->spurious_int > 500) &&
        !(p->flags & AHC_HANDLING_REQINITS) )
    {
      if ( aic_inb(p, ERROR) & PCIERRSTAT )
      {
        aic7xxx_pci_intr(p);
      }
      p->spurious_int = 0;
    }
    else if ( !(p->flags & AHC_HANDLING_REQINITS) )
    {
      p->spurious_int++;
    }
#endif
    return;
  }

  p->spurious_int = 0;

  /*
   * Keep track of interrupts for /proc/scsi
   */
  p->isr_count++;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if ( (p->isr_count < 16) && (aic7xxx_verbose > 0xffff) &&
       (aic7xxx_panic_on_abort) && (p->flags & AHC_PAGESCBS) )
    aic7xxx_check_scbs(p, "Bogus settings at start of interrupt.");
#endif

  /*
   * Handle all the interrupt sources - especially for SCSI
   * interrupts, we won't get a second chance at them.
   */
  if (intstat & CMDCMPLT)
  {
    struct aic7xxx_scb *scb = NULL;
    Scsi_Cmnd *cmd;
    unsigned char scb_index;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if(aic7xxx_verbose > 0xffff)
      printk(INFO_LEAD "Command Complete Int.\n", p->host_no, -1, -1, -1);
#endif
    
    /*
     * Clear interrupt status before running the completion loop.
     * This eliminates a race condition whereby a command could
     * complete between the last check of qoutfifo and the
     * CLRCMDINT statement.  This would result in us thinking the
     * qoutfifo was empty when it wasn't, and in actuality be a lost
     * completion interrupt.  With multiple devices or tagged queueing
     * this could be very bad if we caught all but the last completion
     * and no more are imediately sent.
     */
    aic_outb(p, CLRCMDINT, CLRINT);
    /*
     * The sequencer will continue running when it
     * issues this interrupt. There may be >1 commands
     * finished, so loop until we've processed them all.
     */

    while (p->qoutfifo[p->qoutfifonext] != SCB_LIST_NULL)
    {
      scb_index = p->qoutfifo[p->qoutfifonext];
      p->qoutfifo[p->qoutfifonext++] = SCB_LIST_NULL;
      if ( scb_index >= p->scb_data->numscbs )
        scb = NULL;
      else
        scb = p->scb_data->scb_array[scb_index];
      if (scb == NULL)
      {
        printk(WARN_LEAD "CMDCMPLT with invalid SCB index %d\n", p->host_no,
          -1, -1, -1, scb_index);
        continue;
      }
      else if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
      {
        printk(WARN_LEAD "CMDCMPLT without command for SCB %d, SCB flags "
          "0x%x, cmd 0x%lx\n", p->host_no, -1, -1, -1, scb_index, scb->flags,
          (unsigned long) scb->cmd);
        continue;
      }
      else if (scb->flags & SCB_QUEUED_ABORT)
      {
        pause_sequencer(p);
        if ( ((aic_inb(p, LASTPHASE) & PHASE_MASK) != P_BUSFREE) &&
             (aic_inb(p, SCB_TAG) == scb->hscb->tag) )
        {
          unpause_sequencer(p, FALSE);
          continue;
        }
        aic7xxx_reset_device(p, scb->cmd->target, scb->cmd->channel,
          scb->cmd->lun, scb->hscb->tag);
        scb->flags &= ~(SCB_QUEUED_FOR_DONE | SCB_RESET | SCB_ABORT |
          SCB_QUEUED_ABORT);
        unpause_sequencer(p, FALSE);
      }
      else if (scb->flags & SCB_ABORT)
      {
        /*
         * We started to abort this, but it completed on us, let it
         * through as successful
         */
        scb->flags &= ~(SCB_ABORT|SCB_RESET);
      }
      switch (status_byte(scb->hscb->target_status))
      {
        case QUEUE_FULL:
        case BUSY:
          scb->hscb->target_status = 0;
          scb->cmd->result = 0;
          aic7xxx_error(scb->cmd) = DID_OK;
          break;
        default:
          cmd = scb->cmd;
          if (scb->hscb->residual_SG_segment_count != 0)
          {
            aic7xxx_calculate_residual(p, scb);
          }
          cmd->result |= (aic7xxx_error(cmd) << 16);
          if (scb->tag_action)
            p->dev_flags[TARGET_INDEX(cmd)] |= 
               DEVICE_TAGGED_SUCCESS | DEVICE_SUCCESS | DEVICE_PRESENT;
          else
            p->dev_flags[TARGET_INDEX(cmd)] |= 
               DEVICE_SUCCESS | DEVICE_PRESENT;
          aic7xxx_done(p, scb);
          break;
      }      
    }
  }

  if (intstat & BRKADRINT)
  {
    int i;
    unsigned char errno = aic_inb(p, ERROR);

    printk(KERN_ERR "(scsi%d) BRKADRINT error(0x%x):\n", p->host_no, errno);
    for (i = 0; i < NUMBER(hard_error); i++)
    {
      if (errno & hard_error[i].errno)
      {
        printk(KERN_ERR "  %s\n", hard_error[i].errmesg);
      }
    }
    printk(KERN_ERR "(scsi%d)   SEQADDR=0x%x\n", p->host_no,
      (((aic_inb(p, SEQADDR1) << 8) & 0x100) | aic_inb(p, SEQADDR0)));
    if (aic7xxx_panic_on_abort)
      aic7xxx_panic_abort(p, NULL);
#ifdef CONFIG_PCI
    if (errno & PCIERRSTAT)
      aic7xxx_pci_intr(p);
#endif
    if (errno & (SQPARERR | ILLOPCODE | ILLSADDR))
    {
      sti();
      panic("aic7xxx: unrecoverable BRKADRINT.\n");
    }
    if (errno & ILLHADDR)
    {
      printk(KERN_ERR "(scsi%d) BUG! Driver accessed chip without first "
             "pausing controller!\n", p->host_no);
    }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (errno & DPARERR)
    {
      if (aic_inb(p, DMAPARAMS) & DIRECTION)
        printk("(scsi%d) while DMAing SCB from host to card.\n", p->host_no);
      else
        printk("(scsi%d) while DMAing SCB from card to host.\n", p->host_no);
    }
#endif
    aic_outb(p, CLRPARERR | CLRBRKADRINT, CLRINT);
    unpause_sequencer(p, FALSE);
  }

  if (intstat & SEQINT)
  {
    aic7xxx_handle_seqint(p, intstat);
  }

  if (intstat & SCSIINT)
  {
    aic7xxx_handle_scsiint(p, intstat);
  }

#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if ( (p->isr_count < 16) && (aic7xxx_verbose > 0xffff) &&
       (aic7xxx_panic_on_abort) && (p->flags & AHC_PAGESCBS) )
    aic7xxx_check_scbs(p, "Bogus settings at end of interrupt.");
#endif

}

/*+F*************************************************************************
 * Function:
 *   do_aic7xxx_isr
 *
 * Description:
 *   This is a gross hack to solve a problem in linux kernels 2.1.85 and
 *   above.  Please, children, do not try this at home, and if you ever see
 *   anything like it, please inform the Gross Hack Police immediately
 *-F*************************************************************************/
static void
do_aic7xxx_isr(int irq, void *dev_id, struct pt_regs *regs)
{
  unsigned long cpu_flags;
  struct aic7xxx_host *p;
  
  p = (struct aic7xxx_host *)dev_id;
  if(!p)
    return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,95)
  spin_lock_irqsave(&io_request_lock, cpu_flags);
  if(test_and_set_bit(AHC_IN_ISR_BIT, &p->flags))
  {
    return;
  }
  do
  {
    aic7xxx_isr(irq, dev_id, regs);
  } while ( (aic_inb(p, INTSTAT) & INT_PEND) );
  aic7xxx_done_cmds_complete(p);
  aic7xxx_run_waiting_queues(p);
  clear_bit(AHC_IN_ISR_BIT, &p->flags);
  spin_unlock_irqrestore(&io_request_lock, cpu_flags);
#else
  if(set_bit(AHC_IN_ISR_BIT, (int *)&p->flags))
  {
    return;
  }
  DRIVER_LOCK
  do
  {
    aic7xxx_isr(irq, dev_id, regs);
  } while ( (aic_inb(p, INTSTAT) & INT_PEND) );
  DRIVER_UNLOCK
  aic7xxx_done_cmds_complete(p);
  aic7xxx_run_waiting_queues(p);
  clear_bit(AHC_IN_ISR_BIT, (int *)&p->flags);
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_device_queue_depth
 *
 * Description:
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   AIC7XXX_CMDS_PER_LUN is defined.  If it is defined, then it is used
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 *   The other way we determine queue depth is through the use of the
 *   aic7xxx_tag_info array which is enabled by defining
 *   AIC7XXX_TAGGED_QUEUEING_BY_DEVICE.  This array can be initialized
 *   with queue depths for individual devices.  It also allows tagged
 *   queueing to be [en|dis]abled for a specific adapter.
 *-F*************************************************************************/
static void
aic7xxx_device_queue_depth(struct aic7xxx_host *p, Scsi_Device *device)
{
  int default_depth = 3;
  unsigned char tindex;
  unsigned short target_mask;

  tindex = device->id | (device->channel << 3);
  target_mask = (1 << tindex);

  device->queue_depth = default_depth;
  p->dev_mid_level_queue_depth[tindex] = 3;
  p->dev_temp_queue_depth[tindex] = 1;
  p->dev_max_queue_depth[tindex] = 1;
  p->tagenable &= ~target_mask;

  if (device->tagged_supported)
  {
    int tag_enabled = TRUE;

#ifdef AIC7XXX_CMDS_PER_LUN
    default_depth = AIC7XXX_CMDS_PER_LUN;
#else
    default_depth = 8;  /* Not many SCBs to work with. */
#endif
 
    if (!(p->discenable & target_mask))
    {
      if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
        printk(INFO_LEAD "Disconnection disabled, unable to "
             "enable tagged queueing.\n",
             p->host_no, device->channel, device->id, device->lun);
    }
    else
    {
      if (p->instance >= NUMBER(aic7xxx_tag_info))
      {
        static int print_warning = TRUE;
        if(print_warning)
        {
          printk(KERN_INFO "aic7xxx: WARNING, insufficient tag_info instances for"
                           " installed controllers.\n");
          printk(KERN_INFO "aic7xxx: Please update the aic7xxx_tag_info array in"
                           " the aic7xxx.c source file.\n");
          print_warning = FALSE;
        }
        device->queue_depth = default_depth;
      }
      else
      {

        if (aic7xxx_tag_info[p->instance].tag_commands[tindex] == 255)
        {
          tag_enabled = FALSE;
          device->queue_depth = 3;  /* Tagged queueing is disabled. */
        }
        else if (aic7xxx_tag_info[p->instance].tag_commands[tindex] == 0)
        {
          device->queue_depth = default_depth;
        }
        else
        {
          device->queue_depth =
            aic7xxx_tag_info[p->instance].tag_commands[tindex];
        }
      }
      if ((device->tagged_queue == 0) && tag_enabled)
      {
        if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
        {
              printk(INFO_LEAD "Enabled tagged queuing, queue depth %d.\n",
                p->host_no, device->channel, device->id,
                device->lun, device->queue_depth);
        }
        p->dev_max_queue_depth[tindex] = device->queue_depth;
        p->dev_temp_queue_depth[tindex] = device->queue_depth;
        p->dev_mid_level_queue_depth[tindex] = device->queue_depth;
        p->tagenable |= target_mask;
        p->orderedtag |= target_mask;
        device->tagged_queue = 1;
        device->current_tag = SCB_LIST_NULL;
      }
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_select_queue_depth
 *
 * Description:
 *   Sets the queue depth for each SCSI device hanging off the input
 *   host adapter.  We use a queue depth of 2 for devices that do not
 *   support tagged queueing.  If AIC7XXX_CMDS_PER_LUN is defined, we
 *   use that for tagged queueing devices; otherwise we use our own
 *   algorithm for determining the queue depth based on the maximum
 *   SCBs for the controller.
 *-F*************************************************************************/
static void
aic7xxx_select_queue_depth(struct Scsi_Host *host,
    Scsi_Device *scsi_devs)
{
  Scsi_Device *device;
  struct aic7xxx_host *p = (struct aic7xxx_host *) host->hostdata;
  int scbnum;

  scbnum = 0;
  for (device = scsi_devs; device != NULL; device = device->next)
  {
    if (device->host == host)
    {
      aic7xxx_device_queue_depth(p, device);
      scbnum += device->queue_depth;
    }
  }
  while (scbnum > p->scb_data->numscbs)
  {
    /*
     * Pre-allocate the needed SCBs to get around the possibility of having
     * to allocate some when memory is more or less exhausted and we need
     * the SCB in order to perform a swap operation (possible deadlock)
     */
    if ( aic7xxx_allocate_scb(p) == 0 )
      return;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_probe
 *
 * Description:
 *   Probing for EISA boards: it looks like the first two bytes
 *   are a manufacturer code - three characters, five bits each:
 *
 *               BYTE 0   BYTE 1   BYTE 2   BYTE 3
 *              ?1111122 22233333 PPPPPPPP RRRRRRRR
 *
 *   The characters are baselined off ASCII '@', so add that value
 *   to each to get the real ASCII code for it. The next two bytes
 *   appear to be a product and revision number, probably vendor-
 *   specific. This is what is being searched for at each port,
 *   and what should probably correspond to the ID= field in the
 *   ECU's .cfg file for the card - if your card is not detected,
 *   make sure your signature is listed in the array.
 *
 *   The fourth byte's lowest bit seems to be an enabled/disabled
 *   flag (rest of the bits are reserved?).
 *
 * NOTE:  This function is only needed on Intel and Alpha platforms,
 *   the other platforms we support don't have EISA/VLB busses.  So,
 *   we #ifdef this entire function to avoid compiler warnings about
 *   an unused function.
 *-F*************************************************************************/
#if defined(__i386__) || defined(__alpha__)
static int
aic7xxx_probe(int slot, int base, ahc_flag_type *flags)
{
  int i;
  unsigned char buf[4];

  static struct {
    int n;
    unsigned char signature[sizeof(buf)];
    ahc_chip type;
    int bios_disabled;
  } AIC7xxx[] = {
    { 4, { 0x04, 0x90, 0x77, 0x70 },
      AHC_AIC7770|AHC_EISA, FALSE },  /* mb 7770  */
    { 4, { 0x04, 0x90, 0x77, 0x71 },
      AHC_AIC7770|AHC_EISA, FALSE }, /* host adapter 274x */
    { 4, { 0x04, 0x90, 0x77, 0x56 },
      AHC_AIC7770|AHC_VL, FALSE }, /* 284x BIOS enabled */
    { 4, { 0x04, 0x90, 0x77, 0x57 },
      AHC_AIC7770|AHC_VL, TRUE }   /* 284x BIOS disabled */
  };

  /*
   * The VL-bus cards need to be primed by
   * writing before a signature check.
   */
  for (i = 0; i < sizeof(buf); i++)
  {
    outb(0x80 + i, base);
    buf[i] = inb(base + i);
  }

  for (i = 0; i < NUMBER(AIC7xxx); i++)
  {
    /*
     * Signature match on enabled card?
     */
    if (!memcmp(buf, AIC7xxx[i].signature, AIC7xxx[i].n))
    {
      if (inb(base + 4) & 1)
      {
        if (AIC7xxx[i].bios_disabled)
        {
          *flags |= AHC_USEDEFAULTS;
        }
        else
        {
          *flags |= AHC_BIOS_ENABLED;
        }
        return (i);
      }

      printk("aic7xxx: <Adaptec 7770 SCSI Host Adapter> "
             "disabled at slot %d, ignored.\n", slot);
    }
  }

  return (-1);
}
#endif /* (__i386__) || (__alpha__) */


/*+F*************************************************************************
 * Function:
 *   read_2840_seeprom
 *
 * Description:
 *   Reads the 2840 serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   See read_seeprom (for the 2940) for the instruction set of the 93C46
 *   chip.
 *
 *   The 2840 interface to the 93C46 serial EEPROM is through the
 *   STATUS_2840 and SEECTL_2840 registers.  The CS_2840, CK_2840, and
 *   DO_2840 bits of the SEECTL_2840 register are connected to the chip
 *   select, clock, and data out lines respectively of the serial EEPROM.
 *   The DI_2840 bit of the STATUS_2840 is connected to the data in line
 *   of the serial EEPROM.  The EEPROM_TF bit of STATUS_2840 register is
 *   useful in that it gives us an 800 nsec timer.  After a read from the
 *   SEECTL_2840 register the timing flag is cleared and goes high 800 nsec
 *   later.
 *-F*************************************************************************/
static int
read_284x_seeprom(struct aic7xxx_host *p, struct seeprom_config *sc)
{
  int i = 0, k = 0;
  unsigned char temp;
  unsigned short checksum = 0;
  unsigned short *seeprom = (unsigned short *) sc;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((aic_inb(p, STATUS_2840) & EEPROM_TF) == 0)        \
  {                                                \
    ;  /* Do nothing */                                \
  }                                                \
  (void) aic_inb(p, SEECTL_2840);

  /*
   * Read the first 32 registers of the seeprom.  For the 2840,
   * the 93C46 SEEPROM is a 1024-bit device with 64 16-bit registers
   * but only the first 32 are used by Adaptec BIOS.  The loop
   * will range from 0 to 31.
   */
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    aic_outb(p, CK_2840 | CS_2840, SEECTL_2840);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = CS_2840 | seeprom_read.bits[i];
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 bit address (MSB first, LSB last).
     */
    for (i = 5; i >= 0; i--)
    {
      temp = k;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = CS_2840 | temp;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = CS_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      seeprom[k] = (seeprom[k] << 1) | (aic_inb(p, STATUS_2840) & DI_2840);
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }
    /*
     * The serial EEPROM has a checksum in the last word.  Keep a
     * running checksum for all words read except for the last
     * word.  We'll verify the checksum after all words have been
     * read.
     */
    if (k < (sizeof(*sc) / 2) - 1)
    {
      checksum = checksum + seeprom[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    aic_outb(p, 0, SEECTL_2840);
    CLOCK_PULSE(p);
    aic_outb(p, CK_2840, SEECTL_2840);
    CLOCK_PULSE(p);
    aic_outb(p, 0, SEECTL_2840);
    CLOCK_PULSE(p);
  }

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n", checksum, sc->checksum);
  printk("Serial EEPROM:");
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", seeprom[k]);
  }
  printk("\n");
#endif

  if (checksum != sc->checksum)
  {
    printk("aic7xxx: SEEPROM checksum error, ignoring SEEPROM settings.\n");
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

/*+F*************************************************************************
 * Function:
 *   acquire_seeprom
 *
 * Description:
 *   Acquires access to the memory port on PCI controllers.
 *-F*************************************************************************/
static int
acquire_seeprom(struct aic7xxx_host *p)
{
  int wait;

  /*
   * Request access of the memory port.  When access is
   * granted, SEERDY will go high.  We use a 1 second
   * timeout which should be near 1 second more than
   * is needed.  Reason: after the 7870 chip reset, there
   * should be no contention.
   */
  aic_outb(p, SEEMS, SEECTL);
  wait = 1000;  /* 1000 msec = 1 second */
  while ((wait > 0) && ((aic_inb(p, SEECTL) & SEERDY) == 0))
  {
    wait--;
    mdelay(1);  /* 1 msec */
  }
  if ((aic_inb(p, SEECTL) & SEERDY) == 0)
  {
    aic_outb(p, 0, SEECTL);
    return (0);
  }
  return (1);
}

/*+F*************************************************************************
 * Function:
 *   release_seeprom
 *
 * Description:
 *   Releases access to the memory port on PCI controllers.
 *-F*************************************************************************/
static void
release_seeprom(struct aic7xxx_host *p)
{
  aic_outb(p, 0, SEECTL);
}

/*+F*************************************************************************
 * Function:
 *   read_seeprom
 *
 * Description:
 *   Reads the serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   The instruction set of the 93C46/56/66 chips is as follows:
 *
 *               Start  OP
 *     Function   Bit  Code  Address    Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must precede
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
 *     *Note: The 93C56 and 93C66 have 8 address bits.
 * 
 *
 *   The 93C46 has a four wire interface: clock, chip select, data in, and
 *   data out.  In order to perform one of the above functions, you need
 *   to enable the chip select for a clock period (typically a minimum of
 *   1 usec, with the clock high and low a minimum of 750 and 250 nsec
 *   respectively.  While the chip select remains high, you can clock in
 *   the instructions (above) starting with the start bit, followed by the
 *   OP code, Address, and Data (if needed).  For the READ instruction, the
 *   requested 16-bit register contents is read from the data out line but
 *   is preceded by an initial zero (leading 0, followed by 16-bits, MSB
 *   first).  The clock cycling from low to high initiates the next data
 *   bit to be sent from the chip.
 *
 *   The 78xx interface to the 93C46 serial EEPROM is through the SEECTL
 *   register.  After successful arbitration for the memory port, the
 *   SEECS bit of the SEECTL register is connected to the chip select.
 *   The SEECK, SEEDO, and SEEDI are connected to the clock, data out,
 *   and data in lines respectively.  The SEERDY bit of SEECTL is useful
 *   in that it gives us an 800 nsec timer.  After a write to the SEECTL
 *   register, the SEERDY goes high 800 nsec later.  The one exception
 *   to this is when we first request access to the memory port.  The
 *   SEERDY goes high to signify that access has been granted and, for
 *   this case, has no implied timing.
 *-F*************************************************************************/
static int
read_seeprom(struct aic7xxx_host *p, int offset, 
    unsigned short *scarray, unsigned int len, seeprom_chip_type chip)
{
  int i = 0, k;
  unsigned char temp;
  unsigned short checksum = 0;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((aic_inb(p, SEECTL) & SEERDY) == 0)        \
  {                                                \
    ;  /* Do nothing */                                \
  }

  /*
   * Request access of the memory port.
   */
  if (acquire_seeprom(p) == 0)
  {
    return (0);
  }

  /*
   * Read 'len' registers of the seeprom.  For the 7870, the 93C46
   * SEEPROM is a 1024-bit device with 64 16-bit registers but only
   * the first 32 are used by Adaptec BIOS.  Some adapters use the
   * 93C56 SEEPROM which is a 2048-bit device.  The loop will range
   * from 0 to 'len' - 1.
   */
  for (k = 0; k < len; k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    aic_outb(p, SEEMS | SEECK | SEECS, SEECTL);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = SEEMS | SEECS | (seeprom_read.bits[i] << 1);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 or 8 bit address (MSB first, LSB last).
     */
    for (i = ((int) chip - 1); i >= 0; i--)
    {
      temp = k + offset;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = SEEMS | SEECS | (temp << 1);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = SEEMS | SEECS;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      scarray[k] = (scarray[k] << 1) | (aic_inb(p, SEECTL) & SEEDI);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }

    /*
     * The serial EEPROM should have a checksum in the last word.
     * Keep a running checksum for all words read except for the
     * last word.  We'll verify the checksum after all words have
     * been read.
     */
    if (k < (len - 1))
    {
      checksum = checksum + scarray[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    aic_outb(p, SEEMS, SEECTL);
    CLOCK_PULSE(p);
    aic_outb(p, SEEMS | SEECK, SEECTL);
    CLOCK_PULSE(p);
    aic_outb(p, SEEMS, SEECTL);
    CLOCK_PULSE(p);
  }

  /*
   * Release access to the memory port and the serial EEPROM.
   */
  release_seeprom(p);

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n",
         checksum, scarray[len - 1]);
  printk("Serial EEPROM:");
  for (k = 0; k < len; k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", scarray[k]);
  }
  printk("\n");
#endif
  if (checksum != scarray[len - 1])
  {
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

/*+F*************************************************************************
 * Function:
 *   write_brdctl
 *
 * Description:
 *   Writes a value to the BRDCTL register.
 *-F*************************************************************************/
static void
write_brdctl(struct aic7xxx_host *p, unsigned char value)
{
  unsigned char brdctl;

  if ((p->chip & AHC_CHIPID_MASK) == AHC_AIC7895)
  {
    brdctl = BRDSTB;
    if (p->flags & AHC_CHNLB)
      brdctl |= BRDCS;
  }
  else if (p->features & AHC_ULTRA2)
    brdctl = 0;
  else
    brdctl = BRDSTB | BRDCS;
  aic_outb(p, brdctl, BRDCTL);
  udelay(1);
  brdctl |= value;
  aic_outb(p, brdctl, BRDCTL);
  udelay(1);
  if (p->features & AHC_ULTRA2)
    brdctl |= BRDSTB_ULTRA2;
  else
    brdctl &= ~BRDSTB;
  aic_outb(p, brdctl, BRDCTL);
  udelay(1);
  if (p->features & AHC_ULTRA2)
    brdctl = 0;
  else
    brdctl &= ~BRDCS;
  aic_outb(p, brdctl, BRDCTL);
  udelay(1);
}

/*+F*************************************************************************
 * Function:
 *   read_brdctl
 *
 * Description:
 *   Reads the BRDCTL register.
 *-F*************************************************************************/
static unsigned char
read_brdctl(struct aic7xxx_host *p)
{
  unsigned char brdctl, value;

  if ((p->chip & AHC_CHIPID_MASK) == AHC_AIC7895)
  {
    brdctl = BRDRW;
    if (p->flags & AHC_CHNLB)
      brdctl |= BRDCS;
  }
  else if (p->features & AHC_ULTRA2)
    brdctl = BRDRW_ULTRA2;
  else
    brdctl = BRDRW | BRDCS;
  aic_outb(p, brdctl, BRDCTL);
  udelay(1);
  value = aic_inb(p, BRDCTL);
  aic_outb(p, 0, BRDCTL);
  udelay(1);
  return (value);
}

/*+F*************************************************************************
 * Function:
 *   aic785x_cable_detect
 *
 * Description:
 *   Detect the cables that are present on aic785x class controller chips
 *-F*************************************************************************/
static void
aic785x_cable_detect(struct aic7xxx_host *p, int *int_50,
    int *ext_present, int *eeprom)
{
  unsigned char brdctl;

  aic_outb(p, BRDRW | BRDCS, BRDCTL);
  udelay(1);
  aic_outb(p, 0, BRDCTL);
  udelay(1);
  brdctl = aic_inb(p, BRDCTL);
  udelay(1);
  *int_50 = !(brdctl & BRDDAT5);
  *ext_present = !(brdctl & BRDDAT6);
  *eeprom = (aic_inb(p, SPIOCAP) & EEPROM);
}

/*+F*************************************************************************
 * Function:
 *   aic787x_cable_detect
 *
 * Description:
 *   Detect the cables that are present on aic787x class controller chips
 *
 * NOTE: This functions assumes the SEEPROM will have already been aquired
 *       prior to invocation of this function.
 *-F*************************************************************************/
static void
aic787x_cable_detect(struct aic7xxx_host *p, int *int_50, int *int_68,
    int *ext_present, int *eeprom)
{
  unsigned char brdctl;

  /*
   * First read the status of our cables.  Set the rom bank to
   * 0 since the bank setting serves as a multiplexor for the
   * cable detection logic.  BRDDAT5 controls the bank switch.
   */
  write_brdctl(p, 0);

  /*
   * Now we read the state of the two internal connectors.  BRDDAT6
   * is internal 50, BRDDAT7 is internal 68.  For each, the cable is
   * present if the bit is 0
   */
  brdctl = read_brdctl(p);
  *int_50 = !(brdctl & BRDDAT6);
  *int_68 = !(brdctl & BRDDAT7);

  /*
   * Set the bank bit in brdctl and then read the external cable state
   * and the EEPROM status
   */
  write_brdctl(p, BRDDAT5);
  brdctl = read_brdctl(p);

  *ext_present = !(brdctl & BRDDAT6);
  *eeprom = !(brdctl & BRDDAT7);

  /*
   * We're done, the calling function will release the SEEPROM for us
   */
}

/*+F*************************************************************************
 * Function:
 *   aic787x_ultra2_term_detect
 *
 * Description:
 *   Detect the termination settings present on ultra2 class controllers
 *
 * NOTE: This functions assumes the SEEPROM will have already been aquired
 *       prior to invocation of this function.
 *-F*************************************************************************/
static void
aic7xxx_ultra2_term_detect(struct aic7xxx_host *p, int *enableSE_low,
                           int *enableSE_high, int *enableLVD_low,
                           int *enableLVD_high, int *eprom_present)
{
  unsigned char brdctl;

  brdctl = read_brdctl(p);

  *eprom_present  = (brdctl & BRDDAT7);
  *enableSE_high  = (brdctl & BRDDAT6);
  *enableSE_low   = (brdctl & BRDDAT5);
  *enableLVD_high = (brdctl & BRDDAT4);
  *enableLVD_low  = (brdctl & BRDDAT3);
}

/*+F*************************************************************************
 * Function:
 *   configure_termination
 *
 * Description:
 *   Configures the termination settings on PCI adapters that have
 *   SEEPROMs available.
 *-F*************************************************************************/
static void
configure_termination(struct aic7xxx_host *p)
{
  int internal50_present = 0;
  int internal68_present = 0;
  int external_present = 0;
  int eprom_present = 0;
  int enableSE_low = 0;
  int enableSE_high = 0;
  int enableLVD_low = 0;
  int enableLVD_high = 0;
  unsigned char brddat = 0;
  unsigned char max_target = 0;
  unsigned char sxfrctl1 = aic_inb(p, SXFRCTL1);

  if (acquire_seeprom(p))
  {
    if (p->features & (AHC_WIDE|AHC_TWIN))
      max_target = 16;
    else
      max_target = 8;
    aic_outb(p, SEEMS | SEECS, SEECTL);
    sxfrctl1 &= ~STPWEN;
    if ( (p->adapter_control & CFAUTOTERM) ||
         (p->features & AHC_ULTRA2) )
    {
      if ( (p->adapter_control & CFAUTOTERM) && !(p->features & AHC_ULTRA2) )
      {
        printk(KERN_INFO "(scsi%d) Warning - detected auto-termination\n",
               p->host_no);
        printk(KERN_INFO "(scsi%d) Please verify driver detected settings are "
          "correct.\n", p->host_no);
        printk(KERN_INFO "(scsi%d) If not, then please properly set the device "
          "termination\n", p->host_no);
        printk(KERN_INFO "(scsi%d) in the Adaptec SCSI BIOS by hitting CTRL-A "
          "when prompted\n", p->host_no);
        printk(KERN_INFO "(scsi%d) during machine bootup.\n", p->host_no);
      }
      /* Configure auto termination. */

      if (p->features & AHC_ULTRA2)
      {
        if (aic7xxx_override_term == -1)
          aic7xxx_ultra2_term_detect(p, &enableSE_low, &enableSE_high,
                                     &enableLVD_low, &enableLVD_high,
                                     &eprom_present);
        if (!(p->adapter_control & CFSEAUTOTERM))
        {
          enableSE_low = (p->adapter_control & CFSTERM);
          enableSE_high = (p->adapter_control & CFWSTERM);
        }
        if (!(p->adapter_control & CFAUTOTERM))
        {
          enableLVD_low = enableLVD_high = (p->adapter_control & CFLVDSTERM);
        }
        internal50_present = 0;
        internal68_present = 1;
        external_present = 1;
      }
      else if ( (p->chip & AHC_CHIPID_MASK) >= AHC_AIC7870 )
      {
        aic787x_cable_detect(p, &internal50_present, &internal68_present,
          &external_present, &eprom_present);
      }
      else
      {
        aic785x_cable_detect(p, &internal50_present, &external_present,
          &eprom_present);
      }
      
      if (max_target <= 8)
        internal68_present = 0;

      if ( !(p->features & AHC_ULTRA2) )
      {
        if (max_target > 8)
        {
          printk(KERN_INFO "(scsi%d) Cables present (Int-50 %s, Int-68 %s, "
                 "Ext-68 %s)\n", p->host_no,
                 internal50_present ? "YES" : "NO",
                 internal68_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
        else
        {
          printk(KERN_INFO "(scsi%d) Cables present (Int-50 %s, Ext-50 %s)\n",
                 p->host_no,
                 internal50_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
      }
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(KERN_INFO "(scsi%d) EEPROM %s present.\n", p->host_no,
             eprom_present ? "is" : "is not");

      /*
       * Now set the termination based on what we found.  BRDDAT6
       * controls wide termination enable.
       * Flash Enable = BRDDAT7
       * SE High Term Enable = BRDDAT6
       * SE Low Term Enable = BRDDAT5 (7890)
       * LVD High Term Enable = BRDDAT4 (7890)
       */
      if ( !(p->features & AHC_ULTRA2) &&
           (internal50_present && internal68_present && external_present) )
      {
        printk(KERN_INFO "(scsi%d) Illegal cable configuration!!  Only two\n",
               p->host_no);
        printk(KERN_INFO "(scsi%d) connectors on the SCSI controller may be "
               "in use at a time!\n", p->host_no);
        /*
         * Force termination (low and high byte) on.  This is safer than
         * leaving it completely off, especially since this message comes
         * most often from motherboard controllers that don't even have 3
         * connectors, but instead are failing the cable detection.
         */
        internal50_present = external_present = 0;
        enableSE_high = enableSE_low = 1;
      }

      if ((max_target > 8) &&
          ((external_present == 0) || (internal68_present == 0) ||
           (enableSE_high != 0)))
      {
        brddat |= BRDDAT6;
        p->flags |= AHC_TERM_ENB_SE_HIGH;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) SE High byte termination Enabled\n",
                 p->host_no);
      }

      if ( (((internal50_present ? 1 : 0) +
             (internal68_present ? 1 : 0) +
             (external_present   ? 1 : 0)) <= 1) ||
           (enableSE_low != 0) )
      {
        if (p->features & AHC_ULTRA2)
          brddat |= BRDDAT5;
        else
          sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_SE_LOW;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) SE Low byte termination Enabled\n",
                 p->host_no);
      }

      if (enableLVD_low != 0)
      {
        sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_LVD;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) LVD Low byte termination Enabled\n",
                 p->host_no);
      }
          
      if (enableLVD_high != 0)
      {
        brddat |= BRDDAT4;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) LVD High byte termination Enabled\n",
                 p->host_no);
      }
    }
    else
    {
      if (p->adapter_control & CFSTERM)
      {
        if (p->features & AHC_ULTRA2)
          brddat |= BRDDAT5;
        else
          sxfrctl1 |= STPWEN;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) SE Low byte termination Enabled\n",
                 p->host_no);
      }

      if (p->adapter_control & CFWSTERM)
      {
        brddat |= BRDDAT6;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) SE High byte termination Enabled\n",
                 p->host_no);
      }
    }
    write_brdctl(p, brddat);
    release_seeprom(p);
    aic_outb(p, sxfrctl1, SXFRCTL1);
  }
}

/*+F*************************************************************************
 * Function:
 *   detect_maxscb
 *
 * Description:
 *   Detects the maximum number of SCBs for the controller and returns
 *   the count and a mask in p (p->maxscbs, p->qcntmask).
 *-F*************************************************************************/
static void
detect_maxscb(struct aic7xxx_host *p)
{
  int i;

  /*
   * It's possible that we've already done this for multichannel
   * adapters.
   */
  if (p->scb_data->maxhscbs == 0)
  {
    /*
     * We haven't initialized the SCB settings yet.  Walk the SCBs to
     * determince how many there are.
     */
    aic_outb(p, 0, FREE_SCBH);

    for (i = 0; i < AIC7XXX_MAXSCB; i++)
    {
      aic_outb(p, i, SCBPTR);
      aic_outb(p, i, SCB_CONTROL);
      if (aic_inb(p, SCB_CONTROL) != i)
        break;
      aic_outb(p, 0, SCBPTR);
      if (aic_inb(p, SCB_CONTROL) != 0)
        break;

      aic_outb(p, i, SCBPTR);
      aic_outb(p, 0, SCB_CONTROL);   /* Clear the control byte. */
      aic_outb(p, i + 1, SCB_NEXT);  /* Set the next pointer. */
      aic_outb(p, i - 1, SCB_PREV);  /* Set the prev pointer. */
      aic_outb(p, SCB_LIST_NULL, SCB_TAG);  /* Make the tag invalid. */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS);  /* no busy untagged */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+1);/* targets active yet */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+2);
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+3);
    }

    /* Make sure the last SCB terminates the free list. */
    aic_outb(p, i - 1, SCBPTR);
    aic_outb(p, SCB_LIST_NULL, SCB_NEXT);

    /* Ensure we clear the first (0) SCBs control byte. */
    aic_outb(p, 0, SCBPTR);
    aic_outb(p, 0, SCB_CONTROL);

    p->scb_data->maxhscbs = i;
    /*
     * Use direct indexing instead for speed
     */
    if ( i == AIC7XXX_MAXSCB )
      p->flags &= ~AHC_PAGESCBS;
  }

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_register
 *
 * Description:
 *   Register a Adaptec aic7xxx chip SCSI controller with the kernel.
 *-F*************************************************************************/
static int
aic7xxx_register(Scsi_Host_Template *template, struct aic7xxx_host *p,
  int reset_delay)
{
  int i, result;
  int max_targets;
  int found = 1;
  unsigned char term, scsi_conf;
  struct Scsi_Host *host;

  /*
   * Lock out other contenders for our i/o space.
   */
  request_region(p->base, MAXREG - MINREG, "aic7xxx");


  host = p->host;

  p->scb_data->maxscbs = AIC7XXX_MAXSCB;
  host->can_queue = AIC7XXX_MAXSCB;
  host->cmd_per_lun = 3;
  host->sg_tablesize = AIC7XXX_MAX_SG;
  host->select_queue_depths = aic7xxx_select_queue_depth;
  host->this_id = p->scsi_id;
  host->io_port = p->base;
  host->n_io_port = 0xFF;
  host->base = (unsigned char *) p->mbase;
  host->irq = p->irq;
  if (p->features & AHC_WIDE)
  {
    host->max_id = 16;
  }
  if (p->features & AHC_TWIN)
  {
    host->max_channel = 1;
  }

  p->host = host;
  p->last_reset = jiffies;
  p->host_no = host->host_no;
  host->unique_id = p->instance;
  p->isr_count = 0;
  p->next = NULL;
  p->completeq.head = NULL;
  p->completeq.tail = NULL;
  scbq_init(&p->scb_data->free_scbs);
  scbq_init(&p->waiting_scbs);

  for (i = 0; i < NUMBER(p->untagged_scbs); i++)
  {
    p->untagged_scbs[i] = SCB_LIST_NULL;
    p->qinfifo[i] = SCB_LIST_NULL;
    p->qoutfifo[i] = SCB_LIST_NULL;
  }
  /*
   * We currently have no commands of any type
   */
  p->qinfifonext = 0;
  p->qoutfifonext = 0;

  for (i = 0; i < MAX_TARGETS; i++)
  {
    p->dev_commands_sent[i] = 0;
    p->dev_flags[i] = 0;
    p->dev_active_cmds[i] = 0;
    p->dev_last_reset[i] = jiffies;
    p->dev_last_queue_full[i] = 0;
    p->dev_last_queue_full_count[i] = 0;
    p->dev_max_queue_depth[i] = 1;
    p->dev_temp_queue_depth[i] = 1;
    p->dev_mid_level_queue_depth[i] = 3;
    scbq_init(&p->delayed_scbs[i]);
    init_timer(&p->dev_timer[i]);
    p->dev_timer[i].data = (unsigned long)p;
    p->dev_timer[i].function = (void *)aic7xxx_timer;
  }

  printk(KERN_INFO "(scsi%d) <%s> found at ", p->host_no,
    board_names[p->board_name_index]);
  switch(p->chip)
  {
    case (AHC_AIC7770|AHC_EISA):
      printk("EISA slot %d\n", p->pci_device_fn);
      break;
    case (AHC_AIC7770|AHC_VL):
      printk("VLB slot %d\n", p->pci_device_fn);
      break;
    default:
      printk("PCI %d/%d\n", PCI_SLOT(p->pci_device_fn),
        PCI_FUNC(p->pci_device_fn));
      break;
  }
  if (p->features & AHC_TWIN)
  {
    printk(KERN_INFO "(scsi%d) Twin Channel, A SCSI ID %d, B SCSI ID %d, ",
           p->host_no, p->scsi_id, p->scsi_id_b);
  }
  else
  {
    char *channel;

    channel = "";

    if ((p->flags & AHC_MULTI_CHANNEL) != 0)
    {
      channel = " A";

      if ( (p->flags & (AHC_CHNLB|AHC_CHNLC)) != 0 )
      {
        channel = (p->flags & AHC_CHNLB) ? " B" : " C";
      }
    }
    if (p->features & AHC_WIDE)
    {
      printk(KERN_INFO "(scsi%d) Wide ", p->host_no);
    }
    else
    {
      printk(KERN_INFO "(scsi%d) Narrow ", p->host_no);
    }
    printk("Channel%s, SCSI ID=%d, ", channel, p->scsi_id);
  }
  aic_outb(p, 0, SEQ_FLAGS);

  /*
   * Detect SCB parameters and initialize the SCB array.
   */
  detect_maxscb(p);
  printk("%d/%d SCBs\n", p->scb_data->maxhscbs, p->scb_data->maxscbs);
  if (aic7xxx_verbose & VERBOSE_PROBE2)
  {
    printk(KERN_INFO "(scsi%d) BIOS %sabled, IO Port 0x%lx, IRQ %d\n",
      p->host_no, (p->flags & AHC_BIOS_ENABLED) ? "en" : "dis",
      p->base, p->irq);
    printk(KERN_INFO "(scsi%d) IO Memory at 0x%lx, MMAP Memory at 0x%lx\n",
      p->host_no, p->mbase, (unsigned long)p->maddr);
  }

#ifdef CONFIG_PCI
  /*
   * Now that we know our instance number, we can set the flags we need to
   * force termination if need be.
   */
  if (aic7xxx_stpwlev != -1)
  {
    /*
     * This option only applies to PCI controllers.
     */
    if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI)
    {
      unsigned char devconfig;

#if LINUX_KERNEL_VERSION > KERNEL_VERSION(2,1,92)
      pci_read_config_byte(p->pdev, DEVCONFIG, &devconfig);
#else
      pcibios_read_config_byte(p->pci_bus, p->pci_device_fn,
                               DEVCONFIG, &devconfig);
#endif
      if ( (aic7xxx_stpwlev >> p->instance) & 0x01 )
      {
        devconfig |= 0x02;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("(scsi%d) Force setting STPWLEV bit\n", p->host_no);
      }
      else
      {
        devconfig &= ~0x02;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("(scsi%d) Force clearing STPWLEV bit\n", p->host_no);
      }
#if LINUX_KERNEL_VERSION > KERNEL_VERSION(2,1,92)
      pci_write_config_byte(p->pdev, DEVCONFIG, devconfig);
#else
      pcibios_write_config_byte(p->pci_bus, p->pci_device_fn,
                                DEVCONFIG, devconfig);
#endif
    }
  }
#endif

  /*
   * That took care of devconfig and stpwlev, now for the actual termination
   * settings.
   */
  if (aic7xxx_override_term != -1)
  {
    /*
     * Again, this only applies to PCI controllers.  We don't have problems
     * with the termination on 274x controllers to the best of my knowledge.
     */
    if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI)
    {
      unsigned char term_override;

      term_override = ( (aic7xxx_override_term >> (p->instance * 4)) & 0x0f);
      p->adapter_control &= 
        ~(CFSTERM|CFWSTERM|CFLVDSTERM|CFAUTOTERM|CFSEAUTOTERM);
      if ( (p->features & AHC_ULTRA2) && (term_override & 0x0c) )
      {
        p->adapter_control |= CFLVDSTERM;
      }
      if (term_override & 0x02)
      {
        p->adapter_control |= CFWSTERM;
      }
      if (term_override & 0x01)
      {
        p->adapter_control |= CFSTERM;
      }
    }
  }

  if ( (p->flags & AHC_SEEPROM_FOUND) || (aic7xxx_override_term != -1) )
  {
    if (p->features & AHC_SPIOCAP)
    {
      if ( aic_inb(p, SPIOCAP) & SSPIOCPS )
      /*
       * Update the settings in sxfrctl1 to match the termination
       * settings.
       */
        configure_termination(p);
    }
    else if ((p->chip & AHC_CHIPID_MASK) >= AHC_AIC7870)
    {
      configure_termination(p);
    }
  }

  /*
   * Clear out any possible pending interrupts.
   */
  aic7xxx_clear_intstat(p);

  /*
   * Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1, for both channels
   */
  if (p->features & AHC_TWIN)
  {
    /* Select channel B */
    aic_outb(p, aic_inb(p, SBLKCTL) | SELBUSB, SBLKCTL);

    term = ((p->flags & AHC_TERM_ENB_B) != 0) ? STPWEN : 0;
    aic_outb(p, p->scsi_id_b, SCSIID);
    scsi_conf = aic_inb(p, SCSICONF + 1);
    aic_outb(p, DFON | SPIOEN, SXFRCTL0);
    aic_outb(p, (scsi_conf & ENSPCHK) | term | 
         ENSTIMER | ACTNEGEN, SXFRCTL1);
    aic_outb(p, 0, SIMODE0);
    aic_outb(p, ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1);
    aic_outb(p, 0, SCSIRATE);

    /* Select channel A */
    aic_outb(p, aic_inb(p, SBLKCTL) & ~SELBUSB, SBLKCTL);
  }

  term = ((p->flags & AHC_TERM_ENB_SE_LOW) != 0) ? STPWEN : 0;
  if (p->features & AHC_ULTRA2)
    aic_outb(p, p->scsi_id, SCSIID_ULTRA2);
  else
    aic_outb(p, p->scsi_id, SCSIID);
  scsi_conf = aic_inb(p, SCSICONF);
  aic_outb(p, DFON | SPIOEN, SXFRCTL0);
  aic_outb(p, (scsi_conf & ENSPCHK) | term | 
       ENSTIMER | ACTNEGEN, SXFRCTL1);
  aic_outb(p, 0, SIMODE0);
  aic_outb(p, ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1);
  aic_outb(p, 0, SCSIRATE);
  if ( p->features & AHC_ULTRA2)
    aic_outb(p, 0, SCSIOFFSET);

  /*
   * Look at the information that board initialization or the board
   * BIOS has left us. In the lower four bits of each target's
   * scratch space any value other than 0 indicates that we should
   * initiate synchronous transfers. If it's zero, the user or the
   * BIOS has decided to disable synchronous negotiation to that
   * target so we don't activate the needsdtr flag.
   */
  if ((p->features & (AHC_TWIN|AHC_WIDE)) == 0)
  {
    max_targets = 8;
  }
  else
  {
    max_targets = 16;
  }

  if (!(aic7xxx_no_reset))
  {
    /*
     * If we reset the bus, then clear the transfer settings, else leave
     * them be
     */
    for (i = 0; i < max_targets; i++)
    {
      aic_outb(p, 0, TARG_SCSIRATE + i);
      if (p->features & AHC_ULTRA2)
      {
        aic_outb(p, 0, TARG_OFFSET + i);
      }
      p->transinfo[i].cur_offset = 0;
      p->transinfo[i].cur_period = 0;
      p->transinfo[i].cur_width = MSG_EXT_WDTR_BUS_8_BIT;
    }

    /*
     * If we reset the bus, then clear the transfer settings, else leave
     * them be.
     */
    aic_outb(p, 0, ULTRA_ENB);
    aic_outb(p, 0, ULTRA_ENB + 1);
    p->ultraenb = 0;
  }

  /*
   * Allocate enough hardware scbs to handle the maximum number of
   * concurrent transactions we can have.  We have to make sure that
   * the allocated memory is contiguous memory.  The Linux kmalloc
   * routine should only allocate contiguous memory, but note that
   * this could be a problem if kmalloc() is changed.
   */
  {
    size_t array_size;
    unsigned int hscb_physaddr;
    unsigned long temp;

    array_size = p->scb_data->maxscbs * sizeof(struct aic7xxx_hwscb);
    if (p->scb_data->hscbs == NULL)
    {
      /*
       * A little padding so we can align thing the way we want
       */
      p->scb_data->hscbs = kmalloc(array_size + 0x1f, GFP_ATOMIC);
    }
    if (p->scb_data->hscbs == NULL)
    {
      printk("(scsi%d) Unable to allocate hardware SCB array; "
             "failing detection.\n", p->host_no);
      p->irq = 0;
      return(0);
    }
    /*
     * Save the actual kmalloc buffer pointer off, then align our
     * buffer to a 32 byte boundary
     */
    p->scb_data->hscb_kmalloc_ptr = p->scb_data->hscbs;
    temp = (unsigned long)p->scb_data->hscbs;
    temp += 0x1f;
    temp &= ~0x1f;
    p->scb_data->hscbs = (struct aic7xxx_hwscb *)temp;
    /* At least the control byte of each SCB needs to be 0. */
    memset(p->scb_data->hscbs, 0, array_size);

    /* Tell the sequencer where it can find the hardware SCB array. */
    hscb_physaddr = VIRT_TO_BUS(p->scb_data->hscbs);
    aic_outb(p, hscb_physaddr & 0xFF, HSCB_ADDR);
    aic_outb(p, (hscb_physaddr >> 8) & 0xFF, HSCB_ADDR + 1);
    aic_outb(p, (hscb_physaddr >> 16) & 0xFF, HSCB_ADDR + 2);
    aic_outb(p, (hscb_physaddr >> 24) & 0xFF, HSCB_ADDR + 3);

    /* Set up the fifo areas at the same time */
    hscb_physaddr = VIRT_TO_BUS(&p->untagged_scbs[0]);
    aic_outb(p, hscb_physaddr & 0xFF, SCBID_ADDR);
    aic_outb(p, (hscb_physaddr >> 8) & 0xFF, SCBID_ADDR + 1);
    aic_outb(p, (hscb_physaddr >> 16) & 0xFF, SCBID_ADDR + 2);
    aic_outb(p, (hscb_physaddr >> 24) & 0xFF, SCBID_ADDR + 3);
  }

  /* The Q-FIFOs we just set up are all empty */
  aic_outb(p, 0, QINPOS);
  aic_outb(p, 0, KERNEL_QINPOS);
  aic_outb(p, 0, QOUTPOS);

  if(p->features & AHC_QUEUE_REGS)
  {
    aic_outb(p, SCB_QSIZE_256, QOFF_CTLSTA);
    aic_outb(p, 0, SDSCB_QOFF);
    aic_outb(p, 0, SNSCB_QOFF);
    aic_outb(p, 0, HNSCB_QOFF);
  }

  /*
   * We don't have any waiting selections or disconnected SCBs.
   */
  aic_outb(p, SCB_LIST_NULL, WAITING_SCBH);
  aic_outb(p, SCB_LIST_NULL, DISCONNECTED_SCBH);

  /*
   * Message out buffer starts empty
   */
  aic_outb(p, MSG_NOOP, MSG_OUT);
  aic_outb(p, MSG_NOOP, LAST_MSG);

  /*
   * Set all the other asundry items that haven't been set yet.
   * This includes just dumping init values to a lot of registers simply
   * to make sure they've been touched and are ready for use parity wise
   * speaking.
   */
  aic_outb(p, 0, TMODE_CMDADDR);
  aic_outb(p, 0, TMODE_CMDADDR + 1);
  aic_outb(p, 0, TMODE_CMDADDR + 2);
  aic_outb(p, 0, TMODE_CMDADDR + 3);
  aic_outb(p, 0, TMODE_CMDADDR_NEXT);

  /*
   * Link us into the list of valid hosts
   */
  p->next = first_aic7xxx;
  first_aic7xxx = p;

  /*
   * Clear out any possible pending interrupts, again.
   */
  aic7xxx_clear_intstat(p);

  /*
   * Allocate the first set of scbs for this controller.  This is to stream-
   * line code elsewhere in the driver.  If we have to check for the existence
   * of scbs in certain code sections, it slows things down.  However, as
   * soon as we register the IRQ for this card, we could get an interrupt that
   * includes possibly the SCSI_RSTI interrupt.  If we catch that interrupt
   * then we are likely to segfault if we don't have at least one chunk of
   * SCBs allocated or add checks all through the reset code to make sure
   * that the SCBs have been allocated which is an invalid running condition
   * and therefore I think it's preferable to simply pre-allocate the first
   * chunk of SCBs.
   */
  aic7xxx_allocate_scb(p);

  /*
   * Load the sequencer program, then re-enable the board -
   * resetting the AIC-7770 disables it, leaving the lights
   * on with nobody home.
   */
  aic7xxx_loadseq(p);

  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
  {
    aic_outb(p, ENABLE, BCTL);  /* Enable the boards BUS drivers. */
  }

  if ( !(aic7xxx_no_reset) )
  {
    if (p->features & AHC_TWIN)
    {
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(KERN_INFO "(scsi%d) Resetting channel B\n", p->host_no);
      aic_outb(p, aic_inb(p, SBLKCTL) | SELBUSB, SBLKCTL);
      aic7xxx_reset_current_bus(p);
      aic_outb(p, aic_inb(p, SBLKCTL) & ~SELBUSB, SBLKCTL);
    }
    /* Reset SCSI bus A. */
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {  /* In case we are a 3940, 3985, or 7895, print the right channel */
      char *channel = "";
      if (p->flags & AHC_MULTI_CHANNEL)
      {
        channel = " A";
        if (p->flags & (AHC_CHNLB|AHC_CHNLC))
          channel = (p->flags & AHC_CHNLB) ? " B" : " C";
      }
      printk(KERN_INFO "(scsi%d) Resetting channel%s\n", p->host_no, channel);
    }
    
    /*
     * Some of the new Ultra2 chipsets need a longer delay after a chip
     * reset than just the init setup creates, so we have to delay here
     * before we go into a reset in order to make the chips happy.
     */
    if (p->features & AHC_ULTRA2)
      mdelay(250);
    aic7xxx_reset_current_bus(p);

    /*
     * Delay for the reset delay.
     */
    if (!reset_delay)
      aic7xxx_delay(AIC7XXX_RESET_DELAY);
  }
  else
  {
    if (!reset_delay)
    {
      printk(KERN_INFO "(scsi%d) Not resetting SCSI bus.  Note: Don't use "
             "the no_reset\n", p->host_no);
      printk(KERN_INFO "(scsi%d) option unless you have a verifiable need "
             "for it.\n", p->host_no);
      printk(KERN_INFO "(scsi%d) The no_reset option is known to break some "
             "systems,\n", p->host_no);
      printk(KERN_INFO "(scsi%d) and is not supported by the driver author\n",
             p->host_no);
      aic7xxx_delay(AIC7XXX_RESET_DELAY);
    }
  }
  
  /*
   * Register IRQ with the kernel.  Only allow sharing IRQs with
   * PCI devices.
   */
  if (!(p->chip & AHC_PCI))
  {
    result = (request_irq(p->irq, do_aic7xxx_isr, 0, "aic7xxx", p));
  }
  else
  {
    result = (request_irq(p->irq, do_aic7xxx_isr, SA_SHIRQ,
              "aic7xxx", p));
    if (result < 0)
    {
      result = (request_irq(p->irq, do_aic7xxx_isr, SA_INTERRUPT | SA_SHIRQ,
              "aic7xxx", p));
    }
  }
  if (result < 0)
  {
    printk(KERN_WARNING "(scsi%d) Couldn't register IRQ %d, ignoring "
           "controller.\n", p->host_no, p->irq);
    p->irq = 0;
    return (0);
  }

  unpause_sequencer(p, /* unpause_always */ TRUE);

  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_chip_reset
 *
 * Description:
 *   Perform a chip reset on the aic7xxx SCSI controller.  The controller
 *   is paused upon return.
 *-F*************************************************************************/
int
aic7xxx_chip_reset(struct aic7xxx_host *p)
{
  unsigned char sblkctl;
  int wait;

  /*
   * For some 274x boards, we must clear the CHIPRST bit and pause
   * the sequencer. For some reason, this makes the driver work.
   */
  aic_outb(p, PAUSE | CHIPRST, HCNTRL);

  /*
   * In the future, we may call this function as a last resort for
   * error handling.  Let's be nice and not do any unecessary delays.
   */
  wait = 1000;  /* 1 second (1000 * 1 msec) */
  while (--wait && !(aic_inb(p, HCNTRL) & CHIPRSTACK))
  {
    mdelay(1);  /* 1 msec */
  }

  pause_sequencer(p);

  sblkctl = aic_inb(p, SBLKCTL) & (SELBUSB|SELWIDE);
  if (p->chip & AHC_PCI)
    sblkctl &= ~SELBUSB;
  switch( sblkctl )
  {
    case 0:  /* normal narrow card */
      break;
    case 2:  /* Wide card */
      p->features |= AHC_WIDE;
      break;
    case 8:  /* Twin card */
      p->features |= AHC_TWIN;
      p->flags |= AHC_MULTI_CHANNEL;
      break;
    default: /* hmmm...we don't know what this is */
      printk(KERN_WARNING "aic7xxx: Unsupported adapter type %d, ignoring.\n",
        aic_inb(p, SBLKCTL) & 0x0a);
      return(-1);
  }
  return(0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_alloc
 *
 * Description:
 *   Allocate and initialize a host structure.  Returns NULL upon error
 *   and a pointer to a aic7xxx_host struct upon success.
 *-F*************************************************************************/
static struct aic7xxx_host *
aic7xxx_alloc(Scsi_Host_Template *sht, struct aic7xxx_host *temp)
{
  struct aic7xxx_host *p = NULL;
  struct Scsi_Host *host;
  int i;

  /*
   * Allocate a storage area by registering us with the mid-level
   * SCSI layer.
   */
  host = scsi_register(sht, sizeof(struct aic7xxx_host));

  if (host != NULL)
  {
    p = (struct aic7xxx_host *) host->hostdata;
    memset(p, 0, sizeof(struct aic7xxx_host));
    *p = *temp;
    p->host = host;

    p->scb_data = kmalloc(sizeof(scb_data_type), GFP_ATOMIC);
    if (p->scb_data != NULL)
    {
      memset(p->scb_data, 0, sizeof(scb_data_type));
      scbq_init (&p->scb_data->free_scbs);
    }
    else
    {
      /*
       * For some reason we don't have enough memory.  Free the
       * allocated memory for the aic7xxx_host struct, and return NULL.
       */
      release_region(p->base, MAXREG - MINREG);
      scsi_unregister(host);
      return(NULL);
    }
    p->host_no = host->host_no;
    p->tagenable = 0;
    p->orderedtag = 0;
    for (i=0; i<MAX_TARGETS; i++)
    {
      p->transinfo[i].goal_period = 0;
      p->transinfo[i].goal_offset = 0;
      p->transinfo[i].goal_width = MSG_EXT_WDTR_BUS_8_BIT;
    }
    DRIVER_LOCK_INIT
  }
  return (p);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free
 *
 * Description:
 *   Frees and releases all resources associated with an instance of
 *   the driver (struct aic7xxx_host *).
 *-F*************************************************************************/
static void
aic7xxx_free(struct aic7xxx_host *p)
{
  int i;

  /*
   * Free the allocated hardware SCB space.
   */
  if (p->scb_data != NULL)
  {
    if (p->scb_data->hscbs != NULL)
    {
      kfree(p->scb_data->hscb_kmalloc_ptr);
      p->scb_data->hscbs = p->scb_data->hscb_kmalloc_ptr = NULL;
    }
    /*
     * Free the driver SCBs.  These were allocated on an as-need
     * basis.  We allocated these in groups depending on how many
     * we could fit into a given amount of RAM.  The tail SCB for
     * these allocations has a pointer to the alloced area.
     */
    for (i = 0; i < p->scb_data->numscbs; i++)
    {
      if (p->scb_data->scb_array[i]->kmalloc_ptr != NULL)
        kfree(p->scb_data->scb_array[i]->kmalloc_ptr);
      p->scb_data->scb_array[i] = NULL;
    }
  
    /*
     * Free the SCB data area.
     */
    kfree(p->scb_data);
  }

  /*
   * Free any alloced Scsi_Cmnd structures that might be around for
   * negotiation purposes....
   */
  for (i = 0; i < MAX_TARGETS; i++)
  {
    if(p->dev_wdtr_cmnd[i])
      kfree(p->dev_wdtr_cmnd[i]);
    if(p->dev_sdtr_cmnd[i])
      kfree(p->dev_sdtr_cmnd[i]);
  }

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_load_seeprom
 *
 * Description:
 *   Load the seeprom and configure adapter and target settings.
 *   Returns 1 if the load was successful and 0 otherwise.
 *-F*************************************************************************/
static void
aic7xxx_load_seeprom(struct aic7xxx_host *p, unsigned char *sxfrctl1)
{
  int have_seeprom = 0;
  int i, max_targets, mask;
  unsigned char scsirate, scsi_conf;
  unsigned short scarray[128];
  struct seeprom_config *sc = (struct seeprom_config *) scarray;

  if (aic7xxx_verbose & VERBOSE_PROBE2)
  {
    printk(KERN_INFO "aic7xxx: Loading serial EEPROM...");
  }
  switch (p->chip)
  {
    case (AHC_AIC7770|AHC_EISA):  /* None of these adapters have seeproms. */
      if (aic_inb(p, SCSICONF) & TERM_ENB)
        p->flags |= AHC_TERM_ENB_A;
      if ( (p->features & AHC_TWIN) && (aic_inb(p, SCSICONF + 1) & TERM_ENB) )
        p->flags |= AHC_TERM_ENB_B;
      break;

    case (AHC_AIC7770|AHC_VL):
      have_seeprom = read_284x_seeprom(p, (struct seeprom_config *) scarray);
      break;

    default:
      have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                  scarray, p->sc_size, p->sc_type);
      if (!have_seeprom)
      {
        if(p->sc_type == C46)
          have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                      scarray, p->sc_size, C56_66);
        else
          have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                      scarray, p->sc_size, C46);
      }
      break;
  }

  if (!have_seeprom)
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("\naic7xxx: No SEEPROM available.\n");
    }
    p->flags |= AHC_NEWEEPROM_FMT;
    if (aic_inb(p, SCSISEQ) == 0)
    {
      p->flags |= AHC_USEDEFAULTS;
      p->flags &= ~AHC_BIOS_ENABLED;
      p->scsi_id = p->scsi_id_b = 7;
      *sxfrctl1 |= STPWEN;
      if (aic7xxx_verbose & VERBOSE_PROBE2)
      {
        printk("aic7xxx: Using default values.\n");
      }
    }
    else if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("aic7xxx: Using leftover BIOS values.\n");
    }
    if ( *sxfrctl1 & STPWEN )
    {
      p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      sc->adapter_control &= ~CFAUTOTERM;
      sc->adapter_control |= CFSTERM | CFWSTERM | CFLVDSTERM;
    }
    p->flags |= AHC_EXTEND_TRANS_A | AHC_EXTEND_TRANS_B;
  }
  else
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("done\n");
    }

    /*
     * Note things in our flags
     */
    p->flags |= AHC_SEEPROM_FOUND;

    /*
     * Update the settings in sxfrctl1 to match the termination settings.
     */
    *sxfrctl1 = 0;

    /*
     * Get our SCSI ID from the SEEPROM setting...
     */
    p->scsi_id = (sc->brtime_id & CFSCSIID);

    /*
     * First process the settings that are different between the VLB
     * and PCI adapter seeproms.
     */
    if ((p->chip & AHC_CHIPID_MASK) == AHC_AIC7770)
    {
      /* VLB adapter seeproms */
      if (sc->bios_control & CF284XEXTEND)
        p->flags |= AHC_EXTEND_TRANS_A;

      if (sc->adapter_control & CF284XSTERM)
      {
        *sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      }
    }
    else
    {
      /* PCI adapter seeproms */
      if (sc->bios_control & CFEXTEND)
        p->flags |= AHC_EXTEND_TRANS_A;
      if (sc->bios_control & CFBIOSEN)
        p->flags |= AHC_BIOS_ENABLED;
      else
        p->flags &= ~AHC_BIOS_ENABLED;

      if (sc->adapter_control & CFSTERM)
      {
        *sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      }
    }
    p->sc = *sc;
  }

  p->discenable = 0;
    
  /*
   * Limit to 16 targets just in case.  The 2842 for one is known to
   * blow the max_targets setting, future cards might also.
   */
  max_targets = MIN(sc->max_targets & CFMAXTARG,
                   ((p->features & (AHC_TWIN | AHC_WIDE)) ? 16 : 8));

  if (have_seeprom)
  {
    for (i = 0; i < max_targets; i++)
    {
      if( ((p->features & AHC_ULTRA) &&
          !(sc->adapter_control & CFULTRAEN) &&
           (sc->device_flags[i] & CFSYNCHISULTRA)) ||
          (sc->device_flags[i] & CFNEWULTRAFORMAT) )
      {
        p->flags |= AHC_NEWEEPROM_FMT;
        break;
      }
    }
  }

  for (i = 0; i < max_targets; i++)
  {
    mask = (0x01 << i);
    if (!have_seeprom)
    {
      if(aic_inb(p, SCSISEQ) != 0)
      {
        /*
         * OK...the BIOS set things up and left behind the settings we need.
         * Just make our sc->device_flags[i] entry match what the card has
         * set for this device.
         */
        p->discenable = 
          ~(aic_inb(p, DISC_DSB) | (aic_inb(p, DISC_DSB + 1) << 8) );
        p->ultraenb =
          (aic_inb(p, ULTRA_ENB) | (aic_inb(p, ULTRA_ENB + 1) << 8) );
        sc->device_flags[i] = (p->discenable & mask) ? CFDISC : 0;
        if (aic_inb(p, TARG_SCSIRATE + i) & WIDEXFER)
          sc->device_flags[i] |= CFWIDEB;
        if (p->features & AHC_ULTRA2)
        {
          if (aic_inb(p, TARG_OFFSET + i))
          {
            sc->device_flags[i] |= CFSYNCH;
            sc->device_flags[i] |= (aic_inb(p, TARG_SCSIRATE + i) & 0x07);
            if ( (aic_inb(p, TARG_SCSIRATE + i) & 0x18) == 0x18 )
              sc->device_flags[i] |= CFSYNCHISULTRA;
          }
        }
        else
        {
          if (aic_inb(p, TARG_SCSIRATE + i) & ~WIDEXFER)
          {
            sc->device_flags[i] |= CFSYNCH;
            if (p->features & AHC_ULTRA)
              sc->device_flags[i] |= ((p->ultraenb & mask) ?
                                      CFSYNCHISULTRA : 0);
          }
        }
      }
      else
      {
        /*
         * Assume the BIOS has NOT been run on this card and nothing between
         * the card and the devices is configured yet.
         */
        sc->device_flags[i] = CFDISC;
        if (p->features & AHC_WIDE)
          sc->device_flags[i] |= CFWIDEB;
        if (p->features & AHC_ULTRA2)
          sc->device_flags[i] |= 3;
        else if (p->features & AHC_ULTRA)
          sc->device_flags[i] |= CFSYNCHISULTRA;
        sc->device_flags[i] |= CFSYNCH;
        aic_outb(p, 0, TARG_SCSIRATE + i);
        if (p->features & AHC_ULTRA2)
          aic_outb(p, 0, TARG_OFFSET + i);
      }
    }
    if (sc->device_flags[i] & CFDISC)
    {
      p->discenable |= mask;
    }
    if (p->flags & AHC_NEWEEPROM_FMT)
    {
      if (sc->device_flags[i] & CFSYNCHISULTRA)
      {
        p->ultraenb |= mask;
      }
      else if (sc->device_flags[i] & CFNEWULTRAFORMAT)
      {
        if ( (sc->device_flags[i] & (CFSYNCHISULTRA | CFXFER)) == 0x03 )
        {
          sc->device_flags[i] &= ~CFXFER;
          sc->device_flags[i] |= CFSYNCHISULTRA;
          p->ultraenb |= mask;
        }
      }
    }
    else if (sc->adapter_control & CFULTRAEN)
    {
      p->ultraenb |= mask;
    }
    if ( (sc->device_flags[i] & CFSYNCH) == 0)
    {
      sc->device_flags[i] &= ~CFXFER;
      p->ultraenb &= ~mask;
      p->transinfo[i].user_offset = 0;
      p->transinfo[i].user_period = 0;
      p->transinfo[i].cur_offset = 0;
      p->transinfo[i].cur_period = 0;
      p->needsdtr_copy &= ~mask;
    }
    else
    {
      if (p->features & AHC_ULTRA2)
      {
        p->transinfo[i].user_offset = MAX_OFFSET_ULTRA2;
        p->transinfo[i].cur_offset = aic_inb(p, TARG_OFFSET + i);
        scsirate = (sc->device_flags[i] & CFXFER) |
                   ((p->ultraenb & mask) ? 0x18 : 0x10);
        p->transinfo[i].user_period = aic7xxx_find_period(p, scsirate,
                                       AHC_SYNCRATE_ULTRA2);
        p->transinfo[i].cur_period = aic7xxx_find_period(p,
                                       aic_inb(p, TARG_SCSIRATE + i),
                                       AHC_SYNCRATE_ULTRA2);
      }
      else
      {
        scsirate = (sc->device_flags[i] & CFXFER) << 4;
        if (sc->device_flags[i] & CFWIDEB)
          p->transinfo[i].user_offset = MAX_OFFSET_16BIT;
        else
          p->transinfo[i].user_offset = MAX_OFFSET_8BIT;
        if (p->features & AHC_ULTRA)
        {
          short ultraenb;
          ultraenb = aic_inb(p, ULTRA_ENB) |
            (aic_inb(p, ULTRA_ENB + 1) << 8);
          p->transinfo[i].user_period = aic7xxx_find_period(p,
                                          scsirate,
                                          (p->ultraenb & mask) ?
                                          AHC_SYNCRATE_ULTRA :
                                          AHC_SYNCRATE_FAST);
          p->transinfo[i].cur_period = aic7xxx_find_period(p,
                                         aic_inb(p, TARG_SCSIRATE + i),
                                         (ultraenb & mask) ? 
                                         AHC_SYNCRATE_ULTRA :
                                         AHC_SYNCRATE_FAST);
        }
        else
          p->transinfo[i].user_period = aic7xxx_find_period(p,
                                          scsirate, AHC_SYNCRATE_FAST);
      }
      p->needsdtr_copy |= mask;
    }
    if ( (sc->device_flags[i] & CFWIDEB) && (p->features & AHC_WIDE) )
    {
      p->transinfo[i].user_width = MSG_EXT_WDTR_BUS_16_BIT;
      p->needwdtr_copy |= mask;
    }
    else
    {
      p->transinfo[i].user_width = MSG_EXT_WDTR_BUS_8_BIT;
      p->needwdtr_copy &= ~mask;
    }
    p->transinfo[i].cur_width =
      (aic_inb(p, TARG_SCSIRATE + i) & WIDEXFER) ?
      MSG_EXT_WDTR_BUS_16_BIT : MSG_EXT_WDTR_BUS_8_BIT;
  }
  aic_outb(p, ~(p->discenable & 0xFF), DISC_DSB);
  aic_outb(p, ~((p->discenable >> 8) & 0xFF), DISC_DSB + 1);
  p->needwdtr = p->needwdtr_copy;
  p->needsdtr = p->needsdtr_copy;
  p->wdtr_pending = p->sdtr_pending = 0;

  /*
   * We set the p->ultraenb from the SEEPROM to begin with, but now we make
   * it match what is already down in the card.  If we are doing a reset
   * on the card then this will get put back to a default state anyway.
   * This allows us to not have to pre-emptively negotiate when using the
   * no_reset option.
   */
  if (p->features & AHC_ULTRA)
    p->ultraenb = aic_inb(p, ULTRA_ENB) | (aic_inb(p, ULTRA_ENB + 1) << 8);

  
  scsi_conf = (p->scsi_id & HSCSIID);

  if(have_seeprom)
  {
    p->adapter_control = sc->adapter_control;
    p->bios_control = sc->bios_control;

    switch (p->chip & AHC_CHIPID_MASK)
    {
      case AHC_AIC7895:
      case AHC_AIC7896:
        if (p->adapter_control & CFBPRIMARY)
          p->flags |= AHC_CHANNEL_B_PRIMARY;
      default:
        break;
    }

    if (sc->adapter_control & CFSPARITY)
      scsi_conf |= ENSPCHK;
  }
  else
  {
    scsi_conf |= ENSPCHK | RESET_SCSI;
  }

  /*
   * Only set the SCSICONF and SCSICONF + 1 registers if we are a PCI card.
   * The 2842 and 2742 cards already have these registers set and we don't
   * want to muck with them since we don't set all the bits they do.
   */
  if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI )
  {
    /* Set the host ID */
    aic_outb(p, scsi_conf, SCSICONF);
    /* In case we are a wide card */
    aic_outb(p, p->scsi_id, SCSICONF + 1);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_detect
 *
 * Description:
 *   Try to detect and register an Adaptec 7770 or 7870 SCSI controller.
 *
 * XXX - This should really be called aic7xxx_probe().  A sequence of
 *       probe(), attach()/detach(), and init() makes more sense than
 *       one do-it-all function.  This may be useful when (and if) the
 *       mid-level SCSI code is overhauled.
 *-F*************************************************************************/
int
aic7xxx_detect(Scsi_Host_Template *template)
{
  struct aic7xxx_host *temp_p = NULL;
  struct aic7xxx_host *current_p = NULL;
  struct aic7xxx_host *list_p = NULL;
  int found = 0;
#if defined(__i386__) || defined(__alpha__)
  ahc_flag_type flags = 0;
  int type;
#endif
  unsigned char sxfrctl1;
#if defined(__i386__) || defined(__alpha__)
  unsigned char hcntrl, hostconf;
  unsigned int slot, base;
#endif

#ifdef MODULE
  /*
   * If we are called as a module, the aic7xxx pointer may not be null
   * and it would point to our bootup string, just like on the lilo
   * command line.  IF not NULL, then process this config string with
   * aic7xxx_setup
   */
  if(aic7xxx)
    aic7xxx_setup(aic7xxx, NULL);
  if(dummy_buffer[0] != 'P')
    printk(KERN_WARNING "aic7xxx: Please read the file /usr/src/linux/drivers"
      "/scsi/README.aic7xxx\n"
      "aic7xxx: to see the proper way to specify options to the aic7xxx "
      "module\n"
      "aic7xxx: Specifically, don't use any commas when passing arguments to\n"
      "aic7xxx: insmod or else it might trash certain memory areas.\n");
#endif

  template->proc_dir = &proc_scsi_aic7xxx;
  template->sg_tablesize = AIC7XXX_MAX_SG;


#if defined(__i386__) || defined(__alpha__)
  /*
   * EISA/VL-bus card signature probe.
   */
  slot = MINSLOT;
  while ( (slot <= MAXSLOT) && !(aic7xxx_no_probe) )
  {
    base = SLOTBASE(slot) + MINREG;

    if (check_region(base, MAXREG - MINREG))
    {
      /*
       * Some other driver has staked a
       * claim to this i/o region already.
       */
      slot++;
      continue; /* back to the beginning of the for loop */
    }
    flags = 0;
    type = aic7xxx_probe(slot, base + AHC_HID0, &flags);
    if (type == -1)
    {
      slot++;
      continue;
    }
    temp_p = kmalloc(sizeof(struct aic7xxx_host), GFP_ATOMIC);
    if (temp_p == NULL)
    {
      printk(KERN_WARNING "aic7xxx: Unable to allocate device space.\n");
      slot++;
      continue; /* back to the beginning of the while loop */
    }
    /*
     * Pause the card preserving the IRQ type.  Allow the operator
     * to override the IRQ trigger.
     */
    if (aic7xxx_irq_trigger == 1)
      hcntrl = IRQMS;  /* Level */
    else if (aic7xxx_irq_trigger == 0)
      hcntrl = 0;  /* Edge */
    else
      hcntrl = inb(base + HCNTRL) & IRQMS;  /* Default */
    memset(temp_p, 0, sizeof(struct aic7xxx_host));
    temp_p->unpause = hcntrl | INTEN;
    temp_p->pause = hcntrl | PAUSE | INTEN;
    temp_p->base = base;
    temp_p->mbase = 0;
    temp_p->maddr = 0;
    temp_p->pci_bus = 0;
    temp_p->pci_device_fn = slot;
    aic_outb(temp_p, hcntrl | PAUSE, HCNTRL);
    while( (aic_inb(temp_p, HCNTRL) & PAUSE) == 0 ) ;
    if (aic7xxx_chip_reset(temp_p) == -1)
      temp_p->irq = 0;
    else
      temp_p->irq = aic_inb(temp_p, INTDEF) & 0x0F;
    temp_p->flags |= AHC_PAGESCBS;

    switch (temp_p->irq)
    {
      case 9:
      case 10:
      case 11:
      case 12:
      case 14:
      case 15:
        break;

      default:
        printk(KERN_WARNING "aic7xxx: Host adapter uses unsupported IRQ "
          "level %d, ignoring.\n", temp_p->irq);
        kfree(temp_p);
        slot++;
        continue; /* back to the beginning of the while loop */
    }

    /*
     * We are commited now, everything has been checked and this card
     * has been found, now we just set it up
     */

    /*
     * Insert our new struct into the list at the end
     */
    if (list_p == NULL)
    {
      list_p = current_p = temp_p;
    }
    else
    {
      current_p = list_p;
      while (current_p->next != NULL)
        current_p = current_p->next;
      current_p->next = temp_p;
    }
    if (aic7xxx_extended)
    {
      temp_p->flags |= AHC_EXTEND_TRANS_A;
      if (temp_p->flags & AHC_MULTI_CHANNEL)
        temp_p->flags |= AHC_EXTEND_TRANS_B;
    }

    switch (type)
    {
      case 0:
        temp_p->board_name_index = 2;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("aic7xxx: <%s> at EISA %d\n",
               board_names[2], slot);
        /* FALLTHROUGH */
      case 1:
      {
        temp_p->chip = AHC_AIC7770 | AHC_EISA;
        temp_p->features |= AHC_AIC7770_FE;
        temp_p->bios_control = aic_inb(temp_p, HA_274_BIOSCTRL);

        /*
         * Get the primary channel information.  Right now we don't
         * do anything with this, but someday we will be able to inform
         * the mid-level SCSI code which channel is primary.
         */
        if (temp_p->board_name_index == 0)
        {
          temp_p->board_name_index = 3;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk("aic7xxx: <%s> at EISA %d\n",
                 board_names[3], slot);
        }
        if (temp_p->bios_control & CHANNEL_B_PRIMARY)
        {
          temp_p->flags |= AHC_CHANNEL_B_PRIMARY;
        }

        if ((temp_p->bios_control & BIOSMODE) == BIOSDISABLED)
        {
          temp_p->flags &= ~AHC_BIOS_ENABLED;
        }
        else
        {
          temp_p->flags &= ~AHC_USEDEFAULTS;
          temp_p->flags |= AHC_BIOS_ENABLED;
          if ( (temp_p->bios_control & 0x20) == 0 )
          {
            temp_p->bios_address = 0xcc000;
            temp_p->bios_address += (0x4000 * (temp_p->bios_control & 0x07));
          }
          else
          {
            temp_p->bios_address = 0xd0000;
            temp_p->bios_address += (0x8000 * (temp_p->bios_control & 0x06));
          }
        }
        temp_p->adapter_control = aic_inb(temp_p, SCSICONF) << 8;
        temp_p->adapter_control |= aic_inb(temp_p, SCSICONF + 1);
        if (temp_p->features & AHC_WIDE)
        {
          temp_p->scsi_id = temp_p->adapter_control & HWSCSIID;
          temp_p->scsi_id_b = temp_p->scsi_id;
        }
        else
        {
          temp_p->scsi_id = (temp_p->adapter_control >> 8) & HSCSIID;
          temp_p->scsi_id_b = temp_p->adapter_control & HSCSIID;
        }
        aic7xxx_load_seeprom(temp_p, &sxfrctl1);
        break;
      }

      case 2:
      case 3:
        temp_p->chip = AHC_AIC7770 | AHC_VL;
        temp_p->features |= AHC_AIC7770_FE;
        if (type == 2)
          temp_p->flags |= AHC_BIOS_ENABLED;
        else
          temp_p->flags &= ~AHC_BIOS_ENABLED;
        if (aic_inb(temp_p, SCSICONF) & TERM_ENB)
          sxfrctl1 = STPWEN;
        aic7xxx_load_seeprom(temp_p, &sxfrctl1);
        temp_p->board_name_index = 4;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("aic7xxx: <%s> at VLB %d\n",
               board_names[2], slot);
        switch( aic_inb(temp_p, STATUS_2840) & BIOS_SEL )
        {
          case 0x00:
            temp_p->bios_address = 0xe0000;
            break;
          case 0x20:
            temp_p->bios_address = 0xc8000;
            break;
          case 0x40:
            temp_p->bios_address = 0xd0000;
            break;
          case 0x60:
            temp_p->bios_address = 0xd8000;
            break;
          default:
            break; /* can't get here */
        }
        break;

      default:  /* Won't get here. */
        break;
    }
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk(KERN_INFO "aic7xxx: BIOS %sabled, IO Port 0x%lx, IRQ %d (%s)\n",
        (temp_p->flags & AHC_USEDEFAULTS) ? "dis" : "en", temp_p->base,
        temp_p->irq,
        (temp_p->pause & IRQMS) ? "level sensitive" : "edge triggered");
      printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
             (temp_p->flags & AHC_EXTEND_TRANS_A) ? "en" : "dis");
    }

    /*
     * Set the FIFO threshold and the bus off time.
     */
    hostconf = aic_inb(temp_p, HOSTCONF);
    aic_outb(temp_p, hostconf & DFTHRSH, BUSSPD);
    aic_outb(temp_p, (hostconf << 2) & BOFF, BUSTIME);
    slot++;
    found++;
  }

#endif /* defined(__i386__) || defined(__alpha__) */

#ifdef CONFIG_PCI
  /*
   * PCI-bus probe.
   */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
  if (pci_present())
#else
  if (pcibios_present())
#endif
  {
    struct
    {
      unsigned short      vendor_id;
      unsigned short      device_id;
      ahc_chip            chip;
      ahc_flag_type       flags;
      ahc_feature         features;
      int                 board_name_index;
      unsigned short      seeprom_size;
      unsigned short      seeprom_type;
    } const aic_pdevs[] = {
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7810, AHC_NONE,
       AHC_FNONE, AHC_FENONE,                                1,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7850, AHC_AIC7850,
       AHC_PAGESCBS, AHC_AIC7850_FE,                         5,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7855, AHC_AIC7850,
       AHC_PAGESCBS, AHC_AIC7850_FE,                         6,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7860, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7861, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       8,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7870, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7870_FE,      9,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7871, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7870_FE,     10,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7872, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7870_FE,                                      11,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7873, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7870_FE,                                      12,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7874, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7870_FE,     13,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7880, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     14,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7881, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     15,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7882, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7880_FE,                                      16,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7883, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7880_FE,                                      17,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7884, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     18,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7895, AHC_AIC7895,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7895_FE,                                      19,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7890, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      20,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_2940U2, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      21,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7896, AHC_AIC7896,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7896_FE,                                      22,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_3940U2, AHC_AIC7896,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7896_FE,                                      23,
       32, C56_66 },
    };

    unsigned short command;
    unsigned int  devconfig, i, oldverbose;
#ifdef MMAPIO
    unsigned long page_offset, base;
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
    struct pci_dev *pdev = NULL;
#else
    int index;
    unsigned int piobase, mmapbase;
    unsigned char pci_bus, pci_devfn, pci_irq;
#endif

    for (i = 0; i < NUMBER(aic_pdevs); i++)
    {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
      pdev = NULL;
      while ((pdev = pci_find_device(aic_pdevs[i].vendor_id,
                                     aic_pdevs[i].device_id,
                                     pdev)))
#else
      index = 0;
      while (!(pcibios_find_device(aic_pdevs[i].vendor_id,
                                   aic_pdevs[i].device_id,
                                   index++, &pci_bus, &pci_devfn)) )
#endif
      {
        if ( i == 0 ) /* We found one, but it's the 7810 RAID cont. */
        {
          if (aic7xxx_verbose & (VERBOSE_PROBE|VERBOSE_PROBE2))
          {
            printk(KERN_INFO "aic7xxx: The 7810 RAID controller is not "
              "supported by\n");
            printk(KERN_INFO "         this driver, we are ignoring it.\n");
          }
        }
        else if ( (temp_p = kmalloc(sizeof(struct aic7xxx_host),
                                    GFP_ATOMIC)) != NULL )
        {
          memset(temp_p, 0, sizeof(struct aic7xxx_host));
          temp_p->chip = aic_pdevs[i].chip | AHC_PCI;
          temp_p->flags = aic_pdevs[i].flags;
          temp_p->features = aic_pdevs[i].features;
          temp_p->board_name_index = aic_pdevs[i].board_name_index;
          temp_p->sc_size = aic_pdevs[i].seeprom_size;
          temp_p->sc_type = aic_pdevs[i].seeprom_type;

          /*
           * Read sundry information from PCI BIOS.
           */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
          temp_p->irq = pdev->irq;
          temp_p->pdev = pdev;
          temp_p->pci_bus = pdev->bus->number;
          temp_p->pci_device_fn = pdev->devfn;
          temp_p->base = pdev->base_address[0];
          temp_p->mbase = pdev->base_address[1];
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk("aic7xxx: <%s> at PCI %d/%d\n", 
              board_names[aic_pdevs[i].board_name_index],
              PCI_SLOT(temp_p->pdev->devfn),
              PCI_FUNC(temp_p->pdev->devfn));
          pci_read_config_word(pdev, PCI_COMMAND, &command);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial PCI_COMMAND value was 0x%x\n",
              (int)command);
          }
#ifdef AIC7XXX_STRICT_PCI_SETUP
          command |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
            PCI_COMMAND_INVALIDATE | PCI_COMMAND_MASTER |
            PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#else
          command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#endif
          if (aic7xxx_pci_parity == 0)
            command &= ~(PCI_COMMAND_SERR | PCI_COMMAND_PARITY);
          pci_write_config_word(pdev, PCI_COMMAND, command);
#ifdef AIC7XXX_STRICT_PCI_SETUP
          pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial DEVCONFIG value was 0x%x\n", devconfig);
          }
          devconfig |= 0x80000000;
          if ((aic7xxx_pci_parity == 0) || (aic7xxx_pci_parity == -1))
          {
            devconfig &= ~(0x00000008);
          }
          else
          {
            devconfig |= 0x00000008;
          }
          pci_write_config_dword(pdev, DEVCONFIG, devconfig);
#endif /* AIC7XXX_STRICT_PCI_SETUP */
#else  /* LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92) */
          temp_p->pci_bus = pci_bus;
          temp_p->pci_device_fn = pci_devfn;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk("aic7xxx: <%s> at PCI %d/%d\n", 
              board_names[aic_pdevs[i].board_name_index],
              PCI_SLOT(temp_p->pci_device_fn),
              PCI_FUNC(temp_p->pci_device_fn));
          pcibios_read_config_byte(pci_bus, pci_devfn, PCI_INTERRUPT_LINE,
            &pci_irq);
          temp_p->irq = pci_irq;
          pcibios_read_config_dword(pci_bus, pci_devfn, PCI_BASE_ADDRESS_0,
            &piobase);
          temp_p->base = piobase;
          pcibios_read_config_dword(pci_bus, pci_devfn, PCI_BASE_ADDRESS_1,
            &mmapbase);
          temp_p->mbase = mmapbase;
          pcibios_read_config_word(pci_bus, pci_devfn, PCI_COMMAND, &command);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial PCI_COMMAND value was 0x%x\n",
              (int)command);
          }
#ifdef AIC7XXX_STRICT_PCI_SETUP
          command |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
            PCI_COMMAND_INVALIDATE | PCI_COMMAND_MASTER |
            PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#else
          command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#endif
          if (aic7xxx_pci_parity == 0)
            command &= ~(PCI_COMMAND_SERR | PCI_COMMAND_PARITY);
          pcibios_write_config_word(pci_bus, pci_devfn, PCI_COMMAND, command);
#ifdef AIC7XXX_STRICT_PCI_SETUP
          pcibios_read_config_dword(pci_bus, pci_devfn, DEVCONFIG, &devconfig);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial DEVCONFIG value was 0x%x\n", devconfig);
          }
          devconfig |= 0x80000000;
          if ((aic7xxx_pci_parity == 0) || (aic7xxx_pci_parity == -1))
          {
            devconfig &= ~(0x00000008);
          }
          else
          {
            devconfig |= 0x00000008;
          }
          pcibios_write_config_dword(pci_bus, pci_devfn, DEVCONFIG, devconfig);
#endif /* AIC7XXX_STRICT_PCI_SETUP */
#endif /* LINUIX_VERSION_CODE > KERNEL_VERSION(2,1,92) */

          /*
           * The first bit (LSB) of PCI_BASE_ADDRESS_0 is always set, so
           * we mask it off.
           */
          temp_p->base &= PCI_BASE_ADDRESS_IO_MASK;
          temp_p->mbase &= PCI_BASE_ADDRESS_MEM_MASK;
          temp_p->unpause = INTEN;
          temp_p->pause = temp_p->unpause | PAUSE;

#ifdef MMAPIO
          base = temp_p->mbase & PAGE_MASK;
          page_offset = temp_p->mbase - base;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
          temp_p->maddr = ioremap_nocache(base, page_offset + 256);
#else
          temp_p->maddr = vremap(base, page_offset + 256);
#endif
          if(temp_p->maddr)
          {
            temp_p->maddr += page_offset;
          }
#endif

          pause_sequencer(temp_p);

          /*
           * Clear out any pending PCI error status messages.  Also set
           * verbose to 0 so that we don't emit strange PCI error messages
           * while cleaning out the current status bits.
           */
          oldverbose = aic7xxx_verbose;
          aic7xxx_verbose = 0;
          aic7xxx_pci_intr(temp_p);
          aic7xxx_verbose = oldverbose;

          temp_p->bios_address = 0;

          /*
           * Remember how the card was setup in case there is no seeprom.
           */
          if (temp_p->features & AHC_ULTRA2)
            temp_p->scsi_id = aic_inb(temp_p, SCSIID_ULTRA2) & OID;
          else
            temp_p->scsi_id = aic_inb(temp_p, SCSIID) & OID;
          /*
           * Get current termination setting
           */
          sxfrctl1 = aic_inb(temp_p, SXFRCTL1) & STPWEN;

          if (aic7xxx_chip_reset(temp_p) == -1)
          {
            kfree(temp_p);
            temp_p = NULL;
            continue;
          }

          /*
           * Doing a switch based upon i is really gross, but since Justin
           * changed around the chip ID stuff, we can't use that any more.
           * Since we don't scan the devices the same way as FreeBSD, we end
           * up doing this gross hack in order to avoid totally splitting
           * away from Justin's init code in ahc_pci.c
           */
          switch (i)
          {
            case 7:   /* 3940 */
            case 12:  /* 3940-Ultra */
              switch(PCI_SLOT(temp_p->pci_device_fn))
              {
                case 5:
                  temp_p->flags |= AHC_CHNLB;
                  break;
                default:
                  break;
              }
              break;

            case 8:   /* 3985 */
            case 13:  /* 3985-Ultra */
              switch(PCI_SLOT(temp_p->pci_device_fn))
              {
                case 8:
                  temp_p->flags |= AHC_CHNLB;
                  break;
                case 12:
                  temp_p->flags |= AHC_CHNLC;
                  break;
                default:
                  break;
              }
              break;

            case 15:
            case 18:
            case 19:
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
              if (PCI_FUNC(temp_p->pdev->devfn) != 0)
              {
                temp_p->flags |= AHC_CHNLB;
              }
              /*
               * The 7895 is the only chipset that sets the SCBSIZE32 param
               * in the DEVCONFIG register.  The Ultra2 chipsets use
               * the DSCOMMAND0 register instead.
               */
              if ((temp_p->chip & AHC_CHIPID_MASK) == AHC_AIC7895)
              {
                pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
                devconfig |= SCBSIZE32;
                pci_write_config_dword(pdev, DEVCONFIG, devconfig);
              }
#else
              if (PCI_FUNC(temp_p->pci_device_fn) != 0)
              {
                temp_p->flags |= AHC_CHNLB;
              }
              /*
               * The 7895 is the only chipset that sets the SCBSIZE32 param
               * in the DEVCONFIG register.  The Ultra2 chipsets use
               * the DSCOMMAND0 register instead.
               */
              if ((temp_p->chip & AHC_CHIPID_MASK) == AHC_AIC7895)
              {
                pcibios_read_config_dword(pci_bus, pci_devfn, DEVCONFIG,
                  &devconfig);
                devconfig |= SCBSIZE32;
                pcibios_write_config_dword(pci_bus, pci_devfn, DEVCONFIG,
                  devconfig);
              }
#endif
              break;
            default:
              break;
          }

          /*
           * Loading of the SEEPROM needs to come after we've set the flags
           * to indicate possible CHNLB and CHNLC assigments.  Otherwise,
           * on 394x and 398x cards we'll end up reading the wrong settings
           * for channels B and C
           */
          switch (temp_p->chip & AHC_CHIPID_MASK)
          {
            case AHC_AIC7890:
            case AHC_AIC7896:
              aic_outb(temp_p, 0, SCAMCTL);
              /*
               * We used to set DPARCKEN in this register, but after talking
               * to a tech from Adaptec, I found out they don't use that
               * particular bit in their own register settings, and when you
               * combine that with the fact that I determined that we were
               * seeing Data-Path Parity Errors on things we shouldn't see
               * them on, I think there is a bug in the silicon and the way
               * to work around it is to disable this particular check.  Also
               * This bug only showed up on certain commands, so it seems to
               * be pattern related or some such.  The commands we would
               * typically send as a linux TEST_UNIT_READY or INQUIRY command
               * could cause it to be triggered, while regular commands that
               * actually made reasonable use of the SG array capabilities
               * seemed not to cause the problem.
               */
              /*
              aic_outb(temp_p, aic_inb(temp_p, DSCOMMAND0) |
                               CACHETHEN | DPARCKEN | MPARCKEN |
                               USCBSIZE32 | CIOPARCKEN,
                               DSCOMMAND0);
               */
              aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                CACHETHEN | MPARCKEN | USCBSIZE32 |
                                CIOPARCKEN) & ~DPARCKEN, DSCOMMAND0);
              /* FALLTHROUGH */
            default:
              /*
               * We attempt to read a SEEPROM on *everything*.  If we fail,
               * then we fail, but this covers things like 2910c cards that
               * now have SEEPROMs with their 7856 chipset that we would
               * otherwise ignore.  They still don't have a BIOS, but they
               * have a SEEPROM that the SCSISelect utility on the Adaptec
               * diskettes can configure.
               */
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
            case AHC_AIC7850:
            case AHC_AIC7860:
              /*
               * Set the DSCOMMAND0 register on these cards different from
               * on the 789x cards.  Also, read the SEEPROM as well.
               */
              aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                CACHETHEN | MPARCKEN) & ~DPARCKEN,
                       DSCOMMAND0);
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
            case AHC_AIC7880:
              /*
               * Only set the DSCOMMAND0 register if this is a Rev B.
               * chipset.  For those, we also enable Ultra mode by
               * force due to brain-damage on the part of some BIOSes
               * We overload the devconfig variable here since we can.
               */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
              pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
#else
              pcibios_read_config_dword(pci_bus, pci_devfn, DEVCONFIG,
                                        &devconfig);
#endif
              if ((devconfig & 0xff) >= 1)
              {
                aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                  CACHETHEN | MPARCKEN) & ~DPARCKEN,
                         DSCOMMAND0);
              }
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
          }
          

          /*
           * and then we need another switch based on the type in order to
           * make sure the channel B primary flag is set properly on 7895
           * controllers....Arrrgggghhh!!!  We also have to catch the fact
           * that when you disable the BIOS on the 7895 on the Intel DK440LX
           * motherboard, and possibly others, it only sets the BIOS disabled
           * bit on the A channel...I think I'm starting to lean towards
           * going postal....
           */
          switch(temp_p->chip & AHC_CHIPID_MASK)
          {
            case AHC_AIC7895:
            case AHC_AIC7896:
              current_p = list_p;
              while(current_p != NULL)
              {
                if ( (current_p->pci_bus == temp_p->pci_bus) &&
                     (PCI_SLOT(current_p->pci_device_fn) ==
                      PCI_SLOT(temp_p->pci_device_fn)) )
                {
                  if ( PCI_FUNC(current_p->pci_device_fn) == 0 )
                  {
                    temp_p->flags |= 
                      (current_p->flags & AHC_CHANNEL_B_PRIMARY);
                    temp_p->flags &= ~(AHC_BIOS_ENABLED|AHC_USEDEFAULTS);
                    temp_p->flags |=
                      (current_p->flags & (AHC_BIOS_ENABLED|AHC_USEDEFAULTS));
                  }
                  else
                  {
                    current_p->flags |=
                      (temp_p->flags & AHC_CHANNEL_B_PRIMARY);
                    current_p->flags &= ~(AHC_BIOS_ENABLED|AHC_USEDEFAULTS);
                    current_p->flags |=
                      (temp_p->flags & (AHC_BIOS_ENABLED|AHC_USEDEFAULTS));
                  }
                }
                current_p = current_p->next;
              }
              break;
            default:
              break;
          }

          /*
           * We do another switch based on i so that we can exclude all
           * 3895 devices from the next option since the 3895 cards use
           * shared external SCB RAM while all other cards have dedicated
           * external SCB RAM per channel.  Also exclude the 7850 and
           * 7860 based stuff since they can have garbage in the bit
           * that indicates external RAM and get some of this stuff
           * wrong as a result.
           */
          switch(temp_p->chip & AHC_CHIPID_MASK)
          {
            default:
              break;
            case AHC_AIC7895:
            case AHC_AIC7896:
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
              pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
#else
              pcibios_read_config_dword(pci_bus, pci_devfn, DEVCONFIG,
                                        &devconfig);
#endif
              if (temp_p->features & AHC_ULTRA2)
              {
                if (aic_inb(temp_p, DSCOMMAND0) & RAMPSM_ULTRA2)
                {
                  aic_outb(temp_p,
                           aic_inb(temp_p, DSCOMMAND0) & ~SCBRAMSEL_ULTRA2,
                           DSCOMMAND0);
                  temp_p->flags |= AHC_EXTERNAL_SRAM;
                  devconfig |= EXTSCBPEN;
                }
              }
              else if (devconfig & RAMPSM)
              {
                devconfig &= ~SCBRAMSEL;
                devconfig |= EXTSCBPEN;
                temp_p->flags |= AHC_EXTERNAL_SRAM;
              }
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
              pci_write_config_dword(pdev, DEVCONFIG, devconfig);
#else
              pcibios_write_config_dword(pci_bus, pci_devfn, DEVCONFIG,
                                         devconfig);
#endif
              if ( (temp_p->flags & AHC_EXTERNAL_SRAM) &&
                   (temp_p->flags & AHC_CHNLB) )
                aic_outb(temp_p, 1, CCSCBBADDR);
              break;
          }

          /*
           * Take the LED out of diagnostic mode
           */
          aic_outb(temp_p, 
            (aic_inb(temp_p, SBLKCTL) & ~(DIAGLEDEN | DIAGLEDON)),
            SBLKCTL);

          /*
           * We don't know where this is set in the SEEPROM or by the
           * BIOS, so we default to 100%.  On Ultra2 controllers, use 75%
           * instead.
           */
          if (temp_p->features & AHC_ULTRA2)
          {
            aic_outb(temp_p, RD_DFTHRSH_75 | WR_DFTHRSH_75, DFF_THRSH);
          }
          else
          {
            aic_outb(temp_p, DFTHRSH_100, DSPCISTATUS);
          }

          if (aic7xxx_extended)
            temp_p->flags |= AHC_EXTEND_TRANS_A;

          if ( list_p == NULL )
          {
            list_p = current_p = temp_p;
          }
          else
          {
            current_p = list_p;
            while(current_p->next != NULL)
              current_p = current_p->next;
            current_p->next = temp_p;
          }
          temp_p->next = NULL;
          found++;
        }  /* Found an Adaptec PCI device. */
        else /* Well, we found one, but we couldn't get any memory */
        {
          printk("aic7xxx: Found <%s>\n", 
            board_names[aic_pdevs[i].board_name_index]);
          printk(KERN_INFO "aic7xxx: Unable to allocate device memory, "
            "skipping.\n");
        }
      } /* while(pdev=....) */
    } /* for PCI_DEVICES */
  } /* PCI BIOS present */
#endif CONFIG_PCI
  /*
   * Now, we re-order the probed devices by BIOS address and BUS class.
   * In general, we follow this algorithm to make the adapters show up
   * in the same order under linux that the computer finds them.
   *  1: All VLB/EISA cards with BIOS_ENABLED first, according to BIOS
   *     address, going from lowest to highest.
   *  2: All PCI controllers with BIOS_ENABLED next, according to BIOS
   *     address, going from lowest to highest.
   *  3: Remaining VLB/EISA controllers going in slot order.
   *  4: Remaining PCI controllers, going in PCI device order (reversable)
   */

  {
    struct aic7xxx_host *sort_list[4] = { NULL, NULL, NULL, NULL };
    struct aic7xxx_host *vlb, *pci;
    struct aic7xxx_host *prev_p;
    struct aic7xxx_host *p;
    unsigned char left;

    prev_p = vlb = pci = NULL;

    temp_p = list_p;
    while (temp_p != NULL)
    {
      switch(temp_p->chip & ~AHC_CHIPID_MASK)
      {
        case AHC_EISA:
        case AHC_VL:
        {
          p = temp_p;
          if (p->flags & AHC_BIOS_ENABLED)
            vlb = sort_list[0];
          else
            vlb = sort_list[2];

          if (vlb == NULL)
          {
            vlb = temp_p;
            temp_p = temp_p->next;
            vlb->next = NULL;
          }
          else
          {
            current_p = vlb;
            prev_p = NULL;
            while ( (current_p != NULL) &&
                    (current_p->bios_address < temp_p->bios_address))
            {
              prev_p = current_p;
              current_p = current_p->next;
            }
            if (prev_p != NULL)
            {
              prev_p->next = temp_p;
              temp_p = temp_p->next;
              prev_p->next->next = current_p;
            }
            else
            {
              vlb = temp_p;
              temp_p = temp_p->next;
              vlb->next = current_p;
            }
          }
          
          if (p->flags & AHC_BIOS_ENABLED)
            sort_list[0] = vlb;
          else
            sort_list[2] = vlb;
          
          break;
        }
        default:  /* All PCI controllers fall through to default */
        {

          p = temp_p;
          if (p->flags & AHC_BIOS_ENABLED) 
            pci = sort_list[1];
          else
            pci = sort_list[3];

          if (pci == NULL)
          {
            pci = temp_p;
            temp_p = temp_p->next;
            pci->next = NULL;
          }
          else
          {
            current_p = pci;
            prev_p = NULL;
            if (!aic7xxx_reverse_scan)
            {
              while ( (current_p != NULL) &&
                      ( (PCI_SLOT(current_p->pci_device_fn) |
                        (current_p->pci_bus << 8)) < 
                        (PCI_SLOT(temp_p->pci_device_fn) |
                        (temp_p->pci_bus << 8)) ) )
              {
                prev_p = current_p;
                current_p = current_p->next;
              }
            }
            else
            {
              while ( (current_p != NULL) &&
                      ( (PCI_SLOT(current_p->pci_device_fn) |
                        (current_p->pci_bus << 8)) > 
                        (PCI_SLOT(temp_p->pci_device_fn) |
                        (temp_p->pci_bus << 8)) ) )
              {
                prev_p = current_p;
                current_p = current_p->next;
              }
            }
            /*
             * Are we dealing with a 7985 where we need to sort the
             * channels as well, if so, the bios_address values should
             * be the same
             */
            if ( (current_p) && (temp_p->flags & AHC_MULTI_CHANNEL) &&
                 (temp_p->pci_bus == current_p->pci_bus) &&
                 (PCI_SLOT(temp_p->pci_device_fn) ==
                  PCI_SLOT(current_p->pci_device_fn)) )
            {
              if (temp_p->flags & AHC_CHNLB)
              {
                if ( !(temp_p->flags & AHC_CHANNEL_B_PRIMARY) )
                {
                  prev_p = current_p;
                  current_p = current_p->next;
                }
              }
              else
              {
                if (temp_p->flags & AHC_CHANNEL_B_PRIMARY)
                {
                  prev_p = current_p;
                  current_p = current_p->next;
                }
              }
            }
            if (prev_p != NULL)
            {
              prev_p->next = temp_p;
              temp_p = temp_p->next;
              prev_p->next->next = current_p;
            }
            else
            {
              pci = temp_p;
              temp_p = temp_p->next;
              pci->next = current_p;
            }
          }

          if (p->flags & AHC_BIOS_ENABLED)
            sort_list[1] = pci;
          else
            sort_list[3] = pci;

          break;
        }
      }  /* End of switch(temp_p->type) */
    } /* End of while (temp_p != NULL) */
    /*
     * At this point, the cards have been broken into 4 sorted lists, now
     * we run through the lists in order and register each controller
     */
    {
      int i;
      
      left = found;
      for (i=0; i<NUMBER(sort_list); i++)
      {
        temp_p = sort_list[i];
        while(temp_p != NULL)
        {
          template->name = board_names[temp_p->board_name_index];
          p = aic7xxx_alloc(template, temp_p);
          if (p != NULL)
          {
            p->instance = found - left;
            if (aic7xxx_register(template, p, (--left)) == 0)
            {
              found--;
              aic7xxx_release(p->host);
              scsi_unregister(p->host);
            }
            else if (aic7xxx_dump_card)
            {
              pause_sequencer(p);
              aic7xxx_print_card(p);
              aic7xxx_print_scratch_ram(p);
              unpause_sequencer(p, TRUE);
            }
          }
          current_p = temp_p;
          temp_p = (struct aic7xxx_host *)temp_p->next;
          kfree(current_p);
        }
      }
    }
  }
  return (found);
}

#ifdef AIC7XXX_FAKE_NEGOTIATION_CMDS

/*+F*************************************************************************
 * Function:
 *   aic7xxx_negotiation_complete
 *
 * Description:
 *   Handle completion events for our Negotiation commands.  Clear out the
 *   struct and get it ready for its next use.
 *-F*************************************************************************/
static void
aic7xxx_negotiation_complete(Scsi_Cmnd *cmd)
{
  return;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_build_negotiation_command
 *
 * Description:
 *   Build a Scsi_Cmnd structure to perform negotiation with or else send
 *   a pre-built command specifically for this purpose.
 *-F*************************************************************************/
static void
aic7xxx_build_negotiation_cmnd(struct aic7xxx_host *p, Scsi_Cmnd *old_cmd,
  int tindex)
{

  if ( (p->needwdtr & (1<<tindex)) && !(p->wdtr_pending & (1<<tindex)) )
  {
    if(p->dev_wdtr_cmnd[tindex] == NULL)
    {
      Scsi_Cmnd *cmd;

      if (!(p->dev_wdtr_cmnd[tindex] = kmalloc(sizeof(Scsi_Cmnd), GFP_ATOMIC)) )
      {
        return;
      }
      cmd = p->dev_wdtr_cmnd[tindex];
      memset(cmd, 0, sizeof(Scsi_Cmnd));
      memcpy(cmd, old_cmd, sizeof(Scsi_Cmnd));
      memset(&cmd->cmnd[0], 0, sizeof(cmd->cmnd));
      memset(&cmd->data_cmnd[0], 0, sizeof(cmd->data_cmnd));
      cmd->lun = 0;
      cmd->request_bufflen = 0;
      cmd->request_buffer = NULL;
      cmd->use_sg = cmd->old_use_sg = cmd->sglist_len = 0;
      cmd->bufflen = 0;
      cmd->buffer = NULL;
      cmd->underflow = 0;
      cmd->cmd_len = 6;
    }
    /*
     * Before sending this thing out, we also amke the cmd->next pointer
     * point to the real command so we can stuff any possible SENSE data
     * intp the real command instead of this fake command.  This has to be
     * done each time the command is built, not just the first time, hence
     * it's outside of the above if()...
     */
    p->dev_wdtr_cmnd[tindex]->next = old_cmd;
    aic7xxx_queue(p->dev_wdtr_cmnd[tindex], 
                  aic7xxx_negotiation_complete);
  }
  else if ( (p->needsdtr & (1<<tindex)) && !(p->sdtr_pending & (1<<tindex)) &&
            !(p->wdtr_pending & (1<<tindex)) )
  {
    if(p->dev_sdtr_cmnd[tindex] == NULL)
    {
      Scsi_Cmnd *cmd;

      if (!(p->dev_sdtr_cmnd[tindex] = kmalloc(sizeof(Scsi_Cmnd), GFP_ATOMIC)) )
      {
        return;
      }
      cmd = p->dev_sdtr_cmnd[tindex];
      memset(cmd, 0, sizeof(Scsi_Cmnd));
      memcpy(cmd, old_cmd, sizeof(Scsi_Cmnd));
      memset(&cmd->cmnd[0], 0, sizeof(cmd->cmnd));
      memset(&cmd->data_cmnd[0], 0, sizeof(cmd->data_cmnd));
      cmd->lun = 0;
      cmd->request_bufflen = 0;
      cmd->request_buffer = NULL;
      cmd->use_sg = cmd->old_use_sg = cmd->sglist_len = 0;
      cmd->bufflen = 0;
      cmd->buffer = NULL;
      cmd->underflow = 0;
      cmd->cmd_len = 6;
    }
    /*
     * Before sending this thing out, we also amke the cmd->next pointer
     * point to the real command so we can stuff any possible SENSE data
     * intp the real command instead of this fake command.  This has to be
     * done each time the command is built, not just the first time, hence
     * it's outside of the above if()...
     */
    p->dev_sdtr_cmnd[tindex]->next = old_cmd;
    aic7xxx_queue(p->dev_sdtr_cmnd[tindex], 
                  aic7xxx_negotiation_complete);
  }
}

#endif

#ifdef AIC7XXX_VERBOSE_DEBUGGING
/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_scb
 *
 * Description:
 *   Dump the byte codes for an about to be sent SCB.
 *-F*************************************************************************/
static void
aic7xxx_print_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int i;
  unsigned char *x;  

  x = (unsigned char *)&scb->hscb->control;

  for(i=0; i<32; i++)
  {
    printk("%02x ", x[i]);
  }
  printk("\n");
}
#endif

/*+F*************************************************************************
 * Function:
 *   aic7xxx_buildscb
 *
 * Description:
 *   Build a SCB.
 *-F*************************************************************************/
static void
aic7xxx_buildscb(struct aic7xxx_host *p, Scsi_Cmnd *cmd,
    struct aic7xxx_scb *scb)
{
  unsigned short mask;
  struct aic7xxx_hwscb *hscb;

  mask = (0x01 << TARGET_INDEX(cmd));
  hscb = scb->hscb;

  /*
   * Setup the control byte if we need negotiation and have not
   * already requested it.
   */
  hscb->control = 0;
  scb->tag_action = 0;
  if (p->discenable & mask)
  {
    hscb->control |= DISCENB;
    if (p->tagenable & mask)
    {
      cmd->tag = hscb->tag;
      p->dev_commands_sent[TARGET_INDEX(cmd)]++;
      if (p->dev_commands_sent[TARGET_INDEX(cmd)] < 200)
      {
        hscb->control |= MSG_SIMPLE_Q_TAG;
        scb->tag_action = MSG_SIMPLE_Q_TAG;
      }
      else
      {
        if (p->orderedtag & mask)
        {
          hscb->control |= MSG_ORDERED_Q_TAG;
          scb->tag_action = MSG_ORDERED_Q_TAG;
        }
        else
        {
          hscb->control |= MSG_SIMPLE_Q_TAG;
          scb->tag_action = MSG_SIMPLE_Q_TAG;
        }
        p->dev_commands_sent[TARGET_INDEX(cmd)] = 0;
      }
    }
  }
  if (p->dev_flags[TARGET_INDEX(cmd)] & DEVICE_SCANNED)
  {
#ifdef AIC7XXX_FAKE_NEGOTIATION_CMDS
    if ( (p->needwdtr & mask) && !(p->wdtr_pending & mask) )
    {
      if (cmd == p->dev_wdtr_cmnd[TARGET_INDEX(cmd)])
      {
        p->wdtr_pending |= mask;
        scb->flags |= SCB_MSGOUT_WDTR;
        hscb->control &= DISCENB;
        hscb->control |= MK_MESSAGE;
        scb->tag_action = 0;
      }
      else
      {
        aic7xxx_build_negotiation_cmnd(p, cmd, TARGET_INDEX(cmd));
      }
    }
    else if ( (p->needsdtr & mask) && !(p->sdtr_pending & mask) &&
              !(p->wdtr_pending & mask) )
    {
      if (cmd == p->dev_sdtr_cmnd[TARGET_INDEX(cmd)])
      {
        p->sdtr_pending |= mask;
        scb->flags |= SCB_MSGOUT_SDTR;
        hscb->control &= DISCENB;
        hscb->control |= MK_MESSAGE;
        scb->tag_action = 0;
      }
      else if (cmd != p->dev_wdtr_cmnd[TARGET_INDEX(cmd)])
      {
        aic7xxx_build_negotiation_cmnd(p, cmd, TARGET_INDEX(cmd));
      }
    }
#else
    if ( (p->needwdtr & mask) && !(p->wdtr_pending & mask) &&
         !(p->sdtr_pending & mask) && (cmd->lun == 0) )
    {
      p->wdtr_pending |= mask;
      scb->flags |= SCB_MSGOUT_WDTR;
      hscb->control &= DISCENB;
      hscb->control |= MK_MESSAGE;
      scb->tag_action = 0;
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Building WDTR command.\n", p->host_no,
               CTL_OF_CMD(cmd));
#endif
    }
    else if ( (p->needsdtr & mask) && !(p->wdtr_pending & mask) &&
              !(p->sdtr_pending & mask) && (cmd->lun == 0) )
    {
      p->sdtr_pending |= mask;
      scb->flags |= SCB_MSGOUT_SDTR;
      hscb->control &= DISCENB;
      hscb->control |= MK_MESSAGE;
      scb->tag_action = 0;
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Building SDTR command.\n", p->host_no,
               CTL_OF_CMD(cmd));
#endif
    }
#endif
  }
  hscb->target_channel_lun = ((cmd->target << 4) & 0xF0) |
        ((cmd->channel & 0x01) << 3) | (cmd->lun & 0x07);

  /*
   * The interpretation of request_buffer and request_bufflen
   * changes depending on whether or not use_sg is zero; a
   * non-zero use_sg indicates the number of elements in the
   * scatter-gather array.
   */

  /*
   * XXX - this relies on the host data being stored in a
   *       little-endian format.
   */
  hscb->SCSI_cmd_length = cmd->cmd_len;
  hscb->SCSI_cmd_pointer = cpu_to_le32(VIRT_TO_BUS(cmd->cmnd));

  if (cmd->use_sg)
  {
    struct scatterlist *sg;  /* Must be mid-level SCSI code scatterlist */

    /*
     * We must build an SG list in adapter format, as the kernel's SG list
     * cannot be used directly because of data field size (__alpha__)
     * differences and the kernel SG list uses virtual addresses where
     * we need physical addresses.
     */
    int i;

    sg = (struct scatterlist *)cmd->request_buffer;
    scb->sg_length = 0;
    /*
     * Copy the segments into the SG array.  NOTE!!! - We used to
     * have the first entry both in the data_pointer area and the first
     * SG element.  That has changed somewhat.  We still have the first
     * entry in both places, but now we download the address of
     * scb->sg_list[1] instead of 0 to the sg pointer in the hscb.
     */
    for (i = 0; i < cmd->use_sg; i++)
    {
      scb->sg_list[i].address = cpu_to_le32(VIRT_TO_BUS(sg[i].address));
      scb->sg_list[i].length = cpu_to_le32(sg[i].length);
      scb->sg_length += sg[i].length;
    }
    /* Copy the first SG into the data pointer area. */
    hscb->data_pointer = scb->sg_list[0].address;
    hscb->data_count = scb->sg_list[0].length;
    scb->sg_count = cmd->use_sg;
    hscb->SG_segment_count = cmd->use_sg;
    hscb->SG_list_pointer = cpu_to_le32(VIRT_TO_BUS(&scb->sg_list[1]));

  }
  else
  {
    if (cmd->request_bufflen)
    {
      scb->sg_count = 1;
      scb->sg_list[0].address = cpu_to_le32(VIRT_TO_BUS(cmd->request_buffer));
      scb->sg_list[0].length = cpu_to_le32(cmd->request_bufflen);
      scb->sg_length = cmd->request_bufflen;
      hscb->SG_segment_count = 1;
      hscb->SG_list_pointer = cpu_to_le32(VIRT_TO_BUS(&scb->sg_list[0]));
      hscb->data_count = scb->sg_list[0].length;
      hscb->data_pointer = scb->sg_list[0].address;
    }
    else
    {
      scb->sg_count = 0;
      scb->sg_length = 0;
      hscb->SG_segment_count = 0;
      hscb->SG_list_pointer = 0;
      hscb->data_count = 0;
      hscb->data_pointer = 0;
    }
  }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if((cmd->cmnd[0] == TEST_UNIT_READY) && (aic7xxx_verbose & VERBOSE_PROBE2))
  {
    aic7xxx_print_scb(p, scb);
  }
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue
 *
 * Description:
 *   Queue a SCB to the controller.
 *-F*************************************************************************/
int
aic7xxx_queue(Scsi_Cmnd *cmd, void (*fn)(Scsi_Cmnd *))
{
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
#ifdef AIC7XXX_VERBOSE_DEBUGGING
  int tindex = TARGET_INDEX(cmd);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags = 0;
#endif

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  /*
   * Check to see if channel was scanned.
   */
  
#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if (!(p->flags & AHC_A_SCANNED) && (cmd->channel == 0))
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
      printk(INFO_LEAD "Scanning channel for devices.\n",
        p->host_no, 0, -1, -1);
    p->flags |= AHC_A_SCANNED;
  }
  else
  {
    if (!(p->flags & AHC_B_SCANNED) && (cmd->channel == 1))
    {
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(INFO_LEAD "Scanning channel for devices.\n",
          p->host_no, 1, -1, -1);
      p->flags |= AHC_B_SCANNED;
    }
  }

  if (p->dev_active_cmds[tindex] > (cmd->device->queue_depth + 1))
  {
    printk(WARN_LEAD "Commands queued exceeds queue "
           "depth, active=%d\n",
           p->host_no, CTL_OF_CMD(cmd), 
           p->dev_active_cmds[tindex]);
    if ( p->dev_active_cmds[tindex] > 220 )
      p->dev_active_cmds[tindex] = 0;
  }
#endif

  scb = scbq_remove_head(&p->scb_data->free_scbs);
  if (scb == NULL)
  {
    DRIVER_LOCK
    aic7xxx_allocate_scb(p);
    DRIVER_UNLOCK
    scb = scbq_remove_head(&p->scb_data->free_scbs);
  }
  if (scb == NULL)
  {
    printk(WARN_LEAD "Couldn't get a free SCB.\n", p->host_no,
           CTL_OF_CMD(cmd));
    cmd->result = (DID_BUS_BUSY << 16);
    DRIVER_LOCK
    aic7xxx_queue_cmd_complete(p, cmd);
    DRIVER_UNLOCK
    return 0;
  }
  else
  {
    scb->cmd = cmd;
    aic7xxx_position(cmd) = scb->hscb->tag;

    /*
     * Construct the SCB beforehand, so the sequencer is
     * paused a minimal amount of time.
     */
    aic7xxx_buildscb(p, cmd, scb);

    /*
     * Make sure the Scsi_Cmnd pointer is saved, the struct it points to
     * is set up properly, and the parity error flag is reset, then send
     * the SCB to the sequencer and watch the fun begin.
     */
    cmd->scsi_done = fn;
    cmd->result = DID_OK;
    memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));
    aic7xxx_error(cmd) = DID_OK;
    aic7xxx_status(cmd) = 0;
    cmd->host_scribble = NULL;

    scb->flags |= SCB_ACTIVE | SCB_WAITINGQ;

    DRIVER_LOCK
    scbq_insert_tail(&p->waiting_scbs, scb);
    if ( (p->flags & (AHC_IN_ISR | AHC_IN_ABORT | AHC_IN_RESET)) == 0)
    {
      aic7xxx_run_waiting_queues(p);
    }
    DRIVER_UNLOCK
  }
  return (0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_bus_device_reset
 *
 * Description:
 *   Abort or reset the current SCSI command(s).  If the scb has not
 *   previously been aborted, then we attempt to send a BUS_DEVICE_RESET
 *   message to the target.  If the scb has previously been unsuccessfully
 *   aborted, then we will reset the channel and have all devices renegotiate.
 *   Returns an enumerated type that indicates the status of the operation.
 *-F*************************************************************************/
static int
aic7xxx_bus_device_reset(struct aic7xxx_host *p, Scsi_Cmnd *cmd)
{
  struct aic7xxx_scb   *scb;
  struct aic7xxx_hwscb *hscb;
  int result = -1;
  int channel;
  unsigned char saved_scbptr, lastphase;
  unsigned char hscb_index;
  int disconnected;

  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  hscb = scb->hscb;

  lastphase = aic_inb(p, LASTPHASE);
  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
  {
    printk(INFO_LEAD "Bus Device reset, scb flags 0x%x, ",
         p->host_no, CTL_OF_SCB(scb), scb->flags);
    switch (lastphase)
    {
      case P_DATAOUT:
        printk("Data-Out phase\n");
        break;
      case P_DATAIN:
        printk("Data-In phase\n");
        break;
      case P_COMMAND:
        printk("Command phase\n");
        break;
      case P_MESGOUT:
        printk("Message-Out phase\n");
        break;
      case P_STATUS:
        printk("Status phase\n");
        break;
      case P_MESGIN:
        printk("Message-In phase\n");
        break;
      default:
      /*
       * We're not in a valid phase, so assume we're idle.
       */
        printk("while idle, LASTPHASE = 0x%x\n", lastphase);
        break;
    }
    printk(INFO_LEAD "SCSISIGI 0x%x, SEQADDR 0x%x, SSTAT0 0x%x, SSTAT1 "
         "0x%x\n", p->host_no, CTL_OF_SCB(scb),
         aic_inb(p, SCSISIGI),
         aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
         aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
  }

  channel = cmd->channel;

    /*
     * Send a Device Reset Message:
     * The target that is holding up the bus may not be the same as
     * the one that triggered this timeout (different commands have
     * different timeout lengths).  Our strategy here is to queue an
     * abort message to the timed out target if it is disconnected.
     * Otherwise, if we have an active target we stuff the message buffer
     * with an abort message and assert ATN in the hopes that the target
     * will let go of the bus and go to the mesgout phase.  If this
     * fails, we'll get another timeout a few seconds later which will
     * attempt a bus reset.
     */
  saved_scbptr = aic_inb(p, SCBPTR);
  disconnected = FALSE;

  if (lastphase != P_BUSFREE)
  {
    if (aic_inb(p, SCB_TAG) >= p->scb_data->numscbs)
    {
      printk(WARN_LEAD "Invalid SCB ID %d is active, "
             "SCB flags = 0x%x.\n", p->host_no,
            CTL_OF_CMD(cmd), scb->hscb->tag, scb->flags);
      return(SCSI_RESET_ERROR);
    }
    if (scb->hscb->tag == aic_inb(p, SCB_TAG))
    { 
      if ( (lastphase != P_MESGOUT) && (lastphase != P_MESGIN) )
      {
        if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
          printk(INFO_LEAD "Device reset message in "
                "message buffer\n", p->host_no, CTL_OF_SCB(scb));
        scb->flags |= SCB_RESET | SCB_DEVICE_RESET;
        aic7xxx_error(scb->cmd) = DID_RESET;
        p->dev_flags[TARGET_INDEX(scb->cmd)] &= 
                ~DEVICE_SUCCESS;
        p->dev_flags[TARGET_INDEX(scb->cmd)] |= 
                BUS_DEVICE_RESET_PENDING;
        /* Send the abort message to the active SCB. */
        aic_outb(p, HOST_MSG, MSG_OUT);
        aic_outb(p, lastphase | ATNO, SCSISIGO);
        return(SCSI_RESET_PENDING);
      }
      else
      {
        /* We want to send out the message, but it could screw an already */
        /* in place and being used message.  Instead, we return an error  */
        /* to try and start the bus reset phase since this command is     */
        /* probably hung (aborts failed, and now reset is failing).  We   */
        /* also make sure to set BUS_DEVICE_RESET_PENDING so we won't try */
        /* any more on this device, but instead will escalate to a bus or */
        /* host reset (additionally, we won't try to abort any more).     */
        printk(WARN_LEAD "Device reset, Message buffer "
                "in use\n", p->host_no, CTL_OF_SCB(scb));
        scb->flags |= SCB_RESET | SCB_DEVICE_RESET;
        aic7xxx_error(scb->cmd) = DID_RESET;
        p->dev_flags[TARGET_INDEX(scb->cmd)] &= 
                ~DEVICE_SUCCESS;
        p->dev_flags[TARGET_INDEX(scb->cmd)] |= 
                BUS_DEVICE_RESET_PENDING;
        return(SCSI_RESET_ERROR);
      }
    }
  } /* if (last_phase != P_BUSFREE).....indicates we are idle and can work */
  hscb_index = aic7xxx_find_scb(p, scb);
  if (hscb_index == SCB_LIST_NULL)
  {
    disconnected = (aic7xxx_scb_on_qoutfifo(p, scb)) ? FALSE : TRUE;
  }
  else
  {
    aic_outb(p, hscb_index, SCBPTR);
    if (aic_inb(p, SCB_CONTROL) & DISCONNECTED)
    {
      disconnected = TRUE;
    }
  }
  if (disconnected)
  {
        /*
         * Simply set the MK_MESSAGE flag and the SEQINT handler will do
         * the rest on a reconnect.
         */
    scb->hscb->control |= MK_MESSAGE;
    scb->flags |= SCB_RESET | SCB_DEVICE_RESET;
    p->dev_flags[TARGET_INDEX(scb->cmd)] &= ~DEVICE_SUCCESS;
    p->dev_flags[TARGET_INDEX(scb->cmd)] |= 
        BUS_DEVICE_RESET_PENDING;
    if (hscb_index != SCB_LIST_NULL)
    {
      unsigned char scb_control;

      aic_outb(p, hscb_index, SCBPTR);
      scb_control = aic_inb(p, SCB_CONTROL);
      aic_outb(p, scb_control | MK_MESSAGE, SCB_CONTROL);
    }
        /*
         * Actually requeue this SCB in case we can select the
         * device before it reconnects.  If the transaction we
         * want to abort is not tagged, then this will be the only
         * outstanding command and we can simply shove it on the
         * qoutfifo and be done.  If it is tagged, then it goes right
         * in with all the others, no problem :)  We need to add it
         * to the qinfifo and let the sequencer know it is there.
         * Now, the only problem left to deal with is, *IF* this
         * command completes, in spite of the MK_MESSAGE bit in the
         * control byte, then we need to pick that up in the interrupt
         * routine and clean things up.  This *shouldn't* ever happen.
         */
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Queueing device reset "
           "command.\n", p->host_no, CTL_OF_SCB(scb));
    p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
    if (p->features & AHC_QUEUE_REGS)
      aic_outb(p, p->qinfifonext, HNSCB_QOFF);
    else
      aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
    scb->flags |= SCB_QUEUED_ABORT;
    result = SCSI_RESET_PENDING;
  }
  else if (result == -1)
  {
    result = SCSI_RESET_ERROR;
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  return (result);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_panic_abort
 *
 * Description:
 *   Abort the current SCSI command(s).
 *-F*************************************************************************/
void
aic7xxx_panic_abort(struct aic7xxx_host *p, Scsi_Cmnd *cmd)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
  int i, mask, found, need_tag;
  struct aic7xxx_scb *scb;
  unsigned char qinpos, hscbp;

  found = FALSE;
#endif

  printk("aic7xxx driver version %s/%s\n", AIC7XXX_C_VERSION,
         UTS_RELEASE);
  printk("Controller type:\n    %s\n", board_names[p->board_name_index]);
  printk("p->flags=0x%x, p->chip=0x%x, p->features=0x%x, "
         "sequencer %s paused\n",
     p->flags, p->chip, p->features,
    (aic_inb(p, HCNTRL) & PAUSE) ? "is" : "isn't" );
  pause_sequencer(p);
  disable_irq(p->irq);
  aic7xxx_print_card(p);
  aic7xxx_print_scratch_ram(p);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
  for(i=0; i<MAX_TARGETS; i++)
  {
    if(p->dev_flags[i] & DEVICE_PRESENT)
    {
      mask = (0x01 << i);
      printk(INFO_LEAD "dev_flags=0x%x, WDTR:%c/%c/%c, SDTR:%c/%c/%c,"
             " q_depth=%d:%d:%d\n",
        p->host_no, 0, i, 0, p->dev_flags[i],
        (p->wdtr_pending & mask) ? 'Y' : 'N',
        (p->needwdtr & mask) ? 'Y' : 'N',
        (p->needwdtr_copy & mask) ? 'Y' : 'N',
        (p->sdtr_pending & mask) ? 'Y' : 'N',
        (p->needsdtr & mask) ? 'Y' : 'N',
        (p->needsdtr_copy & mask) ? 'Y' : 'N',
        p->dev_active_cmds[i],
        p->dev_max_queue_depth[i], p->dev_mid_level_queue_depth[i]);
      printk(INFO_LEAD "targ_scsirate=0x%x", p->host_no, 0, i, 0,
             aic_inb(p, TARG_SCSIRATE + i));
      if (p->features & AHC_ULTRA2)
        printk(", targ_offset=%d", aic_inb(p, TARG_OFFSET + i));
      printk("\n");
    }
  }
  /*
   * Search for this command and see if we can't track it down, it's the
   * one causing the timeout.  Print out this command first, then all other
   * active commands afterwords.
   */
  need_tag = -1;
  if ( cmd )
  {
    scb = p->scb_data->scb_array[aic7xxx_position(cmd)];
    if ( (scb->flags & SCB_ACTIVE) && (scb->cmd == cmd) )
    {
      printk("Timed out command is scb #%d:\n", scb->hscb->tag);
      printk("Tag%d: flags=0x%x, control=0x%x, TCL=0x%x, %s\n", scb->hscb->tag,
             scb->flags, scb->hscb->control, scb->hscb->target_channel_lun,
             (scb->flags & SCB_WAITINGQ) ? "WAITINGQ" : "Sent" );
      need_tag = scb->hscb->tag;
      if (scb->flags & SCB_WAITINGQ) found=TRUE;
    }
  }
  printk("QINFIFO: (TAG) ");
  qinpos = aic_inb(p, QINPOS);
  while ( qinpos != p->qinfifonext )
  {
    if (p->qinfifo[qinpos] == need_tag)
      found=TRUE;
    printk("%d ", p->qinfifo[qinpos++]);
  }  
  printk("\n");
  printk("Current SCB: (SCBPTR/TAG/CONTROL) %d/%d/0x%x\n", aic_inb(p, SCBPTR),
         aic_inb(p, SCB_TAG), aic_inb(p, SCB_CONTROL) );
  if (aic_inb(p, SCB_TAG) == need_tag)  found=TRUE;
  printk("WAITING_SCBS: (SCBPTR/TAG/CONTROL) %d->",
         hscbp = aic_inb(p, WAITING_SCBH));
  while (hscbp != SCB_LIST_NULL)
  {
    aic_outb(p, hscbp, SCBPTR);
    printk("%d/%d/0x%x ", hscbp, aic_inb(p, SCB_TAG), aic_inb(p, SCB_CONTROL));
    hscbp = aic_inb(p, SCB_NEXT);
    if (aic_inb(p, SCB_TAG) == need_tag)  found=TRUE;
  }
  printk("\n");
  printk("DISCONNECTED_SCBS: (SCBPTR/TAG/CONTROL) %d->",
         hscbp = aic_inb(p, DISCONNECTED_SCBH));
  while (hscbp != SCB_LIST_NULL)
  {
    aic_outb(p, hscbp, SCBPTR);
    printk("%d/%d/0x%x ", hscbp, aic_inb(p, SCB_TAG), aic_inb(p, SCB_CONTROL));
    hscbp = aic_inb(p, SCB_NEXT);
    if (aic_inb(p, SCB_TAG) == need_tag)  found=TRUE;
  }
  printk("\n");
  printk("FREE_SCBS: (SCBPTR/TAG/CONTROL) %d->",
         hscbp = aic_inb(p, FREE_SCBH));
  while (hscbp != SCB_LIST_NULL)
  {
    aic_outb(p, hscbp, SCBPTR);
    printk("%d/%d/0x%x ", hscbp, aic_inb(p, SCB_TAG), aic_inb(p, SCB_CONTROL));
    hscbp = aic_inb(p, SCB_NEXT);
  }
  printk("\n");

  if (found == FALSE)
  {
    /*
     * We haven't found the offending SCB yet, and it should be around
     * somewhere, so go look for it in the cards SCBs.
     */
    printk("SCBPTR CONTROL TAG PREV NEXT\n");
    for(i=0; i<p->scb_data->maxhscbs; i++)
    {
      aic_outb(p, i, SCBPTR);
      printk("   %3d      %02x  %02x   %02x   %02x\n", i,
             aic_inb(p, SCB_CONTROL), aic_inb(p, SCB_TAG),
             aic_inb(p, SCB_PREV), aic_inb(p, SCB_NEXT));
    }
  }
  

  for (i=0; i < p->scb_data->numscbs; i++)
  {
    scb = p->scb_data->scb_array[i];
    if ( (scb->flags & SCB_ACTIVE) && (scb->cmd != cmd) )
    {
      printk("Tag%d: flags=0x%x, control=0x%x, TCL=0x%x, %s\n", scb->hscb->tag,
             scb->flags, scb->hscb->control, scb->hscb->target_channel_lun,
             (scb->flags & SCB_WAITINGQ) ? "WAITINGQ" : "Sent" );
    }
  }
#endif
  sti();
  for(;;) barrier();
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort
 *
 * Description:
 *   Abort the current SCSI command(s).
 *-F*************************************************************************/
int
aic7xxx_abort(Scsi_Cmnd *cmd)
{
  struct aic7xxx_scb  *scb = NULL;
  struct aic7xxx_host *p;
  int    result, found=0;
  unsigned char tmp_char, saved_hscbptr, next_hscbptr, prev_hscbptr;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags = 0;
#endif
  Scsi_Cmnd *cmd_next, *cmd_prev;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);

  /*
   * I added a new config option to the driver: "panic_on_abort" that will
   * cause the driver to panic and the machine to stop on the first abort
   * or reset call into the driver.  At that point, it prints out a lot of
   * usefull information for me which I can then use to try and debug the
   * problem.  Simply enable the boot time prompt in order to activate this
   * code.
   */
  if (aic7xxx_panic_on_abort)
    aic7xxx_panic_abort(p, cmd);

  DRIVER_LOCK

/*
 *  Run the isr to grab any command in the QOUTFIFO and any other misc.
 *  assundry tasks.  This should also set up the bh handler if there is
 *  anything to be done, but it won't run until we are done here since
 *  we are following a straight code path without entering the scheduler
 *  code.
 */

  pause_sequencer(p);
  while ( (aic_inb(p, INTSTAT) & INT_PEND) && !(p->flags & AHC_IN_ISR))
  {
    aic7xxx_isr(p->irq, p, (void *)NULL);
    pause_sequencer(p);
    aic7xxx_done_cmds_complete(p);
  }

  if ((scb == NULL) || (cmd->serial_number != cmd->serial_number_at_timeout))
                      /*  Totally bogus cmd since it points beyond our  */
  {                   /*  valid SCB range or doesn't even match it's own*/
                      /*  timeout serial number.                        */
    if (aic7xxx_verbose & VERBOSE_ABORT_MID)
      printk(INFO_LEAD "Abort called with bogus Scsi_Cmnd "
        "pointer.\n", p->host_no, CTL_OF_CMD(cmd));
    unpause_sequencer(p, FALSE);
    DRIVER_UNLOCK
    return(SCSI_ABORT_NOT_RUNNING);
  }
  if (scb->cmd != cmd)  /*  Hmmm...either this SCB is currently free with a */
  {                     /*  NULL cmd pointer (NULLed out when freed) or it  */
                        /*  has already been recycled for another command   */
                        /*  Either way, this SCB has nothing to do with this*/
                        /*  command and we need to deal with cmd without    */
                        /*  touching the SCB.                               */
                        /*  The theory here is to return a value that will  */
                        /*  make the queued for complete command actually   */
                        /*  finish successfully, or to indicate that we     */
                        /*  don't have this cmd any more and the mid level  */
                        /*  code needs to find it.                          */
    cmd_next = p->completeq.head;
    cmd_prev = NULL;
    while (cmd_next != NULL) 
    {
      if (cmd_next == cmd) 
      {
        if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
          printk(INFO_LEAD "Abort called for command "
          "on completeq, completing.\n", p->host_no, CTL_OF_CMD(cmd));
        if ( cmd_prev == NULL )
          p->completeq.head = (Scsi_Cmnd *)cmd_next->host_scribble;
        else
          cmd_prev->host_scribble = cmd_next->host_scribble;
        cmd_next->scsi_done(cmd_next);
        unpause_sequencer(p, FALSE);
        DRIVER_UNLOCK
        return(SCSI_ABORT_NOT_RUNNING); /* It's already back as a successful
                                         * completion */
      }                                  
      cmd_prev = cmd_next;
      cmd_next = (Scsi_Cmnd *)cmd_next->host_scribble;
    }
    if (aic7xxx_verbose & VERBOSE_ABORT_MID)
      printk(INFO_LEAD "Abort called for already completed"
        " command.\n", p->host_no, CTL_OF_CMD(cmd));
    unpause_sequencer(p, FALSE);
    DRIVER_UNLOCK
    return(SCSI_ABORT_NOT_RUNNING);
  }
    
/*   At this point we know the following:
 *     the SCB pointer is valid
 *     the command pointer passed in to us and the scb->cmd pointer match
 *     this then means that the command we need to abort is the same as the
 *     command held by the scb pointer and is a valid abort request.
 *   Now, we just have to figure out what to do from here.  Current plan is:
 *     if we have already been here on this command, escalate to a reset
 *     if scb is on waiting list or QINFIFO, send it back as aborted, but
 *       we also need to be aware of the possibility that we could be using
 *       a faked negotiation command that is holding this command up,  if
 *       so we need to take care of that command instead, which means we
 *       would then treat this one like it was sitting around disconnected
 *       instead.
 *     if scb is on WAITING_SCB list in sequencer, free scb and send back
 *     if scb is disconnected and not completed, abort with abort message
 *     if scb is currently running, then it may be causing the bus to hang
 *       so we want a return value that indicates a reset would be appropriate
 *       if the command does not finish shortly
 *     if scb is already complete but not on completeq, we're screwed because
 *       this can't happen (except if the command is in the QOUTFIFO, in which
 *       case we would like it to complete successfully instead of having to
 *       to be re-done)
 *   All other scenarios already dealt with by previous code.
 */

  if ( scb->flags & (SCB_ABORT | SCB_RESET | SCB_QUEUED_ABORT) )
  {
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
      printk(INFO_LEAD "SCB aborted once already, "
        "escalating.\n", p->host_no, CTL_OF_SCB(scb));
    unpause_sequencer(p, FALSE);
    DRIVER_UNLOCK
    return(SCSI_ABORT_SNOOZE);
  }
  if ( (p->flags & (AHC_RESET_PENDING | AHC_ABORT_PENDING)) || 
          (p->dev_flags[TARGET_INDEX(scb->cmd)] & 
           BUS_DEVICE_RESET_PENDING) )
  {
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
      printk(INFO_LEAD "Reset/Abort pending for this "
        "device, not wasting our time.\n", p->host_no, CTL_OF_SCB(scb));
    unpause_sequencer(p, FALSE);
    DRIVER_UNLOCK
    return(SCSI_ABORT_PENDING);
  }

  found = 0;
  p->flags |= AHC_IN_ABORT;
  if (aic7xxx_verbose & VERBOSE_ABORT)
    printk(INFO_LEAD "Aborting scb %d, flags 0x%x\n",
         p->host_no, CTL_OF_SCB(scb), scb->hscb->tag, scb->flags);

/*
 *   First, let's check to see if the currently running command is our target
 *    since if it is, the return is fairly easy and quick since we don't want
 *    to touch the command in case it might complete, but we do want a timeout
 *    in case it's actually hung, so we really do nothing, but tell the mid
 *    level code to reset the timeout.
 */

  if ( scb->hscb->tag == aic_inb(p, SCB_TAG) )
  {
   /*
    *  Check to see if the sequencer is just sitting on this command, or
    *   if it's actively being run.
    */
    result = aic_inb(p, LASTPHASE);
    switch (result)
    {
      case P_DATAOUT:    /*    For any of these cases, we can assume we are */
      case P_DATAIN:     /*    an active command and act according.  For    */
      case P_COMMAND:    /*    anything else we are going to fall on through*/
      case P_STATUS:     /*    The SCSI_ABORT_SNOOZE will give us two abort */
      case P_MESGOUT:    /*    chances to finish and then escalate to a     */
      case P_MESGIN:     /*    reset call                                   */
        if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
          printk(INFO_LEAD "SCB is currently active.  "
                "Waiting on completion.\n", p->host_no, CTL_OF_SCB(scb));
        unpause_sequencer(p, FALSE);
        p->flags &= ~AHC_IN_ABORT;
        scb->flags |= SCB_RECOVERY_SCB; /*  Note the fact that we've been  */
        p->flags |= AHC_ABORT_PENDING;  /*  here so we will know not to    */
        DRIVER_UNLOCK                   /*  muck with other SCBs if this   */
        return(SCSI_ABORT_PENDING);     /*  one doesn't complete and clear */
        break;                          /*  out.                           */
      default:
        break;
    }
  }

  if ((found == 0) && (scb->flags & SCB_WAITINGQ))
  {
    int tindex = TARGET_INDEX(cmd);
#ifdef AIC7XXX_FAKE_NEGOTIATION_CMDS
    unsigned short mask;

    mask = (1 << tindex);

    if (p->wdtr_pending & mask)
    {
      if (p->dev_wdtr_cmnd[tindex]->next != cmd)
        found = 1;
      else
        found = 0;
    }
    else if (p->sdtr_pending & mask)
    {
      if (p->dev_sdtr_cmnd[tindex]->next != cmd)
        found = 1;
      else
        found = 0;
    }
    else
    {
      found = 1;
    }
    if (found == 0)
    {
      /*
       * OK..this means the command we are currently getting an abort
       * for has an outstanding negotiation command in front of it.
       * We don't really have a way to tie back into the negotiation
       * commands, so we just send this back as pending, then it
       * will get reset in 2 seconds.
       */
      unpause_sequencer(p, TRUE);
      scb->flags |= SCB_ABORT;
      DRIVER_UNLOCK
      return(SCSI_ABORT_PENDING);
    }
#endif
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS) 
      printk(INFO_LEAD "SCB found on waiting list and "
          "aborted.\n", p->host_no, CTL_OF_SCB(scb));
    scbq_remove(&p->waiting_scbs, scb);
    scbq_remove(&p->delayed_scbs[tindex], scb);
    p->dev_active_cmds[tindex]++;
    p->activescbs++;
    scb->flags &= ~(SCB_WAITINGQ | SCB_ACTIVE);
    scb->flags |= SCB_ABORT | SCB_QUEUED_FOR_DONE;
    found = 1;
  }

/*
 *  We just checked the waiting_q, now for the QINFIFO
 */
  if ( found == 0 )
  {
    if ( ((found = aic7xxx_search_qinfifo(p, cmd->target, 
                     cmd->channel,
                     cmd->lun, scb->hscb->tag, SCB_ABORT | SCB_QUEUED_FOR_DONE,
                     FALSE, NULL)) != 0) &&
                    (aic7xxx_verbose & VERBOSE_ABORT_PROCESS))
      printk(INFO_LEAD "SCB found in QINFIFO and "
        "aborted.\n", p->host_no, CTL_OF_SCB(scb));
  }

/*
 *  QINFIFO, waitingq, completeq done.  Next, check WAITING_SCB list in card
 */

  if ( found == 0 )
  {
    unsigned char scb_next_ptr;
    prev_hscbptr = SCB_LIST_NULL;
    saved_hscbptr = aic_inb(p, SCBPTR);
    next_hscbptr = aic_inb(p, WAITING_SCBH);
    while ( next_hscbptr != SCB_LIST_NULL )
    {
      aic_outb(p,  next_hscbptr, SCBPTR );
      if ( scb->hscb->tag == aic_inb(p, SCB_TAG) )
      {
        found = 1;
        if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
          printk(INFO_LEAD "SCB found on hardware waiting"
            " list and aborted.\n", p->host_no, CTL_OF_SCB(scb));
        if ( prev_hscbptr == SCB_LIST_NULL )
        {
            aic_outb(p, aic_inb(p, SCB_NEXT), WAITING_SCBH);
            /* stop the selection since we just
             * grabbed the scb out from under the
             * card
             */
            aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
            aic_outb(p, CLRSELTIMEO, CLRSINT1);
        }
        else
        {
            scb_next_ptr = aic_inb(p, SCB_NEXT);
            aic_outb(p, prev_hscbptr, SCBPTR);
            aic_outb(p, scb_next_ptr, SCB_NEXT);
            aic_outb(p, next_hscbptr, SCBPTR);
        }
        aic_outb(p, SCB_LIST_NULL, SCB_TAG);
        aic_outb(p, 0, SCB_CONTROL);
        aic7xxx_add_curscb_to_free_list(p);
        scb->flags = SCB_ABORT | SCB_QUEUED_FOR_DONE;
        break;
      }
      prev_hscbptr = next_hscbptr;
      next_hscbptr = aic_inb(p, SCB_NEXT);
    }
    aic_outb(p,  saved_hscbptr, SCBPTR );
  }
        
/*
 *  Hmmm...completeq, QOUTFIFO, QINFIFO, WAITING_SCBH, waitingq all checked.
 *  OK...the sequencer's paused, interrupts are off, and we haven't found the
 *  command anyplace where it could be easily aborted.  Time for the hard
 *  work.  We also know the command is valid.  This essentially means the
 *  command is disconnected, or connected but not into any phases yet, which
 *  we know due to the tests we ran earlier on the current active scb phase.
 *  At this point we can queue the abort tag and go on with life.
 */

  if ( found == 0 )
  {
    p->flags |= AHC_ABORT_PENDING;
    scb->flags |= SCB_QUEUED_ABORT | SCB_ABORT | SCB_RECOVERY_SCB;
    scb->hscb->control |= MK_MESSAGE;
    result=aic7xxx_find_scb(p, scb);
    if ( result != SCB_LIST_NULL ) 
    {
      saved_hscbptr = aic_inb(p, SCBPTR);
      aic_outb(p, result, SCBPTR);
      tmp_char = aic_inb(p, SCB_CONTROL);
      aic_outb(p,  tmp_char | MK_MESSAGE, SCB_CONTROL);
      aic_outb(p, saved_hscbptr, SCBPTR);
    }
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
      printk(INFO_LEAD "SCB disconnected.  Queueing Abort"
        " SCB.\n", p->host_no, CTL_OF_SCB(scb));
    p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
    if (p->features & AHC_QUEUE_REGS)
      aic_outb(p, p->qinfifonext, HNSCB_QOFF);
    else
      aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
  }
  if (found)
  {
    aic7xxx_run_done_queue(p, TRUE);
    aic7xxx_run_waiting_queues(p);
  }
  p->flags &= ~AHC_IN_ABORT;
  unpause_sequencer(p, FALSE);
  DRIVER_UNLOCK

/*
 *  On the return value.  If we found the command and aborted it, then we know
 *  it's already sent back and there is no reason for a further timeout, so
 *  we use SCSI_ABORT_SUCCESS.  On the queued abort side, we aren't so certain
 *  there hasn't been a bus hang or something that might keep the abort from
 *  from completing.  Therefore, we use SCSI_ABORT_PENDING.  The first time this
 *  is passed back, the timeout on the command gets extended, the second time
 *  we pass this back, the mid level SCSI code calls our reset function, which
 *  would shake loose a hung bus.
 */
  if ( found != 0 )
    return(SCSI_ABORT_SUCCESS);
  else
    return(SCSI_ABORT_PENDING); 
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset
 *
 * Description:
 *   Resetting the bus always succeeds - is has to, otherwise the
 *   kernel will panic! Try a surgical technique - sending a BUS
 *   DEVICE RESET message - on the offending target before pulling
 *   the SCSI bus reset line.
 *-F*************************************************************************/
int
aic7xxx_reset(Scsi_Cmnd *cmd, unsigned int flags)
{
  struct aic7xxx_scb *scb = NULL;
  struct aic7xxx_host *p;
  int    tindex;
  int    result = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,95)
  unsigned long cpu_flags = 0;
#endif
#define DEVICE_RESET 0x01
#define BUS_RESET    0x02
#define HOST_RESET   0x04
#define FAIL         0x08
#define RESET_DELAY  0x10
  int        action;
  Scsi_Cmnd *cmd_prev, *cmd_next;


  if ( cmd == NULL )
  {
    printk(KERN_WARNING "(scsi?:?:?:?) Reset called with NULL Scsi_Cmnd "
      "pointer, failing.\n");
    return(SCSI_RESET_SNOOZE);
  }

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  tindex = TARGET_INDEX(cmd);

  /*
   * I added a new config option to the driver: "panic_on_abort" that will
   * cause the driver to panic and the machine to stop on the first abort
   * or reset call into the driver.  At that point, it prints out a lot of
   * usefull information for me which I can then use to try and debug the
   * problem.  Simply enable the boot time prompt in order to activate this
   * code.
   */
  if (aic7xxx_panic_on_abort)
    aic7xxx_panic_abort(p, cmd);

  DRIVER_LOCK

  pause_sequencer(p);
  while ( (aic_inb(p, INTSTAT) & INT_PEND) && !(p->flags & AHC_IN_ISR))
  {
    aic7xxx_isr(p->irq, p, (void *)NULL );
    pause_sequencer(p);
    aic7xxx_done_cmds_complete(p);
  }

  if (scb == NULL)
  {
    if (aic7xxx_verbose & VERBOSE_RESET_MID)
      printk(INFO_LEAD "Reset called with bogus Scsi_Cmnd"
           "->SCB mapping, improvising.\n", p->host_no, CTL_OF_CMD(cmd));
    if ( flags & SCSI_RESET_SUGGEST_HOST_RESET )
    {
      action = HOST_RESET;
    }
    else
    {
      action = BUS_RESET;
    }
  }
  else if (scb->cmd != cmd) 
  {
    if (aic7xxx_verbose & VERBOSE_RESET_MID)
    printk(INFO_LEAD "Reset called with recycled SCB "
        "for cmd.\n", p->host_no, CTL_OF_CMD(cmd));
    cmd_prev = NULL;
    cmd_next = p->completeq.head;
    while ( cmd_next != NULL )
    {
      if (cmd_next == cmd)
      {
        if (aic7xxx_verbose & VERBOSE_RESET_RETURN)
          printk(INFO_LEAD "Reset, found cmd on completeq"
          ", completing.\n", p->host_no, CTL_OF_CMD(cmd));
        unpause_sequencer(p, FALSE);
        DRIVER_UNLOCK
        return(SCSI_RESET_NOT_RUNNING);
      }
      cmd_prev = cmd_next;
      cmd_next = (Scsi_Cmnd *)cmd_next->host_scribble;
    }
    if ( !(flags & SCSI_RESET_SYNCHRONOUS) )
    {
      if (aic7xxx_verbose & VERBOSE_RESET_RETURN)
        printk(INFO_LEAD "Reset, cmd not found,"
          " failing.\n", p->host_no, CTL_OF_CMD(cmd));
      unpause_sequencer(p, FALSE);
      DRIVER_UNLOCK
      return(SCSI_RESET_NOT_RUNNING);
    }
    else
    {
      if (aic7xxx_verbose & VERBOSE_RESET_MID)
        printk(INFO_LEAD "Reset called, no scb, "
          "flags 0x%x\n", p->host_no, CTL_OF_CMD(cmd), flags);
      scb = NULL;
      action = HOST_RESET;
    }
  }
  else
  {
    if (aic7xxx_verbose & VERBOSE_RESET_MID)
      printk(INFO_LEAD "Reset called, scb %d, flags "
        "0x%x\n", p->host_no, CTL_OF_SCB(scb), scb->hscb->tag, scb->flags);
    if ( aic7xxx_scb_on_qoutfifo(p, scb) )
    {
      if(aic7xxx_verbose & VERBOSE_RESET_RETURN)
        printk(INFO_LEAD "SCB on qoutfifo, returning.\n", p->host_no,
          CTL_OF_SCB(scb));
      unpause_sequencer(p, FALSE);
      DRIVER_UNLOCK
      return(SCSI_RESET_NOT_RUNNING);
    }
    if ( flags & SCSI_RESET_SUGGEST_HOST_RESET )
    {
      action = HOST_RESET;
    }
    else if ( flags & SCSI_RESET_SUGGEST_BUS_RESET )
    {
      action = BUS_RESET;
    }
    else 
    {
      action = DEVICE_RESET;
    }
  }
  if ( (action & DEVICE_RESET) && 
        (p->dev_flags[tindex] & BUS_DEVICE_RESET_PENDING) )
  {
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Bus device reset already sent to "
        "device, escalating.\n", p->host_no, CTL_OF_CMD(cmd));
    action = BUS_RESET;
  }
  if ( (action & DEVICE_RESET) &&
       (scb->flags & SCB_QUEUED_ABORT) )
  {
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    {
      printk(INFO_LEAD "Have already attempted to reach "
        "device with queued\n", p->host_no, CTL_OF_CMD(cmd));
      printk(INFO_LEAD "message, will escalate to bus "
        "reset.\n", p->host_no, CTL_OF_CMD(cmd));
    }
    action = BUS_RESET;
  }
  if ( (action & DEVICE_RESET) && 
       (p->flags & (AHC_RESET_PENDING | AHC_ABORT_PENDING)) )
  {
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
     printk(INFO_LEAD "Bus device reset stupid when "
        "other action has failed.\n", p->host_no, CTL_OF_CMD(cmd));
    action = BUS_RESET;
  }
  if ( (action & BUS_RESET) && !(p->features & AHC_TWIN) )
  {
    action = HOST_RESET;
  }
  if ( ((jiffies - p->dev_last_reset[tindex]) < (HZ * 3)) &&
       !(action & (HOST_RESET | BUS_RESET)))
  {
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    {
      printk(INFO_LEAD "Reset called too soon after last "
        "reset without requesting\n", p->host_no, CTL_OF_CMD(cmd));
      printk(INFO_LEAD "bus or host reset, escalating.\n", p->host_no,
        CTL_OF_CMD(cmd));
    }
    action = BUS_RESET;
  }
  if ( ((jiffies - p->last_reset) < (HZ * 3)) &&
        (action & (HOST_RESET | BUS_RESET)) )
  {
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Reset called too soon after "
        "last bus reset, delaying.\n", p->host_no, CTL_OF_CMD(cmd));
    action = RESET_DELAY;
  }
  if ( (action & (BUS_RESET | HOST_RESET)) && (p->flags & AHC_IN_RESET)
        && ((jiffies - p->reset_start) > (2 * HZ * 3)) )
  {
    printk(KERN_ERR "(scsi%d:%d:%d:%d) Yikes!!  Card must have left to go "
        "back to Adaptec!!\n", p->host_no, CTL_OF_CMD(cmd));
    unpause_sequencer(p, FALSE);
    DRIVER_UNLOCK
    return(SCSI_RESET_SNOOZE);
  }
/*
 *  By this point, we want to already know what we are going to do and
 *  only have the following code implement our course of action.
 */
  switch (action)
  {
    case RESET_DELAY:
      unpause_sequencer(p, FALSE);
      DRIVER_UNLOCK
      return(SCSI_RESET_PENDING);
      break;
    case FAIL:
      unpause_sequencer(p, FALSE);
      DRIVER_UNLOCK
      return(SCSI_RESET_ERROR);
      break;
    case DEVICE_RESET:
      p->flags |= AHC_IN_RESET;
      result = aic7xxx_bus_device_reset(p, cmd);
      aic7xxx_run_done_queue(p, TRUE);
      /*  We can't rely on run_waiting_queues to unpause the sequencer for
       *  PCI based controllers since we use AAP */
      aic7xxx_run_waiting_queues(p);
      unpause_sequencer(p, FALSE);
      p->flags &= ~AHC_IN_RESET;
      DRIVER_UNLOCK
      return(result);
      break;
    case BUS_RESET:
    case HOST_RESET:
    default:
      p->reset_start = jiffies;
      p->flags |= AHC_IN_RESET;
      aic7xxx_reset_channel(p, cmd->channel, TRUE);
      if ( (p->features & AHC_TWIN) && (action & HOST_RESET) )
      {
        aic7xxx_reset_channel(p, cmd->channel ^ 0x01, TRUE);
        restart_sequencer(p);
      }
      p->last_reset = jiffies;
      if (action != HOST_RESET)
        result = SCSI_RESET_SUCCESS | SCSI_RESET_BUS_RESET;
      else
      {
        result = SCSI_RESET_SUCCESS | SCSI_RESET_HOST_RESET;
        aic_outb(p,  aic_inb(p, SIMODE1) & ~(ENREQINIT|ENBUSFREE),
          SIMODE1);
        aic7xxx_clear_intstat(p);
        p->flags &= ~AHC_HANDLING_REQINITS;
        p->msg_type = MSG_TYPE_NONE;
        p->msg_index = 0;
        p->msg_len = 0;
      }
      aic7xxx_run_done_queue(p, TRUE);
      p->flags &= ~AHC_IN_RESET;
      /*  We can't rely on run_waiting_queues to unpause the sequencer for
       *  PCI based controllers since we use AAP */
      aic7xxx_run_waiting_queues(p);
      unpause_sequencer(p, FALSE);
      DRIVER_UNLOCK
      return(result);
      break;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_biosparam
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 *-F*************************************************************************/
int
aic7xxx_biosparam(Disk *disk, kdev_t dev, int geom[])
{
  int heads, sectors, cylinders;
  struct aic7xxx_host *p;

  p = (struct aic7xxx_host *) disk->device->host->hostdata;

  /*
   * XXX - if I could portably find the card's configuration
   *       information, then this could be autodetected instead
   *       of left to a boot-time switch.
   */
  heads = 64;
  sectors = 32;
  cylinders = disk->capacity / (heads * sectors);

  if ((p->flags & AHC_EXTEND_TRANS_A) && (cylinders > 1024))
  {
    heads = 255;
    sectors = 63;
    cylinders = disk->capacity / (heads * sectors);
  }

  geom[0] = heads;
  geom[1] = sectors;
  geom[2] = cylinders;

  return (0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_release
 *
 * Description:
 *   Free the passed in Scsi_Host memory structures prior to unloading the
 *   module.
 *-F*************************************************************************/
int
aic7xxx_release(struct Scsi_Host *host)
{
  struct aic7xxx_host *p = (struct aic7xxx_host *) host->hostdata;
  struct aic7xxx_host *next, *prev;

  if(p->irq)
    free_irq(p->irq, p);
  release_region(p->base, MAXREG - MINREG);
#ifdef MMAPIO
  if(p->maddr)
  {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
    vfree((void *) (((unsigned long) p->maddr) & PAGE_MASK));
#else
    iounmap((void *) (((unsigned long) p->maddr) & PAGE_MASK));
#endif
  }
#endif /* MMAPIO */
  prev = NULL;
  next = first_aic7xxx;
  while(next != NULL)
  {
    if(next == p)
    {
      if(prev == NULL)
        first_aic7xxx = next->next;
      else
        prev->next = next->next;
    }
    else
    {
      prev = next;
    }
    next = next->next;
  }
  aic7xxx_free(p);
  return(0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_card
 *
 * Description:
 *   Print out all of the control registers on the card
 *
 *   NOTE: This function is not yet safe for use on the VLB and EISA
 *   controllers, so it isn't used on those controllers at all.
 *-F*************************************************************************/
static void
aic7xxx_print_card(struct aic7xxx_host *p)
{
  int i, j, k, chip;
  static struct register_ranges {
    int num_ranges;
    int range_val[32];
  } cards_ds[] = {
    { 0, {0,} }, /* none */
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1f, 0x1f, 0x60, 0x60, /*7771*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9b, 0x9f} },
    { 9, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7850*/
          0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    { 9, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7860*/
          0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1c, 0x1f, 0x60, 0x60, /*7870*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1a, 0x1c, 0x1f, 0x60, 0x60, /*7880*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {16, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7890*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9f, 0x9f,
          0xe0, 0xf1, 0xf4, 0xf4, 0xf6, 0xf6, 0xf8, 0xf8, 0xfa, 0xfc,
          0xfe, 0xff} },
    {12, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1b, 0x1f, 0x60, 0x60, /*7895*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a,
          0x9f, 0x9f, 0xe0, 0xf1} },
    {16, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7896*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9f, 0x9f,
          0xe0, 0xf1, 0xf4, 0xf4, 0xf6, 0xf6, 0xf8, 0xf8, 0xfa, 0xfc,
          0xfe, 0xff} },
  };
#ifdef CONFIG_PCI
  static struct register_ranges cards_ns[] = {
    { 0, {0,} }, /* none */
    { 0, {0,} }, /* 7771 */
    { 7, {0x04, 0x08, 0x0c, 0x0e, 0x10, 0x17, 0x28, 0x2b, 0x30, 0x33,
          0x3c, 0x41, 0x43, 0x47} },
    { 7, {0x04, 0x08, 0x0c, 0x0e, 0x10, 0x17, 0x28, 0x2b, 0x30, 0x33,
          0x3c, 0x41, 0x43, 0x47} },
    { 5, {0x04, 0x08, 0x0c, 0x0e, 0x10, 0x17, 0x30, 0x33, 0x3c, 0x41} },
    { 5, {0x04, 0x08, 0x0c, 0x0e, 0x10, 0x17, 0x30, 0x34, 0x3c, 0x47} },
    { 5, {0x04, 0x08, 0x0c, 0x1b, 0x30, 0x34, 0x3c, 0x43, 0xdc, 0xe3} },
    { 6, {0x04, 0x08, 0x0c, 0x0e, 0x10, 0x17, 0x30, 0x34, 0x3c, 0x47,
          0xdc, 0xe3} },
    { 6, {0x04, 0x08, 0x0c, 0x1b, 0x30, 0x34, 0x3c, 0x43, 0xdc, 0xe3,
          0xff, 0xff} }
  };
#endif
  chip = p->chip & AHC_CHIPID_MASK;
  /*
   * Let's run through the PCI space first....
   */
  printk("%s at ",
         board_names[p->board_name_index]);
  switch(p->chip & ~AHC_CHIPID_MASK)
  {
    case AHC_VL:
      printk("VLB Slot %d.\n", p->pci_device_fn);
      break;
    case AHC_EISA:
      printk("EISA Slot %d.\n", p->pci_device_fn);
      break;
    case AHC_PCI:
    default:
      printk("PCI %d/%d.\n", PCI_SLOT(p->pci_device_fn),
             PCI_FUNC(p->pci_device_fn));
      break;
  }

#ifdef CONFIG_PCI
  {
    unsigned char temp;
    
    printk("PCI Dump:\n");
    k=0;
    for(i=0; i<cards_ns[chip].num_ranges; i++)
    {
      for(j  = cards_ns[chip].range_val[ i * 2 ];
          j <= cards_ns[chip].range_val[ i * 2 + 1 ] ;
          j++)
      {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
        pci_read_config_byte(p->pdev, j, &temp);
#else
        pcibios_read_config_byte(p->pci_bus, p->pci_device_fn, j, &temp);
#endif
        printk("%02x:%02x ", j, temp);
        if(++k == 13)
        {
          printk("\n");
          k = 0;
        }
      }
    }
  }
  if(k != 0)
    printk("\n");
#endif /* CONFIG_PCI */

  /*
   * Now the registers on the card....
   */
  printk("Card Dump:\n");
  k = 0;
  for(i=0; i<cards_ds[chip].num_ranges; i++)
  {
    for(j  = cards_ds[chip].range_val[ i * 2 ];
        j <= cards_ds[chip].range_val[ i * 2 + 1 ] ;
        j++)
    {
      printk("%02x:%02x ", j, aic_inb(p, j));
      if(++k == 13)
      {
        printk("\n");
        k=0;
      }
    }
  }
  if(k != 0)
    printk("\n");
  if (p->flags & AHC_SEEPROM_FOUND)
  {
    unsigned short *sc1;
    sc1 = (unsigned short *)&p->sc;
    
    printk("SEEPROM dump.\n");
    for(i=1; i<=32; i++)
    {
      printk("0x%04x", sc1[i-1]);
      if ( (i % 8) == 0 )
        printk("\n");
      else
        printk("  ");
    }
  }

  /*
   * If this was an Ultra2 controller, then we just hosed the card in terms
   * of the QUEUE REGS.  This function is only called at init time or by
   * the panic_abort function, so it's safe to assume a generic init time
   * setting here
   */

  if(p->features & AHC_QUEUE_REGS)
  {
    aic_outb(p, 0, SDSCB_QOFF);
    aic_outb(p, 0, SNSCB_QOFF);
    aic_outb(p, 0, HNSCB_QOFF);
  }

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_scratch_ram
 *
 * Description:
 *   Print out the scratch RAM values on the card.
 *-F*************************************************************************/
static void
aic7xxx_print_scratch_ram(struct aic7xxx_host *p)
{
  int i, k;

  k = 0;
  printk("Scratch RAM:\n");
  for(i = SRAM_BASE; i < SEQCTL; i++)
  {
    printk("%02x:%02x ", i, aic_inb(p, i));
    if(++k == 13)
    {
      printk("\n");
      k=0;
    }
  }
  if (p->features & AHC_MORE_SRAM)
  {
    for(i = TARG_OFFSET; i < 0x80; i++)
    {
      printk("%02x:%02x ", i, aic_inb(p, i));
      if(++k == 13)
      {
        printk("\n");
        k=0;
      }
    }
  }
  printk("\n");
}


#include "aic7xxx_proc.c"

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = AIC7XXX;

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
