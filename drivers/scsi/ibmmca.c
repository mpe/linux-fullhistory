/*
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 *
 * Copyright (c) 1995 Strom Systems, Inc. under the terms of the GNU 
 * General Public License. Written by Martin Kolinek, December 1995.
 */

/* Update history:
   Jan 15 1996:  First public release.
   - Martin Kolinek

   Jan 23 1996:  Scrapped code which reassigned scsi devices to logical
   device numbers. Instead, the existing assignment (created
   when the machine is powered-up or rebooted) is used. 
   A side effect is that the upper layer of Linux SCSI 
   device driver gets bogus scsi ids (this is benign), 
   and also the hard disks are ordered under Linux the 
   same way as they are under dos (i.e., C: disk is sda, 
   D: disk is sdb, etc.).
   - Martin Kolinek

   I think that the CD-ROM is now detected only if a CD is 
   inside CD_ROM while Linux boots. This can be fixed later,
   once the driver works on all types of PS/2's.
   - Martin Kolinek

   Feb 7 1996:   Modified biosparam function. Fixed the CD-ROM detection. 
   For now, devices other than harddisk and CD_ROM are 
   ignored. Temporarily modified abort() function 
   to behave like reset().
   - Martin Kolinek

   Mar 31 1996:  The integrated scsi subsystem is correctly found
   in PS/2 models 56,57, but not in model 76. Therefore
   the ibmmca_scsi_setup() function has been added today.
   This function allows the user to force detection of
   scsi subsystem. The kernel option has format
   ibmmcascsi=n
   where n is the scsi_id (pun) of the subsystem. Most likely, n is 7.
   - Martin Kolinek

   Aug 21 1996:  Modified the code which maps ldns to (pun,0).  It was
   insufficient for those of us with CD-ROM changers.
   - Chris Beauregard
 
   Dec 14 1996: More improvements to the ldn mapping.  See check_devices
   for details.  Did more fiddling with the integrated SCSI detection,
   but I think it's ultimately hopeless without actually testing the
   model of the machine.  The 56, 57, 76 and 95 (ultimedia) all have
   different integrated SCSI register configurations.  However, the 56
   and 57 are the only ones that have problems with forced detection.
   - Chris Beauregard
 
   Mar 8-16 1997: Modified driver to run as a module and to support 
   multiple adapters. A structure, called ibmmca_hostdata, is now
   present, containing all the variables, that were once only
   available for one single adapter. The find_subsystem-routine has vanished.
   The hardware recognition is now done in ibmmca_detect directly.
   This routine checks for presence of MCA-bus, checks the interrupt
   level and continues with checking the installed hardware.
   Certain PS/2-models do not recognize a SCSI-subsystem automatically.
   Hence, the setup defined by command-line-parameters is checked first.
   Thereafter, the routine probes for an integrated SCSI-subsystem.
   Finally, adapters are checked. This method has the advantage to cover all
   possible combinations of multiple SCSI-subsystems on one MCA-board. Up to
   eight SCSI-subsystems can be recognized and announced to the upper-level
   drivers with this improvement. A set of defines made changes to other
   routines as small as possible.
   - Klaus Kudielka
   
   May 30 1997: (v1.5b)
   1) SCSI-command capability enlarged by the recognition of MODE_SELECT.
      This needs the RD-Bit to be disabled on IM_OTHER_SCSI_CMD_CMD which 
      allows data to be written from the system to the device. It is a
      necessary step to be allowed to set blocksize of SCSI-tape-drives and 
      the tape-speed, whithout confusing the SCSI-Subsystem.
   2) The recognition of a tape is included in the check_devices routine.
      This is done by checking for TYPE_TAPE, that is already defined in
      the kernel-scsi-environment. The markup of a tape is done in the 
      global ldn_is_tape[] array. If the entry on index ldn 
      is 1, there is a tapedrive connected.
   3) The ldn_is_tape[] array is necessary to distinguish between tape- and 
      other devices. Fixed blocklength devices should not cause a problem
      with the SCB-command for read and write in the ibmmca_queuecommand
      subroutine. Therefore, I only derivate the READ_XX, WRITE_XX for
      the tape-devices, as recommended by IBM in this Technical Reference,
      mentioned below. (IBM recommends to avoid using the read/write of the
      subsystem, but the fact was, that read/write causes a command error from
      the subsystem and this causes kernel-panic.)
   4) In addition, I propose to use the ldn instead of a fix char for the
      display of PS2_DISK_LED_ON(). On 95, one can distinguish between the
      devices that are accessed. It shows activity and easyfies debugging.   
   The tape-support has been tested with a SONY SDT-5200 and a HP DDS-2
   (I do not know yet the type). Optimization and CD-ROM audio-support, 
   I am working on ...
   - Michael Lang
   
   June 19 1997: (v1.6b)
   1) Submitting the extra-array ldn_is_tape[] -> to the local ld[]
      device-array. 
   2) CD-ROM Audio-Play seems to work now.
   3) When using DDS-2 (120M) DAT-Tapes, mtst shows still density-code
      0x13 for ordinary DDS (61000 BPM) instead 0x24 for DDS-2. This appears 
      also on Adaptec 2940 adaptor in a PCI-System. Therefore, I assume that 
      the problem is independent of the low-level-driver/bus-architecture.
   4) Hexadecimal ldn on PS/2-95 LED-display.
   5) Fixing of the PS/2-LED on/off that it works right with tapedrives and
      does not confuse the disk_rw_in_progress counter.
   - Michael Lang
  
   June 21 1997: (v1.7b)
   1) Adding of a proc_info routine to inform in /proc/scsi/ibmmca/<host> the
      outer-world about operational load statistics on the different ldns,
      seen by the driver. Everybody that has more than one IBM-SCSI should
      test this, because I only have one and cannot see what happens with more
      than one IBM-SCSI hosts.
   2) Definition of a driver version-number to have a better recognition of 
      the source when there are existing too much releases that may confuse
      the user, when reading about release-specific problems. Up to know,
      I calculated the version-number to be 1.7. Because we are in BETA-test
      yet, it is today 1.7b.
   3) Sorry for the heavy bug I programmed on June 19 1997! After that, the
      CD-ROM did not work any more! The C7-command was a fake impression
      I got while programming. Now, the READ and WRITE commands for CD-ROM are
      no longer running over the subsystem, but just over 
      IM_OTHER_SCSI_CMD_CMD. On my observations (PS/2-95), now CD-ROM mounts
      much faster(!) and hopefully all fancy multimedia-functions, like direct
      digital recording from audio-CDs also work. (I tried it with cdda2wav
      from the cdwtools-package and it filled up the harddisk immediately :-).)
      To easify boolean logics, a further local device-type in ld[], called
      is_cdrom has been included.
   4) If one uses a SCSI-device of unsupported type/commands, one
      immediately runs into a kernel-panic caused by Command Error. To better
      understand which SCSI-command caused the problem, I extended this
      specific panic-message slightly.
   - Michael Lang
 
   June 25 1997: (v1.8b)
   1) Some cosmetical changes for the handling of SCSI-device-types.
      Now, also CD-Burners / WORMs and SCSI-scanners should work. For
      MO-drives I have no experience, therefore not yet supported.
      In logical_devices I changed from different type-variables to one
      called 'device_type' where the values, corresponding to scsi.h,
      of a SCSI-device are stored.
   2) There existed a small bug, that maps a device, coming after a SCSI-tape
      wrong. Therefore, e.g. a CD-ROM changer would have been mapped wrong
      -> problem removed.
   3) Extension of the logical_device structure. Now it contains also device,
      vendor and revision-level of a SCSI-device for internal usage.
   - Michael Lang

   June 26-29 1997: (v2.0b)
   1) The release number 2.0b is necessary because of the completely new done
      recognition and handling of SCSI-devices with the adapter. As I got
      from Chris the hint, that the subsystem can reassign ldns dynamically,
      I remembered this immediate_assign-command, I found once in the handbook.
      Now, the driver first kills all ldn assignments that are set by default
      on the SCSI-subsystem. After that, it probes on all puns and luns for
      devices by going through all combinations with immediate_assign and
      probing for devices, using device_inquiry. The found physical(!) pun,lun
      structure is stored in get_scsi[][] as device types. This is followed
      by the assignment of all ldns to existing SCSI-devices. If more ldns
      than devices are available, they are assigned to non existing pun,lun
      combinations to satisfy the adapter. With this, the dynamical mapping
      was possible to implement. (For further info see the text in the 
      source-code and in the description below. Read the description
      below BEFORE installing this driver on your system!)
   2) Changed the name IBMMCA_DRIVER_VERSION to IBMMCA_SCSI_DRIVER_VERSION.
   3) The LED-display shows on PS/2-95 no longer the ldn, but the SCSI-ID
      (pun) of the accessed SCSI-device. This is now senseful, because the 
      pun known within the driver is exactly the pun of the physical device
      and no longer a fake one.
   4) The /proc/scsi/ibmmca/<host_no> consists now of the first part, where
      hit-statistics of ldns is shown and a second part, where the maps of 
      physical and logical SCSI-devices are displayed. This could be very 
      interesting, when one is using more than 15 SCSI-devices in order to 
      follow the dynamical remapping of ldns.
   - Michael Lang
 
   June 26-29 1997: (v2.0b-1)
   1) I forgot to switch the local_checking_phase_flag to 1 and back to 0
      in the dynamical remapping part in ibmmca_queuecommand for the 
      device_exist routine. Sorry.
   - Michael Lang
 
   July 1-13 1997: (v3.0b,c)
   1) Merging of the driver-developments of Klaus Kudielka and Michael Lang 
      in order to get a optimum and unified driver-release for the 
      IBM-SCSI-Subsystem-Adapter(s).
         For people, using the Kernel-release >=2.1.0, module-support should 
      be no problem. For users, running under <2.1.0, module-support may not 
      work, because the methods have changed between 2.0.x and 2.1.x.
   2) Added some more effective statistics for /proc-output.
   3) Change typecasting at necessary points from (unsigned long) to
      virt_to_bus().
   4) Included #if... at special points to have specific adaption of the
      driver to kernel 2.0.x and 2.1.x. It should therefore also run with 
      later releases.
   5) Magneto-Optical drives and medium-changers are also recognized, now.
      Therefore, we have a completely gapfree recognition of all SCSI-
      device-types, that are known by Linux up to kernel 2.1.31.
   6) The flag SCSI_IBMMCA_DEV_RESET has been inserted. If it is set within
      the configuration, each connected SCSI-device will get a reset command
      during boottime. This can be necessary for some special SCSI-devices.
      This flag should be included in Config.in.
      (See also the new Config.in file.)
   Probable next improvement: bad disk handler.
   - Michael Lang
 
   Sept 14 1997: (v3.0c)
   1) Some debugging and speed optimization applied.
   - Michael Lang

   Dec 15, 1997
    - chrisb@truespectra.com
    - made the front panel display thingy optional, specified from the
    command-line via ibmmcascsi=display.  Along the lines of the /LED
    option for the OS/2 driver.
    - fixed small bug in the LED display that would hang some machines.
    - reversed ordering of the drives (using the
    IBMMCA_SCSI_ORDER_STANDARD define).  This is necessary for two main
    reasons:
	- users who've already installed Linux won't be screwed.  Keep
	in mind that not everyone is a kernel hacker.
	- be consistent with the BIOS ordering of the drives.  In the
	BIOS, id 6 is C:, id 0 might be D:.  With this scheme, they'd be
	backwards.  This confuses the crap out of those heathens who've
	got a impure Linux installation (which, <wince>, I'm one of).
    This whole problem arises because IBM is actually non-standard with
    the id to BIOS mappings.  You'll find, in fdomain.c, a similar
    comment about a few FD BIOS revisions.  The Linux (and apparently
    industry) standard is that C: maps to scsi id (0,0).  Let's stick
    with that standard.
    - Since this is technically a branch of my own, I changed the
    version number to 3.0e-cpb.

   Jan 17, 1998: (v3.0f)
   1) Addition of some statistical info for /proc in proc_info.
   2) Taking care of the SCSI-assignment problem, dealed by Chris at Dec 15
      1997. In fact, IBM is right, concerning the assignment of SCSI-devices 
      to driveletters. It is conform to the ANSI-definition of the SCSI-
      standard to assign drive C: to SCSI-id 6, because it is the highest
      hardware priority after the hostadapter (that has still today by
      default everywhere id 7). Also realtime-operating systems that I use, 
      like LynxOS and OS9, which are quite industrial systems use top-down
      numbering of the harddisks, that is also starting at id 6. Now, one
      sits a bit between two chairs. On one hand side, using the define
      IBMMCA_SCSI_ORDER_STANDARD makes Linux assigning disks conform to
      the IBM- and ANSI-SCSI-standard and keeps this driver downward
      compatible to older releases, on the other hand side, people is quite
      habituated in believing that C: is assigned to (0,0) and much other
      SCSI-BIOS do so. Therefore, I moved the IBMMCA_SCSI_ORDER_STANDARD 
      define out of the driver and put it into Config.in as subitem of 
      'IBM SCSI support'. A help, added to Documentation/Configure.help 
      explains the differences between saying 'y' or 'n' to the user, when 
      IBMMCA_SCSI_ORDER_STANDARD prompts, so the ordinary user is enabled to 
      choose the way of assignment, depending on his own situation and gusto.
   3) Adapted SCSI_IBMMCA_DEV_RESET to the local naming convention, so it is
      now called IBMMCA_SCSI_DEV_RESET.
   4) Optimization of proc_info and its subroutines.
   5) Added more in-source-comments and extended the driver description by
      some explanation about the SCSI-device-assignment problem.
   - Michael Lang
   
   Jan 18, 1998: (v3.0g)
   1) Correcting names to be absolutely conform to the later 2.1.x releases.
      This is necessary for 
            IBMMCA_SCSI_DEV_RESET -> CONFIG_IBMMCA_SCSI_DEV_RESET
            IBMMCA_SCSI_ORDER_STANDARD -> CONFIG_IBMMCA_SCSI_ORDER_STANDARD
   - Michael Lang

	TODO:
 
	- It seems that the handling of bad disks is really bad -
	  non-existent, in fact.
        - More testing of the full driver-controlled dynamical ldn 
          (re)mapping for up to 56 SCSI-devices.
        - Support more SCSI-device-types, if Linux defines more.
        - Support more of the SCSI-command set.
	- Support some of the caching abilities, particularly Read Prefetch.
	  This fetches data into the cache, which later gets hit by the
	  regular Read Data.
        - Abort and Reset functions still slightly buggy. Especially when
          floppydisk(!) operations report errors.

******************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/blk.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mca.h>
#include <asm/system.h>
#include <asm/spinlock.h>
#include <asm/io.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "ibmmca.h"

#include <linux/config.h>		/* for CONFIG_SCSI_IBMMCA etc. */

/*--------------------------------------------------------------------*/

/* current version of this driver-source: */
#define IBMMCA_SCSI_DRIVER_VERSION "3.0f"

/* use standard Linux ordering, where C: maps to (0,0), unlike the IBM
standard which seems to like C: => (6,0) */
/* #define IBMMCA_SCSI_ORDER_STANDARD is defined/undefined in Config.in
 * now, while configuring the kernel. */

/*
   Driver Description

   (A) Subsystem Detection
   This is done in the ibmmca_detect() function and is easy, since
   the information about MCA integrated subsystems and plug-in 
   adapters is readily available in structure *mca_info.

   (B) Physical Units, Logical Units, and Logical Devices
   There can be up to 56 devices on SCSI bus (besides the adapter):
   there are up to 7 "physical units" (each identified by physical unit 
   number or pun, also called the scsi id, this is the number you select
   with hardware jumpers), and each physical unit can have up to 8 
   "logical units" (each identified by logical unit number, or lun, 
   between 0 and 7). 

   Typically the adapter has pun=7, so puns of other physical units
   are between 0 and 6. Almost all physical units have only one   
   logical unit, with lun=0. A CD-ROM jukebox would be an example of 
   a physical unit with more than one logical unit.

   The embedded microprocessor of IBM SCSI subsystem hides the complex
   two-dimensional (pun,lun) organization from the operating system.
   When the machine is powered-up (or rebooted, I am not sure), the 
   embedded microprocessor checks, on it own, all 56 possible (pun,lun) 
   combinations, and first 15 devices found are assigned into a 
   one-dimensional array of so-called "logical devices", identified by 
   "logical device numbers" or ldn. The last ldn=15 is reserved for 
   the subsystem itself. 

   One consequence of information hiding is that the real (pun,lun)    
   numbers are also hidden. Therefore this driver takes the following
   approach: It checks the ldn's (0 to 6) to find out which ldn's
   have devices assigned. This is done by function check_devices() and
   device_exists(). The interrupt handler has a special paragraph of code
   (see local_checking_phase_flag) to assist in the checking. Assume, for
   example, that three logical devices were found assigned at ldn 0, 1, 2.
   These are presented to the upper layer of Linux SCSI driver
   as devices with bogus (pun, lun) equal to (0,0), (1,0), (2,0). 
   On the other hand, if the upper layer issues a command to device
   say (4,0), this driver returns DID_NO_CONNECT error.

   That last paragraph is no longer correct, but is left for
   historical purposes.  It limited the number of devices to 7, far
   fewer than the 15 that it could use.  Now it just maps
   ldn -> (ldn/8,ldn%8).  We end up with a real mishmash of puns
   and luns, but it all seems to work. - Chris Beaurgard

   And that last paragraph is also no longer correct.  It uses a
   slightly more complex mapping that will always map hard disks to
   (x,0), for some x, and consecutive none disk devices will usually
   share puns.
 
   Again, the last paragraphs are no longer correct. Now, the physical
   SCSI-devices on the SCSI-bus are probed via immediate_assign- and
   device_inquiry-commands. This delivers a exact map of the physical
   SCSI-world that is now stored in the get_scsi[][]-array. This means,
   that the once hidden pun,lun assignment is now known to this driver.
   It no longer believes in default-settings of the subsystem and maps all
   ldns to existing pun,lun by foot. This assures full control of the ldn
   mapping and allows dynamical remapping of ldns to different pun,lun, if
   there are more SCSI-devices installed than ldns available (n>15). The
   ldns from 0 to 6 get 'hardwired' by this driver to puns 0 to 7 at lun=0,
   excluding the pun of the subsystem. This assures, that at least simple 
   SCSI-installations have optimum access-speed and are not touched by
   dynamical remapping. The ldns 7 to 14 are put to existing devices with 
   lun>0 or to non-existing devices, in order to satisfy the subsystem, if 
   there are less than 15 SCSI-devices connected. In the case of more than 15 
   devices, the dynamical mapping goes active. If the get_scsi[][] reports a 
   device to be existant, but it has no ldn assigned, it gets a ldn out of 7 
   to 14. The numbers are assigned in cyclic order. Therefore it takes 8 
   dynamical assignments on SCSI-devices, until a certain device 
   looses its ldn again. This assures, that dynamical remapping is avoided 
   during intense I/O between up to eight SCSI-devices (means pun,lun 
   combinations). A further advantage of this method is, that people who
   build their kernel without probing on all luns will get what they expect.
 
   IMPORTANT: Because of the now correct recognition of physical pun,lun, and 
   their report to mid-level- and higher-level-drivers, the new reported puns
   can be different from the old, faked puns. Therefore, Linux will eventually
   change /dev/sdXXX assignments and prompt you for corrupted superblock
   repair on boottime. In this case DO NOT PANIC, YOUR DISKS ARE STILL OK!!!
   You have to reboot (CTRL-D) with a old kernel and set the /etc/fstab-file
   entries right. After that, the system should come up as errorfree as before.
   If your boot-partition is not coming up, also edit the /etc/lilo.conf-file
   in a Linux session booted on old kernel and run lilo before reboot. Check
   lilo.conf anyway to get boot on other partitions with foreign OSes right
   again. 
 
   The problem is, that Linux does not assign the SCSI-devices in the
   way as described in the ANSI-SCSI-standard. Linux assigns /dev/sda to 
   the device with at minimum id 0. But the first drive should be at id 6,
   because for historical reasons, drive at id 6 has, by hardware, the highest
   priority and a drive at id 0 the lowest. IBM was one of the rare producers,
   where the BIOS assigns drives belonging to the ANSI-SCSI-standard. Most 
   other producers' BIOS does not (I think even Adaptec-BIOS). The 
   IBMMCA_SCSI_ORDER_STANDARD flag helps to be able to choose the preferred 
   way of SCSI-device-assignment. Defining this flag would result in Linux 
   determining the devices in the same order as DOS and OS/2 does on your 
   MCA-machine. This is also standard on most industrial computers. Leaving 
   this flag undefined will get your devices ordered in the default way of 
   Linux. See also the remarks of Chris Beauregard from Dec 15, 1997 and
   the followups.
   
   (C) Regular Processing 
   Only three functions get involved: ibmmca_queuecommand(), issue_cmd(),
   and interrupt_handler().

   The upper layer issues a scsi command by calling function 
   ibmmca_queuecommand(). This function fills a "subsystem control block"
   (scb) and calls a local function issue_cmd(), which writes a scb 
   command into subsystem I/O ports. Once the scb command is carried out, 
   interrupt_handler() is invoked. If a device is determined to be existant
   and it has not assigned any ldn, it gets one dynamically.

   (D) Abort, Reset.
   These are implemented with busy waiting for interrupt to arrive.
   The abort does not worked well for me, so I instead call the 
   ibmmca_reset() from the ibmmca_abort() function.

   (E) Disk Geometry
   The ibmmca_biosparams() function should return same disk geometry 
   as bios. This is needed for fdisk, etc. The returned geometry is 
   certainly correct for disk smaller than 1 gigabyte, but I am not 
   100% sure that it is correct for larger disks.

   (F) Kernel Boot Option 
   The function ibmmca_scsi_setup() is called if option ibmmcascsi=n 
   is passed to the kernel. See file linux/init/main.c for details.
   
   (G) Driver Module Support
   Is implemented and tested by K. Kudielka. This could probably not work
   on kernels <2.1.0.
  
   (H) Multiple Hostadapter Support
   This driver supports up to eight interfaces of type IBM-SCSI-Subsystem. 
   Integrated-, and MCA-adapters are automatically recognized. Unrecognizable
   IBM-SCSI-Subsystem interfaces can be specified as kernel-parameters.
 
   (I) /proc-Filesystem Information
   Information about the driver condition is given in 
   /proc/scsi/ibmmca/<host_no>. ibmmca_proc_info provides this information.
 */

/*--------------------------------------------------------------------*/
/* Here are the values and structures specific for the subsystem. 
 * The source of information is "Update for the PS/2 Hardware 
 * Interface Technical Reference, Common Interfaces", September 1991, 
 * part number 04G3281, available in the U.S. for $21.75 at 
 * 1-800-IBM-PCTB, elsewhere call your local friendly IBM 
 * representative.
 * In addition to SCSI subsystem, this update contains fairly detailed 
 * (at hardware register level) sections on diskette  controller,
 * keyboard controller, serial port controller, VGA, and XGA.
 *
 * Additional information from "Personal System/2 Micro Channel SCSI
 * Adapter with Cache Technical Reference", March 1990, PN 68X2365,
 * probably available from the same source (or possibly found buried
 * in officemates desk).
 *
 * Further literature/program-sources referred for this driver:
 * 
 * Friedhelm Schmidt, "SCSI-Bus und IDE-Schnittstelle - Moderne Peripherie-
 * Schnittstellen: Hardware, Protokollbeschreibung und Anwendung", 2. Aufl.
 * Addison Wesley, 1996.
 * 
 * Michael K. Johnson, "The Linux Kernel Hackers' Guide", Version 0.6, Chapel
 * Hill - North Carolina, 1995
 * 
 * Andreas Kaiser, "SCSI TAPE BACKUP for OS/2 2.0", Version 2.12, Stuttgart
 * 1993
 */

/*--------------------------------------------------------------------*/

/* driver configuration */
#define IM_MAX_HOSTS      8             /* maximum number of host adapters */
#define IM_RESET_DELAY    10            /* seconds allowed for a reset */

/* driver debugging - #undef all for normal operation */

/* if defined: count interrupts and ignore this special one: */
#undef  IM_DEBUG_TIMEOUT  50            
/* verbose interrupt: */
#undef  IM_DEBUG_INT                   
/* verbose queuecommand: */
#undef  IM_DEBUG_CMD    
/* verbose queucommand for specific SCSI-device type: */
#undef  IM_DEBUG_CMD_SPEC_DEV          
/* verbose device probing */
#undef  IM_DEBUG_PROBE

/* device type that shall be displayed on syslog (only during debugging): */
#define IM_DEBUG_CMD_DEVICE   TYPE_TAPE

/* relative addresses of hardware registers on a subsystem */
#define IM_CMD_REG   (shpnt->io_port)   /*Command Interface, (4 bytes long) */
#define IM_ATTN_REG  (shpnt->io_port+4) /*Attention (1 byte) */
#define IM_CTR_REG   (shpnt->io_port+5) /*Basic Control (1 byte) */
#define IM_INTR_REG  (shpnt->io_port+6) /*Interrupt Status (1 byte, r/o) */
#define IM_STAT_REG  (shpnt->io_port+7) /*Basic Status (1 byte, read only) */

/* basic I/O-port of first adapter */
#define IM_IO_PORT   0x3540
/* maximum number of hosts that can be found */
#define IM_N_IO_PORT 8

/*requests going into the upper nibble of the Attention register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_IMM_CMD   0x10	/*immediate command */
#define IM_SCB       0x30	/*Subsystem Control Block command */
#define IM_LONG_SCB  0x40	/*long Subsystem Control Block command */
#define IM_EOI       0xe0	/*end-of-interrupt request */

/*values for bits 7,1,0 of Basic Control reg. (bits 6-2 reserved) */
#define IM_HW_RESET     0x80	/*hardware reset */
#define IM_ENABLE_DMA   0x02	/*enable subsystem's busmaster DMA */
#define IM_ENABLE_INTR  0x01	/*enable interrupts to the system */

/*to interpret the upper nibble of Interrupt Status register */
/*note: the lower nibble specifies the device(0-14), or subsystem(15) */
#define IM_SCB_CMD_COMPLETED               0x10
#define IM_SCB_CMD_COMPLETED_WITH_RETRIES  0x50
#define IM_ADAPTER_HW_FAILURE              0x70
#define IM_IMMEDIATE_CMD_COMPLETED         0xa0
#define IM_CMD_COMPLETED_WITH_FAILURE      0xc0
#define IM_CMD_ERROR                       0xe0
#define IM_SOFTWARE_SEQUENCING_ERROR       0xf0

/*to interpret bits 3-0 of Basic Status register (bits 7-4 reserved) */
#define IM_CMD_REG_FULL   0x08
#define IM_CMD_REG_EMPTY  0x04
#define IM_INTR_REQUEST   0x02
#define IM_BUSY           0x01

/*immediate commands (word written into low 2 bytes of command reg) */
#define IM_RESET_IMM_CMD        0x0400
#define IM_FEATURE_CTR_IMM_CMD  0x040c
#define IM_DMA_PACING_IMM_CMD   0x040d
#define IM_ASSIGN_IMM_CMD       0x040e
#define IM_ABORT_IMM_CMD        0x040f
#define IM_FORMAT_PREP_IMM_CMD  0x0417

/*SCB (Subsystem Control Block) structure */
struct im_scb
  {
    unsigned short command;	/*command word (read, etc.) */
    unsigned short enable;	/*enable word, modifies cmd */
    union
      {
	unsigned long log_blk_adr;	/*block address on SCSI device */
	unsigned char scsi_cmd_length;	/*6,10,12, for other scsi cmd */
      }
    u1;
    unsigned long sys_buf_adr;	/*physical system memory adr */
    unsigned long sys_buf_length;	/*size of sys mem buffer */
    unsigned long tsb_adr;	/*Termination Status Block adr */
    unsigned long scb_chain_adr;	/*optional SCB chain address */
    union
      {
	struct
	  {
	    unsigned short count;	/*block count, on SCSI device */
	    unsigned short length;	/*block length, on SCSI device */
	  }
	blk;
	unsigned char scsi_command[12];		/*other scsi command */
      }
    u2;
  };

/*structure scatter-gather element (for list of system memory areas) */
struct im_sge
  {
    void *address;
    unsigned long byte_length;
  };

/*values for SCB command word */
#define IM_NO_SYNCHRONOUS      0x0040	/*flag for any command */
#define IM_NO_DISCONNECT       0x0080	/*flag for any command */
#define IM_READ_DATA_CMD       0x1c01
#define IM_WRITE_DATA_CMD      0x1c02
#define IM_READ_VERIFY_CMD     0x1c03
#define IM_WRITE_VERIFY_CMD    0x1c04
#define IM_REQUEST_SENSE_CMD   0x1c08
#define IM_READ_CAPACITY_CMD   0x1c09
#define IM_DEVICE_INQUIRY_CMD  0x1c0b
#define IM_OTHER_SCSI_CMD_CMD  0x241f

/* unused, but supported, SCB commands */
#define IM_GET_COMMAND_COMPLETE_STATUS_CMD   0x1c07 /* command status */
#define IM_GET_POS_INFO_CMD                  0x1c0a /* returns neat stuff */
#define IM_READ_PREFETCH_CMD                 0x1c31 /* caching controller only */
#define IM_FOMAT_UNIT_CMD                    0x1c16 /* format unit */
#define IM_REASSIGN_BLOCK_CMD                0x1c18 /* in case of error */

/*values to set bits in the enable word of SCB */
#define IM_READ_CONTROL              0x8000
#define IM_REPORT_TSB_ONLY_ON_ERROR  0x4000
#define IM_RETRY_ENABLE              0x2000
#define IM_POINTER_TO_LIST           0x1000
#define IM_SUPRESS_EXCEPTION_SHORT   0x0400
#define IM_CHAIN_ON_NO_ERROR         0x0001

/*TSB (Termination Status Block) structure */
struct im_tsb
  {
    unsigned short end_status;
    unsigned short reserved1;
    unsigned long residual_byte_count;
    unsigned long sg_list_element_adr;
    unsigned short status_length;
    unsigned char dev_status;
    unsigned char cmd_status;
    unsigned char dev_error;
    unsigned char cmd_error;
    unsigned short reserved2;
    unsigned short reserved3;
    unsigned short low_of_last_scb_adr;
    unsigned short high_of_last_scb_adr;
  };

/*subsystem uses interrupt request level 14 */
#define IM_IRQ  14

/*--------------------------------------------------------------------*/
/*
	The model 95 doesn't have a standard activity light.  Instead it
	has a row of LEDs on the front.  We use the last one as the activity
	indicator if we think we're on a model 95.  I suspect the model id
	check will be either too narrow or too general, and some machines
	won't have an activity indicator.  Oh well...

	The regular PS/2 disk led is turned on/off by bits 6,7 of system
	control port.
*/

/* LED display-port (actually, last LED on display) */
#define MOD95_LED_PORT	   0x108
/* system-control-register of PS/2s with diskindicator */
#define PS2_SYS_CTR        0x92

/* The SCSI-ID(!) of the accessed SCSI-device is shown on PS/2-95 machines' LED
   displays. ldn is no longer displayed here, because the ldn mapping is now 
   done dynamically and the ldn <-> pun,lun maps can be looked-up at boottime 
   or during uptime in /proc/scsi/ibmmca/<host_no> in case of trouble, 
   interest, debugging or just for having fun. The left number gives the
   host-adapter number and the right shows the accessed SCSI-ID. */

/* use_display is set by the ibmmcascsi=display command line arg */
static int use_display = 0;
#define PS2_DISK_LED_ON(ad,id) {\
	if( use_display ) { outb((char)(id+48), MOD95_LED_PORT ); \
        outb((char)(ad+48), MOD95_LED_PORT+1); } \
	else outb(inb(PS2_SYS_CTR) | 0xc0, PS2_SYS_CTR); \
}

/* bug fixed, Dec 15, 1997, where | was replaced by & here */
#define PS2_DISK_LED_OFF() {\
	if( use_display ) { outb( ' ', MOD95_LED_PORT ); \
        outb(' ', MOD95_LED_PORT+1); } \
	else outb(inb(PS2_SYS_CTR) & 0x3f, PS2_SYS_CTR); \
}

/*--------------------------------------------------------------------*/

/*list of supported subsystems */
struct subsys_list_struct
  {
    unsigned short mca_id;
    char *description;
  };

/* List of possible IBM-SCSI-adapters */
struct subsys_list_struct subsys_list[] =
{
  {0x8efc, "IBM Fast SCSI-2 Adapter"},
  {0x8efd, "IBM 7568 Industrial Computer SCSI Adapter w/cache"},
  {0x8ef8, "IBM Expansion Unit SCSI Controller"},
  {0x8eff, "IBM SCSI Adapter w/Cache"},
  {0x8efe, "IBM SCSI Adapter"},
};

/*for /proc filesystem */
struct proc_dir_entry proc_scsi_ibmmca =
{
  PROC_SCSI_IBMMCA, 6, "ibmmca",
  S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/* Max number of logical devices (can be up from 0 to 14).  15 is the address
of the adapter itself. */
#define MAX_LOG_DEV  15

/*local data for a logical device */
struct logical_device
  {
    struct im_scb scb; /* SCSI-subsystem-control-block structure */
    struct im_tsb tsb;
    struct im_sge sge[16];
    Scsi_Cmnd *cmd;  /* SCSI-command that is currently in progress */
     
    int device_type; /* type of the SCSI-device. See include/scsi/scsi.h
		        for interpretation of the possible values */
    int block_length;/* blocksize of a particular logical SCSI-device */
  };

/* statistics of the driver during operations (for proc_info) */
struct Driver_Statistics
   {
      /* SCSI statistics on the adapter */
      int ldn_access[MAX_LOG_DEV+1];         /* total accesses on a ldn */
      int ldn_read_access[MAX_LOG_DEV+1];    /* total read-access on a ldn */
      int ldn_write_access[MAX_LOG_DEV+1];   /* total write-access on a ldn */
      int ldn_inquiry_access[MAX_LOG_DEV+1]; /* total inquiries on a ldn */
      int ldn_modeselect_access[MAX_LOG_DEV+1]; /* total mode selects on ldn */
      int total_accesses;                    /* total accesses on all ldns */
      int total_interrupts;                  /* total interrupts (should be
						same as total_accesses) */
      /* dynamical assignment statistics */
      int total_scsi_devices;                /* number of physical pun,lun */
      int dyn_flag;                          /* flag showing dynamical mode */
      int dynamical_assignments;             /* number of remappings of ldns */
      int ldn_assignments[MAX_LOG_DEV+1];    /* number of remappings of each
					        ldn */
   };

/* data structure for each host adapter */
struct ibmmca_hostdata
{
  /* array of logical devices: */
    struct logical_device _ld[MAX_LOG_DEV];   
  /* array to convert (pun, lun) into logical device number: */
    unsigned char _get_ldn[8][8];
  /*array that contains the information about the physical SCSI-devices
   attached to this host adapter: */
    unsigned char _get_scsi[8][8];
  /* used only when checking logical devices: */
    int _local_checking_phase_flag;
  /* report received interrupt: */
    int _got_interrupt;
  /* report termination-status of SCSI-command: */
    int _stat_result;
  /* reset status (used only when doing reset): */
    int _reset_status;
  /* code of the last SCSI command (needed for panic info): */
    int _last_scsi_command;
  /* Counter that points on the next reassignable ldn for dynamical 
   remapping. The default value is 7, that is the first reassignable 
   number in the list at boottime: */
    int _next_ldn;
  /* Statistics-structure for this IBM-SCSI-host: */
    struct Driver_Statistics _IBM_DS;
};

/* macros to access host data structure */
#define HOSTDATA(shpnt) ((struct ibmmca_hostdata *) shpnt->hostdata)
#define subsystem_pun (shpnt->this_id)
#define ld (HOSTDATA(shpnt)->_ld)
#define get_ldn (HOSTDATA(shpnt)->_get_ldn)
#define get_scsi (HOSTDATA(shpnt)->_get_scsi)
#define local_checking_phase_flag (HOSTDATA(shpnt)->_local_checking_phase_flag)
#define got_interrupt (HOSTDATA(shpnt)->_got_interrupt)
#define stat_result (HOSTDATA(shpnt)->_stat_result)
#define reset_status (HOSTDATA(shpnt)->_reset_status)
#define last_scsi_command (HOSTDATA(shpnt)->_last_scsi_command)
#define next_ldn (HOSTDATA(shpnt)->_next_ldn)
#define IBM_DS (HOSTDATA(shpnt)->_IBM_DS)

/* Define a arbitrary number as subsystem-marker-type. This number is, as 
   described in the ANSI-SCSI-standard, not occupied by other device-types. */
#define TYPE_IBM_SCSI_ADAPTER   0x2F

/* Define 0xFF for no device type, because this type is not defined within
   the ANSI-SCSI-standard, therefore, it can be used and should not cause any
   harm. */
#define TYPE_NO_DEVICE          0xFF

/* define medium-changer. If this is not defined previously, e.g. Linux
   2.0.x, define this type here. */
#ifndef TYPE_MEDIUM_CHANGER
#define TYPE_MEDIUM_CHANGER     0x08
#endif

/* define possible operations for the immediate_assign command */
#define SET_LDN        0
#define REMOVE_LDN     1

/* reset status flag contents */
#define IM_RESET_NOT_IN_PROGRESS   0
#define IM_RESET_IN_PROGRESS       1
#define IM_RESET_FINISHED_OK       2
#define IM_RESET_FINISHED_FAIL     3

/*-----------------------------------------------------------------------*/

/* if this is nonzero, ibmmcascsi option has been passed to the kernel */
static int io_port[IM_MAX_HOSTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int scsi_id[IM_MAX_HOSTS] = { 7, 7, 7, 7, 7, 7, 7, 7 };

/* fill module-parameters only, when this define is present.
   (that is kernel version 2.1.x) */
#ifdef MODULE_PARM
MODULE_PARM(io_port, "1-" __MODULE_STRING(IM_MAX_HOSTS) "i");
MODULE_PARM(scsi_id, "1-" __MODULE_STRING(IM_MAX_HOSTS) "i"); 
MODULE_PARM(display, "1i");
#endif

/*counter of concurrent disk read/writes, to turn on/off disk led */
static int disk_rw_in_progress = 0;

/* host information */
static int found = 0;
static struct Scsi_Host *hosts[IM_MAX_HOSTS+1] = { NULL };
/*-----------------------------------------------------------------------*/

/*local functions in forward declaration */
static void interrupt_handler (int irq, void *dev_id, struct pt_regs *regs);
static void do_interrupt_handler (int irq, void *dev_id, struct pt_regs *regs);
static void issue_cmd (struct Scsi_Host *shpnt, unsigned long cmd_reg, 
                       unsigned char attn_reg);
static void internal_done (Scsi_Cmnd * cmd);
static void check_devices (struct Scsi_Host *shpnt);
static int immediate_assign(struct Scsi_Host *shpnt, unsigned int pun, 
                            unsigned int lun, unsigned int ldn, 
                            unsigned int operation);
static int device_inquiry(struct Scsi_Host *shpnt, int ldn, 
                          unsigned char *buf);
static char *ti_p(int value);
static char *ti_l(int value);
static int device_exists (struct Scsi_Host *shpnt, int ldn, int *block_length,
                          int *device_type);
static struct Scsi_Host *ibmmca_register(Scsi_Host_Template * template, 
					 int port, int id);

/* local functions needed for proc_info */
static int ldn_access_load(struct Scsi_Host *shpnt, int ldn);
static int ldn_access_total_read_write(struct Scsi_Host *shpnt);

/*--------------------------------------------------------------------*/

static void 
do_interrupt_handler (int irq, void *dev_id, struct pt_regs *regs)
{
  unsigned long flags;

  spin_lock_irqsave(&io_request_lock, flags);
  interrupt_handler(irq, dev_id, regs);
  spin_unlock_irqrestore(&io_request_lock, flags);
}

static void 
interrupt_handler (int irq, void *dev_id, struct pt_regs *regs)
{
  int i = 0;
  struct Scsi_Host *shpnt;
  unsigned int intr_reg;
  unsigned int cmd_result;
  unsigned int ldn;
  unsigned long flags;

  /* search for one adapter-response on shared interrupt */
  do
    shpnt = hosts[i++];
  while (shpnt && !(inb(IM_STAT_REG) & IM_INTR_REQUEST));

  /* return if some other device on this IRQ caused the interrupt */
  if (!shpnt) return;

  /*get command result and logical device */
  intr_reg = inb (IM_INTR_REG);
  cmd_result = intr_reg & 0xf0;
  ldn = intr_reg & 0x0f;

  /*must wait for attention reg not busy, then send EOI to subsystem */
  save_flags(flags);
  while (1) {
      cli ();
      if (!(inb (IM_STAT_REG) & IM_BUSY)) 
        break;
      restore_flags(flags);
    }
  outb (IM_EOI | ldn, IM_ATTN_REG);
  restore_flags (flags);

  /*these should never happen (hw fails, or a local programming bug) */
  if (cmd_result == IM_ADAPTER_HW_FAILURE)
    panic ("IBM MCA SCSI: subsystem hardware failure. Last SCSI_CMD=0x%X. \n",
	   last_scsi_command);
  if (cmd_result == IM_CMD_ERROR)
    panic ("IBM MCA SCSI: command error. Last SCSI_CMD=0x%X. \n",
	   last_scsi_command);
  if (cmd_result == IM_SOFTWARE_SEQUENCING_ERROR)
    panic ("IBM MCA SCSI: software sequencing error. Last SCSI_CMD=0x%X. \n",
	   last_scsi_command);

  /* if no panic appeared, increase the interrupt-counter */
  IBM_DS.total_interrupts++;
   
  /*only for local checking phase */
  if (local_checking_phase_flag)
    {
      stat_result = cmd_result;
      got_interrupt = 1;
      reset_status = IM_RESET_FINISHED_OK;
      return;
    }

  /*handling of commands coming from upper level of scsi driver */
  else
    {
      Scsi_Cmnd *cmd;

      /*verify ldn, and may handle rare reset immediate command */
      if (ldn >= MAX_LOG_DEV)
	{
	  if (ldn == 0xf && reset_status == IM_RESET_IN_PROGRESS)
	    {
	      if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
		{
		  reset_status = IM_RESET_FINISHED_FAIL;
		}
	      else
		{
		  /*reset disk led counter, turn off disk led */
		  disk_rw_in_progress = 0;
		  PS2_DISK_LED_OFF ();
		  reset_status = IM_RESET_FINISHED_OK;
		}
	      return;
	    }
	  else
	    panic ("IBM MCA SCSI: invalid logical device number.\n");
	}

#ifdef IM_DEBUG_TIMEOUT
      {
        static int count = 0;

        if (++count == IM_DEBUG_TIMEOUT) {
          printk("IBM MCA SCSI: Ignoring interrupt.\n");
          return;
        }
      }
#endif

      /*if no command structure, just return, else clear cmd */
      cmd = ld[ldn].cmd;
      if (!cmd)
	return;
      ld[ldn].cmd = 0;

#ifdef IM_DEBUG_INT
      printk("cmd=%02x ireg=%02x ds=%02x cs=%02x de=%02x ce=%02x\n", 
         cmd->cmnd[0], intr_reg, 
         ld[ldn].tsb.dev_status, ld[ldn].tsb.cmd_status, 
         ld[ldn].tsb.dev_error, ld[ldn].tsb.cmd_error); 
#endif

      /*if this is end of media read/write, may turn off PS/2 disk led */
      if ((ld[ldn].device_type!=TYPE_NO_LUN)&&
	  (ld[ldn].device_type!=TYPE_NO_DEVICE))
	{ /* only access this, if there was a valid device addressed */
	  switch (cmd->cmnd[0])
	    {
	    case READ_6:
	    case WRITE_6:
	    case READ_10:
	    case WRITE_10:
	    case READ_12:
	    case WRITE_12:
	      if (--disk_rw_in_progress == 0)
		PS2_DISK_LED_OFF ();
	    }
	}
       
      /*write device status into cmd->result, and call done function */
      if (cmd_result == IM_CMD_COMPLETED_WITH_FAILURE)
	cmd->result = ld[ldn].tsb.dev_status & 0x1e;
      else
	cmd->result = 0;
      (cmd->scsi_done) (cmd);
    }
}

/*--------------------------------------------------------------------*/

static void 
issue_cmd (struct Scsi_Host *shpnt, unsigned long cmd_reg, 
           unsigned char attn_reg)
{
  unsigned long flags;
  /*must wait for attention reg not busy */
  save_flags(flags);
  while (1)
    {
      cli ();
      if (!(inb (IM_STAT_REG) & IM_BUSY))
	break;
      restore_flags (flags);
    }

  /*write registers and enable system interrupts */
  outl (cmd_reg, IM_CMD_REG);
  outb (attn_reg, IM_ATTN_REG);
  restore_flags (flags);
}

/*--------------------------------------------------------------------*/

static void 
internal_done (Scsi_Cmnd * cmd)
{
  cmd->SCp.Status++;
}

/*--------------------------------------------------------------------*/

static int ibmmca_getinfo (char *buf, int slot, void *dev)
{
  struct Scsi_Host *shpnt = dev;
  int len = 0;

  len += sprintf (buf + len, "Subsystem PUN: %d\n", subsystem_pun);
  len += sprintf (buf + len, "I/O base address: 0x%lx\n", IM_CMD_REG);
  return len;
}

/*--------------------------------------------------------------------*/

/* SCSI-SCB-command for device_inquiry */
static int device_inquiry(struct Scsi_Host *shpnt, int ldn, unsigned char *buf)
{   
  struct im_scb scb;
  struct im_tsb tsb;
  int retries;

  for (retries = 0; retries < 3; retries++)
    {
      /*fill scb with inquiry command */
      scb.command = IM_DEVICE_INQUIRY_CMD;
      scb.enable = IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
      scb.sys_buf_adr = virt_to_bus(buf);
      scb.sys_buf_length = 255;
      scb.tsb_adr = virt_to_bus(&tsb);

      /*issue scb to passed ldn, and busy wait for interrupt */
      got_interrupt = 0;
      issue_cmd (shpnt, virt_to_bus(&scb), IM_SCB | ldn);
      while (!got_interrupt)
	barrier ();

      /*if command succesful, break */
      if (stat_result == IM_SCB_CMD_COMPLETED)
	break;
    }

  /*if all three retries failed, return "no device at this ldn" */
  if (retries >= 3)
    return 0;
  else
    return 1;
}

/* SCSI-immediate-command for assign. This functions maps/unmaps specific
   ldn-numbers on SCSI (PUN,LUN). It is needed for presetting of the
   subsystem and for dynamical remapping od ldns. */
static int immediate_assign(struct Scsi_Host *shpnt, unsigned int pun, 
                            unsigned int lun, unsigned int ldn, 
                            unsigned int operation)
{
   int retries;
   unsigned long imm_command;

   for (retries=0; retries<3; retries ++)
     {	
        imm_command = inl(IM_CMD_REG);
        imm_command &= (unsigned long)(0xF8000000); /* keep reserved bits */
        imm_command |= (unsigned long)(IM_ASSIGN_IMM_CMD);
        imm_command |= (unsigned long)((lun & 7) << 24);
        imm_command |= (unsigned long)((operation & 1) << 23);
        imm_command |= (unsigned long)((pun & 7) << 20);
        imm_command |= (unsigned long)((ldn & 15) << 16);
	
        got_interrupt = 0;
        issue_cmd (shpnt, (unsigned long)(imm_command), IM_IMM_CMD | 0xf);
        while (!got_interrupt)
           barrier ();
	       
        /*if command succesful, break */
        if (stat_result == IM_IMMEDIATE_CMD_COMPLETED)
           break;
     }
   
   if (retries >= 3) 
     return 0;
   else
     return 1;
}

/* type-interpreter for physical device numbers */
static char *ti_p(int value)
{
   switch (value)
     {
	case TYPE_IBM_SCSI_ADAPTER: return("A"); break;
	case TYPE_DISK:             return("D"); break;
	case TYPE_TAPE:             return("T"); break;
	case TYPE_PROCESSOR:        return("P"); break;
	case TYPE_WORM:             return("W"); break;
	case TYPE_ROM:              return("R"); break;
	case TYPE_SCANNER:          return("S"); break;
	case TYPE_MOD:              return("M"); break;
        case TYPE_MEDIUM_CHANGER:   return("C"); break;
	case TYPE_NO_LUN:           return("+"); break; /* show NO_LUN */
        case TYPE_NO_DEVICE:
	default:                    return("-"); break;
     }
   return("-");
}

/* interpreter for logical device numbers (ldn) */
static char *ti_l(int value)
{
   const char hex[16] = ("0123456789abcdef");
   static char answer[2];

   answer[1] = (char)(0x0);
   if (value<=MAX_LOG_DEV)
     answer[0] = hex[value];
   else
     answer[0] = '-';
   
   return (char *)&answer;
}

/* 
   The following routine probes the SCSI-devices in four steps:
   1. The current ldn -> pun,lun mapping is removed on the SCSI-adapter.
   2. ldn 0 is used to go through all possible combinations of pun,lun and
      a device_inquiry is done to fiddle out whether there is a device
      responding or not. This physical map is stored in get_scsi[][].
   3. The 15 available ldns (0-14) are mapped to existing pun,lun.
      If there are more devices than ldns, it stops at 14 for the boot
      time. Dynamical remapping will be done in ibmmca_queuecommand.
   4. If there are less than 15 valid pun,lun, the remaining ldns are
      mapped to NON-existing pun,lun to satisfy the adapter. Information
      about pun,lun -> ldn is stored as before in get_ldn[][].
   This method leads to the result, that the SCSI-pun,lun shown to Linux
   mid-level- and higher-level-drivers is exactly corresponding to the
   physical reality on the SCSI-bus. Therefore, it is possible that users
   of older releases of this driver have to rewrite their fstab-file, because
   the /dev/sdXXX could have changed due to the right pun,lun report, now.
   The assignment of ALL ldns avoids dynamical remapping by the adapter
   itself.
 */
static void check_devices (struct Scsi_Host *shpnt)
{
  int id, lun, ldn;
  unsigned char buf[256];
  int count_devices = 0; /* local counter for connected device */
   
  /* assign default values to certain variables */
  
  IBM_DS.dyn_flag = 0; /* normally no need for dynamical ldn management */
  next_ldn = 7; /* next ldn to be assigned is 7, because 0-6 is 'hardwired'*/
  last_scsi_command = 0; /* emptify last SCSI-command storage */
  
  /* initialize the very important driver-informational arrays/structs */
  memset (ld, 0, sizeof ld);
  memset (get_ldn, TYPE_NO_DEVICE, sizeof get_ldn); /* this is essential ! */
  memset (get_scsi, TYPE_NO_DEVICE, sizeof get_scsi); /* this is essential ! */

  for (lun=0; lun<8; lun++) /* mark the adapter at its pun on all luns*/
    {
      get_scsi[subsystem_pun][lun] = TYPE_IBM_SCSI_ADAPTER; 
      get_ldn[subsystem_pun][lun] = MAX_LOG_DEV; /* make sure, the subsystem
						    ldn is active for all
						    luns. */
    }

  /* STEP 1: */
  printk("IBM MCA SCSI: Removing current logical SCSI-device mapping.");
  for (ldn=0; ldn<MAX_LOG_DEV; ldn++)
    {
#ifdef IM_DEBUG_PROBE
      printk(".");
#endif
      immediate_assign(shpnt,0,0,ldn,REMOVE_LDN); /* remove ldn (wherever)*/
    }

  lun = 0; /* default lun is 0 */

  /* STEP 2: */
  printk("\nIBM MCA SCSI: Probing SCSI-devices.");
  for (id=0; id<8; id++)
#ifdef CONFIG_SCSI_MULTI_LUN
    for (lun=0; lun<8; lun++)
#endif
      {
#ifdef IM_DEBUG_PROBE
	printk(".");
#endif
	if (id != subsystem_pun) 
	  {            /* if pun is not the adapter: */
	    immediate_assign(shpnt,id,lun,0,SET_LDN); /*set ldn=0 to pun,lun*/
	    if (device_inquiry(shpnt, 0, buf)) /* probe device */
	      {
		get_scsi[id][lun]=(unsigned char)buf[0];  /* entry, even 
							     for NO_LUN */
		if (buf[0] != TYPE_NO_LUN)
		  count_devices++; /* a existing device is found */
	      }
	    immediate_assign(shpnt,id,lun,0,REMOVE_LDN); /* remove ldn */
	  }
      }
  
  /* STEP 3: */   
  printk("\nIBM MCA SCSI: Mapping SCSI-devices.");
   
  ldn = 0;
  lun = 0;

#ifdef CONFIG_SCSI_MULTI_LUN   
  for (lun=0; lun<8 && ldn<MAX_LOG_DEV; lun++)
#endif
    for (id=0; id<8 && ldn<MAX_LOG_DEV; id++)
      {
#ifdef IM_DEBUG_PROBE
	printk(".");
#endif
	if (id != subsystem_pun)
	  {
	    if (get_scsi[id][lun] != TYPE_NO_LUN && 
		get_scsi[id][lun] != TYPE_NO_DEVICE)
	      {
		/* Only map if accepted type. Always enter for 
		   lun == 0 to get no gaps into ldn-mapping for ldn<7. */
		immediate_assign(shpnt,id,lun,ldn,SET_LDN);
		get_ldn[id][lun]=ldn; /* map ldn */
		if (device_exists (shpnt, ldn, &ld[ldn].block_length,
				   &ld[ldn].device_type))
		  {
#ifdef CONFIG_IBMMCA_SCSI_DEV_RESET
		    int ticks;
		    printk("(resetting)");
		    ticks = IM_RESET_DELAY*HZ;
		    reset_status = IM_RESET_IN_PROGRESS;
		    issue_cmd (shpnt, IM_RESET_IMM_CMD, IM_IMM_CMD | ldn);
		    while (reset_status == IM_RESET_IN_PROGRESS && --ticks) 
		      {
			mdelay(1+999/HZ);
			barrier();
		      }
		    /* if reset did not complete, just claim */
		    if (!ticks) 
		       {
		          printk("IBM MCA SCSI: reset did not complete within %d seconds.\n",
			         IM_RESET_DELAY);
		          reset_status = IM_RESET_FINISHED_OK; 
			                       /* did not work, finish */
		       }
#endif
		    ldn++;
		  }
		else
		  {
		    /* device vanished, probably because we don't know how to
		     * handle it or because it has problems */
		     if (lun > 0)
		       {
			  /* remove mapping */
			  get_ldn[id][lun]=TYPE_NO_DEVICE;
			  immediate_assign(shpnt,0,0,ldn,REMOVE_LDN);
		       }
		     else ldn++;
		  }
	      }
	   else if (lun == 0)
	      {
		 /* map lun == 0, even if no device exists */
		 immediate_assign(shpnt,id,lun,ldn,SET_LDN);
		 get_ldn[id][lun]=ldn; /* map ldn */
		 ldn++;
	      }
	  }	 
      }

   /* STEP 4: */
   
   /* map remaining ldns to non-existing devices */
   for (lun=1; lun<8 && ldn<MAX_LOG_DEV; lun++)
     for (id=0; id<8 && ldn<MAX_LOG_DEV; id++)
     {
	if (get_scsi[id][lun] == TYPE_NO_LUN ||
	    get_scsi[id][lun] == TYPE_NO_DEVICE)
	  {
	     /* Map remaining ldns only to NON-existing pun,lun
	        combinations to make sure an inquiry will fail. 
	        For MULTI_LUN, it is needed to avoid adapter autonome
	        SCSI-remapping. */
	     immediate_assign(shpnt,id,lun,ldn,SET_LDN);
	     get_ldn[id][lun]=ldn;
	     ldn++;
	  }
     }	
	
   printk("\n");
#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
   printk("IBM MCA SCSI: SCSI-access-order: IBM/ANSI.\n");
#else
   printk("IBM MCA SCSI: SCSI-access-order: Linux.\n");
#endif
   
#ifdef IM_DEBUG_PROBE
   /* Show the physical and logical mapping during boot. */
   printk("IBM MCA SCSI: Determined SCSI-device-mapping:\n");
   printk("    Physical SCSI-Device Map               Logical SCSI-Device Map\n");
   printk("ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
   for (id=0; id<8; id++)
     {
        printk("%2d     %2s %2s %2s %2s %2s %2s %2s %2s",
	       id, ti_p(get_scsi[id][0]), ti_p(get_scsi[id][1]), 
	       ti_p(get_scsi[id][2]), ti_p(get_scsi[id][3]), 
	       ti_p(get_scsi[id][4]), ti_p(get_scsi[id][5]), 
	       ti_p(get_scsi[id][6]), ti_p(get_scsi[id][7]));

	printk("       %2d     ",id);
	for (lun=0; lun<8; lun++)
	  printk("%2s ",ti_l(get_ldn[id][lun]));
	printk("\n");
     }
#endif

   /* assign total number of found SCSI-devices to the statistics struct */
   IBM_DS.total_scsi_devices = count_devices;
    
   /* decide for output in /proc-filesystem, if the configuration of
      SCSI-devices makes dynamical reassignment of devices necessary */
   if (count_devices>=MAX_LOG_DEV) 
     IBM_DS.dyn_flag = 1; /* dynamical assignment is necessary */
   else 
     IBM_DS.dyn_flag = 0; /* dynamical assignment is not necessary */

   /* If no SCSI-devices are assigned, return 1 in order to cause message. */
   if (ldn == 0)
     printk("IBM MCA SCSI: Warning: No SCSI-devices found/assignable!\n");

  /* reset the counters for statistics on the current adapter */
  IBM_DS.total_accesses = 0;
  IBM_DS.total_interrupts = 0;
  IBM_DS.dynamical_assignments = 0;
  memset (IBM_DS.ldn_access, 0x0, sizeof (IBM_DS.ldn_access));
  memset (IBM_DS.ldn_read_access, 0x0, sizeof (IBM_DS.ldn_read_access));
  memset (IBM_DS.ldn_write_access, 0x0, sizeof (IBM_DS.ldn_write_access));
  memset (IBM_DS.ldn_inquiry_access, 0x0, sizeof (IBM_DS.ldn_inquiry_access));
  memset (IBM_DS.ldn_modeselect_access, 0x0, sizeof (IBM_DS.ldn_modeselect_access));
  memset (IBM_DS.ldn_assignments, 0x0, sizeof (IBM_DS.ldn_assignments));
   
  return;
}

/*--------------------------------------------------------------------*/

static int 
device_exists (struct Scsi_Host *shpnt, int ldn, int *block_length, 
	       int *device_type)
{
  struct im_scb scb;
  struct im_tsb tsb;
  unsigned char buf[256];
  int retries;

  /* if no valid device found, return immediately with 0 */
  if (!(device_inquiry(shpnt, ldn, buf))) return 0;
   
  /*if device is CD_ROM, assume block size 2048 and return */
  if (buf[0] == TYPE_ROM)
    {
      *device_type = TYPE_ROM;
      *block_length = 2048; /* (standard blocksize for yellow-/red-book) */
      return 1;
    }
  
  if (buf[0] == TYPE_WORM) /* CD-burner, WORM, Linux handles this as CD-ROM 
			      therefore, the block_length is also 2048. */
    {
      *device_type = TYPE_WORM;
      *block_length = 2048;
      return 1;
    }
   
  /* if device is disk, use "read capacity" to find its block size */
  if (buf[0] == TYPE_DISK)
    {
      *device_type = TYPE_DISK;

      for (retries = 0; retries < 3; retries++)
	{
	  /*fill scb with read capacity command */
	  scb.command = IM_READ_CAPACITY_CMD;
	  scb.enable = IM_READ_CONTROL;
	  scb.sys_buf_adr = virt_to_bus(buf);
	  scb.sys_buf_length = 8;
	  scb.tsb_adr = virt_to_bus(&tsb);

	  /*issue scb to passed ldn, and busy wait for interrupt */
	  got_interrupt = 0;
	  issue_cmd (shpnt, virt_to_bus(&scb), IM_SCB | ldn);
	  while (!got_interrupt)
	    barrier ();

	  /*if got capacity, get block length and return one device found */
	  if (stat_result == IM_SCB_CMD_COMPLETED)
	    {
	      *block_length = buf[7] + (buf[6] << 8) + (buf[5] << 16) + (buf[4] << 24);
	      return 1;
	    }
	}

      /*if all three retries failed, return "no device at this ldn" */
      if (retries >= 3)
	return 0;
    }

  /* if this is a magneto-optical drive, treat it like a harddisk */
  if (buf[0] == TYPE_MOD)
    {
      *device_type = TYPE_MOD;

      for (retries = 0; retries < 3; retries++)
	{
	  /*fill scb with read capacity command */
	  scb.command = IM_READ_CAPACITY_CMD;
	  scb.enable = IM_READ_CONTROL;
	  scb.sys_buf_adr = virt_to_bus(buf);
	  scb.sys_buf_length = 8;
	  scb.tsb_adr = virt_to_bus(&tsb);

	  /*issue scb to passed ldn, and busy wait for interrupt */
	  got_interrupt = 0;
	  issue_cmd (shpnt, virt_to_bus(&scb), IM_SCB | ldn);
	  while (!got_interrupt)
	    barrier ();

	  /*if got capacity, get block length and return one device found */
	  if (stat_result == IM_SCB_CMD_COMPLETED)
	    {
	      *block_length = buf[7] + (buf[6] << 8) + (buf[5] << 16) + (buf[4] << 24);
	      return 1;
	    }
	}

      /*if all three retries failed, return "no device at this ldn" */
      if (retries >= 3)
	return 0;
    }   
   
  if (buf[0] == TYPE_TAPE) /* TAPE-device found */
    {
      *device_type = TYPE_TAPE;
      *block_length = 0; /* not in use (setting by mt and mtst in op.) */
      return 1;   
    }

  if (buf[0] == TYPE_PROCESSOR) /* HP-Scanners, diverse SCSI-processing units*/
    {
      *device_type = TYPE_PROCESSOR;
      *block_length = 0; /* they set their stuff on drivers */
      return 1;
    }
  
  if (buf[0] == TYPE_SCANNER) /* other SCSI-scanners */
    {
      *device_type = TYPE_SCANNER;
      *block_length = 0; /* they set their stuff on drivers */
      return 1;
    }

  if (buf[0] == TYPE_MEDIUM_CHANGER) /* Medium-Changer */
    {
      *device_type = TYPE_MEDIUM_CHANGER;
      *block_length = 0; /* One never knows, what to expect on a medium
			    changer device. */
      return 1;
    }

  /* Up to now, no SCSI-devices that are known up to kernel 2.1.31 are
     ignored! MO-drives are now supported and treated as harddisk. */   
  return 0;
}

/*--------------------------------------------------------------------*/

#ifdef CONFIG_SCSI_IBMMCA

void 
ibmmca_scsi_setup (char *str, int *ints)
{
   if( str && !strcmp( str, "display" ) ) {
   	use_display = 1;
   } else if( ints ) {
	   int i;
	   for (i = 0; i < IM_MAX_HOSTS && 2*i+2 < ints[0]; i++) {
	      io_port[i] = ints[2*i+2];
	      scsi_id[i] = ints[2*i+2];
	   }
   }
}

#endif

/*--------------------------------------------------------------------*/

int
ibmmca_detect (Scsi_Host_Template * template)
{
  struct Scsi_Host *shpnt;
  int port, id, i, list_size, slot;
  unsigned pos2, pos3;

  /* if this is not MCA machine, return "nothing found" */
  if (!MCA_bus)
    return 0;

  /* get interrupt request level */
  if (request_irq (IM_IRQ, do_interrupt_handler, SA_SHIRQ, "ibmmca", hosts))
    {
      printk("IBM MCA SCSI: Unable to get IRQ %d.\n", IM_IRQ);
      return 0;
    }

  /* if ibmmcascsi setup option was passed to kernel, return "found" */
  for (i = 0; i < IM_MAX_HOSTS; i++)
    if (io_port[i] > 0 && scsi_id[i] >= 0 && scsi_id[i] < 8)
      {
      printk("IBM MCA SCSI: forced detection, io=0x%x, scsi id=%d.\n",
              io_port[i], scsi_id[i]);
      ibmmca_register(template, io_port[i], scsi_id[i]);
      }
  if (found) return found;

    /*
     * Patched by ZP Gu to work with the 9556 as well; the 9556 has
     * pos2 = 05, but it should be 00, as it should be interfaced
     * via port = 0x3540.
     */

  /* first look for the SCSI integrated on the motherboard */
  pos2 = mca_read_stored_pos(MCA_INTEGSCSI, 2);
//  if (pos2 != 0xff) {
    if ((pos2 & 1) == 0) {
      port = IM_IO_PORT + ((pos2 & 0x0e) << 2);
    } else {
      port = IM_IO_PORT;
    }
      pos3 = mca_read_stored_pos(MCA_INTEGSCSI, 3);
      id = (pos3 & 0xe0) >> 5;

      printk("IBM MCA SCSI: integrated SCSI found, io=0x%x, scsi id=%d.\n",
              port, id);
      if ((shpnt = ibmmca_register(template, port, id)))
        {
          mca_set_adapter_name(MCA_INTEGSCSI, "PS/2 Integrated SCSI");
          mca_set_adapter_procfn(MCA_INTEGSCSI, (MCA_ProcFn) ibmmca_getinfo,
                                 shpnt);
        }
//    }

  /* now look for other adapters */
  list_size = sizeof(subsys_list) / sizeof(struct subsys_list_struct);
  for (i = 0; i < list_size; i++)
    {
      slot = 0;
      while ((slot = mca_find_adapter(subsys_list[i].mca_id, slot))
             != MCA_NOTFOUND)
        {
          pos2 = mca_read_stored_pos(slot, 2);
          pos3 = mca_read_stored_pos(slot, 3);
          port = IM_IO_PORT + ((pos2 & 0x0e) << 2);
          id = (pos3 & 0xe0) >> 5;
          printk ("IBM MCA SCSI: %s found in slot %d, io=0x%x, scsi id=%d.\n",
                  subsys_list[i].description, slot + 1, port, id);
          if ((shpnt = ibmmca_register(template, port, id)))
            {
              mca_set_adapter_name (slot, subsys_list[i].description);
              mca_set_adapter_procfn (slot, (MCA_ProcFn) ibmmca_getinfo,
                                      shpnt);
            }
          slot++;
        }
    }

  if (!found) {
    free_irq (IM_IRQ, hosts);
    printk("IBM MCA SCSI: No adapter attached.\n");
  }

  return found;
}

static struct Scsi_Host *
ibmmca_register(Scsi_Host_Template * template, int port, int id)
{
  struct Scsi_Host *shpnt;
  int i, j;

  /* check I/O region */
  if (check_region(port, IM_N_IO_PORT))
    {
      printk("IBM MCA SCSI: Unable to get I/O region 0x%x-0x%x.\n",
        port, port + IM_N_IO_PORT);
      return NULL;
    }

  /* register host */
  shpnt = scsi_register(template, sizeof(struct ibmmca_hostdata));
  if (!shpnt)
    {
      printk("IBM MCA SCSI: Unable to register host.\n");
      return NULL;
    }

  /* request I/O region */
  request_region(port, IM_N_IO_PORT, "ibmmca");

  hosts[found++] = shpnt;
  shpnt->irq = IM_IRQ;
  shpnt->io_port = port;
  shpnt->n_io_port = IM_N_IO_PORT;
  shpnt->this_id = id;

  reset_status = IM_RESET_NOT_IN_PROGRESS;

  for (i = 0; i < 8; i++)
    for (j = 0; j < 8; j++)
      get_ldn[i][j] = MAX_LOG_DEV;

  /* check which logical devices exist */
  local_checking_phase_flag = 1;
  check_devices(shpnt);
  local_checking_phase_flag = 0;

  /* an ibm mca subsystem has been detected */
  return shpnt;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_command (Scsi_Cmnd * cmd)
{
  ibmmca_queuecommand (cmd, internal_done);
  cmd->SCp.Status = 0;
  while (!cmd->SCp.Status)
    barrier ();
  return cmd->result;
}

/*--------------------------------------------------------------------*/

int
ibmmca_release(struct Scsi_Host *shpnt)
{
  release_region(shpnt->io_port, shpnt->n_io_port);
  if (!(--found))
    free_irq(shpnt->irq, hosts);
  return 0;
}

/*--------------------------------------------------------------------*/

/* The following routine is the SCSI command queue. The old edition is
   now improved by dynamical reassignment of ldn numbers that are 
   currently not assigned. The mechanism works in a way, that first
   the physical structure is checked. If at a certain pun,lun a device
   should be present, the routine proceeds to the ldn check from
   get_ldn. An answer of 0xff would show-up, that the aimed device is
   currently not assigned any ldn. At this point, the dynamical 
   remapping algorithm is called. It works in a way, that it goes in
   cyclic order through the ldns from 7 to 14. If a ldn is assigned,
   it takes 8 dynamical reassignment calls, until a device looses its
   ldn again. With this method it is assured, that while doing 
   intense I/O between up to eight devices, no dynamical remapping is
   done there. ldns 0 through 6(!) are left untouched, which means, that
   puns 0 through 7(!) on lun=0 are always accessible without remapping.
   These ldns are statically assigned by this driver. The subsystem always 
   occupies at least one pun, therefore 7 ldns (at lun=0) for other devices 
   are sufficient. (The adapter uses always ldn=15, at whatever pun it is.) */
int ibmmca_queuecommand (Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
  unsigned int ldn;
  unsigned int scsi_cmd;
  struct im_scb *scb;
  struct Scsi_Host *shpnt = cmd->host;
  
  int current_ldn;
  int id,lun;

  /* use industry standard ordering of the IDs */
#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
  int target = 6 - cmd->target;
#else
  int target = cmd->target;
#endif

  /*if (target,lun) is NO LUN or not existing at all, return error */
  if ((get_scsi[target][cmd->lun] == TYPE_NO_LUN)||
      (get_scsi[target][cmd->lun] == TYPE_NO_DEVICE))
     {
	cmd->result = DID_NO_CONNECT << 16;
	done (cmd);
	return 0;
     }
   
  /*if (target,lun) unassigned, do further checks... */
  ldn = get_ldn[target][cmd->lun];
  if (ldn >= MAX_LOG_DEV) /* on invalid ldn do special stuff */
    {
      if (ldn > MAX_LOG_DEV) /* dynamical remapping if ldn unassigned */
	 {
	    current_ldn = next_ldn; /* stop-value for one circle */
	    while (ld[next_ldn].cmd) /* search for a occupied, but not in */
	      {                      /* command-processing ldn. */
		 next_ldn ++;
		 if (next_ldn>=MAX_LOG_DEV) 
		   next_ldn = 7;
		 if (current_ldn == next_ldn) /* One circle done ? */
		   {         /* no non-processing ldn found */
		      printk("IBM MCA SCSI: Cannot assign SCSI-device dynamically!\n");
		      printk("              On ldn 7-14 SCSI-commands everywhere in progress.\n");
		      printk("              Reporting DID_NO_CONNECT for device (%d,%d).\n",
			     target, cmd->lun);
		      cmd->result = DID_NO_CONNECT << 16;/* return no connect*/
		      done (cmd);
		      return 0;
		   }
	      }

	    /* unmap non-processing ldn */
	    for (id=0; id<8; id ++)
	      for (lun=0; lun<8; lun++)
	      {
		 if (get_ldn[id][lun] == next_ldn)
		   {
		      get_ldn[id][lun] = TYPE_NO_DEVICE; /* unmap entry */
		      goto DYN_ASSIGN;  /* jump out as fast as possible */
		   }
	      }

DYN_ASSIGN:	    
	    /* unassign found ldn (pun,lun does not matter for remove) */
	    immediate_assign(shpnt,0,0,next_ldn,REMOVE_LDN);
	    /* assign found ldn to aimed pun,lun */
	    immediate_assign(shpnt,target,cmd->lun,next_ldn,SET_LDN);
	    /* map found ldn to pun,lun */
	    get_ldn[target][cmd->lun] = next_ldn;
            /* change ldn to the right value, that is now next_ldn */
	    ldn = next_ldn;
	    /* set reduced interrupt_handler-mode for checking */
	    local_checking_phase_flag = 1;
	    /* get device information for ld[ldn] */
	    if (device_exists (shpnt, ldn, &ld[ldn].block_length,
			       &ld[ldn].device_type))
	       {
		 ld[ldn].cmd = 0; /* To prevent panic set 0, because
				     devices that were not assigned,
				     should have nothing in progress. */
		  
		 /* increase assignment counters for statistics in /proc */
		 IBM_DS.dynamical_assignments++;
		 IBM_DS.ldn_assignments[ldn]++;
	       }
	    else
	         /* panic here, because a device, found at boottime has 
		    vanished */
	         panic("IBM MCA SCSI: ldn=0x%x, SCSI-device on (%d,%d) vanished!\n",
		       ldn, target, cmd->lun);
	    
	    /* set back to normal interrupt_handling */
	    local_checking_phase_flag = 0;
	    
	    /* Information on syslog terminal */
	    printk("IBM MCA SCSI: ldn=0x%x dynamically reassigned to (%d,%d).\n",
		   ldn, target, cmd->lun);
	    
	    /* increase next_ldn for next dynamical assignment */ 
	    next_ldn ++;
	    if (next_ldn>=MAX_LOG_DEV) next_ldn = 7;
	 }       
      else
	 {  /* wall against Linux accesses to the subsystem adapter */	 
            cmd->result = DID_NO_CONNECT << 16;
            done (cmd);
            return 0;
	 }
    }

  /*verify there is no command already in progress for this log dev */
  if (ld[ldn].cmd)
    panic ("IBM MCA SCSI: cmd already in progress for this ldn.\n");

  /*save done in cmd, and save cmd for the interrupt handler */
  cmd->scsi_done = done;
  ld[ldn].cmd = cmd;

  /*fill scb information independent of the scsi command */
  scb = &(ld[ldn].scb);
  scb->enable = IM_REPORT_TSB_ONLY_ON_ERROR;
  scb->tsb_adr = virt_to_bus(&(ld[ldn].tsb));
  if (cmd->use_sg)
    {
      int i = cmd->use_sg;
      struct scatterlist *sl = (struct scatterlist *) cmd->request_buffer;
      if (i > 16)
	panic ("IBM MCA SCSI: scatter-gather list too long.\n");
      while (--i >= 0)
	{
	  ld[ldn].sge[i].address = (void *) virt_to_bus(sl[i].address);
	  ld[ldn].sge[i].byte_length = sl[i].length;
	}
      scb->enable |= IM_POINTER_TO_LIST;
      scb->sys_buf_adr = virt_to_bus(&(ld[ldn].sge[0]));
      scb->sys_buf_length = cmd->use_sg * sizeof (struct im_sge);
    }
  else
    {
      scb->sys_buf_adr = virt_to_bus(cmd->request_buffer);
      scb->sys_buf_length = cmd->request_bufflen;
    }

  /*fill scb information dependent on scsi command */
  scsi_cmd = cmd->cmnd[0];
   
#ifdef IM_DEBUG_CMD
  printk("issue scsi cmd=%02x to ldn=%d\n", scsi_cmd, ldn);
#endif

  /* for specific device-type debugging: */
#ifdef IM_DEBUG_CMD_SPEC_DEV
  if (ld[ldn].device_type==IM_DEBUG_CMD_DEVICE)
     printk("(SCSI-device-type=0x%x) issue scsi cmd=%02x to ldn=%d\n", 
	    ld[ldn].device_type, scsi_cmd, ldn);
#endif

  /* for possible panics store current command */
  last_scsi_command = scsi_cmd; 
   
  /* update statistical info */
  IBM_DS.total_accesses++;
  IBM_DS.ldn_access[ldn]++;
   
  switch (scsi_cmd)
    {
    case READ_6:
    case WRITE_6:
    case READ_10:
    case WRITE_10:
    case READ_12:
    case WRITE_12:       
      /* statistics for proc_info */
      if ((scsi_cmd == READ_6)||(scsi_cmd == READ_10)||(scsi_cmd == READ_12))
	 IBM_DS.ldn_read_access[ldn]++; /* increase READ-access on ldn stat. */
      else if ((scsi_cmd == WRITE_6)||(scsi_cmd == WRITE_10)||
	       (scsi_cmd == WRITE_12))
	 IBM_DS.ldn_write_access[ldn]++; /* increase write-count on ldn stat.*/

      /* Distinguish between disk and other devices. Only disks (that are the
	 most frequently accessed devices) should be supported by the 
         IBM-SCSI-Subsystem commands. */
      switch (ld[ldn].device_type)
	 {
	  case TYPE_DISK: /* for harddisks enter here ... */
	  case TYPE_MOD:  /* ... try it also for MO-drives (send flames as */
			  /* you like, if this won't work.) */
           if (scsi_cmd == READ_6 || scsi_cmd == READ_10 || 
	       scsi_cmd == READ_12)
	     {
	       scb->command = IM_READ_DATA_CMD;
	       scb->enable |= IM_READ_CONTROL;
	     }
           else
	     {
	       scb->command = IM_WRITE_DATA_CMD;
	     }
           if (scsi_cmd == READ_6 || scsi_cmd == WRITE_6)
	     {
	       scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[3]) << 0) |
	         (((unsigned) cmd->cmnd[2]) << 8) |
	         ((((unsigned) cmd->cmnd[1]) & 0x1f) << 16);
	       scb->u2.blk.count = (unsigned) cmd->cmnd[4];
	     }
           else
	     {
	       scb->u1.log_blk_adr = (((unsigned) cmd->cmnd[5]) << 0) |
	         (((unsigned) cmd->cmnd[4]) << 8) |
	         (((unsigned) cmd->cmnd[3]) << 16) |
	         (((unsigned) cmd->cmnd[2]) << 24);
	       scb->u2.blk.count = (((unsigned) cmd->cmnd[8]) << 0) |
	         (((unsigned) cmd->cmnd[7]) << 8);
	     }
           scb->u2.blk.length = ld[ldn].block_length;
	   if (++disk_rw_in_progress == 1)
	      PS2_DISK_LED_ON (shpnt->host_no, target);
	  break;
	    
	  /* for other devices, enter here. Other types are not known by
	     Linux! TYPE_NO_LUN is forbidden as valid device. */
          case TYPE_ROM:
	  case TYPE_TAPE:
	  case TYPE_PROCESSOR:
	  case TYPE_WORM:
	  case TYPE_SCANNER:
	  case TYPE_MEDIUM_CHANGER:
	  
	   /* If there is a sequential-device, IBM recommends to use
	      IM_OTHER_SCSI_CMD_CMD instead of subsystem READ/WRITE. 
	      Good/modern CD-ROM-drives are capable of
	      reading sequential AND random-access. This leads to the problem,
	      that random-accesses are covered by the subsystem, but 
	      sequentials are not, as like for tape-drives. Therefore, it is
	      the easiest way to use IM_OTHER_SCSI_CMD_CMD for all read-ops
	      on CD-ROM-drives in order not to run into timing problems and
	      to have a stable state. In addition, data-access on CD-ROMs
	      works faster like that. Strange, but obvious. */
	    
           scb->command = IM_OTHER_SCSI_CMD_CMD;
	   if (scsi_cmd == READ_6 || scsi_cmd == READ_10 || 
	       scsi_cmd == READ_12) /* enable READ */
              scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
	   else
	      scb->enable |= IM_SUPRESS_EXCEPTION_SHORT; /* assume WRITE */
	    
           scb->u1.scsi_cmd_length = cmd->cmd_len;
           memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
	    
	   /* Read/write on this non-disk devices is also displayworthy, 
	      so flash-up the LED/display. */
	   if (++disk_rw_in_progress == 1)
	      PS2_DISK_LED_ON (shpnt->host_no, target);
	 break;
	 }
      break;
    case INQUIRY:
      IBM_DS.ldn_inquiry_access[ldn]++;
      scb->command = IM_DEVICE_INQUIRY_CMD;
      scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
      break;

    case READ_CAPACITY:
      scb->command = IM_READ_CAPACITY_CMD;
      scb->enable |= IM_READ_CONTROL;
      /* the length of system memory buffer must be exactly 8 bytes */
      if (scb->sys_buf_length >= 8)
	scb->sys_buf_length = 8;
      break;

    /* Commands that need read-only-mode (system <- device): */
    case REQUEST_SENSE:
      scb->command = IM_REQUEST_SENSE_CMD;
      scb->enable |= IM_READ_CONTROL;
      break;
       
    /* Commands that need write-only-mode (system -> device): */
    case MODE_SELECT:
    case MODE_SELECT_10:
      IBM_DS.ldn_modeselect_access[ldn]++;
      scb->command = IM_OTHER_SCSI_CMD_CMD;      
      scb->enable |= IM_SUPRESS_EXCEPTION_SHORT; /*Select needs WRITE-enabled*/
      scb->u1.scsi_cmd_length = cmd->cmd_len;
      memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
      break;
            
    /* For other commands, read-only is useful. Most other commands are 
       running without an input-data-block. */
    default:
      scb->command = IM_OTHER_SCSI_CMD_CMD;
      scb->enable |= IM_READ_CONTROL | IM_SUPRESS_EXCEPTION_SHORT;
      scb->u1.scsi_cmd_length = cmd->cmd_len;
      memcpy (scb->u2.scsi_command, cmd->cmnd, cmd->cmd_len);
      break;
    }

  /*issue scb command, and return */
  issue_cmd (shpnt, virt_to_bus(scb), IM_SCB | ldn);
  return 0;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_abort (Scsi_Cmnd * cmd)
{
  /* The code below doesn't work right now, so we tell the upper layer
     that we can't abort. This eventually causes a reset.
     */
  return SCSI_ABORT_SNOOZE ;

#if 0
  struct Scsi_host *shpnt = cmd->host;
  unsigned int ldn;
  void (*saved_done) (Scsi_Cmnd *);

#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
  int target = 6 - cmd->target;
#else
  int target = cmd->target;
#endif

  /*get logical device number, and disable system interrupts */
  printk ("IBM MCA SCSI: sending abort to device id=%d lun=%d.\n",
	  target, cmd->lun);
  ldn = get_ldn[target][cmd->lun];
  cli ();

  /*if cmd for this ldn has already finished, no need to abort */
  if (!ld[ldn].cmd)
    {
      /* sti (); */
      return SCSI_ABORT_NOT_RUNNING;
    }

  /* Clear ld.cmd, save done function, install internal done, 
   * send abort immediate command (this enables sys. interrupts), 
   * and wait until the interrupt arrives. 
   */
  ld[ldn].cmd = 0;
  saved_done = cmd->scsi_done;
  cmd->scsi_done = internal_done;
  cmd->SCp.Status = 0;
  issue_cmd (shpnt, T_IMM_CMD, IM_IMM_CMD | ldn);
  while (!cmd->SCp.Status)
    barrier ();

  /*if abort went well, call saved done, then return success or error */
  if (cmd->result == 0)
    {
      cmd->result |= DID_ABORT << 16;
      saved_done (cmd);
      return SCSI_ABORT_SUCCESS;
    }
  else
    return SCSI_ABORT_ERROR;
#endif
}

/*--------------------------------------------------------------------*/

int
ibmmca_reset (Scsi_Cmnd * cmd, unsigned int reset_flags)
{
  struct Scsi_Host *shpnt = cmd->host;
  int ticks = IM_RESET_DELAY*HZ;

  if (local_checking_phase_flag) {
    printk("IBM MCA SCSI: unable to reset while checking devices.\n");
    return SCSI_RESET_SNOOZE;
  }

  /* issue reset immediate command to subsystem, and wait for interrupt */
  printk("IBM MCA SCSI: resetting all devices.\n");
  cli ();
  reset_status = IM_RESET_IN_PROGRESS;
  issue_cmd (shpnt, IM_RESET_IMM_CMD, IM_IMM_CMD | 0xf);
  while (reset_status == IM_RESET_IN_PROGRESS && --ticks) {
    mdelay(1+999/HZ);
    barrier();
  }
  /* if reset did not complete, just return an error*/
  if (!ticks) {
    printk("IBM MCA SCSI: reset did not complete within %d seconds.\n",
           IM_RESET_DELAY);
    reset_status = IM_RESET_FINISHED_FAIL;
    return SCSI_RESET_ERROR;
  }

  /* if reset failed, just return an error */
  if (reset_status == IM_RESET_FINISHED_FAIL) {
    printk("IBM MCA SCSI: reset failed.\n");
    return SCSI_RESET_ERROR;
  }

  /* so reset finished ok - call outstanding done's, and return success */
  printk ("IBM MCA SCSI: reset completed without error.\n");
  {
    int i;
    for (i = 0; i < MAX_LOG_DEV; i++)
      {
        Scsi_Cmnd *cmd = ld[i].cmd;
        if (cmd && cmd->scsi_done)
          {
            ld[i].cmd = 0;
            cmd->result = DID_RESET;
            (cmd->scsi_done) (cmd);
          }
      }
  }
  return SCSI_RESET_SUCCESS;
}

/*--------------------------------------------------------------------*/

int 
ibmmca_biosparam (Disk * disk, kdev_t dev, int *info)
{
  info[0] = 64;
  info[1] = 32;
  info[2] = disk->capacity / (info[0] * info[1]);
  if (info[2] >= 1024)
    {
      info[0] = 128;
      info[1] = 63;
      info[2] = disk->capacity / (info[0] * info[1]);
      if (info[2] >= 1024)
	{
	  info[0] = 255;
	  info[1] = 63;
	  info[2] = disk->capacity / (info[0] * info[1]);
	  if (info[2] >= 1024)
	    info[2] = 1023;
	}
    }
  return 0;
}

/* calculate percentage of total accesses on a ldn */
static int ldn_access_load(struct Scsi_Host *shpnt, int ldn)
{
   if (IBM_DS.total_accesses == 0) return (0);
   if (IBM_DS.ldn_access[ldn] == 0) return (0);
   return (IBM_DS.ldn_access[ldn] * 100) / IBM_DS.total_accesses;
}

/* calculate total amount of r/w-accesses */
static int ldn_access_total_read_write(struct Scsi_Host *shpnt)
{
   int a = 0;
   int i;
   
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS.ldn_read_access[i]+IBM_DS.ldn_write_access[i];
   return(a);
}

static int ldn_access_total_inquiry(struct Scsi_Host *shpnt)
{
   int a = 0;
   int i;
   
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS.ldn_inquiry_access[i];
   return(a);
}

static int ldn_access_total_modeselect(struct Scsi_Host *shpnt)
{
   int a = 0;
   int i;
   
   for (i=0; i<=MAX_LOG_DEV; i++)
     a+=IBM_DS.ldn_modeselect_access[i];
   return(a);
}

/* routine to display info in the proc-fs-structure (a deluxe feature) */
int ibmmca_proc_info (char *buffer, char **start, off_t offset, int length,
		      int hostno, int inout)
{
   int len=0;
   int i,id,lun;
   struct Scsi_Host *shpnt;
   unsigned long flags;

   for (i = 0; hosts[i] && hosts[i]->host_no != hostno; i++);
   shpnt = hosts[i];
   if (!shpnt) {
       len += sprintf(buffer+len, "\nCan't find adapter for host number %d\n", hostno);
       return len;
   }

   save_flags(flags);
   cli();

   len += sprintf(buffer+len, "\n             IBM-SCSI-Subsystem-Linux-Driver, Version %s\n\n\n",
		  IBMMCA_SCSI_DRIVER_VERSION);
   len += sprintf(buffer+len, " SCSI Access-Statistics:\n");
#ifdef CONFIG_IBMMCA_SCSI_ORDER_STANDARD
   len += sprintf(buffer+len, "               ANSI-SCSI-standard order.: Yes\n");
#else
   len += sprintf(buffer+len, "               ANSI-SCSI-standard order.: No\n");
#endif
#ifdef CONFIG_SCSI_MULTI_LUN
   len += sprintf(buffer+len, "               Multiple LUN probing.....: Yes\n");
#else
   len += sprintf(buffer+len, "               Multiple LUN probing.....: No\n");
#endif
   len += sprintf(buffer+len, "               This Hostnumber..........: %d\n",
		  hostno);
   len += sprintf(buffer+len, "               Base I/O-Port............: 0x%lx\n",
		  IM_CMD_REG);
   len += sprintf(buffer+len, "               (Shared) IRQ.............: %d\n",
		  IM_IRQ);
   len += sprintf(buffer+len, "               Total Interrupts.........: %d\n",
		  IBM_DS.total_interrupts);
   len += sprintf(buffer+len, "               Total SCSI Accesses......: %d\n",
		  IBM_DS.total_accesses);
   len += sprintf(buffer+len, "                 Total SCSI READ/WRITE..: %d\n",
		  ldn_access_total_read_write(shpnt));
   len += sprintf(buffer+len, "                 Total SCSI Inquiries...: %d\n",
		  ldn_access_total_inquiry(shpnt));
   len += sprintf(buffer+len, "                 Total SCSI Modeselects.: %d\n",
		  ldn_access_total_modeselect(shpnt));
   len += sprintf(buffer+len, "                 Total SCSI other cmds..: %d\n\n",
		  IBM_DS.total_accesses - ldn_access_total_read_write(shpnt)
		  - ldn_access_total_modeselect(shpnt)
		  - ldn_access_total_inquiry(shpnt));
   
   len += sprintf(buffer+len, " Logical-Device-Number (LDN) Access-Statistics:\n");
   len += sprintf(buffer+len, "         LDN | Accesses [%%] |   READ    |   WRITE   | ASSIGNMENTS\n");
   len += sprintf(buffer+len, "        -----|--------------|-----------|-----------|--------------\n");
   for (i=0; i<=MAX_LOG_DEV; i++)
      len += sprintf(buffer+len, "         %2X  |    %3d       |  %8d |  %8d | %8d\n",
		     i, ldn_access_load(shpnt, i), IBM_DS.ldn_read_access[i],
		     IBM_DS.ldn_write_access[i], IBM_DS.ldn_assignments[i]);
   len += sprintf(buffer+len, "        -----------------------------------------------------------\n\n");
   
   len += sprintf(buffer+len, " Dynamical-LDN-Assignment-Statistics:\n");
   len += sprintf(buffer+len, "               Number of physical SCSI-devices..: %d (+ Adapter)\n",
		  IBM_DS.total_scsi_devices);
   len += sprintf(buffer+len, "               Dynamical Assignment necessaray..: %s\n", 
		  IBM_DS.dyn_flag ? "Yes" : "No ");
   len += sprintf(buffer+len, "               Next LDN to be assigned..........: 0x%x\n",
		  next_ldn);
   len += sprintf(buffer+len, "               Dynamical assignments done yet...: %d\n",
		  IBM_DS.dynamical_assignments);

   len += sprintf(buffer+len, "\n Current SCSI-Device-Mapping:\n");
   len += sprintf(buffer+len, "        Physical SCSI-Device Map               Logical SCSI-Device Map\n");
   len += sprintf(buffer+len, "    ID\\LUN  0  1  2  3  4  5  6  7       ID\\LUN  0  1  2  3  4  5  6  7\n");
   for (id=0; id<=7; id++)
     {
	len += sprintf(buffer+len, "    %2d     %2s %2s %2s %2s %2s %2s %2s %2s",
	       id, ti_p(get_scsi[id][0]), ti_p(get_scsi[id][1]), 
	       ti_p(get_scsi[id][2]), ti_p(get_scsi[id][3]), 
	       ti_p(get_scsi[id][4]), ti_p(get_scsi[id][5]), 
	       ti_p(get_scsi[id][6]), ti_p(get_scsi[id][7]));
	
	len += sprintf(buffer+len, "       %2d     ",id);
	for (lun=0; lun<8; lun++)
	  len += sprintf(buffer+len,"%2s ",ti_l(get_ldn[id][lun]));
	len += sprintf(buffer+len,"\n");
     }
   
   len += sprintf(buffer+len, "(A = IBM-Subsystem, D = Harddisk, T = Tapedrive, P = Processor, W = WORM,\n");
   len += sprintf(buffer+len, " R = CD-ROM, S = Scanner, M = MO-Drive, C = Medium-Changer, + = unprovided LUN,\n");
   len += sprintf(buffer+len, " - = nothing found)\n\n");
   
   *start = buffer + offset;
   len -= offset;
   if (len > length) 
     len = length;
   
   restore_flags(flags);
   
   return len;
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = IBMMCA;

#include "scsi_module.c"
#endif

/*--------------------------------------------------------------------*/



