/* $Id: advansys.c,v 1.15 1996/08/12 17:20:23 bobf Exp bobf $ */
/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 * 
 * Copyright (c) 1995-1996 Advanced System Products, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * The latest version of this driver is available at the AdvanSys
 * FTP and BBS sites listed below.
 *
 * Please send questions, comments, bug reports to:
 * bobf@advansys.com (Bob Frey)
 */

/*
 * The driver has been run with the v1.2.13, v1.3.57, and v2.0.11 kernels.
 */
#define ASC_VERSION "1.5"    /* AdvanSys Driver Version */

/*

  Documentation for the AdvanSys Driver

  A. Adapters Supported by this Driver
  B. Linux v1.2.X - Directions for Adding the AdvanSys Driver
  C. Linux v1.3.X, v2.X.X - Directions for Adding the AdvanSys Driver
  D. Source Comments
  E. Driver Compile Time Options and Debugging
  F. Driver LILO Option
  G. Release History
  H. Known Problems or Issues
  I. Credits
  J. AdvanSys Contact Information

  A. Adapters Supported by this Driver
 
     AdvanSys (Advanced System Products, Inc.) manufactures the following
     Bus-Mastering SCSI-2 Host Adapters for the ISA, EISA, VL, and PCI
     buses. This Linux driver supports all of these adapters.
     
     The CDB counts below indicate the number of SCSI CDB (Command
     Descriptor Block) requests that can be stored in the RISC chip
     cache and board LRAM. A CDB is a single SCSI command. The driver
     detect routine will display the number of CDBs available for each
     adapter detected. This value can be lowered in the BIOS by changing
     the 'Host Queue Size' adapter setting.

     Connectivity Products:
        ABP510/5150 - Bus-Master ISA (240 CDB) (Footnote 1)
        ABP5140 - Bus-Master ISA PnP (16 CDB) (Footnote 1)
        ABP5142 - Bus-Master ISA PnP with floppy (16 CDB)
        ABP920 - Bus-Master PCI (16 CDB)
        ABP930 - Bus-Master PCI (16 CDB)
        ABP960 - Bus-Master PCI MAC/PC (16 CDB) (Footnote 2)
  
     Single Channel Products:
        ABP542 - Bus-Master ISA with floppy (240 CDB)
        ABP742 - Bus-Master EISA (240 CDB)
        ABP842 - Bus-Master VL (240 CDB)
        ABP940 - Bus-Master PCI (240 CDB)
        ABP940U - Bus-Master PCI Ultra (240 CDB)
        ABP970 - Bus-Master PCI MAC/PC (240 CDB)
     
     Dual Channel Products:
        ABP752 - Dual Channel Bus-Master EISA (240 CDB Per Channel)
        ABP852 - Dual Channel Bus-Master VL (240 CDB Per Channel)
        ABP950 - Dual Channel Bus-Master PCI (240 CDB Per Channel)
     
     Footnotes:
       1. These boards have been shipped by HP with the 4020i CD-R drive.
          They have no BIOS so they cannot control a boot device, but they
          can control secondary devices.
   
       2. This board has been shipped by Iomega with the Jaz Jet drive.
  
  B. Linux v1.2.X - Directions for Adding the AdvanSys Driver

     These directions apply to v1.2.13. For versions that follow v1.2.13.
     but precede v1.3.57 some of the changes for Linux v1.3.X listed
     below may need to be modified or included.
 
     There are two source files: advansys.h and advansys.c. Copy
     both of these files to the directory /usr/src/linux/drivers/scsi.
    
     1. Add the following line to /usr/src/linux/arch/i386/config.in
        after "comment 'SCSI low-level drivers'":
    
          bool 'AdvanSys SCSI support' CONFIG_SCSI_ADVANSYS y
    
     2. Add the following lines to /usr/src/linux/drivers/scsi/hosts.c
        after "#include "hosts.h"":
    
          #ifdef CONFIG_SCSI_ADVANSYS
          #include "advansys.h"
          #endif
    
        and after "static Scsi_Host_Template builtin_scsi_hosts[] =":
    
          #ifdef CONFIG_SCSI_ADVANSYS
          ADVANSYS,
          #endif
    
     3. Add the following lines to /usr/src/linux/drivers/scsi/Makefile:
    
          ifdef CONFIG_SCSI_ADVANSYS
          SCSI_SRCS := $(SCSI_SRCS) advansys.c
          SCSI_OBJS := $(SCSI_OBJS) advansys.o
          else
          SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) advansys.o
          endif

     4. (Optional) If you would like to enable the LILO command line
        and /etc/lilo.conf 'advansys' option, make the following changes.
        This option can be used to disable I/O port scanning or to limit
        I/O port scanning to specific addresses. Refer to the 'Driver
        LILO Option' section below. Add the following lines to
        /usr/src/linux/init/main.c in the prototype section:

          extern void advansys_setup(char *str, int *ints);

        and add the following lines to the bootsetups[] array.

          #ifdef CONFIG_SCSI_ADVANSYS
             { "advansys=", advansys_setup },
          #endif

     5. If you have the HP 4020i CD-R driver and Linux v1.2.X you should
        add a fix to the CD-ROM target driver. This fix will allow
        you to mount CDs with the iso9660 file system. Linux v1.3.X
        already has this fix. In the file /usr/src/linux/drivers/scsi/sr.c
        and function get_sectorsize() after the line:

        if(scsi_CDs[i].sector_size == 0) scsi_CDs[i].sector_size = 2048;

        add the following line:

        if(scsi_CDs[i].sector_size == 2340) scsi_CDs[i].sector_size = 2048;

     6. In the directory /usr/src/linux run 'make config' to configure
        the AdvanSys driver, then run 'make vmlinux' or 'make zlilo' to
        make the kernel. If the AdvanSys driver is not configured, then
        a loadable module can be built by running 'make modules' and
        'make modules_install'. Use 'insmod' and 'rmmod' to install
        and remove advansys.o.
 
  C. Linux v1.3.X, v2.X.X - Directions for Adding the AdvanSys Driver

     These directions apply to v1.3.57. For versions that precede v1.3.57
     some of these changes may need to be modified or eliminated. Beginning
     with v1.3.58 this driver is included with the Linux distribution.

     There are two source files: advansys.h and advansys.c. Copy
     both of these files to the directory /usr/src/linux/drivers/scsi.
   
     1. Add the following line to /usr/src/linux/drivers/scsi/Config.in
        after "comment 'SCSI low-level drivers'":
   
          dep_tristate 'AdvanSys SCSI support' CONFIG_SCSI_ADVANSYS $CONFIG_SCSI
   
     2. Add the following lines to /usr/src/linux/drivers/scsi/hosts.c
        after "#include "hosts.h"":
   
          #ifdef CONFIG_SCSI_ADVANSYS
          #include "advansys.h"
          #endif
   
        and after "static Scsi_Host_Template builtin_scsi_hosts[] =":
   
          #ifdef CONFIG_SCSI_ADVANSYS
          ADVANSYS,
          #endif
   
     3. Add the following lines to /usr/src/linux/drivers/scsi/Makefile:
   
          ifeq ($(CONFIG_SCSI_ADVANSYS),y)
          L_OBJS += advansys.o
          else
            ifeq ($(CONFIG_SCSI_ADVANSYS),m)
            M_OBJS += advansys.o
            endif
          endif
   
     4. Add the following line to /usr/src/linux/include/linux/proc_fs.h
        in the enum scsi_directory_inos array:
   
          PROC_SCSI_ADVANSYS,
   
     5. (Optional) If you would like to enable the LILO command line
        and /etc/lilo.conf 'advansys' option, make the following changes.
        This option can be used to disable I/O port scanning or to limit
        I/O port scanning to specific addresses. Refer to the 'Driver
        LILO Option' section below. Add the following lines to
        /usr/src/linux/init/main.c in the prototype section:
   
          extern void advansys_setup(char *str, int *ints);
   
        and add the following lines to the bootsetups[] array.
   
          #ifdef CONFIG_SCSI_ADVANSYS
             { "advansys=", advansys_setup },
          #endif
   
     6. In the directory /usr/src/linux run 'make config' to configure
        the AdvanSys driver, then run 'make vmlinux' or 'make zlilo' to
        make the kernel. If the AdvanSys driver is not configured, then
        a loadable module can be built by running 'make modules' and
        'make modules_install'. Use 'insmod' and 'rmmod' to install
        and remove advansys.o.

  D. Source Comments
 
     1. Use tab stops set to 4 for the source files. For vi use 'se tabstops=4'.
 
     2. This driver should be maintained in multiple files. But to make
        it easier to include with Linux and to follow Linux conventions,
        the whole driver is maintained in the source files advansys.h and
        advansys.c. In this file logical sections of the driver begin with
        a comment that contains '---'. The following are the logical sections
        of the driver below.
 
           --- Linux Version
           --- Linux Include Files 
           --- Driver Options
           --- Asc Library Constants and Macros
           --- Debugging Header
           --- Driver Constants and Macros
           --- Driver Structures
           --- Driver Data
           --- Driver Function Prototypes
           --- Linux 'Scsi_Host_Template' and advansys_setup() Functions
           --- Loadable Driver Support
           --- Miscellaneous Driver Functions
           --- Functions Required by the Asc Library
           --- Tracing and Debugging Functions
           --- Asc Library Functions
 
     3. The string 'XXX' is used to flag code that needs to be re-written
        or that contains a problem that needs to be addressed.
 
     4. I have stripped comments from and reformatted the source for the
        Asc Library which is included in this file. I haven't done this
        to obfuscate the code. Actually I have done this to deobfuscate
        the code. The Asc Library source can be found under the following
        headings.
 
           --- Asc Library Constants and Macros
           --- Asc Library Functions
 
  E. Driver Compile Time Options and Debugging
 
     In this source file the following constants can be defined. They are
     defined in the source below. Both of these options are enabled by
     default.
 
     1. ADVANSYS_DEBUG - enable for debugging and assertions
 
        The amount of debugging output can be controlled with the global
        variable 'asc_dbglvl'. The higher the number the more output. By
        default the debug level is 0.
        
        If the driver is loaded at boot time and the LILO Driver Option
        is included in the system, the debug level can be changed by
        specifying a 5th (ASC_NUM_BOARD_SUPPORTED + 1) I/O Port. The
        first three hex digits of the pseudo I/O Port must be set to
        'deb' and the fourth hex digit specifies the debug level: 0 - F.
        The following command line will look for an adapter at 0x330
        and set the debug level to 2.

           linux advansys=0x330,0,0,0,0xdeb2

        If the driver is built as a loadable module this variable can be
        defined when the driver is loaded. The following insmod command
        will set the debug level to one.
  
           insmod advansys.o asc_dbglvl=1
 
        Debugging Message Levels:
           0: Errors Only
           1: High-Level Tracing
           2-N: Verbose Tracing
 
        I don't know the approved way for turning on printk()s to the
        console. Here's a program I use to do this. Debug output is
        logged in /var/adm/messages.
 
          main()
          {
                  syscall(103, 7, 0, 0);
          }
 
        I found that increasing LOG_BUF_LEN to 40960 in kernel/printk.c
        prevents most level 1 debug messages from being lost.
 
     2. ADVANSYS_STATS - enable statistics
 
        Statistics are maintained on a per adapter basis. Driver entry
        point call counts and tranfer size counts are maintained.
        Statistics are only available for kernels greater than or equal
        to v1.3.0 with the CONFIG_PROC_FS (/proc) file system configured.

        AdvanSys SCSI adapter files have the following path name format:

           /proc/scsi/advansys/[0-(ASC_NUM_BOARD_SUPPORTED-1)]

        This information can be displayed with cat. For example:

           cat /proc/scsi/advansys/0

        When ADVANSYS_STATS is not defined the AdvanSys /proc files only
        contain adapter and device configuration information.


  F. Driver LILO Option
 
     If init/main.c is modified as described in the 'Directions for Adding
     the AdvanSys Driver to Linux' section (B.4.) above, the driver will
     recognize the 'advansys' LILO command line and /etc/lilo.conf option.
     This option can be used to either disable I/O port scanning or to limit
     scanning to 1 - 4 I/O ports. Regardless of the option setting EISA and
     PCI boards will still be searched for and detected. This option only
     affects searching for ISA and VL boards.

     Examples:
       1. Eliminate I/O port scanning:
            boot: linux advansys=
              or
            boot: linux advansys=0x0
       2. Limit I/O port scanning to one I/O port:
            boot: linux advansys=0x110
       3. Limit I/O port scanning to four I/O ports:
            boot: linux advansys=0x110,0x210,0x230,0x330

     For a loadable module the same effect can be achieved by setting
     the 'asc_iopflag' variable and 'asc_ioport' array when loading
     the driver, e.g.

           insmod advansys.o asc_iopflag=1 asc_ioport=0x110,0x330

     If ADVANSYS_DEBUG is defined a 5th (ASC_NUM_BOARD_SUPPORTED + 1)
     I/O Port may be added to specify the driver debug level. Refer to
     the 'Driver Compile Time Options and Debugging' section above for
     more information.

  G. Release History

     BETA-1.0 (12/23/95): 
         First Release

     BETA-1.1 (12/28/95):
         1. Prevent advansys_detect() from being called twice.
         2. Add LILO 0xdeb[0-f] option to set 'asc_dbglvl'.

     1.2 (1/12/96):
         1. Prevent re-entrancy in the interrupt handler which
            resulted in the driver hanging Linux.
         2. Fix problem that prevented ABP-940 cards from being
            recognized on some PCI motherboards.
         3. Add support for the ABP-5140 PnP ISA card.
         4. Fix check condition return status.
         5. Add conditionally compiled code for Linux v1.3.X.

     1.3 (2/23/96):
         1. Fix problem in advansys_biosparam() that resulted in the
            wrong drive geometry being returned for drives > 1GB with
            extended translation enabled.
         2. Add additional tracing during device initialization.
         3. Change code that only applies to ISA PnP adapter.
         4. Eliminate 'make dep' warning.
         5. Try to fix problem with handling resets by increasing their
            timeout value.

     1.4 (5/8/96):
         1. Change definitions to eliminate conflicts with other subsystems.
         2. Add versioning code for the shared interrupt changes.
         3. Eliminate problem in asc_rmqueue() with iterating after removing
            a request.
         4. Remove reset request loop problem from the "Known Problems or
            Issues" section. This problem was isolated and fixed in the
            mid-level SCSI driver.
        
     1.5 (8/8/96):
         1. Add support for ABP-940U (PCI Ultra) adapter.
         2. Add support for IRQ sharing by setting the SA_SHIRQ flag for
            request_irq and supplying a dev_id pointer to both request_irq()
            and free_irq().
         3. In AscSearchIOPortAddr11() restore a call to check_region() which
            should be used before any I/O port probing.
         4. Fix bug in asc_prt_hex() which resulted in the displaying
            the wrong data.
         5. Incorporate miscellaneous Asc Library bug fixes and new microcode.
         6. Change driver versioning to be specific to each Linux sub-level.
         7. Change statistics gathering to be per adapter instead of global
            to the driver.
         8. Add more information and statistics to the adapter /proc file:
            /proc/scsi/advansys[0...].
         9. Remove 'cmd_per_lun' from the "Known Problems or Issues" list.
            This problem has been addressed with the SCSI mid-level changes
            made in v1.3.89. The advansys_select_queue_depths() function
            was added for the v1.3.89 changes.

  H. Known Problems or Issues

     1. For the first scsi command sent to a device the driver increases
        the timeout value. This gives the driver more time to perform
        its own initialization for the board and each device. The timeout
        value is only changed on the first scsi command for each device
        and never thereafter. The same change is made for reset commands.

  I. Credits

     Nathan Hartwell <mage@cdc3.cdc.net> provided the directions and
     basis for the Linux v1.3.X changes which were included in the
     1.2 release.

     Thomas E Zerucha <zerucha@shell.portal.com> pointed out a bug
     in advansys_biosparam() which was fixed in the 1.3 release.

  J. AdvanSys Contact Information
 
     Mail:                   Advanced System Products, Inc.
                             1150 Ringwood Court
                             San Jose, CA 95131
     Operator:               1-408-383-9400
     FAX:                    1-408-383-9612
     Tech Support:           1-800-525-7440/1-408-467-2930
     BBS:                    1-408-383-9540 (14400,N,8,1)
     Interactive FAX:        1-408-383-9753
     Customer Direct Sales:  1-800-883-1099/1-408-383-5777
     Tech Support E-Mail:    support@advansys.com
     FTP Site:               ftp.advansys.com (login: anonymous)
     Web Site:               http://www.advansys.com
*/


/*
 * --- Linux Version
 */

/* Convert Linux Version, Patch-level, Sub-level to LINUX_VERSION_CODE. */
#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif /* LINUX_VERSION_CODE */


/*
 * --- Linux Include Files 
 */

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
#ifdef MODULE
#include <linux/module.h>
#endif /* MODULE */
#endif /* version >= v1.3.0 */
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
#include <linux/proc_fs.h>
#endif /* version >= v1.3.0 */
#include <asm/io.h>
#include <asm/system.h>
#include <asm/dma.h>
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,0)
#include "../block/blk.h"
#else /* version >= v1.3.0 */
#include <linux/blk.h>
#include <linux/stat.h>
#endif /* version >= v1.3.0 */
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include "advansys.h"


/*
 * --- Driver Options
 */

#define ADVANSYS_DEBUG /* Enable assertions and tracing. */
/*
 * Because of no /proc to display them, statistics are disabled
 * for version prior to v1.3.0.
 */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,0)
#undef ADVANSYS_STATS /* Disable statistics */
#else /* version >= v1.3.0 */
#define ADVANSYS_STATS /* Enable statistics. */
#endif /* version >= v1.3.0 */


/*
 * --- Asc Library Constants and Macros
 */

#define ASC_LIB_VERSION_MAJOR  1
#define ASC_LIB_VERSION_MINOR  21
#define ASC_LIB_SERIAL_NUMBER  88

typedef unsigned char uchar;

#ifndef NULL
#define NULL     (0)
#endif
#ifndef TRUE
#define TRUE     (1)
#endif
#ifndef FALSE
#define FALSE    (0)
#endif
#define  REG     register
#define rchar    REG __s8
#define rshort   REG __s16
#define rint     REG __s32
#define rlong    REG __s32
#define ruchar   REG __u8
#define rushort  REG __u16
#define ruint    REG __u32
#define rulong   REG __u32
#define NULLPTR   ( void *)0
#define FNULLPTR  ( void dosfar *)0UL
#define EOF      (-1)
#define EOS      '\0'
#define ERR      (-1)
#define UB_ERR   (uchar)(0xFF)
#define UW_ERR   (uint)(0xFFFF)
#define UL_ERR   (ulong)(0xFFFFFFFFUL)
#define iseven_word( val )  ( ( ( ( uint )val) & ( uint )0x0001 ) == 0 )
#define isodd_word( val )   ( ( ( ( uint )val) & ( uint )0x0001 ) != 0 )
#define toeven_word( val )  ( ( ( uint )val ) & ( uint )0xFFFE )
#define biton( val, bits )   ((( uint )( val >> bits ) & (uint)0x0001 ) != 0 )
#define bitoff( val, bits )  ((( uint )( val >> bits ) & (uint)0x0001 ) == 0 )
#define lbiton( val, bits )  ((( ulong )( val >> bits ) & (ulong)0x00000001UL ) != 0 )
#define lbitoff( val, bits ) ((( ulong )( val >> bits ) & (ulong)0x00000001UL ) == 0 )
#define  absh( val )    ( ( val ) < 0 ? -( val ) : ( val ) )
#define  swapbyte( ch )  ( ( ( (ch) << 4 ) | ( (ch) >> 4 ) ) )
#ifndef GBYTE
#define GBYTE       (0x40000000UL)
#endif
#ifndef MBYTE
#define MBYTE       (0x100000UL)
#endif
#ifndef KBYTE
#define KBYTE       (0x400)
#endif
#define HI_BYTE(x) ( *( ( __u8 *)(&x)+1 ) )
#define LO_BYTE(x) ( *( ( __u8 *)&x ) )
#define HI_WORD(x) ( *( ( __u16 *)(&x)+1 ) )
#define LO_WORD(x) ( *( ( __u16 *)&x ) )
#ifndef MAKEWORD
#define MAKEWORD(lo, hi)    ((__u16) (((__u16) lo) | ((__u16) hi << 8)))
#endif
#ifndef MAKELONG
#define MAKELONG(lo, hi)    ((__u32) (((__u32) lo) | ((__u32) hi << 16)))
#endif
#define SwapWords(dWord)        ((__u32) ((dWord >> 16) | (dWord << 16)))
#define SwapBytes(word)         ((__u16) ((word >> 8) | (word << 8)))
#define BigToLittle(dWord) ((__u32) (SwapWords(MAKELONG(SwapBytes(LO_WORD(dWord)), SwapBytes(HI_WORD(dWord))))))
#define LittleToBig(dWord)      BigToLittle(dWord)
#define AscPCICmdRegBits_BusMastering     0x0007
#define AscPCIConfigVendorIDRegister      0x0000
#define AscPCIConfigDeviceIDRegister      0x0002
#define AscPCIConfigCommandRegister       0x0004
#define AscPCIConfigStatusRegister        0x0006
#define AscPCIConfigCacheSize             0x000C
#define AscPCIConfigLatencyTimer          0x000D
#define AscPCIIOBaseRegister              0x0010
#define ASC_PCI_ID2BUS( id )    ((id) & 0xFF)
#define ASC_PCI_ID2DEV( id )    (((id) >> 11) & 0x1F)
#define ASC_PCI_ID2FUNC( id )   (((id) >> 8) & 0x7)
#define ASC_PCI_MKID( bus, dev, func ) ((((dev) & 0x1F) << 11) | (((func) & 0x7) << 8) | ((bus) & 0xFF))

#define Lptr
#define dosfar
#define far
#define PortAddr  unsigned short	/* port address size  */
#define Ptr2Func	ulong
#define inp(port)           inb(port)
#define inpw(port)          inw(port)
#define inpl(port)    		inl(port)
#define outp(port, byte)    outb((byte), (port))
#define outpw(port, word)   outw((word), (port))
#define outpl(port, long)   outl((long), (port))
#define ASC_MAX_SG_QUEUE	5
#define ASC_MAX_SG_LIST		(1 + ((ASC_SG_LIST_PER_Q) * (ASC_MAX_SG_QUEUE)))

#define CC_INIT_INQ_DISPLAY     FALSE
#define CC_CLEAR_LRAM_SRB_PTR   FALSE
#define CC_VERIFY_LRAM_COPY     FALSE
#define CC_DEBUG_SG_LIST        FALSE
#define CC_FAST_STRING_IO       FALSE
#define CC_WRITE_IO_COUNT       FALSE
#define CC_CLEAR_DMA_REMAIN     FALSE
#define CC_DISABLE_PCI_PARITY_INT TRUE
#define CC_LINK_BUSY_Q         FALSE
#define CC_LITTLE_ENDIAN_HOST  TRUE
#define CC_STRUCT_ALIGNED      TRUE
#define CC_MEMORY_MAPPED_IO    FALSE
#define CC_INCLUDE_EEP_CONFIG  TRUE
#define CC_PCI_ULTRA           TRUE
#define CC_INIT_TARGET_READ_CAPACITY    TRUE
#define CC_INIT_TARGET_TEST_UNIT_READY  TRUE
#define CC_ASC_SCSI_Q_USRDEF         FALSE
#define CC_ASC_SCSI_REQ_Q_USRDEF     FALSE
#define CC_ASCISR_CHECK_INT_PENDING  TRUE
#define CC_CHK_FIX_EEP_CONTENT       TRUE
#define CC_CHK_AND_COALESCE_SG_LIST  FALSE
#define CC_DISABLE_PCI_PARITY_INT    TRUE
#define CC_INCLUDE_EEP_CONFIG        TRUE
#define CC_INIT_INQ_DISPLAY          FALSE
#define CC_INIT_TARGET_TEST_UNIT_READY  TRUE
#define CC_INIT_TARGET_START_UNIT       TRUE
#define CC_PLEXTOR_VL                FALSE
#define CC_TMP_USE_EEP_SDTR          FALSE
#define CC_CHK_COND_REDO_SDTR        TRUE
#define CC_SET_PCI_LATENCY_TIMER_ZERO  TRUE
#define CC_FIX_QUANTUM_XP34301_1071  FALSE
#define CC_DISABLE_ASYN_FIX_WANGTEK_TAPE  TRUE

#define ASC_CS_TYPE  unsigned short
#ifndef asc_ptr_type
#define asc_ptr_type
#endif

#ifndef ASC_GET_PTR2FUNC
#define ASC_GET_PTR2FUNC( fun )  ( Ptr2Func )( fun )
#endif
#define FLIP_BYTE_NIBBLE( x )    ( ((x<<4)& 0xFF) | (x>>4) )
#define ASC_IS_ISA          (0x0001)
#define ASC_IS_ISAPNP       (0x0081)
#define ASC_IS_EISA         (0x0002)
#define ASC_IS_PCI          (0x0004)
#define ASC_IS_PCI_ULTRA    (0x0104)
#define ASC_IS_PCMCIA       (0x0008)
#define ASC_IS_MCA          (0x0020)
#define ASC_IS_VL           (0x0040)
#define ASC_ISA_PNP_PORT_ADDR  (0x279)
#define ASC_ISA_PNP_PORT_WRITE (ASC_ISA_PNP_PORT_ADDR+0x800)
#define ASC_IS_WIDESCSI_16  (0x0100)
#define ASC_IS_WIDESCSI_32  (0x0200)
#define ASC_IS_BIG_ENDIAN   (0x8000)
#define ASC_CHIP_MIN_VER_VL      (0x01)
#define ASC_CHIP_MAX_VER_VL      (0x07)
#define ASC_CHIP_MIN_VER_PCI     (0x09)
#define ASC_CHIP_MAX_VER_PCI     (0x0F)
#define ASC_CHIP_VER_PCI_BIT     (0x08)
#define ASC_CHIP_MIN_VER_ISA     (0x11)
#define ASC_CHIP_MIN_VER_ISA_PNP (0x21)
#define ASC_CHIP_MAX_VER_ISA     (0x27)
#define ASC_CHIP_VER_ISA_BIT     (0x30)
#define ASC_CHIP_VER_ISAPNP_BIT  (0x20)
#define ASC_CHIP_VER_ASYN_BUG    (0x21)
#define ASC_CHIP_VER_1ST_PCI_ULTRA   (0x0A)
#define ASC_CHIP_MIN_VER_EISA (0x41)
#define ASC_CHIP_MAX_VER_EISA (0x47)
#define ASC_CHIP_VER_EISA_BIT (0x40)
#define ASC_CHIP_LATEST_VER_EISA   ( ( ASC_CHIP_MIN_VER_EISA - 1 ) + 3 )
#define ASC_MAX_VL_DMA_ADDR     (0x07FFFFFFL)
#define ASC_MAX_VL_DMA_COUNT    (0x07FFFFFFL)
#define ASC_MAX_PCI_DMA_ADDR    (0xFFFFFFFFL)
#define ASC_MAX_PCI_DMA_COUNT   (0xFFFFFFFFL)
#define ASC_MAX_ISA_DMA_ADDR    (0x00FFFFFFL)
#define ASC_MAX_ISA_DMA_COUNT   (0x00FFFFFFL)
#define ASC_MAX_EISA_DMA_ADDR   (0x07FFFFFFL)
#define ASC_MAX_EISA_DMA_COUNT  (0x07FFFFFFL)
#if !CC_STRUCT_ALIGNED
#define DvcGetQinfo( iop_base, s_addr, outbuf, words)  \
AscMemWordCopyFromLram(iop_base, s_addr, outbuf, words)
#define DvcPutScsiQ( iop_base, s_addr, outbuf, words) \
AscMemWordCopyToLram(iop_base, s_addr, outbuf, words)
#endif
#ifdef ASC_CHIP_VERSION
#endif
#if CC_MEMORY_MAPPED_IO
#define inp( port )            *( (uchar *)(port) )
#define outp( port, data )     *( (uchar *)(port) ) = ( uchar )( data )
#if CC_LITTLE_ENDIAN_HOST
#define inpw( port )              *( (ushort *)(port) )
#define outpw( port, data )       *( (ushort *)(port) ) = ( ushort )( data )
#else
#define inpw( port )             EndianSwap16Bit( (*((ushort *)(port))) )
#define outpw( port, data )      *( (ushort *)(port) ) = EndianSwap16Bit( (ushort)(data) )
#define inpw_noswap( port )          *( (ushort *)(port) )
#define outpw_noswap( port, data )   *( (ushort *)(port) ) = ( ushort )( data )
#endif
#endif
#ifndef inpw_noswap
#define inpw_noswap( port )         inpw( port )
#endif
#ifndef outpw_noswap
#define outpw_noswap( port, data )  outpw( port, data )
#endif
#define ASC_SCSI_ID_BITS  3
#define ASC_SCSI_TIX_TYPE     uchar
#define ASC_ALL_DEVICE_BIT_SET  0xFF
#ifdef ASC_WIDESCSI_16
#undef  ASC_SCSI_ID_BITS
#define ASC_SCSI_ID_BITS  4
#define ASC_ALL_DEVICE_BIT_SET  0xFFFF
#endif
#ifdef ASC_WIDESCSI_32
#undef  ASC_SCSI_ID_BITS
#define ASC_SCSI_ID_BITS  5
#define ASC_ALL_DEVICE_BIT_SET  0xFFFFFFFFL
#endif
#if ASC_SCSI_ID_BITS == 3
#define ASC_SCSI_BIT_ID_TYPE  uchar
#define ASC_MAX_TID       7
#define ASC_MAX_LUN       7
#define ASC_SCSI_WIDTH_BIT_SET  0xFF
#elif ASC_SCSI_ID_BITS == 4
#define ASC_SCSI_BIT_ID_TYPE   ushort
#define ASC_MAX_TID         15
#define ASC_MAX_LUN         7
#define ASC_SCSI_WIDTH_BIT_SET  0xFFFF
#elif ASC_SCSI_ID_BITS == 5
#define ASC_SCSI_BIT_ID_TYPE    ulong
#define ASC_MAX_TID         31
#define ASC_MAX_LUN         7
#define ASC_SCSI_WIDTH_BIT_SET  0xFFFFFFFF
#else
#error  ASC_SCSI_ID_BITS definition is wrong
#endif
#define ASC_MAX_SENSE_LEN   32
#define ASC_MIN_SENSE_LEN   14
#define ASC_MAX_CDB_LEN     12
#define SCSICMD_TestUnitReady     0x00
#define SCSICMD_Rewind            0x01
#define SCSICMD_Rezero            0x01
#define SCSICMD_RequestSense      0x03
#define SCSICMD_Format            0x04
#define SCSICMD_FormatUnit        0x04
#define SCSICMD_Read6             0x08
#define SCSICMD_Write6            0x0A
#define SCSICMD_Seek6             0x0B
#define SCSICMD_Inquiry           0x12
#define SCSICMD_Verify6           0x13
#define SCSICMD_ModeSelect6       0x15
#define SCSICMD_ModeSense6        0x1A
#define SCSICMD_StartStopUnit     0x1B
#define SCSICMD_LoadUnloadTape    0x1B
#define SCSICMD_ReadCapacity      0x25
#define SCSICMD_Read10            0x28
#define SCSICMD_Write10           0x2A
#define SCSICMD_Seek10            0x2B
#define SCSICMD_Erase10           0x2C
#define SCSICMD_WriteAndVerify10  0x2E
#define SCSICMD_Verify10          0x2F
#define SCSICMD_WriteBuffer       0x3B
#define SCSICMD_ReadBuffer        0x3C
#define SCSICMD_ReadLong          0x3E
#define SCSICMD_WriteLong         0x3F
#define SCSICMD_ReadTOC           0x43
#define SCSICMD_ReadHeader        0x44
#define SCSICMD_ModeSelect10      0x55
#define SCSICMD_ModeSense10       0x5A
#define SCSI_TYPE_DASD     0x00
#define SCSI_TYPE_SASD     0x01
#define SCSI_TYPE_PRN      0x02
#define SCSI_TYPE_PROC     0x03
#define SCSI_TYPE_WORM     0x04
#define SCSI_TYPE_CDROM    0x05
#define SCSI_TYPE_SCANNER  0x06
#define SCSI_TYPE_OPTMEM   0x07
#define SCSI_TYPE_MED_CHG  0x08
#define SCSI_TYPE_COMM     0x09
#define SCSI_TYPE_UNKNOWN  0x1F
#define SCSI_TYPE_NO_DVC   0xFF
#define ASC_SCSIDIR_NOCHK    0x00
#define ASC_SCSIDIR_T2H      0x08
#define ASC_SCSIDIR_H2T      0x10
#define ASC_SCSIDIR_NODATA   0x18
#define SCSI_SENKEY_NO_SENSE      0x00
#define SCSI_SENKEY_UNDEFINED     0x01
#define SCSI_SENKEY_NOT_READY     0x02
#define SCSI_SENKEY_MEDIUM_ERR    0x03
#define SCSI_SENKEY_HW_ERR        0x04
#define SCSI_SENKEY_ILLEGAL       0x05
#define SCSI_SENKEY_ATTENSION     0x06
#define SCSI_SENKEY_PROTECTED     0x07
#define SCSI_SENKEY_BLANK         0x08
#define SCSI_SENKEY_V_UNIQUE      0x09
#define SCSI_SENKEY_CPY_ABORT     0x0A
#define SCSI_SENKEY_ABORT         0x0B
#define SCSI_SENKEY_EQUAL         0x0C
#define SCSI_SENKEY_VOL_OVERFLOW  0x0D
#define SCSI_SENKEY_MISCOMP       0x0E
#define SCSI_SENKEY_RESERVED      0x0F
#define ASC_SRB_HOST( x )  ( ( uchar )( ( uchar )( x ) >> 4 ) )
#define ASC_SRB_TID( x )   ( ( uchar )( ( uchar )( x ) & ( uchar )0x0F ) )
#define ASC_SRB_LUN( x )   ( ( uchar )( ( uint )( x ) >> 13 ) )
#define PUT_CDB1( x )   ( ( uchar )( ( uint )( x ) >> 8 ) )
#define SS_GOOD              0x00
#define SS_CHK_CONDITION     0x02
#define SS_CONDITION_MET     0x04
#define SS_TARGET_BUSY       0x08
#define SS_INTERMID          0x10
#define SS_INTERMID_COND_MET 0x14
#define SS_RSERV_CONFLICT    0x18
#define SS_CMD_TERMINATED    0x22
#define SS_QUEUE_FULL        0x28
#define MS_CMD_DONE    0x00
#define MS_EXTEND      0x01
#define MS_SDTR_LEN    0x03
#define MS_SDTR_CODE   0x01
#define M1_SAVE_DATA_PTR        0x02
#define M1_RESTORE_PTRS         0x03
#define M1_DISCONNECT           0x04
#define M1_INIT_DETECTED_ERR    0x05
#define M1_ABORT                0x06
#define M1_MSG_REJECT           0x07
#define M1_NO_OP                0x08
#define M1_MSG_PARITY_ERR       0x09
#define M1_LINK_CMD_DONE        0x0A
#define M1_LINK_CMD_DONE_WFLAG  0x0B
#define M1_BUS_DVC_RESET        0x0C
#define M1_ABORT_TAG            0x0D
#define M1_CLR_QUEUE            0x0E
#define M1_INIT_RECOVERY        0x0F
#define M1_RELEASE_RECOVERY     0x10
#define M1_KILL_IO_PROC         0x11
#define M2_QTAG_MSG_SIMPLE      0x20
#define M2_QTAG_MSG_HEAD        0x21
#define M2_QTAG_MSG_ORDERED     0x22
#define M2_IGNORE_WIDE_RESIDUE  0x23
#if CC_LITTLE_ENDIAN_HOST
typedef struct {
	uchar               peri_dvc_type:5;
	uchar               peri_qualifier:3;
} ASC_SCSI_INQ0;

#else
typedef struct {
	uchar               peri_qualifier:3;
	uchar               peri_dvc_type:5;
} ASC_SCSI_INQ0;

#endif
#if CC_LITTLE_ENDIAN_HOST
typedef struct {
	uchar               dvc_type_modifier:7;
	uchar               rmb:1;
} ASC_SCSI_INQ1;

#else
typedef struct {
	uchar               rmb:1;
	uchar               dvc_type_modifier:7;
} ASC_SCSI_INQ1;

#endif
#if CC_LITTLE_ENDIAN_HOST
typedef struct {
	uchar               ansi_apr_ver:3;
	uchar               ecma_ver:3;
	uchar               iso_ver:2;
} ASC_SCSI_INQ2;

#else
typedef struct {
	uchar               iso_ver:2;
	uchar               ecma_ver:3;
	uchar               ansi_apr_ver:3;
} ASC_SCSI_INQ2;

#endif
#if CC_LITTLE_ENDIAN_HOST
typedef struct {
	uchar               rsp_data_fmt:4;
	uchar               res:2;
	uchar               TemIOP:1;
	uchar               aenc:1;
} ASC_SCSI_INQ3;

#else
typedef struct {
	uchar               aenc:1;
	uchar               TemIOP:1;
	uchar               res:2;
	uchar               rsp_data_fmt:4;
} ASC_SCSI_INQ3;

#endif
#if CC_LITTLE_ENDIAN_HOST
typedef struct {
	uchar               StfRe:1;
	uchar               CmdQue:1;
	uchar               Reserved:1;
	uchar               Linked:1;
	uchar               Sync:1;
	uchar               WBus16:1;
	uchar               WBus32:1;
	uchar               RelAdr:1;
} ASC_SCSI_INQ7;

#else
typedef struct {
	uchar               RelAdr:1;
	uchar               WBus32:1;
	uchar               WBus16:1;
	uchar               Sync:1;
	uchar               Linked:1;
	uchar               Reserved:1;
	uchar               CmdQue:1;
	uchar               StfRe:1;
} ASC_SCSI_INQ7;

#endif
typedef struct {
	ASC_SCSI_INQ0       byte0;
	ASC_SCSI_INQ1       byte1;
	ASC_SCSI_INQ2       byte2;
	ASC_SCSI_INQ3       byte3;
	uchar               add_len;
	uchar               res1;
	uchar               res2;
	ASC_SCSI_INQ7       byte7;
	uchar               vendor_id[8];
	uchar               product_id[16];
	uchar               product_rev_level[4];
} ASC_SCSI_INQUIRY;

#if CC_LITTLE_ENDIAN_HOST
typedef struct asc_req_sense {
	uchar               err_code:7;
	uchar               info_valid:1;
	uchar               segment_no;
	uchar               sense_key:4;
	uchar               reserved_bit:1;
	uchar               sense_ILI:1;
	uchar               sense_EOM:1;
	uchar               file_mark:1;
	uchar               info1[4];
	uchar               add_sense_len;
	uchar               cmd_sp_info[4];
	uchar               asc;
	uchar               ascq;
	uchar               fruc;
	uchar               sks_byte0:7;
	uchar               sks_valid:1;
	uchar               sks_bytes[2];
	uchar               notused[2];
	uchar               ex_sense_code;
	uchar               info2[4];
} ASC_REQ_SENSE;

#else
typedef struct asc_req_sense {
	uchar               info_valid:1;
	uchar               err_code:7;
	uchar               segment_no;
	uchar               file_mark:1;
	uchar               sense_EOM:1;
	uchar               sense_ILI:1;
	uchar               reserved_bit:1;
	uchar               sense_key:4;
	uchar               info1[4];
	uchar               add_sense_len;
	uchar               cmd_sp_info[4];
	uchar               asc;
	uchar               ascq;
	uchar               fruc;
	uchar               sks_valid:1;
	uchar               sks_byte0:7;
	uchar               sks_bytes[2];
	uchar               notused[2];
	uchar               ex_sense_code;
	uchar               info2[4];
} ASC_REQ_SENSE;

#endif
#define ASC_SG_LIST_PER_Q   7
#define QS_FREE        0x00
#define QS_READY       0x01
#define QS_DISC1       0x02
#define QS_DISC2       0x04
#define QS_BUSY        0x08
#define QS_ABORTED     0x40
#define QS_DONE        0x80
#define QC_NO_CALLBACK   0x01
#define QC_SG_SWAP_QUEUE 0x02
#define QC_SG_HEAD       0x04
#define QC_DATA_IN       0x08
#define QC_DATA_OUT      0x10
#define QC_URGENT        0x20
#define QC_MSG_OUT       0x40
#define QC_REQ_SENSE     0x80
#define QCSG_SG_XFER_LIST  0x02
#define QCSG_SG_XFER_MORE  0x04
#define QCSG_SG_XFER_END   0x08
#define QD_IN_PROGRESS       0x00
#define QD_NO_ERROR          0x01
#define QD_ABORTED_BY_HOST   0x02
#define QD_WITH_ERROR        0x04
#define QD_INVALID_REQUEST   0x80
#define QD_INVALID_HOST_NUM  0x81
#define QD_INVALID_DEVICE    0x82
#define QD_ERR_INTERNAL      0xFF
#define QHSTA_NO_ERROR               0x00
#define QHSTA_M_SEL_TIMEOUT          0x11
#define QHSTA_M_DATA_OVER_RUN        0x12
#define QHSTA_M_DATA_UNDER_RUN       0x12
#define QHSTA_M_UNEXPECTED_BUS_FREE  0x13
#define QHSTA_M_BAD_BUS_PHASE_SEQ    0x14
#define QHSTA_D_QDONE_SG_LIST_CORRUPTED 0x21
#define QHSTA_D_ASC_DVC_ERROR_CODE_SET  0x22
#define QHSTA_D_HOST_ABORT_FAILED       0x23
#define QHSTA_D_EXE_SCSI_Q_FAILED       0x24
#define QHSTA_D_EXE_SCSI_Q_BUSY_TIMEOUT 0x25
#define QHSTA_D_ASPI_NO_BUF_POOL        0x26
#define QHSTA_M_WTM_TIMEOUT         0x41
#define QHSTA_M_BAD_CMPL_STATUS_IN  0x42
#define QHSTA_M_NO_AUTO_REQ_SENSE   0x43
#define QHSTA_M_AUTO_REQ_SENSE_FAIL 0x44
#define QHSTA_M_TARGET_STATUS_BUSY  0x45
#define QHSTA_M_BAD_TAG_CODE        0x46
#define QHSTA_M_BAD_QUEUE_FULL_OR_BUSY  0x47
#define QHSTA_M_HUNG_REQ_SCSI_BUS_RESET 0x48
#define QHSTA_D_LRAM_CMP_ERROR        0x81
#define QHSTA_M_MICRO_CODE_ERROR_HALT 0xA1
#define ASC_FLAG_SCSIQ_REQ        0x01
#define ASC_FLAG_BIOS_SCSIQ_REQ   0x02
#define ASC_FLAG_BIOS_ASYNC_IO    0x04
#define ASC_FLAG_SRB_LINEAR_ADDR  0x08
#define ASC_FLAG_WIN16            0x10
#define ASC_FLAG_WIN32            0x20
#define ASC_FLAG_ISA_OVER_16MB    0x40
#define ASC_FLAG_DOS_VM_CALLBACK  0x80
#define ASC_TAG_FLAG_EXTRA_BYTES               0x10
#define ASC_TAG_FLAG_DISABLE_DISCONNECT        0x04
#define ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX  0x08
#define ASC_TAG_FLAG_DISABLE_CHK_COND_INT_HOST 0x40
#define ASC_SCSIQ_CPY_BEG              4
#define ASC_SCSIQ_SGHD_CPY_BEG         2
#define ASC_SCSIQ_B_FWD                0
#define ASC_SCSIQ_B_BWD                1
#define ASC_SCSIQ_B_STATUS             2
#define ASC_SCSIQ_B_QNO                3
#define ASC_SCSIQ_B_CNTL               4
#define ASC_SCSIQ_B_SG_QUEUE_CNT       5
#define ASC_SCSIQ_D_DATA_ADDR          8
#define ASC_SCSIQ_D_DATA_CNT          12
#define ASC_SCSIQ_B_SENSE_LEN         20
#define ASC_SCSIQ_DONE_INFO_BEG       22
#define ASC_SCSIQ_D_SRBPTR            22
#define ASC_SCSIQ_B_TARGET_IX         26
#define ASC_SCSIQ_B_CDB_LEN           28
#define ASC_SCSIQ_B_TAG_CODE          29
#define ASC_SCSIQ_W_VM_ID             30
#define ASC_SCSIQ_DONE_STATUS         32
#define ASC_SCSIQ_HOST_STATUS         33
#define ASC_SCSIQ_SCSI_STATUS         34
#define ASC_SCSIQ_CDB_BEG             36
#define ASC_SCSIQ_DW_REMAIN_XFER_ADDR 56
#define ASC_SCSIQ_DW_REMAIN_XFER_CNT  60
#define ASC_SCSIQ_B_SG_WK_QP          49
#define ASC_SCSIQ_B_SG_WK_IX          50
#define ASC_SCSIQ_W_REQ_COUNT         52
#define ASC_SCSIQ_B_LIST_CNT          6
#define ASC_SCSIQ_B_CUR_LIST_CNT      7
#define ASC_SGQ_B_SG_CNTL             4
#define ASC_SGQ_B_SG_HEAD_QP          5
#define ASC_SGQ_B_SG_LIST_CNT         6
#define ASC_SGQ_B_SG_CUR_LIST_CNT     7
#define ASC_SGQ_LIST_BEG              8
#define ASC_DEF_SCSI1_QNG    4
#define ASC_MAX_SCSI1_QNG    4
#define ASC_DEF_SCSI2_QNG    16
#define ASC_MAX_SCSI2_QNG    32
#define ASC_TAG_CODE_MASK    0x23
#define ASC_STOP_REQ_RISC_STOP      0x01
#define ASC_STOP_ACK_RISC_STOP      0x03
#define ASC_STOP_CLEAN_UP_BUSY_Q    0x10
#define ASC_STOP_CLEAN_UP_DISC_Q    0x20
#define ASC_STOP_HOST_REQ_RISC_HALT 0x40
#define ASC_TIDLUN_TO_IX( tid, lun )  ( ASC_SCSI_TIX_TYPE )( (tid) + ((lun)<<ASC_SCSI_ID_BITS) )
#define ASC_TID_TO_TARGET_ID( tid )   ( ASC_SCSI_BIT_ID_TYPE )( 0x01 << (tid) )
#define ASC_TIX_TO_TARGET_ID( tix )   ( 0x01 << ( (tix) & ASC_MAX_TID ) )
#define ASC_TIX_TO_TID( tix )         ( (tix) & ASC_MAX_TID )
#define ASC_TID_TO_TIX( tid )         ( (tid) & ASC_MAX_TID )
#define ASC_TIX_TO_LUN( tix )         ( ( (tix) >> ASC_SCSI_ID_BITS ) & ASC_MAX_LUN )
#define ASC_QNO_TO_QADDR( q_no )      ( (ASC_QADR_BEG)+( ( int )(q_no) << 6 ) )

typedef struct asc_scisq_1 {
	uchar               status;
	uchar               q_no;
	uchar               cntl;
	uchar               sg_queue_cnt;
	uchar               target_id;
	uchar               target_lun;
	ulong               data_addr;
	ulong               data_cnt;
	ulong               sense_addr;
	uchar               sense_len;
	uchar               extra_bytes;
} ASC_SCSIQ_1;

typedef struct asc_scisq_2 {
	ulong               srb_ptr;
	uchar               target_ix;
	uchar               flag;
	uchar               cdb_len;
	uchar               tag_code;
	ushort              vm_id;
} ASC_SCSIQ_2;

typedef struct asc_scsiq_3 {
	uchar               done_stat;
	uchar               host_stat;
	uchar               scsi_stat;
	uchar               scsi_msg;
} ASC_SCSIQ_3;

typedef struct asc_scsiq_4 {
	uchar               cdb[ASC_MAX_CDB_LEN];
	uchar               y_first_sg_list_qp;
	uchar               y_working_sg_qp;
	uchar               y_working_sg_ix;
	uchar               y_res;
	ushort              x_req_count;
	ushort              x_reconnect_rtn;
	ulong               x_saved_data_addr;
	ulong               x_saved_data_cnt;
} ASC_SCSIQ_4;

typedef struct asc_q_done_info {
	ASC_SCSIQ_2         d2;
	ASC_SCSIQ_3         d3;
	uchar               q_status;
	uchar               q_no;
	uchar               cntl;
	uchar               sense_len;
	uchar               extra_bytes;
	uchar               res;
	ulong               remain_bytes;
} ASC_QDONE_INFO;

typedef struct asc_sg_list {
	ulong               addr;
	ulong               bytes;
} ASC_SG_LIST;

typedef struct asc_sg_head {
	ushort              entry_cnt;
	ushort              queue_cnt;
	ushort              entry_to_copy;
	ushort              res;
	ASC_SG_LIST         sg_list[ASC_MAX_SG_LIST];
} ASC_SG_HEAD;

#define ASC_MIN_SG_LIST   2

typedef struct asc_min_sg_head {
	ushort              entry_cnt;
	ushort              queue_cnt;
	ushort              entry_to_copy;
	ushort              res;
	ASC_SG_LIST         sg_list[ASC_MIN_SG_LIST];
} ASC_MIN_SG_HEAD;

#define QCX_SORT        (0x0001)
#define QCX_COALEASE    (0x0002)

#if CC_LINK_BUSY_Q
typedef struct asc_ext_scsi_q {
	ulong               lba;
	ushort              lba_len;
	struct asc_scsi_q dosfar *next;
	struct asc_scsi_q dosfar *join;
	ushort              cntl;
	ushort              buffer_id;
	uchar               q_required;
	uchar               res;
} ASC_EXT_SCSI_Q;
#endif

typedef struct asc_scsi_q {
	ASC_SCSIQ_1         q1;
	ASC_SCSIQ_2         q2;
	uchar dosfar       *cdbptr;
	ASC_SG_HEAD dosfar *sg_head;
#if CC_LINK_BUSY_Q
	ASC_EXT_SCSI_Q      ext;
#endif
#if CC_ASC_SCSI_Q_USRDEF
	ASC_SCSI_Q_USR      usr;
#endif
} ASC_SCSI_Q;

typedef struct asc_scsi_req_q {
	ASC_SCSIQ_1         r1;
	ASC_SCSIQ_2         r2;
	uchar dosfar       *cdbptr;
	ASC_SG_HEAD dosfar *sg_head;
#if CC_LINK_BUSY_Q
	ASC_EXT_SCSI_Q      ext;
#endif
	uchar dosfar       *sense_ptr;
	ASC_SCSIQ_3         r3;
	uchar               cdb[ASC_MAX_CDB_LEN];
	uchar               sense[ASC_MIN_SENSE_LEN];
#if CC_ASC_SCSI_REQ_Q_USRDEF
	ASC_SCSI_REQ_Q_USR  usr;
#endif
} ASC_SCSI_REQ_Q;

typedef struct asc_scsi_bios_req_q {
	ASC_SCSIQ_1         r1;
	ASC_SCSIQ_2         r2;
	uchar dosfar       *cdbptr;
	ASC_SG_HEAD dosfar *sg_head;
	uchar dosfar       *sense_ptr;
	ASC_SCSIQ_3         r3;
	uchar               cdb[ASC_MAX_CDB_LEN];
	uchar               sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_BIOS_REQ_Q;

typedef struct asc_risc_q {
	uchar               fwd;
	uchar               bwd;
	ASC_SCSIQ_1         i1;
	ASC_SCSIQ_2         i2;
	ASC_SCSIQ_3         i3;
	ASC_SCSIQ_4         i4;
} ASC_RISC_Q;

typedef struct asc_sg_list_q {
	uchar               seq_no;
	uchar               q_no;
	uchar               cntl;
	uchar               sg_head_qp;
	uchar               sg_list_cnt;
	uchar               sg_cur_list_cnt;
} ASC_SG_LIST_Q;

typedef struct asc_risc_sg_list_q {
	uchar               fwd;
	uchar               bwd;
	ASC_SG_LIST_Q       sg;
	ASC_SG_LIST         sg_list[7];
} ASC_RISC_SG_LIST_Q;

#define ASC_EXE_SCSI_IO_MAX_IDLE_LOOP  0x1000000UL
#define ASC_EXE_SCSI_IO_MAX_WAIT_LOOP  1024
#define ASCQ_ERR_NO_ERROR             0
#define ASCQ_ERR_IO_NOT_FOUND         1
#define ASCQ_ERR_LOCAL_MEM            2
#define ASCQ_ERR_CHKSUM               3
#define ASCQ_ERR_START_CHIP           4
#define ASCQ_ERR_INT_TARGET_ID        5
#define ASCQ_ERR_INT_LOCAL_MEM        6
#define ASCQ_ERR_HALT_RISC            7
#define ASCQ_ERR_GET_ASPI_ENTRY       8
#define ASCQ_ERR_CLOSE_ASPI           9
#define ASCQ_ERR_HOST_INQUIRY         0x0A
#define ASCQ_ERR_SAVED_SRB_BAD        0x0B
#define ASCQ_ERR_QCNTL_SG_LIST        0x0C
#define ASCQ_ERR_Q_STATUS             0x0D
#define ASCQ_ERR_WR_SCSIQ             0x0E
#define ASCQ_ERR_PC_ADDR              0x0F
#define ASCQ_ERR_SYN_OFFSET           0x10
#define ASCQ_ERR_SYN_XFER_TIME        0x11
#define ASCQ_ERR_LOCK_DMA             0x12
#define ASCQ_ERR_UNLOCK_DMA           0x13
#define ASCQ_ERR_VDS_CHK_INSTALL      0x14
#define ASCQ_ERR_MICRO_CODE_HALT      0x15
#define ASCQ_ERR_SET_LRAM_ADDR        0x16
#define ASCQ_ERR_CUR_QNG              0x17
#define ASCQ_ERR_SG_Q_LINKS           0x18
#define ASCQ_ERR_SCSIQ_PTR            0x19
#define ASCQ_ERR_ISR_RE_ENTRY         0x1A
#define ASCQ_ERR_CRITICAL_RE_ENTRY    0x1B
#define ASCQ_ERR_ISR_ON_CRITICAL      0x1C
#define ASCQ_ERR_SG_LIST_ODD_ADDRESS  0x1D
#define ASCQ_ERR_XFER_ADDRESS_TOO_BIG 0x1E
#define ASCQ_ERR_SCSIQ_NULL_PTR       0x1F
#define ASCQ_ERR_SCSIQ_BAD_NEXT_PTR   0x20
#define ASCQ_ERR_GET_NUM_OF_FREE_Q    0x21
#define ASCQ_ERR_SEND_SCSI_Q          0x22
#define ASCQ_ERR_HOST_REQ_RISC_HALT   0x23
#define ASCQ_ERR_RESET_SDTR           0x24
#define ASC_WARN_NO_ERROR             0x0000
#define ASC_WARN_IO_PORT_ROTATE       0x0001
#define ASC_WARN_EEPROM_CHKSUM        0x0002
#define ASC_WARN_IRQ_MODIFIED         0x0004
#define ASC_WARN_AUTO_CONFIG          0x0008
#define ASC_WARN_CMD_QNG_CONFLICT     0x0010
#define ASC_WARN_EEPROM_RECOVER       0x0020
#define ASC_WARN_CFG_MSW_RECOVER      0x0040
#define ASC_WARN_SET_PCI_CONFIG_SPACE 0x0080
#define ASC_IERR_WRITE_EEPROM         0x0001
#define ASC_IERR_MCODE_CHKSUM         0x0002
#define ASC_IERR_SET_PC_ADDR          0x0004
#define ASC_IERR_START_STOP_CHIP      0x0008
#define ASC_IERR_IRQ_NO               0x0010
#define ASC_IERR_SET_IRQ_NO           0x0020
#define ASC_IERR_CHIP_VERSION         0x0040
#define ASC_IERR_SET_SCSI_ID          0x0080
#define ASC_IERR_GET_PHY_ADDR         0x0100
#define ASC_IERR_BAD_SIGNATURE        0x0200
#define ASC_IERR_NO_BUS_TYPE          0x0400
#define ASC_IERR_SCAM                 0x0800
#define ASC_IERR_SET_SDTR             0x1000
#define ASC_IERR_RW_LRAM              0x8000
#define ASC_DEF_IRQ_NO  10
#define ASC_MAX_IRQ_NO  15
#define ASC_MIN_IRQ_NO  10
#define ASC_MIN_REMAIN_Q        (0x02)
#define ASC_DEF_MAX_TOTAL_QNG   (0xF0)
#define ASC_MIN_TAG_Q_PER_DVC   (0x04)
#define ASC_DEF_TAG_Q_PER_DVC   (0x04)
#define ASC_MIN_FREE_Q        ASC_MIN_REMAIN_Q
#define ASC_MIN_TOTAL_QNG     (( ASC_MAX_SG_QUEUE )+( ASC_MIN_FREE_Q ))
#define ASC_MAX_TOTAL_QNG 240
#define ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG 16
#define ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG   8
#define ASC_MAX_PCI_INRAM_TOTAL_QNG  20
#define ASC_MAX_INRAM_TAG_QNG   16
#define ASC_IOADR_TABLE_MAX_IX  11
#define ASC_IOADR_GAP   0x10
#define ASC_SEARCH_IOP_GAP 0x10
#define ASC_MIN_IOP_ADDR   ( PortAddr )0x0100
#define ASC_MAX_IOP_ADDR   ( PortAddr )0x3F0
#define ASC_IOADR_1     ( PortAddr )0x0110
#define ASC_IOADR_2     ( PortAddr )0x0130
#define ASC_IOADR_3     ( PortAddr )0x0150
#define ASC_IOADR_4     ( PortAddr )0x0190
#define ASC_IOADR_5     ( PortAddr )0x0210
#define ASC_IOADR_6     ( PortAddr )0x0230
#define ASC_IOADR_7     ( PortAddr )0x0250
#define ASC_IOADR_8     ( PortAddr )0x0330
#define ASC_IOADR_DEF   ASC_IOADR_8
#define ASC_LIB_SCSIQ_WK_SP        256
#define ASC_MAX_SYN_XFER_NO        16
#define ASC_SYN_XFER_NO            8
#define ASC_SYN_MAX_OFFSET         0x0F
#define ASC_DEF_SDTR_OFFSET        0x0F
#define ASC_DEF_SDTR_INDEX         0x00
#define SYN_XFER_NS_0  25
#define SYN_XFER_NS_1  30
#define SYN_XFER_NS_2  35
#define SYN_XFER_NS_3  40
#define SYN_XFER_NS_4  50
#define SYN_XFER_NS_5  60
#define SYN_XFER_NS_6  70
#define SYN_XFER_NS_7  85
#define SYN_ULTRA_XFER_NS_0    12
#define SYN_ULTRA_XFER_NS_1    19
#define SYN_ULTRA_XFER_NS_2    25
#define SYN_ULTRA_XFER_NS_3    32
#define SYN_ULTRA_XFER_NS_4    38
#define SYN_ULTRA_XFER_NS_5    44
#define SYN_ULTRA_XFER_NS_6    50
#define SYN_ULTRA_XFER_NS_7    57
#define SYN_ULTRA_XFER_NS_8    63
#define SYN_ULTRA_XFER_NS_9    69
#define SYN_ULTRA_XFER_NS_10   75
#define SYN_ULTRA_XFER_NS_11   82
#define SYN_ULTRA_XFER_NS_12   88
#define SYN_ULTRA_XFER_NS_13   94
#define SYN_ULTRA_XFER_NS_14  100
#define SYN_ULTRA_XFER_NS_15  107
#define SYN_XMSG_WLEN  3

typedef struct sdtr_xmsg {
	uchar               msg_type;
	uchar               msg_len;
	uchar               msg_req;
	uchar               xfer_period;
	uchar               req_ack_offset;
	uchar               res;
} SDTR_XMSG;

typedef struct asc_dvc_cfg {
	ASC_SCSI_BIT_ID_TYPE can_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE cmd_qng_enabled;
	ASC_SCSI_BIT_ID_TYPE disc_enable;
	uchar               res;
	uchar               chip_scsi_id:4;
	uchar               isa_dma_speed:4;
	uchar               isa_dma_channel;
	uchar               chip_version;
	ushort              pci_device_id;
	ushort              lib_serial_no;
	ushort              lib_version;
	ushort              mcode_date;
	ushort              mcode_version;
	uchar               max_tag_qng[ASC_MAX_TID + 1];
	uchar dosfar       *overrun_buf;
	uchar               sdtr_period_offset[ASC_MAX_TID + 1];
	ushort              pci_slot_info;
} ASC_DVC_CFG;

#define ASC_DEF_DVC_CNTL       0xFFFF
#define ASC_DEF_CHIP_SCSI_ID   7
#define ASC_DEF_ISA_DMA_SPEED  4
#define ASC_INIT_STATE_NULL          0x0000
#define ASC_INIT_STATE_BEG_GET_CFG   0x0001
#define ASC_INIT_STATE_END_GET_CFG   0x0002
#define ASC_INIT_STATE_BEG_SET_CFG   0x0004
#define ASC_INIT_STATE_END_SET_CFG   0x0008
#define ASC_INIT_STATE_BEG_LOAD_MC   0x0010
#define ASC_INIT_STATE_END_LOAD_MC   0x0020
#define ASC_INIT_STATE_BEG_INQUIRY   0x0040
#define ASC_INIT_STATE_END_INQUIRY   0x0080
#define ASC_INIT_RESET_SCSI_DONE     0x0100
#define ASC_INIT_STATE_WITHOUT_EEP   0x8000
#define ASC_PCI_DEVICE_ID_REV_A      0x1100
#define ASC_PCI_DEVICE_ID_REV_B      0x1200
#define ASC_BUG_FIX_IF_NOT_DWB       0x0001
#define ASC_BUG_FIX_ASYN_USE_SYN     0x0002
#define ASYN_SDTR_DATA_FIX_PCI_REV_AB 0x41
#define ASC_MIN_TAGGED_CMD  7
#define ASC_MAX_SCSI_RESET_WAIT      30

typedef struct asc_dvc_var {
	PortAddr            iop_base;
	ushort              err_code;
	ushort              dvc_cntl;
	ushort              bug_fix_cntl;
	ushort              bus_type;
	Ptr2Func            isr_callback;
	Ptr2Func            exe_callback;
	ASC_SCSI_BIT_ID_TYPE init_sdtr;
	ASC_SCSI_BIT_ID_TYPE sdtr_done;
	ASC_SCSI_BIT_ID_TYPE use_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE unit_not_ready;
	ASC_SCSI_BIT_ID_TYPE queue_full_or_busy;
	ASC_SCSI_BIT_ID_TYPE start_motor;
	uchar               scsi_reset_wait;
	uchar               chip_no;
	char                is_in_int;
	uchar               max_total_qng;
	uchar               cur_total_qng;
	uchar               in_critical_cnt;
	uchar               irq_no;
	uchar               last_q_shortage;
	ushort              init_state;
	uchar               cur_dvc_qng[ASC_MAX_TID + 1];
	uchar               max_dvc_qng[ASC_MAX_TID + 1];
	ASC_SCSI_Q dosfar  *scsiq_busy_head[ASC_MAX_TID + 1];
	ASC_SCSI_Q dosfar  *scsiq_busy_tail[ASC_MAX_TID + 1];
	uchar               sdtr_period_tbl[ASC_MAX_SYN_XFER_NO];
	ASC_DVC_CFG dosfar *cfg;
	Ptr2Func            saved_ptr2func;
	ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer_always;
	char                redo_scam;
	ushort              res2;
	uchar               dos_int13_table[ASC_MAX_TID + 1];
	ulong               max_dma_count;
	ASC_SCSI_BIT_ID_TYPE no_scam;
	ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer;
	uchar               max_sdtr_index;
	uchar               res4;
	ulong               drv_ptr;
	ulong               res6;
	ulong               res7;
	ulong               res8;
} ASC_DVC_VAR;

typedef int         (dosfar * ASC_ISR_CALLBACK) (ASC_DVC_VAR asc_ptr_type *, ASC_QDONE_INFO dosfar *);
typedef int         (dosfar * ASC_EXE_CALLBACK) (ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_Q dosfar *);

typedef struct asc_dvc_inq_info {
	uchar               type[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_DVC_INQ_INFO;

typedef struct asc_cap_info {
	ulong               lba;
	ulong               blk_size;
} ASC_CAP_INFO;

typedef struct asc_cap_info_array {
	ASC_CAP_INFO        cap_info[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_CAP_INFO_ARRAY;

#define ASC_MCNTL_NO_SEL_TIMEOUT  ( ushort )0x0001
#define ASC_MCNTL_NULL_TARGET     ( ushort )0x0002
#define ASC_CNTL_INITIATOR         ( ushort )0x0001
#define ASC_CNTL_BIOS_GT_1GB       ( ushort )0x0002
#define ASC_CNTL_BIOS_GT_2_DISK    ( ushort )0x0004
#define ASC_CNTL_BIOS_REMOVABLE    ( ushort )0x0008
#define ASC_CNTL_NO_SCAM           ( ushort )0x0010
#define ASC_CNTL_INT_MULTI_Q       ( ushort )0x0080
#define ASC_CNTL_NO_LUN_SUPPORT    ( ushort )0x0040
#define ASC_CNTL_NO_VERIFY_COPY    ( ushort )0x0100
#define ASC_CNTL_RESET_SCSI        ( ushort )0x0200
#define ASC_CNTL_INIT_INQUIRY      ( ushort )0x0400
#define ASC_CNTL_INIT_VERBOSE      ( ushort )0x0800
#define ASC_CNTL_SCSI_PARITY       ( ushort )0x1000
#define ASC_CNTL_BURST_MODE        ( ushort )0x2000
#define ASC_CNTL_USE_8_IOP_BASE    ( ushort )0x4000
#define ASC_EEP_DVC_CFG_BEG_VL    2
#define ASC_EEP_MAX_DVC_ADDR_VL   15
#define ASC_EEP_DVC_CFG_BEG      32
#define ASC_EEP_MAX_DVC_ADDR     45
#define ASC_EEP_DEFINED_WORDS    10
#define ASC_EEP_MAX_ADDR         63
#define ASC_EEP_RES_WORDS         0
#define ASC_EEP_MAX_RETRY        20
#define ASC_MAX_INIT_BUSY_RETRY   8
#define ASC_EEP_ISA_PNP_WSIZE    16

typedef struct asceep_config {
	ushort              cfg_lsw;
	ushort              cfg_msw;
	uchar               init_sdtr;
	uchar               disc_enable;
	uchar               use_cmd_qng;
	uchar               start_motor;
	uchar               max_total_qng;
	uchar               max_tag_qng;
	uchar               bios_scan;
	uchar               power_up_wait;
	uchar               no_scam;
	uchar               chip_scsi_id:4;
	uchar               isa_dma_speed:4;
	uchar               dos_int13_table[ASC_MAX_TID + 1];
	uchar               adapter_info[6];
	ushort              cntl;
	ushort              chksum;
} ASCEEP_CONFIG;

#define ASC_EEP_CMD_READ          0x80
#define ASC_EEP_CMD_WRITE         0x40
#define ASC_EEP_CMD_WRITE_ABLE    0x30
#define ASC_EEP_CMD_WRITE_DISABLE 0x00
#define ASC_OVERRUN_BSIZE  0x00000048UL
#define ASCV_MSGOUT_BEG         0x0000
#define ASCV_MSGOUT_SDTR_PERIOD (ASCV_MSGOUT_BEG+3)
#define ASCV_MSGOUT_SDTR_OFFSET (ASCV_MSGOUT_BEG+4)
#define ASCV_MSGIN_BEG          (ASCV_MSGOUT_BEG+8)
#define ASCV_MSGIN_SDTR_PERIOD  (ASCV_MSGIN_BEG+3)
#define ASCV_MSGIN_SDTR_OFFSET  (ASCV_MSGIN_BEG+4)
#define ASCV_SDTR_DATA_BEG      (ASCV_MSGIN_BEG+8)
#define ASCV_SDTR_DONE_BEG      (ASCV_SDTR_DATA_BEG+8)
#define ASCV_MAX_DVC_QNG_BEG    ( ushort )0x0020
#define ASCV_ASCDVC_ERR_CODE_W  ( ushort )0x0030
#define ASCV_MCODE_CHKSUM_W   ( ushort )0x0032
#define ASCV_MCODE_SIZE_W     ( ushort )0x0034
#define ASCV_STOP_CODE_B      ( ushort )0x0036
#define ASCV_DVC_ERR_CODE_B   ( ushort )0x0037
#define ASCV_OVERRUN_PADDR_D  ( ushort )0x0038
#define ASCV_OVERRUN_BSIZE_D  ( ushort )0x003C
#define ASCV_HALTCODE_W       ( ushort )0x0040
#define ASCV_CHKSUM_W         ( ushort )0x0042
#define ASCV_MC_DATE_W        ( ushort )0x0044
#define ASCV_MC_VER_W         ( ushort )0x0046
#define ASCV_NEXTRDY_B        ( ushort )0x0048
#define ASCV_DONENEXT_B       ( ushort )0x0049
#define ASCV_USE_TAGGED_QNG_B ( ushort )0x004A
#define ASCV_SCSIBUSY_B       ( ushort )0x004B
#define ASCV_Q_DONE_IN_PROGRESS_B  ( ushort )0x004C
#define ASCV_CURCDB_B         ( ushort )0x004D
#define ASCV_RCLUN_B          ( ushort )0x004E
#define ASCV_BUSY_QHEAD_B     ( ushort )0x004F
#define ASCV_DISC1_QHEAD_B    ( ushort )0x0050
#define ASCV_DISC_ENABLE_B    ( ushort )0x0052
#define ASCV_CAN_TAGGED_QNG_B ( ushort )0x0053
#define ASCV_HOSTSCSI_ID_B    ( ushort )0x0055
#define ASCV_MCODE_CNTL_B     ( ushort )0x0056
#define ASCV_NULL_TARGET_B    ( ushort )0x0057
#define ASCV_FREE_Q_HEAD_W    ( ushort )0x0058
#define ASCV_DONE_Q_TAIL_W    ( ushort )0x005A
#define ASCV_FREE_Q_HEAD_B    ( ushort )(ASCV_FREE_Q_HEAD_W+1)
#define ASCV_DONE_Q_TAIL_B    ( ushort )(ASCV_DONE_Q_TAIL_W+1)
#define ASCV_HOST_FLAG_B      ( ushort )0x005D
#define ASCV_TOTAL_READY_Q_B  ( ushort )0x0064
#define ASCV_VER_SERIAL_B     ( ushort )0x0065
#define ASCV_HALTCODE_SAVED_W ( ushort )0x0066
#define ASCV_WTM_FLAG_B       ( ushort )0x0068
#define ASCV_RISC_FLAG_B      ( ushort )0x006A
#define ASCV_REQ_SG_LIST_QP   ( ushort )0x006B
#define ASC_HOST_FLAG_IN_ISR        0x01
#define ASC_HOST_FLAG_ACK_INT       0x02
#define ASC_RISC_FLAG_GEN_INT      0x01
#define ASC_RISC_FLAG_REQ_SG_LIST  0x02
#define IOP_CTRL         (0x0F)
#define IOP_STATUS       (0x0E)
#define IOP_INT_ACK      IOP_STATUS
#define IOP_REG_IFC      (0x0D)
#define IOP_SYN_OFFSET    (0x0B)
#define IOP_EXTRA_CONTROL (0x0D)
#define IOP_REG_PC        (0x0C)
#define IOP_RAM_ADDR      (0x0A)
#define IOP_RAM_DATA      (0x08)
#define IOP_EEP_DATA      (0x06)
#define IOP_EEP_CMD       (0x07)
#define IOP_VERSION       (0x03)
#define IOP_CONFIG_HIGH   (0x04)
#define IOP_CONFIG_LOW    (0x02)
#define IOP_SIG_BYTE      (0x01)
#define IOP_SIG_WORD      (0x00)
#define IOP_REG_DC1      (0x0E)
#define IOP_REG_DC0      (0x0C)
#define IOP_REG_SB       (0x0B)
#define IOP_REG_DA1      (0x0A)
#define IOP_REG_DA0      (0x08)
#define IOP_REG_SC       (0x09)
#define IOP_DMA_SPEED    (0x07)
#define IOP_REG_FLAG     (0x07)
#define IOP_FIFO_H       (0x06)
#define IOP_FIFO_L       (0x04)
#define IOP_REG_ID       (0x05)
#define IOP_REG_QP       (0x03)
#define IOP_REG_IH       (0x02)
#define IOP_REG_IX       (0x01)
#define IOP_REG_AX       (0x00)
#define IFC_REG_LOCK      (0x00)
#define IFC_REG_UNLOCK    (0x09)
#define IFC_WR_EN_FILTER  (0x10)
#define IFC_RD_NO_EEPROM  (0x10)
#define IFC_SLEW_RATE     (0x20)
#define IFC_ACT_NEG       (0x40)
#define IFC_INP_FILTER    (0x80)
#define IFC_INIT_DEFAULT  ( IFC_ACT_NEG | IFC_REG_UNLOCK )
#define SC_SEL   ( uchar )(0x80)
#define SC_BSY   ( uchar )(0x40)
#define SC_ACK   ( uchar )(0x20)
#define SC_REQ   ( uchar )(0x10)
#define SC_ATN   ( uchar )(0x08)
#define SC_IO    ( uchar )(0x04)
#define SC_CD    ( uchar )(0x02)
#define SC_MSG   ( uchar )(0x01)
#define SEC_ACTIVE_NEGATE    ( uchar )( 0x40 )
#define SEC_SLEW_RATE        ( uchar )( 0x20 )
#define ASC_HALT_EXTMSG_IN     ( ushort )0x8000
#define ASC_HALT_CHK_CONDITION ( ushort )0x8100
#define ASC_HALT_SS_QUEUE_FULL ( ushort )0x8200
#define ASC_HALT_DISABLE_ASYN_USE_SYN_FIX  ( ushort )0x8300
#define ASC_HALT_ENABLE_ASYN_USE_SYN_FIX   ( ushort )0x8400
#define ASC_HALT_SDTR_REJECTED ( ushort )0x4000
#define ASC_MAX_QNO        0xF8
#define ASC_DATA_SEC_BEG   ( ushort )0x0080
#define ASC_DATA_SEC_END   ( ushort )0x0080
#define ASC_CODE_SEC_BEG   ( ushort )0x0080
#define ASC_CODE_SEC_END   ( ushort )0x0080
#define ASC_QADR_BEG       (0x4000)
#define ASC_QADR_USED      ( ushort )( ASC_MAX_QNO * 64 )
#define ASC_QADR_END       ( ushort )0x7FFF
#define ASC_QLAST_ADR      ( ushort )0x7FC0
#define ASC_QBLK_SIZE      0x40
#define ASC_BIOS_DATA_QBEG 0xF8
#define ASC_MIN_ACTIVE_QNO 0x01
#define ASC_QLINK_END      0xFF
#define ASC_EEPROM_WORDS   0x10
#define ASC_MAX_MGS_LEN    0x10
#define ASC_BIOS_ADDR_DEF  0xDC00
#define ASC_BIOS_SIZE      0x3800
#define ASC_BIOS_RAM_OFF   0x3800
#define ASC_BIOS_RAM_SIZE  0x800
#define ASC_BIOS_MIN_ADDR  0xC000
#define ASC_BIOS_MAX_ADDR  0xEC00
#define ASC_BIOS_BANK_SIZE 0x0400
#define ASC_MCODE_START_ADDR  0x0080
#define ASC_CFG0_HOST_INT_ON    0x0020
#define ASC_CFG0_BIOS_ON        0x0040
#define ASC_CFG0_VERA_BURST_ON  0x0080
#define ASC_CFG0_SCSI_PARITY_ON 0x0800
#define ASC_CFG1_SCSI_TARGET_ON 0x0080
#define ASC_CFG1_LRAM_8BITS_ON  0x0800
#define ASC_CFG_MSW_CLR_MASK    0x30C0
#define CSW_TEST1             ( ASC_CS_TYPE )0x8000
#define CSW_AUTO_CONFIG       ( ASC_CS_TYPE )0x4000
#define CSW_RESERVED1         ( ASC_CS_TYPE )0x2000
#define CSW_IRQ_WRITTEN       ( ASC_CS_TYPE )0x1000
#define CSW_33MHZ_SELECTED    ( ASC_CS_TYPE )0x0800
#define CSW_TEST2             ( ASC_CS_TYPE )0x0400
#define CSW_TEST3             ( ASC_CS_TYPE )0x0200
#define CSW_RESERVED2         ( ASC_CS_TYPE )0x0100
#define CSW_DMA_DONE          ( ASC_CS_TYPE )0x0080
#define CSW_FIFO_RDY          ( ASC_CS_TYPE )0x0040
#define CSW_EEP_READ_DONE     ( ASC_CS_TYPE )0x0020
#define CSW_HALTED            ( ASC_CS_TYPE )0x0010
#define CSW_SCSI_RESET_ACTIVE ( ASC_CS_TYPE )0x0008
#define CSW_PARITY_ERR        ( ASC_CS_TYPE )0x0004
#define CSW_SCSI_RESET_LATCH  ( ASC_CS_TYPE )0x0002
#define CSW_INT_PENDING       ( ASC_CS_TYPE )0x0001
#define CIW_CLR_SCSI_RESET_INT ( ASC_CS_TYPE )0x1000
#define CIW_INT_ACK      ( ASC_CS_TYPE )0x0100
#define CIW_TEST1        ( ASC_CS_TYPE )0x0200
#define CIW_TEST2        ( ASC_CS_TYPE )0x0400
#define CIW_SEL_33MHZ    ( ASC_CS_TYPE )0x0800
#define CIW_IRQ_ACT      ( ASC_CS_TYPE )0x1000
#define CC_CHIP_RESET   ( uchar )0x80
#define CC_SCSI_RESET   ( uchar )0x40
#define CC_HALT         ( uchar )0x20
#define CC_SINGLE_STEP  ( uchar )0x10
#define CC_DMA_ABLE     ( uchar )0x08
#define CC_TEST         ( uchar )0x04
#define CC_BANK_ONE     ( uchar )0x02
#define CC_DIAG         ( uchar )0x01
#define ASC_1000_ID0W      0x04C1
#define ASC_1000_ID0W_FIX  0x00C1
#define ASC_1000_ID1B      0x25
#define ASC_EISA_BIG_IOP_GAP   (0x1C30-0x0C50)
#define ASC_EISA_SMALL_IOP_GAP (0x0020)
#define ASC_EISA_MIN_IOP_ADDR  (0x0C30)
#define ASC_EISA_MAX_IOP_ADDR  (0xFC50)
#define ASC_EISA_REV_IOP_MASK  (0x0C83)
#define ASC_EISA_PID_IOP_MASK  (0x0C80)
#define ASC_EISA_CFG_IOP_MASK  (0x0C86)
#define ASC_GET_EISA_SLOT( iop )  ( PortAddr )( (iop) & 0xF000 )
#define ASC_EISA_ID_740    0x01745004UL
#define ASC_EISA_ID_750    0x01755004UL
#define INS_HALTINT        ( ushort )0x6281
#define INS_HALT           ( ushort )0x6280
#define INS_SINT           ( ushort )0x6200
#define INS_RFLAG_WTM      ( ushort )0x7380
#define ASC_MC_SAVE_CODE_WSIZE  0x500
#define ASC_MC_SAVE_DATA_WSIZE  0x40

typedef struct asc_mc_saved {
	ushort              data[ASC_MC_SAVE_DATA_WSIZE];
	ushort              code[ASC_MC_SAVE_CODE_WSIZE];
} ASC_MC_SAVED;

#define AscGetQDoneInProgress( port )         AscReadLramByte( (port), ASCV_Q_DONE_IN_PROGRESS_B )
#define AscPutQDoneInProgress( port, val )    AscWriteLramByte( (port), ASCV_Q_DONE_IN_PROGRESS_B, val )
#define AscGetVarFreeQHead( port )            AscReadLramWord( (port), ASCV_FREE_Q_HEAD_W )
#define AscGetVarDoneQTail( port )            AscReadLramWord( (port), ASCV_DONE_Q_TAIL_W )
#define AscPutVarFreeQHead( port, val )       AscWriteLramWord( (port), ASCV_FREE_Q_HEAD_W, val )
#define AscPutVarDoneQTail( port, val )       AscWriteLramWord( (port), ASCV_DONE_Q_TAIL_W, val )
#define AscGetRiscVarFreeQHead( port )        AscReadLramByte( (port), ASCV_NEXTRDY_B )
#define AscGetRiscVarDoneQTail( port )        AscReadLramByte( (port), ASCV_DONENEXT_B )
#define AscPutRiscVarFreeQHead( port, val )   AscWriteLramByte( (port), ASCV_NEXTRDY_B, val )
#define AscPutRiscVarDoneQTail( port, val )   AscWriteLramByte( (port), ASCV_DONENEXT_B, val )
#define AscPutMCodeSDTRDoneAtID( port, id, data )  AscWriteLramByte( (port), ( ushort )( ( ushort )ASCV_SDTR_DONE_BEG+( ushort )id ), (data) ) ;
#define AscGetMCodeSDTRDoneAtID( port, id )        AscReadLramByte( (port), ( ushort )( ( ushort )ASCV_SDTR_DONE_BEG+( ushort )id ) ) ;
#define AscPutMCodeInitSDTRAtID( port, id, data )  AscWriteLramByte( (port), ( ushort )( ( ushort )ASCV_SDTR_DATA_BEG+( ushort )id ), data ) ;
#define AscGetMCodeInitSDTRAtID( port, id )        AscReadLramByte( (port), ( ushort )( ( ushort )ASCV_SDTR_DATA_BEG+( ushort )id ) ) ;
#define AscSynIndexToPeriod( index )        ( uchar )( asc_dvc->sdtr_period_tbl[ (index) ] )
#define AscGetChipSignatureByte( port )     ( uchar )inp( (port)+IOP_SIG_BYTE )
#define AscGetChipSignatureWord( port )     ( ushort )inpw( (port)+IOP_SIG_WORD )
#define AscGetChipVerNo( port )             ( uchar )inp( (port)+IOP_VERSION )
#define AscGetChipCfgLsw( port )            ( ushort )inpw( (port)+IOP_CONFIG_LOW )
#define AscGetChipCfgMsw( port )            ( ushort )inpw( (port)+IOP_CONFIG_HIGH )
#define AscSetChipCfgLsw( port, data )      outpw( (port)+IOP_CONFIG_LOW, data )
#define AscSetChipCfgMsw( port, data )      outpw( (port)+IOP_CONFIG_HIGH, data )
#define AscGetChipEEPCmd( port )            ( uchar )inp( (port)+IOP_EEP_CMD )
#define AscSetChipEEPCmd( port, data )      outp( (port)+IOP_EEP_CMD, data )
#define AscGetChipEEPData( port )           ( ushort )inpw( (port)+IOP_EEP_DATA )
#define AscSetChipEEPData( port, data )     outpw( (port)+IOP_EEP_DATA, data )
#define AscGetChipLramAddr( port )          ( ushort )inpw( ( PortAddr )((port)+IOP_RAM_ADDR) )
#define AscSetChipLramAddr( port, addr )    outpw( ( PortAddr )( (port)+IOP_RAM_ADDR ), addr )
#define AscGetChipLramData( port )          ( ushort )inpw( (port)+IOP_RAM_DATA )
#define AscSetChipLramData( port, data )    outpw( (port)+IOP_RAM_DATA, data )
#define AscGetChipLramDataNoSwap( port )         ( ushort )inpw_noswap( (port)+IOP_RAM_DATA )
#define AscSetChipLramDataNoSwap( port, data )   outpw_noswap( (port)+IOP_RAM_DATA, data )
#define AscGetChipIFC( port )               ( uchar )inp( (port)+IOP_REG_IFC )
#define AscSetChipIFC( port, data )          outp( (port)+IOP_REG_IFC, data )
#define AscGetChipStatus( port )            ( ASC_CS_TYPE )inpw( (port)+IOP_STATUS )
#define AscSetChipStatus( port, cs_val )    outpw( (port)+IOP_STATUS, cs_val )
#define AscGetChipControl( port )           ( uchar )inp( (port)+IOP_CTRL )
#define AscSetChipControl( port, cc_val )   outp( (port)+IOP_CTRL, cc_val )
#define AscGetChipSyn( port )               ( uchar )inp( (port)+IOP_SYN_OFFSET )
#define AscSetChipSyn( port, data )         outp( (port)+IOP_SYN_OFFSET, data )
#define AscSetPCAddr( port, data )          outpw( (port)+IOP_REG_PC, data )
#define AscGetPCAddr( port )                ( ushort )inpw( (port)+IOP_REG_PC )
#define AscIsIntPending( port )             ( AscGetChipStatus(port) & ( CSW_INT_PENDING | CSW_SCSI_RESET_LATCH ) )
#define AscGetChipScsiID( port )            ( ( AscGetChipCfgLsw(port) >> 8 ) & ASC_MAX_TID )
#define AscGetExtraControl( port )          ( uchar )inp( (port)+IOP_EXTRA_CONTROL )
#define AscSetExtraControl( port, data )    outp( (port)+IOP_EXTRA_CONTROL, data )
#define AscReadChipAX( port )               ( ushort )inpw( (port)+IOP_REG_AX )
#define AscWriteChipAX( port, data )        outpw( (port)+IOP_REG_AX, data )
#define AscReadChipIX( port )               ( uchar )inp( (port)+IOP_REG_IX )
#define AscWriteChipIX( port, data )        outp( (port)+IOP_REG_IX, data )
#define AscReadChipIH( port )               ( ushort )inpw( (port)+IOP_REG_IH )
#define AscWriteChipIH( port, data )        outpw( (port)+IOP_REG_IH, data )
#define AscReadChipQP( port )               ( uchar )inp( (port)+IOP_REG_QP )
#define AscWriteChipQP( port, data )        outp( (port)+IOP_REG_QP, data )
#define AscReadChipFIFO_L( port )           ( ushort )inpw( (port)+IOP_REG_FIFO_L )
#define AscWriteChipFIFO_L( port, data )    outpw( (port)+IOP_REG_FIFO_L, data )
#define AscReadChipFIFO_H( port )           ( ushort )inpw( (port)+IOP_REG_FIFO_H )
#define AscWriteChipFIFO_H( port, data )    outpw( (port)+IOP_REG_FIFO_H, data )
#define AscReadChipDmaSpeed( port )         ( uchar )inp( (port)+IOP_DMA_SPEED )
#define AscWriteChipDmaSpeed( port, data )  outp( (port)+IOP_DMA_SPEED, data )
#define AscReadChipDA0( port )              ( ushort )inpw( (port)+IOP_REG_DA0 )
#define AscWriteChipDA0( port )             outpw( (port)+IOP_REG_DA0, data )
#define AscReadChipDA1( port )              ( ushort )inpw( (port)+IOP_REG_DA1 )
#define AscWriteChipDA1( port )             outpw( (port)+IOP_REG_DA1, data )
#define AscReadChipDC0( port )              ( ushort )inpw( (port)+IOP_REG_DC0 )
#define AscWriteChipDC0( port )             outpw( (port)+IOP_REG_DC0, data )
#define AscReadChipDC1( port )              ( ushort )inpw( (port)+IOP_REG_DC1 )
#define AscWriteChipDC1( port )             outpw( (port)+IOP_REG_DC1, data )
#define AscReadChipDvcID( port )            ( uchar )inp( (port)+IOP_REG_ID )
#define AscWriteChipDvcID( port, data )     outp( (port)+IOP_REG_ID, data )
int                 AscWriteEEPCmdReg(PortAddr iop_base, uchar cmd_reg);
int                 AscWriteEEPDataReg(PortAddr iop_base, ushort data_reg);
void                AscWaitEEPRead(void);
void                AscWaitEEPWrite(void);
ushort              AscReadEEPWord(PortAddr, uchar);
ushort              AscWriteEEPWord(PortAddr, uchar, ushort);
ushort              AscGetEEPConfig(PortAddr, ASCEEP_CONFIG dosfar *, ushort);
int                 AscSetEEPConfigOnce(PortAddr, ASCEEP_CONFIG dosfar *, ushort);
int                 AscSetEEPConfig(PortAddr, ASCEEP_CONFIG dosfar *, ushort);
ushort              AscEEPSum(PortAddr, uchar, uchar);
int                 AscStartChip(PortAddr);
int                 AscStopChip(PortAddr);
void                AscSetChipIH(PortAddr, ushort);
int                 AscIsChipHalted(PortAddr);
void                AscResetScsiBus(PortAddr);
int                 AscResetChip(PortAddr);
void                AscSetChipCfgDword(PortAddr, ulong);
ulong               AscGetChipCfgDword(PortAddr);
void                AscAckInterrupt(PortAddr);
void                AscDisableInterrupt(PortAddr);
void                AscEnableInterrupt(PortAddr);
void                AscSetBank(PortAddr, uchar);
uchar               AscGetBank(PortAddr);
int                 AscResetChipAndScsiBus(PortAddr);
ushort              AscGetIsaDmaChannel(PortAddr);
ushort              AscSetIsaDmaChannel(PortAddr, ushort);
uchar               AscSetIsaDmaSpeed(PortAddr, uchar);
uchar               AscGetIsaDmaSpeed(PortAddr);
uchar               AscReadLramByte(PortAddr, ushort);
ushort              AscReadLramWord(PortAddr, ushort);
ulong               AscReadLramDWord(PortAddr, ushort);
void                AscWriteLramWord(PortAddr, ushort, ushort);
void                AscWriteLramDWord(PortAddr, ushort, ulong);
void                AscWriteLramByte(PortAddr, ushort, uchar);
int                 AscVerWriteLramDWord(PortAddr, ushort, ulong);
int                 AscVerWriteLramWord(PortAddr, ushort, ushort);
int                 AscVerWriteLramByte(PortAddr, ushort, uchar);
ulong               AscMemSumLramWord(PortAddr, ushort, rint);
void                AscMemWordSetLram(PortAddr, ushort, ushort, rint);
void                AscMemWordCopyToLram(PortAddr, ushort, ushort dosfar *, int);
void                AscMemDWordCopyToLram(PortAddr, ushort, ulong dosfar *, int);
void                AscMemWordCopyFromLram(PortAddr, ushort, ushort dosfar *, int);
int                 AscMemWordCmpToLram(PortAddr, ushort, ushort dosfar *, int);
ushort              AscInitAscDvcVar(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitFromEEP(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitWithoutEEP(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitFromAscDvcVar(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitMicroCodeVar(ASC_DVC_VAR asc_ptr_type * asc_dvc);
void dosfar         AscInitPollIsrCallBack(ASC_DVC_VAR asc_ptr_type *,
										   ASC_QDONE_INFO dosfar *);
int                 AscTestExternalLram(ASC_DVC_VAR asc_ptr_type *);
ushort              AscTestLramEndian(PortAddr);
uchar               AscMsgOutSDTR(ASC_DVC_VAR asc_ptr_type *, uchar, uchar);
uchar               AscCalSDTRData(ASC_DVC_VAR asc_ptr_type *, uchar, uchar);
void                AscSetChipSDTR(PortAddr, uchar, uchar);
int                 AscInitChipAllSynReg(ASC_DVC_VAR asc_ptr_type *, uchar);
uchar               AscGetSynPeriodIndex(ASC_DVC_VAR asc_ptr_type *, ruchar);
uchar               AscAllocFreeQueue(PortAddr, uchar);
uchar               AscAllocMultipleFreeQueue(PortAddr, uchar, uchar);
int                 AscRiscHaltedAbortSRB(ASC_DVC_VAR asc_ptr_type *, ulong);
int                 AscRiscHaltedAbortTIX(ASC_DVC_VAR asc_ptr_type *, uchar);
int                 AscRiscHaltedAbortALL(ASC_DVC_VAR asc_ptr_type *);
int                 AscHostReqRiscHalt(PortAddr);
int                 AscStopQueueExe(PortAddr);
int                 AscStartQueueExe(PortAddr);
int                 AscCleanUpDiscQueue(PortAddr);
int                 AscCleanUpBusyQueue(PortAddr);
int                 _AscAbortTidBusyQueue(ASC_DVC_VAR asc_ptr_type *,
										  ASC_QDONE_INFO dosfar *, uchar);
int                 _AscAbortSrbBusyQueue(ASC_DVC_VAR asc_ptr_type *,
										  ASC_QDONE_INFO dosfar *, ulong);
int                 AscWaitTixISRDone(ASC_DVC_VAR asc_ptr_type *, uchar);
int                 AscWaitISRDone(ASC_DVC_VAR asc_ptr_type *);
ulong               AscGetOnePhyAddr(ASC_DVC_VAR asc_ptr_type *, uchar dosfar *, ulong);
int                 AscSendScsiQueue(ASC_DVC_VAR asc_ptr_type * asc_dvc,
									 ASC_SCSI_Q dosfar * scsiq,
									 uchar n_q_required);
int                 AscPutReadyQueue(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_Q dosfar *, uchar);
int                 AscPutReadySgListQueue(ASC_DVC_VAR asc_ptr_type *,
										   ASC_SCSI_Q dosfar *, uchar);
int                 AscAbortScsiIO(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_Q dosfar *);
void                AscExeScsiIO(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_Q dosfar *);
int                 AscSetChipSynRegAtID(PortAddr, uchar, uchar);
int                 AscSetRunChipSynRegAtID(PortAddr, uchar, uchar);
ushort              AscInitLram(ASC_DVC_VAR asc_ptr_type *);
int                 AscReInitLram(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitQLinkVar(ASC_DVC_VAR asc_ptr_type *);
int                 AscSetLibErrorCode(ASC_DVC_VAR asc_ptr_type *, ushort);
int                 _AscWaitQDone(PortAddr, ASC_SCSI_Q dosfar *);
int                 AscEnterCritical(void);
void                AscLeaveCritical(int);
int                 AscIsrChipHalted(ASC_DVC_VAR asc_ptr_type *);
uchar               _AscCopyLramScsiDoneQ(PortAddr, ushort,
										  ASC_QDONE_INFO dosfar *, ulong);
int                 AscIsrQDone(ASC_DVC_VAR asc_ptr_type *);
ushort              AscIsrExeBusyQueue(ASC_DVC_VAR asc_ptr_type *, uchar);
int                 AscScsiSetupCmdQ(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_REQ_Q dosfar *,
									 uchar dosfar *, ulong);
int                 AscScsiInquiry(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_REQ_Q dosfar *,
								   uchar dosfar *, int);
int                 AscScsiTestUnitReady(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_REQ_Q dosfar *);
int                 AscScsiStartStopUnit(ASC_DVC_VAR asc_ptr_type *,
										 ASC_SCSI_REQ_Q dosfar *, uchar);
int                 AscScsiReadCapacity(ASC_DVC_VAR asc_ptr_type *,
										ASC_SCSI_REQ_Q dosfar *,
										uchar dosfar *);
ulong dosfar       *swapfarbuf4(uchar dosfar *);
int                 PollQueueDone(ASC_DVC_VAR asc_ptr_type *,
								  ASC_SCSI_REQ_Q dosfar *,
								  int);
int                 PollScsiReadCapacity(ASC_DVC_VAR asc_ptr_type *,
										 ASC_SCSI_REQ_Q dosfar *,
										 ASC_CAP_INFO dosfar *);
int                 PollScsiInquiry(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_REQ_Q dosfar *,
									uchar dosfar *, int);
int                 PollScsiTestUnitReady(ASC_DVC_VAR asc_ptr_type *,
										  ASC_SCSI_REQ_Q dosfar *);
int                 PollScsiStartUnit(ASC_DVC_VAR asc_ptr_type *,
									  ASC_SCSI_REQ_Q dosfar *);
int                 InitTestUnitReady(ASC_DVC_VAR asc_ptr_type *,
									  ASC_SCSI_REQ_Q dosfar *);
void                AscDispInquiry(uchar, uchar, ASC_SCSI_INQUIRY dosfar *);
int                 AscPollQDone(ASC_DVC_VAR asc_ptr_type *,
								 ASC_SCSI_REQ_Q dosfar *, int);
int                 AscCompareString(uchar *, uchar *, int);
int                 AscSetBIOSBank(PortAddr, int, ushort);
int                 AscSetVlBIOSBank(PortAddr, int);
int                 AscSetEisaBIOSBank(PortAddr, int);
int                 AscSetIsaBIOSBank(PortAddr, int);
ushort              AscGetEisaChipCfg(PortAddr);
ushort              AscGetEisaChipGpReg(PortAddr);
ushort              AscSetEisaChipCfg(PortAddr, ushort);
ushort              AscSetEisaChipGpReg(PortAddr, ushort);
ulong               AscGetEisaProductID(PortAddr);
PortAddr            AscSearchIOPortAddrEISA(PortAddr);
void                AscClrResetScsiBus(PortAddr);
uchar               AscGetChipScsiCtrl(PortAddr);
uchar               AscSetChipScsiID(PortAddr, uchar);
uchar               AscGetChipVersion(PortAddr, ushort);
ushort              AscGetChipBusType(PortAddr);
ulong               AscLoadMicroCode(PortAddr, ushort,
									 ushort dosfar *, ushort);
int                 AscFindSignature(PortAddr);
PortAddr            AscSearchIOPortAddr11(PortAddr);
PortAddr            AscSearchIOPortAddr100(PortAddr);
void                AscToggleIRQAct(PortAddr);
void                AscClrResetChip(PortAddr);
short               itos(ushort, uchar dosfar *, short, short);
int                 insnchar(uchar dosfar *, short, short, ruchar, short);
void                itoh(ushort, ruchar dosfar *);
void                btoh(uchar, ruchar dosfar *);
void                ltoh(ulong, ruchar dosfar *);
uchar dosfar       *todstr(ushort, uchar dosfar *);
uchar dosfar       *tohstr(ushort, uchar dosfar *);
uchar dosfar       *tobhstr(uchar, uchar dosfar *);
uchar dosfar       *tolhstr(ulong, uchar dosfar *);
void                AscSetISAPNPWaitForKey(void);
uchar               AscGetChipIRQ(PortAddr, ushort);
uchar               AscSetChipIRQ(PortAddr, uchar, ushort);
int                 AscIsBiosEnabled(PortAddr, ushort);
int                 AscEnableBios(PortAddr, ushort);
ushort              AscGetChipBiosAddress(PortAddr, ushort);
ushort              AscSetChipBiosAddress(PortAddr, ushort, ushort);
void                AscSingleStepChip(PortAddr);
int                 AscPollQTailSync(PortAddr);
int                 AscPollQHeadSync(PortAddr);
int                 AscWaitQTailSync(PortAddr);
int                 _AscRestoreMicroCode(PortAddr, ASC_MC_SAVED dosfar *);
int                 AscSCAM(ASC_DVC_VAR asc_ptr_type *);
ushort              SwapByteOfWord(ushort word_val);
ulong               SwapWordOfDWord(ulong dword_val);
ulong               AdjEndianDword(ulong dword_val);
int                 AscAdjEndianScsiQ(ASC_SCSI_Q dosfar *);
int                 AscAdjEndianQDoneInfo(ASC_QDONE_INFO dosfar *);
int                 AscCoalesceSgList(ASC_SCSI_Q dosfar *);
extern int          DvcEnterCritical(void);
extern void         DvcLeaveCritical(int);
extern void         DvcSetMemory(uchar dosfar *, uint, uchar);
extern void         DvcCopyMemory(uchar dosfar *, uchar dosfar *, uint);
extern void         DvcInPortWords(PortAddr, ushort dosfar *, int);
extern void         DvcOutPortWords(PortAddr, ushort dosfar *, int);
extern void         DvcOutPortDWords(PortAddr, ulong dosfar *, int);
extern uchar        DvcReadPCIConfigByte(ASC_DVC_VAR asc_ptr_type *, ushort);
extern void         DvcWritePCIConfigByte(ASC_DVC_VAR asc_ptr_type *, ushort, uchar);
ushort 				AscGetChipBiosAddress(PortAddr, ushort);
extern void         DvcSleepMilliSecond(ulong);
extern void         DvcDelayNanoSecond(ASC_DVC_VAR asc_ptr_type *, ulong);
extern void         DvcDisplayString(uchar dosfar *);
extern ulong        DvcGetPhyAddr(uchar dosfar * buf_addr, ulong buf_len);
extern ulong        DvcGetSGList(ASC_DVC_VAR asc_ptr_type *, uchar dosfar *, ulong,
								 ASC_SG_HEAD dosfar *);
extern void         DvcSCAMDelayMS(ulong);
extern int          DvcDisableCPUInterrupt(void);
extern void         DvcRestoreCPUInterrupt(int);
void                DvcPutScsiQ(PortAddr, ushort, ushort dosfar *, int);
void                DvcGetQinfo(PortAddr, ushort, ushort dosfar *, int);
PortAddr            AscSearchIOPortAddr(PortAddr, ushort);
ushort              AscInitGetConfig(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitSetConfig(ASC_DVC_VAR asc_ptr_type *);
ushort              AscInitAsc1000Driver(ASC_DVC_VAR asc_ptr_type *);
int                 AscInitScsiTarget(ASC_DVC_VAR asc_ptr_type *,
									  ASC_DVC_INQ_INFO dosfar *,
									  uchar dosfar *,
									  ASC_CAP_INFO_ARRAY dosfar *,
									  ushort);
int                 AscInitPollBegin(ASC_DVC_VAR asc_ptr_type *);
int                 AscInitPollEnd(ASC_DVC_VAR asc_ptr_type *);
int                 AscInitPollTarget(ASC_DVC_VAR asc_ptr_type *,
									  ASC_SCSI_REQ_Q dosfar *,
									  ASC_SCSI_INQUIRY dosfar *,
									  ASC_CAP_INFO dosfar *);
int                 AscExeScsiQueue(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_Q dosfar *);
int                 AscISR(ASC_DVC_VAR asc_ptr_type *);
void                AscISR_AckInterrupt(ASC_DVC_VAR asc_ptr_type *);
int                 AscISR_CheckQDone(ASC_DVC_VAR asc_ptr_type *,
									  ASC_QDONE_INFO dosfar *,
									  uchar dosfar *);
int                 AscStartUnit(ASC_DVC_VAR asc_ptr_type *, ASC_SCSI_TIX_TYPE);
int                 AscStopUnit(
								   ASC_DVC_VAR asc_ptr_type * asc_dvc,
								   ASC_SCSI_TIX_TYPE target_ix
);
uint                AscGetNumOfFreeQueue(ASC_DVC_VAR asc_ptr_type *, uchar, uchar);
int                 AscSgListToQueue(int);
int                 AscQueueToSgList(int);
int                 AscSetDvcErrorCode(ASC_DVC_VAR asc_ptr_type *, uchar);
int                 AscAbortSRB(ASC_DVC_VAR asc_ptr_type *, ulong);
int                 AscResetDevice(ASC_DVC_VAR asc_ptr_type *, uchar);
int                 AscResetSB(ASC_DVC_VAR asc_ptr_type *);
void                AscEnableIsaDma(uchar);
void                AscDisableIsaDma(uchar);
ulong               AscGetMaxDmaAddress(ushort);
ulong               AscGetMaxDmaCount(ushort);
int                 AscSaveMicroCode(ASC_DVC_VAR asc_ptr_type *, ASC_MC_SAVED dosfar *);
int                 AscRestoreOldMicroCode(ASC_DVC_VAR asc_ptr_type *, ASC_MC_SAVED dosfar *);
int                 AscRestoreNewMicroCode(ASC_DVC_VAR asc_ptr_type *, ASC_MC_SAVED dosfar *);

/*
 * --- Debugging Header
 */

#ifdef ADVANSYS_DEBUG
#define STATIC
#else /* ADVANSYS_DEBUG */
#define STATIC static
#endif /* ADVANSYS_DEBUG */


/*
 * --- Driver Constants and Macros
 */

#define ASC_NUM_BOARD_SUPPORTED 4
#define ASC_NUM_BUS				4

/* Reference Scsi_Host hostdata */
#define ASC_BOARDP(host) ((struct asc_board *) &((host)->hostdata))

#define NO_ISA_DMA				0xff		/* No ISA DMA Channel Used */

#define ASC_INFO_SIZE			128			/* advansys_info() line size */

/* /proc/scsi/advansys/[0...] related definitions */
#define ASC_PRTBUF_SIZE			1024
#define ASC_PRTLINE_SIZE		160

#define ASC_PRT_NEXT() \
	if (cp) { \
		totlen += len; \
		leftlen -= len; \
		if (leftlen == 0) { \
			return totlen; \
		} \
		cp += len; \
	}

#define ASC_MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Asc Library return codes */
#define ASC_TRUE		1
#define ASC_FALSE		0
#define ASC_NOERROR		1
#define ASC_BUSY		0
#define ASC_ERROR		(-1)

/* Scsi_Cmnd function return codes */
#define STATUS_BYTE(byte)	(byte)
#define MSG_BYTE(byte)		((byte) << 8)
#define HOST_BYTE(byte)		((byte) << 16)
#define DRIVER_BYTE(byte)	((byte) << 24)

/*
 * REQ and REQP are the generic name for a SCSI request block and pointer.
 * REQPTID(reqp) returns reqp's target id.
 * REQPNEXT(reqp) returns reqp's next pointer.
 * REQPNEXTP(reqp) returns a pointer to reqp's next pointer.
 */
typedef Scsi_Cmnd			REQ, *REQP;
#define REQPNEXT(reqp)		((REQP) ((reqp)->host_scribble))
#define REQPNEXTP(reqp)		((REQP *) &((reqp)->host_scribble))
#define REQPTID(reqp)		((reqp)->target)

/* asc_enqueue() flags */
#define ASC_FRONT		1
#define ASC_BACK		2

/* PCI configuration declarations */

#define PCI_BASE_CLASS_PREDEFINED			0x00
#define PCI_BASE_CLASS_MASS_STORAGE			0x01
#define PCI_BASE_CLASS_NETWORK				0x02
#define PCI_BASE_CLASS_DISPLAY				0x03
#define PCI_BASE_CLASS_MULTIMEDIA			0x04
#define PCI_BASE_CLASS_MEMORY_CONTROLLER	0x05
#define PCI_BASE_CLASS_BRIDGE_DEVICE		0x06

/* MASS STORAGE */
#define PCI_SUB_CLASS_SCSI_CONTROLLER			0x00
#define PCI_SUB_CLASS_IDE_CONTROLLER			0x01
#define PCI_SUB_CLASS_FLOPPY_DISK_CONTROLLER	0x02
#define PCI_SUB_CLASS_IPI_BUS_CONTROLLER		0x03
#define PCI_SUB_CLASS_OTHER_MASS_CONTROLLER		0x80

/* NETWORK CONTROLLER */
#define PCI_SUB_CLASS_ETHERNET_CONTROLLER		0x00
#define PCI_SUB_CLASS_TOKEN_RING_CONTROLLER		0x01
#define PCI_SUB_CLASS_FDDI_CONTROLLER			0x02
#define PCI_SUB_CLASS_OTHER_NETWORK_CONTROLLER	0x80

/* DISPLAY CONTROLLER */
#define PCI_SUB_CLASS_VGA_CONTROLLER			0x00
#define PCI_SUB_CLASS_XGA_CONTROLLER			0x01
#define PCI_SUB_CLASS_OTHER_DISPLAY_CONTROLLER	0x80

/* MULTIMEDIA CONTROLLER */
#define PCI_SUB_CLASS_VIDEO_DEVICE				0x00
#define PCI_SUB_CLASS_AUDIO_DEVICE				0x01
#define PCI_SUB_CLASS_OTHER_MULTIMEDIA_DEVICE	0x80

/* MEMORY CONTROLLER */
#define PCI_SUB_CLASS_RAM_CONTROLLER			0x00
#define PCI_SUB_CLASS_FLASH_CONTROLLER			0x01
#define PCI_SUB_CLASS_OTHER_MEMORY_CONTROLLER	0x80

/* BRIDGE CONTROLLER */
#define PCI_SUB_CLASS_HOST_BRIDGE_CONTROLLER		0x00
#define PCI_SUB_CLASS_ISA_BRIDGE_CONTROLLER			0x01
#define PCI_SUB_CLASS_EISA_BRIDGE_CONTROLLER		0x02
#define PCI_SUB_CLASS_MC_BRIDGE_CONTROLLER			0x03
#define PCI_SUB_CLASS_PCI_TO_PCI_BRIDGE_CONTROLLER	0x04
#define PCI_SUB_CLASS_PCMCIA_BRIDGE_CONTROLLER		0x05
#define PCI_SUB_CLASS_OTHER_BRIDGE_CONTROLLER		0x80

#define PCI_MAX_SLOT			0x1F
#define PCI_MAX_BUS				0xFF
#define PCI_IOADDRESS_MASK		0xFFFE
#define ASC_PCI_VENDORID		0x10CD
#define ASC_PCI_DEVICE_ID_1100	0x1100
#define ASC_PCI_DEVICE_ID_1200	0x1200
#define ASC_PCI_DEVICE_ID_1300	0x1300

/* PCI IO Port Addresses to generate special cycle */

#define PCI_CONFIG_ADDRESS_MECH1		0x0CF8
#define PCI_CONFIG_DATA_MECH1			0x0CFC

#define PCI_CONFIG_FORWARD_REGISTER		0x0CFA	/* 0=type 0; 1=type 1; */

#define PCI_CONFIG_BUS_NUMBER_MASK		0x00FF0000
#define PCI_CONFIG_DEVICE_FUNCTION_MASK	0x0000FF00
#define PCI_CONFIG_REGISTER_NUMBER_MASK	0x000000F8

#define PCI_DEVICE_FOUND				0x0000
#define PCI_DEVICE_NOT_FOUND			0xffff

#define SUBCLASS_OFFSET 	0x0A
#define CLASSCODE_OFFSET	0x0B
#define VENDORID_OFFSET		0x00
#define DEVICEID_OFFSET		0x02

#ifndef ADVANSYS_STATS
#define ASC_STATS(shp, counter)
#define ASC_STATS_ADD(shp, counter, count)
#else /* ADVANSYS_STATS */
#define ASC_STATS(shp, counter) \
	(ASC_BOARDP(shp)->asc_stats.counter++)

#define ASC_STATS_ADD(shp, counter, count) \
	(ASC_BOARDP(shp)->asc_stats.counter += (count))
#endif /* ADVANSYS_STATS */

#define ASC_CEILING(val, unit) (((val) + ((unit) - 1))/(unit))
#define ASC_TENTHS(num, den) ((((num) * 10)/(den)) - (10 * ((num)/(den))))

/*
 * Display a message to the console.
 */
#define ASC_PRINT(s) \
	{ \
		printk("advansys: "); \
		printk(s); \
	}

#define ASC_PRINT1(s, a1) \
	{ \
		printk("advansys: "); \
		printk((s), (a1)); \
	}

#define ASC_PRINT2(s, a1, a2) \
	{ \
		printk("advansys: "); \
		printk((s), (a1), (a2)); \
	}

#define ASC_PRINT3(s, a1, a2, a3) \
	{ \
		printk("advansys: "); \
		printk((s), (a1), (a2), (a3)); \
	}

#define ASC_PRINT4(s, a1, a2, a3, a4) \
	{ \
		printk("advansys: "); \
		printk((s), (a1), (a2), (a3), (a4)); \
	}


#ifndef ADVANSYS_DEBUG

#define	ASC_DBG(lvl, s)
#define	ASC_DBG1(lvl, s, a1)
#define	ASC_DBG2(lvl, s, a1, a2)
#define	ASC_DBG3(lvl, s, a1, a2, a3)
#define	ASC_DBG4(lvl, s, a1, a2, a3, a4)
#define	ASC_DBG_PRT_SCSI_HOST(lvl, s)
#define	ASC_DBG_PRT_DVC_VAR(lvl, v)
#define	ASC_DBG_PRT_DVC_CFG(lvl, c)
#define	ASC_DBG_PRT_SCSI_Q(lvl, scsiqp)
#define	ASC_DBG_PRT_QDONE_INFO(lvl, qdone)
#define	ASC_DBG_PRT_HEX(lvl, name, start, length)
#define	ASC_DBG_PRT_CDB(lvl, cdb, len)
#define	ASC_DBG_PRT_SENSE(lvl, sense, len)
#define ASC_DBG_PRT_INQUIRY(lvl, inq, len)
#define ASC_ASSERT(a)

#else /* ADVANSYS_DEBUG */

/*
 * Debugging Message Levels:
 * 0: Errors Only
 * 1: High-Level Tracing
 * 2-N: Verbose Tracing
 */

#define	ASC_DBG(lvl, s) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			printk(s); \
		} \
	}

#define	ASC_DBG1(lvl, s, a1) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			printk((s), (a1)); \
		} \
	}

#define	ASC_DBG2(lvl, s, a1, a2) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			printk((s), (a1), (a2)); \
		} \
	}

#define	ASC_DBG3(lvl, s, a1, a2, a3) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			printk((s), (a1), (a2), (a3)); \
		} \
	}

#define	ASC_DBG4(lvl, s, a1, a2, a3, a4) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			printk((s), (a1), (a2), (a3), (a4)); \
		} \
	}

#define	ASC_DBG_PRT_SCSI_HOST(lvl, s) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_scsi_host(s); \
		} \
	}

#define	ASC_DBG_PRT_DVC_VAR(lvl, v) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_dvc_var(v); \
		} \
	}

#define	ASC_DBG_PRT_DVC_CFG(lvl, c) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_dvc_cfg(c); \
		} \
	}

#define	ASC_DBG_PRT_SCSI_Q(lvl, scsiqp) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_scsi_q(scsiqp); \
		} \
	}

#define	ASC_DBG_PRT_QDONE_INFO(lvl, qdone) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_qdone_info(qdone); \
		} \
	}

#define	ASC_DBG_PRT_HEX(lvl, name, start, length) \
	{ \
		if (asc_dbglvl >= (lvl)) { \
			asc_prt_hex((name), (start), (length)); \
		} \
	}

#define	ASC_DBG_PRT_CDB(lvl, cdb, len) \
		ASC_DBG_PRT_HEX((lvl), "CDB", (uchar *) (cdb), (len));

#define	ASC_DBG_PRT_SENSE(lvl, sense, len) \
		ASC_DBG_PRT_HEX((lvl), "SENSE", (uchar *) (sense), (len));

#define ASC_DBG_PRT_INQUIRY(lvl, inq, len) \
		ASC_DBG_PRT_HEX((lvl), "INQUIRY", (uchar *) (inq), (len));

#define ASC_ASSERT(a) \
	{ \
		if (!(a)) { \
			printk("ASC_ASSERT() Failure: file %s, line %d\n", \
				__FILE__, __LINE__); \
		} \
	}
#endif /* ADVANSYS_DEBUG */


/*
 * --- Driver Structures
 */

#ifdef ADVANSYS_STATS

/* Per board statistics structure */
struct asc_stats {
	/* Driver Entrypoint Statistics */
	ulong 	command;		/* # calls to advansys_command() */
	ulong 	queuecommand;	/* # calls to advansys_queuecommand() */
	ulong 	abort;			/* # calls to advansys_abort() */
	ulong 	reset;			/* # calls to advansys_reset() */
	ulong 	biosparam;		/* # calls to advansys_biosparam() */
	ulong 	check_interrupt;/* # advansys_interrupt() check pending calls */
	ulong 	interrupt;		/* # advansys_interrupt() interrupts */
	ulong 	callback;		/* # calls asc_isr_callback() */
	/* AscExeScsiQueue() Statistics */
	ulong 	asc_noerror;	/* # AscExeScsiQueue() ASC_NOERROR returns. */
	ulong 	asc_busy;		/* # AscExeScsiQueue() ASC_BUSY returns. */
	ulong 	asc_error;		/* # AscExeScsiQueue() ASC_ERROR returns. */
	ulong 	asc_unknown;	/* # AscExeScsiQueue() unknown returns. */
	/* Data Transfer Statistics */
	ulong 	cont_cnt;		/* # non-scatter-gather I/O requests received */
	ulong 	cont_xfer;		/* # contiguous transfer 512-bytes */
	ulong 	sg_cnt;			/* # scatter-gather I/O requests received */
	ulong 	sg_elem;		/* # scatter-gather elements */
	ulong 	sg_xfer;		/* # scatter-gather tranfer 512-bytes */
	/* Device SCSI Command Queuing Statistics */
	ASC_SCSI_BIT_ID_TYPE queue_full;
	ushort	queue_full_cnt[ASC_MAX_TID+1];
};
#endif /* ADVANSYS_STATS */

/*
 * Request queuing structure
 */
typedef struct asc_queue {
	ASC_SCSI_BIT_ID_TYPE	tidmask;				  /* queue mask */
	REQP					queue[ASC_MAX_TID+1];	  /* queue linked list */
#ifdef ADVANSYS_STATS
	short					cur_count[ASC_MAX_TID+1]; /* current queue count */
	short					max_count[ASC_MAX_TID+1]; /* maximum queue count */
#endif /* ADVANSYS_STATS */
} asc_queue_t;

/*
 * Structure allocated for each board.
 *
 * This structure is allocated by scsi_register() at the end
 * of the 'Scsi_Host' structure starting at the 'hostdata'
 * field. It is guaranteed to be allocated from DMA-able memory.
 */
struct asc_board {
	int				 	 id;					  /* Board Id */
	/* Asc Library */
	ASC_DVC_VAR			 asc_dvc_var;			  /* Board configuration */
	ASC_DVC_CFG			 asc_dvc_cfg;			  /* Device configuration */
	/* Queued Commands */
	asc_queue_t			 active;				  /* Active command queue */
	asc_queue_t			 pending;		  		  /* Pending command queue */
	/* Target Initialization */
	ASC_SCSI_BIT_ID_TYPE init_tidmask;			  /* Target initialized mask */
	ASC_SCSI_REQ_Q		 scsireqq;
	ASC_CAP_INFO		 cap_info;
	ASC_SCSI_INQUIRY	 inquiry;
	ASCEEP_CONFIG		 eep_config;			  /* EEPROM configuration */
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
	/* /proc/scsi/advansys/[0...] */
	char				 *prtbuf;				  /* Statistics Print Buffer */
#endif /* version >= v1.3.0 */
#ifdef ADVANSYS_STATS
	struct asc_stats	 asc_stats;				  /* Board statistics */
#endif /* ADVANSYS_STATS */
};

/*
 * PCI configuration structures
 */
typedef struct _PCI_DATA_
{
	uchar	type;
	uchar	bus;
	uchar	slot;
	uchar	func;
	uchar	offset;
} PCI_DATA;

typedef struct _PCI_DEVICE_
{
	ushort	vendorID;
	ushort	deviceID;
	ushort	slotNumber;
	ushort	slotFound;
	uchar	busNumber;
	uchar	maxBusNumber;
	uchar	devFunc;
	ushort	startSlot;
	ushort	endSlot;
	uchar	bridge;
	uchar	type;
} PCI_DEVICE;

typedef struct _PCI_CONFIG_SPACE_
{
	ushort	vendorID;
	ushort	deviceID;
	ushort	command;
	ushort	status;
	uchar	revision;
	uchar	classCode[3];
	uchar	cacheSize;
	uchar	latencyTimer;
	uchar	headerType;
	uchar	bist;
	ulong	baseAddress[6];
	ushort	reserved[4];
	ulong	optionRomAddr;
	ushort	reserved2[4];
	uchar	irqLine;
	uchar	irqPin;
	uchar	minGnt;
	uchar	maxLatency;
} PCI_CONFIG_SPACE;


/*
 * --- Driver Data
 */

/* Note: All driver global data should be initialized. */

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
struct proc_dir_entry proc_scsi_advansys =
{
	PROC_SCSI_ADVANSYS,				/* unsigned short low_ino */
	8,								/* unsigned short namelen */
	"advansys",						/* const char *name */
	S_IFDIR | S_IRUGO | S_IXUGO,	/* mode_t mode */
	2								/* nlink_t nlink */
};
#endif /* version >= v1.3.0 */

/* Number of boards detected in system. */
STATIC int asc_board_count = 0;
STATIC struct Scsi_Host	*asc_host[ASC_NUM_BOARD_SUPPORTED] = { 0 };

/* Global list of commands needing done function. */
STATIC Scsi_Cmnd *asc_scsi_done = NULL;

/* Overrun buffer shared between all boards. */
STATIC uchar overrun_buf[ASC_OVERRUN_BSIZE] = { 0 };

/* List of supported bus types. */
STATIC ushort asc_bus[ASC_NUM_BUS] = {
	ASC_IS_ISA,
	ASC_IS_VL,
	ASC_IS_EISA,
	ASC_IS_PCI,
};

STATIC int pci_scan_method = -1;

/*
 * Used with the LILO 'advansys' option to eliminate or
 * limit I/O port probing at boot time, cf. advansys_setup().
 */
int asc_iopflag = ASC_FALSE;
int asc_ioport[ASC_NUM_BOARD_SUPPORTED] = { 0, 0, 0, 0 };

#ifdef ADVANSYS_DEBUG
char *
asc_bus_name[ASC_NUM_BUS] = {
	"ASC_IS_ISA",
	"ASC_IS_VL",
	"ASC_IS_EISA",
	"ASC_IS_PCI",
};

int		asc_dbglvl = 0;
#endif /* ADVANSYS_DEBUG */


/*
 * --- Driver Function Prototypes
 *
 * advansys.h contains function prototypes for functions global to Linux.
 */

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
STATIC int			asc_proc_copy(off_t, off_t, char *, int , char *, int);
#endif /* version >= v1.3.0 */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,70)
STATIC void 		advansys_interrupt(int, struct pt_regs *);
#else /* version >= v1.3.70 */
STATIC void 		advansys_interrupt(int, void *, struct pt_regs *);
#endif /* version >= v1.3.70 */
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
STATIC void 		advansys_select_queue_depths(struct Scsi_Host *,
												Scsi_Device *);
#endif /* version >= v1.3.89 */
STATIC void 		advansys_command_done(Scsi_Cmnd *);
STATIC int 			asc_execute_scsi_cmnd(Scsi_Cmnd *);
STATIC void 		asc_isr_callback(ASC_DVC_VAR *, ASC_QDONE_INFO *);
STATIC int 			asc_init_dev(ASC_DVC_VAR *, Scsi_Cmnd *);
STATIC int 			asc_srch_pci_dev(PCI_DEVICE *);
STATIC uchar 		asc_scan_method(void);
STATIC int 			asc_pci_find_dev(PCI_DEVICE *);
STATIC void 		asc_get_pci_cfg(PCI_DEVICE *, PCI_CONFIG_SPACE *);
STATIC ushort 		asc_get_cfg_word(PCI_DATA *);
STATIC uchar 		asc_get_cfg_byte(PCI_DATA *);
STATIC void 		asc_put_cfg_byte(PCI_DATA *, uchar);
void				asc_enqueue(asc_queue_t *, REQP, int);
REQP 				asc_dequeue(asc_queue_t *, int);
int					asc_rmqueue(asc_queue_t *, REQP);
int					asc_isqueued(asc_queue_t *, REQP);
void				asc_execute_queue(asc_queue_t *);
STATIC int			asc_prt_board_devices(struct Scsi_Host *, char *, int);
STATIC int			asc_prt_board_eeprom(struct Scsi_Host *, char *, int);
STATIC int			asc_prt_board_info(struct Scsi_Host *, char *, int);
STATIC int			asc_proc_copy(off_t, off_t, char *, int , char *, int);
STATIC int			asc_prt_line(char *, int, char *fmt, ...);

/* XXX - Asc Library Routines not supposed to be used directly */
int             	AscFindSignature(PortAddr);
ushort          	AscGetEEPConfig(PortAddr, ASCEEP_CONFIG *, ushort);

#ifdef ADVANSYS_STATS
STATIC int			asc_prt_board_stats(struct Scsi_Host *, char *, int);
#endif /* ADVANSYS_STATS */
#ifdef ADVANSYS_DEBUG
STATIC void 		asc_prt_scsi_host(struct Scsi_Host *);
STATIC void 		asc_prt_dvc_cfg(ASC_DVC_CFG *);
STATIC void 		asc_prt_dvc_var(ASC_DVC_VAR *);
STATIC void 		asc_prt_scsi_q(ASC_SCSI_Q *);
STATIC void 		asc_prt_qdone_info(ASC_QDONE_INFO *);
STATIC void 		asc_prt_hex(char *f, uchar *, int);
STATIC int 			interrupts_enabled(void);
#endif /* ADVANSYS_DEBUG */


/*
 * --- Linux 'Scsi_Host_Template' and advansys_setup() Functions
 */

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
/*
 * advansys_proc_info() - /proc/scsi/advansys/[0-(ASC_NUM_BOARD_SUPPORTED-1)]
 *
 * *buffer: I/O buffer
 * **start: if inout == FALSE pointer into buffer where user read should start
 * offset: current offset into a /proc/scsi/advansys/[0...] file
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * inout: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written to a
 * /proc/scsi/advansys/[0...] file.
 *
 * Note: This function uses the per board buffer 'prtbuf' which is
 * allocated when the board is initialized in advansys_detect(). The
 * buffer is ASC_PRTBUF_SIZE bytes. The function asc_proc_copy() is
 * used to write to the buffer. The way asc_proc_copy() is written
 * if 'prtbuf' is too small it will not be overwritten. Instead the
 * user just won't get all the available statistics.
 */
int
advansys_proc_info(char *buffer, char **start, off_t offset, int length, 
				   int hostno, int inout)
{
	struct Scsi_Host	*shp;
	struct asc_board	*boardp;
	int					i;
	char				*cp;
	int					cplen;
	int					cnt;
	int					totcnt;
	int					leftlen;
	char				*curbuf;
	off_t				advoffset;
    Scsi_Device			*scd;

	ASC_DBG(1, "advansys_proc_info: begin\n");

	/*
	 * User write not supported.
	 */
	if (inout == TRUE) {
		return(-ENOSYS);
	}

	/*
	 * User read of /proc/scsi/advansys/[0...] file.
	 */

	/* Find the specified board. */
	for (i = 0; i < asc_board_count; i++) {
		if (asc_host[i]->host_no == hostno) {
			break;
		}
	}
	if (i == asc_board_count) {
		return(-ENOENT);
	}

	shp = asc_host[i];
	boardp = ASC_BOARDP(shp);

	/* Copy read data starting at the beginning of the buffer. */
	*start = buffer;
	curbuf = buffer;
	advoffset = 0;
	totcnt = 0;
	leftlen = length;

	/*
	 * Get board configuration information.
	 *
	 * advansys_info() returns the board string from its own static buffer.
	 */
	cp = (char *) advansys_info(shp);
	strcat(cp, "\n");
	cplen = strlen(cp);
	/* Copy board information. */
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	/*
	 * Display driver information for each device attached to the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_devices(shp, cp, ASC_PRTBUF_SIZE);
	ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	/*
	 * Display target driver information for each device attached
	 * to the board.
	 */
    for (scd = scsi_devices; scd; scd = scd->next) {
		if (scd->host == shp) {
			cp = boardp->prtbuf;
			/*
			 * Note: If proc_print_scsidevice() writes more than
			 * ASC_PRTBUF_SIZE bytes, it will overrun 'prtbuf'.
			 */
	    	proc_print_scsidevice(scd, cp, &cplen, 0);
			ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
			cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
			totcnt += cnt;
			leftlen -= cnt;
			if (leftlen == 0) {
				ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
				return totcnt;
			}
			advoffset += cplen;
			curbuf += cnt;
		}
    }
    
	/*
	 * Display EEPROM configuration for the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_eeprom(shp, cp, ASC_PRTBUF_SIZE);
	ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

#ifdef ADVANSYS_STATS
	/*
	 * Display driver statistics for the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_stats(shp, cp, ASC_PRTBUF_SIZE);
	ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;
#endif /* ADVANSYS_STATS */

	/*
	 * Display Asc Library dynamic configuration information
	 * for the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_info(shp, cp, ASC_PRTBUF_SIZE);
	ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);

	return totcnt;
}
#endif /* version >= v1.3.0 */


/*
 * advansys_detect()
 *
 * Detect function for AdvanSys adapters.
 *
 * Argument is a pointer to the host driver's scsi_hosts entry.
 *
 * Return number of adapters found.
 *
 * Note: Because this function is called during system initialization
 * it must not call SCSI mid-level functions including scsi_malloc()
 * and scsi_free().
 */
int
advansys_detect(Scsi_Host_Template *tpnt)
{
	static int			detect_called = ASC_FALSE;
	int					iop;
	int					bus;
	struct Scsi_Host	*shp;
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	int					ioport = 0;
	int					share_irq = FALSE;
	PCI_DEVICE			pciDevice;
	PCI_CONFIG_SPACE	pciConfig;
	int					ret;
	extern PortAddr		_asc_def_iop_base[ASC_IOADR_TABLE_MAX_IX];


	if (detect_called == ASC_FALSE) {
		detect_called = ASC_TRUE;
	} else {
		printk("AdvanSys SCSI: advansys_detect() mulitple calls ignored\n");
		return 0;
	}

	ASC_DBG(1, "advansys_detect: begin\n");

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
	tpnt->proc_dir = &proc_scsi_advansys;
#endif /* version >= v1.3.0 */

	asc_board_count = 0;

	/*
	 * If I/O port probing has been modified, then verify and
	 * clean-up the 'asc_ioport' list.
	 */
	if (asc_iopflag == ASC_TRUE) {
		for (ioport = 0; ioport < ASC_NUM_BOARD_SUPPORTED; ioport++) {
			ASC_DBG2(1, "advansys_detect: asc_ioport[%d] %x\n",
				ioport, asc_ioport[ioport]);
			if (asc_ioport[ioport] != 0) {
				for (iop = 0; iop < ASC_IOADR_TABLE_MAX_IX; iop++) {
					if (_asc_def_iop_base[iop] == asc_ioport[ioport]) {
						break;
					}
				}
				if (iop == ASC_IOADR_TABLE_MAX_IX) {
					printk("AdvanSys SCSI: specified I/O Port 0x%X is invalid\n",
						asc_ioport[ioport]);
					asc_ioport[ioport] = 0;
				}
			}
		}
		ioport = 0;
	}

	memset(&pciDevice, 0, sizeof(PCI_DEVICE));
	memset(&pciConfig, 0, sizeof(PCI_CONFIG_SPACE));
	pciDevice.maxBusNumber = PCI_MAX_BUS;
	pciDevice.endSlot = PCI_MAX_SLOT;

	for (bus = 0; bus < ASC_NUM_BUS; bus++) {

		ASC_DBG2(1, "advansys_detect: bus search type %d (%s)\n",
			bus, asc_bus_name[bus]);
		iop = 0;

		while (asc_board_count < ASC_NUM_BOARD_SUPPORTED) {

			ASC_DBG1(2, "advansys_detect: asc_board_count %d\n",
				asc_board_count);

			switch (asc_bus[bus]) {
			case ASC_IS_ISA:
			case ASC_IS_VL:
				if (asc_iopflag == ASC_FALSE) {
					iop = AscSearchIOPortAddr(iop, asc_bus[bus]);
				} else {
					/*
					 * ISA and VL I/O port scanning has either been
					 * eliminated or limited to selected ports on
					 * the LILO command line, /etc/lilo.conf, or
					 * by setting variables when the module was loaded.
					 */
					ASC_DBG(1, "advansys_detect: I/O port scanning modified\n");
				ioport_try_again:
					iop = 0;
					for (; ioport < ASC_NUM_BOARD_SUPPORTED; ioport++) {
						if ((iop = asc_ioport[ioport]) != 0) {
							break;
						}
					}
					if (iop) {
						ASC_DBG1(1, "advansys_detect: probing I/O port %x...\n",
							iop);
						if (check_region(iop, ASC_IOADR_GAP) != 0) {
							printk("AdvanSys SCSI: specified I/O Port 0x%X is busy\n", iop);
							/* Don't try this I/O port twice. */
							asc_ioport[ioport] = 0;
							goto ioport_try_again;
						} else if (AscFindSignature(iop) == ASC_FALSE) {
							printk("AdvanSys SCSI: specified I/O Port 0x%X has no adapter\n", iop);
							/* Don't try this I/O port twice. */
							asc_ioport[ioport] = 0;
							goto ioport_try_again;
						} else {
							/*
							 * If this isn't an ISA board, then it must be
							 * a VL board. If currently looking an ISA
							 * board is being looked for then try for
							 * another ISA board in 'asc_ioport'.
							 */
							if (asc_bus[bus] == ASC_IS_ISA &&
							    (AscGetChipVersion(iop, ASC_IS_ISA) &
								 ASC_CHIP_VER_ISA_BIT) == 0) {
							 	/*
								 * Don't clear 'asc_ioport[ioport]'. Try
								 * this board again for VL. Increment
								 * 'ioport' past this board.
								 */
								 ioport++;
								 goto ioport_try_again;
							}
						}
						/*
						 * This board appears good, don't try the I/O port
						 * again by clearing its value. Increment 'ioport'
						 * for the next iteration.
						 */
						asc_ioport[ioport++] = 0;
					}
				}
				break;

			case ASC_IS_EISA:
				iop = AscSearchIOPortAddr(iop, asc_bus[bus]);
				break;

			case ASC_IS_PCI:
					if (asc_srch_pci_dev(&pciDevice) != PCI_DEVICE_FOUND) {
						iop = 0;
					} else {
						ASC_DBG2(2,
							"advansys_detect: slotFound %d, busNumber %d\n",
							pciDevice.slotFound, pciDevice.busNumber);
						asc_get_pci_cfg(&pciDevice, &pciConfig);
						iop = pciConfig.baseAddress[0] & PCI_IOADDRESS_MASK;
						ASC_DBG2(2, "advansys_detect: iop %x, irqLine %d\n",
							iop, pciConfig.irqLine);
					}
				break;

			default:
				ASC_PRINT1("advansys_detect: unknown bus type: %d\n",
					asc_bus[bus]);
				break;
			}
			ASC_DBG1(1, "advansys_detect: iop %x\n", iop);

			/*
			 * Adapter not found, try next bus type.
			 */
			if (iop == 0) {
				break;
			}

			/*
			 * Adapter found.
			 *
			 * Register the adapter, get its configuration, and
			 * initialize it.
			 */
			ASC_DBG(2, "advansys_detect: scsi_register()\n");
			shp = scsi_register(tpnt, sizeof(struct asc_board));

			/* Save a pointer to the Scsi_host of each board found. */
			asc_host[asc_board_count++] = shp;

			/* Initialize private per board data */
			boardp = ASC_BOARDP(shp);
			memset(boardp, 0, sizeof(struct asc_board));
			boardp->id = asc_board_count - 1;
			asc_dvc_varp = &boardp->asc_dvc_var;
			asc_dvc_varp->cfg = &boardp->asc_dvc_cfg;
			asc_dvc_varp->cfg->overrun_buf = &overrun_buf[0];
			asc_dvc_varp->iop_base = iop;

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
			if ((boardp->prtbuf =
				kmalloc(ASC_PRTBUF_SIZE, GFP_ATOMIC)) == NULL) {
				ASC_PRINT3(
"advansys_detect: Board %d: kmalloc(%d, %d) returned NULL\n",
					boardp->id, ASC_PRTBUF_SIZE, GFP_ATOMIC);
				scsi_unregister(shp);
				asc_board_count--;
				continue;
			}
#endif /* version >= v1.3.0 */

			/*
			 * Set the board bus type and PCI IRQ for AscInitGetConfig().
			 */
			asc_dvc_varp->bus_type = asc_bus[bus];
			switch (asc_dvc_varp->bus_type) {
			case ASC_IS_ISA:
				shp->unchecked_isa_dma = TRUE;
				share_irq = FALSE;
				break;
			case ASC_IS_VL:
				shp->unchecked_isa_dma = FALSE;
				share_irq = FALSE;
				break;
			case ASC_IS_EISA:
				shp->unchecked_isa_dma = FALSE;
				share_irq = TRUE;
				break;
			case ASC_IS_PCI:
				shp->irq = asc_dvc_varp->irq_no = pciConfig.irqLine;
				asc_dvc_varp->cfg->pci_device_id = pciConfig.deviceID;
				asc_dvc_varp->cfg->pci_slot_info =
						ASC_PCI_MKID(pciDevice.busNumber,
									 pciDevice.slotFound,
									 pciDevice.devFunc);
				shp->unchecked_isa_dma = FALSE;
				share_irq = TRUE;
				break;
			default:
				ASC_PRINT2(
"advansys_detect: Board %d: unknown adapter type: %d",
					boardp->id, asc_dvc_varp->bus_type);
				shp->unchecked_isa_dma = TRUE;
				share_irq = FALSE;
				break;
			}

			/*
			 * Get the board configuration.
			 *
			 * AscInitGetConfig() may change the board's bus_type value.
			 * The asc_bus[bus] value should no longer be used. If the
			 * bus_type field must be referenced only use the bit-wise
			 * AND operator "&".
			 */
			ASC_DBG(2, "advansys_detect: AscInitGetConfig()\n");
			switch(ret = AscInitGetConfig(asc_dvc_varp)) {
			case 0:	/* No error */
				break;
			case ASC_WARN_IO_PORT_ROTATE:
				ASC_PRINT1(
"AscInitGetConfig: Board: %d: I/O port address modified\n",
					boardp->id);
				break;
			case ASC_WARN_AUTO_CONFIG:
				ASC_PRINT1(
"AscInitGetConfig: Board %d: I/O port increment switch enabled\n",
					boardp->id);
				break;
			case ASC_WARN_EEPROM_CHKSUM:
				ASC_PRINT1(
"AscInitGetConfig: Board %d: EEPROM checksum error\n",
					boardp->id);
				break;
			case ASC_WARN_IRQ_MODIFIED:
				ASC_PRINT1(
"AscInitGetConfig: Board %d: IRQ modified\n",
					boardp->id);
				break;
			case ASC_WARN_CMD_QNG_CONFLICT:
				ASC_PRINT1(
"AscInitGetConfig: Board %d: tag queuing enabled w/o disconnects\n",
					boardp->id);
				break;
			default:
				ASC_PRINT2(
"AscInitGetConfig: Board %d: unknown warning: %x\n",
					boardp->id, ret);
				break;
			}
			if (asc_dvc_varp->err_code != 0) {
				ASC_PRINT3(
"AscInitGetConfig: Board %d error: init_state %x, err_code %x\n",
					boardp->id, asc_dvc_varp->init_state,
					asc_dvc_varp->err_code);
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
				kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
				scsi_unregister(shp);
				asc_board_count--;
				continue;
			}

			/*
			 * Set the adapter's target id bit in the init_tidmask field.
			 */
			boardp->init_tidmask |=
				ASC_TIX_TO_TARGET_ID(asc_dvc_varp->cfg->chip_scsi_id);

			/*
			 * Save EEPROM settings for the board.
			 */
			boardp->eep_config.init_sdtr = asc_dvc_varp->init_sdtr;
			boardp->eep_config.disc_enable = asc_dvc_varp->cfg->disc_enable;
			boardp->eep_config.use_cmd_qng = asc_dvc_varp->cfg->cmd_qng_enabled;
			boardp->eep_config.isa_dma_speed = asc_dvc_varp->cfg->isa_dma_speed;
			boardp->eep_config.start_motor = asc_dvc_varp->start_motor;
			boardp->eep_config.cntl = asc_dvc_varp->dvc_cntl;
			boardp->eep_config.no_scam = asc_dvc_varp->no_scam;
			boardp->eep_config.max_total_qng = asc_dvc_varp->max_total_qng;
			boardp->eep_config.chip_scsi_id = asc_dvc_varp->cfg->chip_scsi_id;
			/* 'max_tag_qng' is set to the same value for every device. */
			boardp->eep_config.max_tag_qng = asc_dvc_varp->cfg->max_tag_qng[0];

			/*
			 * Modify board configuration.
			 */
			asc_dvc_varp->isr_callback = (Ptr2Func) asc_isr_callback;
			asc_dvc_varp->exe_callback = (Ptr2Func) NULL;

			ASC_DBG(2, "advansys_detect: AscInitSetConfig()\n");
			switch (ret = AscInitSetConfig(asc_dvc_varp)) {
			case 0:	/* No error. */
				break;
			case ASC_WARN_IO_PORT_ROTATE:
				ASC_PRINT1(
"AscInitSetConfig: Board %d: I/O port address modified\n",
					boardp->id);
				break;
			case ASC_WARN_AUTO_CONFIG:
				ASC_PRINT1(
"AscInitSetConfig: Board %d: I/O port increment switch enabled\n",
					boardp->id);
				break;
			case ASC_WARN_EEPROM_CHKSUM:
				ASC_PRINT1(
"AscInitSetConfig: Board %d: EEPROM checksum error\n",
					boardp->id);
				break;
			case ASC_WARN_IRQ_MODIFIED:
				ASC_PRINT1(
"AscInitSetConfig: Board %d: IRQ modified\n",
					boardp->id);
				break;
			case ASC_WARN_CMD_QNG_CONFLICT:
				ASC_PRINT1(
"AscInitSetConfig: Board %d: tag queuing w/o disconnects\n",
					boardp->id);
				break;
			default:
				ASC_PRINT2(
"AscInitSetConfig: Board %d: unknown warning: %x\n",
					boardp->id, ret);
				break;
			}
			if (asc_dvc_varp->err_code != 0) {
				ASC_PRINT3(
"AscInitSetConfig: Board %d error: init_state %x, err_code %x\n",
					boardp->id, asc_dvc_varp->init_state,
					asc_dvc_varp->err_code);
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
				kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
				scsi_unregister(shp);
				asc_board_count--;
				continue;
			}

			/*
			 * Finish initializing the 'Scsi_Host' structure.
			 */

			/* AscInitSetConfig() will set the IRQ for non-PCI boards. */
			if (asc_dvc_varp->bus_type != ASC_IS_PCI) {
				shp->irq = asc_dvc_varp->irq_no;
			}

			shp->io_port = asc_dvc_varp->iop_base;
			shp->n_io_port = ASC_IOADR_GAP;
			shp->this_id = asc_dvc_varp->cfg->chip_scsi_id;

			/* Maximum number of queues this adapter can handle. */
			shp->can_queue = asc_dvc_varp->max_total_qng;

#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,89)
			/*
			 * Set a conservative 'cmd_per_lun' value to prevent memory
			 * allocation failures.
			 */
#ifdef MODULE
			shp->cmd_per_lun = 1;
#else /* MODULE */
			shp->cmd_per_lun = 4;
#endif /* MODULE */
			ASC_DBG1(1, "advansys_detect: cmd_per_lun: %d\n", shp->cmd_per_lun);
#else /* version >= v1.3.89 */
			/*
			 * Use the host 'select_queue_depths' function to determine
			 * the number of commands to queue per device.
			 */
			shp->select_queue_depths = advansys_select_queue_depths;

			shp->cmd_per_lun = 0; /* 'cmd_per_lun' is no longer used. */
#endif /* version >= v1.3.89 */

			
			/*
			 * Maximum number of scatter-gather elements adapter can handle.
			 *
			 * Set a conservative 'sg_tablesize' value to prevent memory
			 * allocation failures.
			 */
#ifdef MODULE
			shp->sg_tablesize = 8;
#else /* MODULE */
			shp->sg_tablesize = ASC_MAX_SG_LIST;
#endif /* MODULE */
			ASC_DBG1(1, "advansys_detect: sg_tablesize: %d\n",
				shp->sg_tablesize);

			/* BIOS start address. */
			shp->base = (char *) ((ulong) AscGetChipBiosAddress(
												asc_dvc_varp->iop_base,
												asc_dvc_varp->bus_type));

			/*
			 * Register Board Resources - I/O Port, DMA, IRQ
			 */

			/* Register I/O port range */
			ASC_DBG(2, "advansys_detect: request_region()\n");
			request_region(shp->io_port, shp->n_io_port, "advansys");

			/* Register DMA channel for ISA bus. */
			if ((asc_dvc_varp->bus_type & ASC_IS_ISA) == 0) {
				shp->dma_channel = NO_ISA_DMA;
			} else {
				shp->dma_channel = asc_dvc_varp->cfg->isa_dma_channel;
				if ((ret = request_dma(shp->dma_channel, "advansys")) != 0) {
					ASC_PRINT3(
"advansys_detect: Board %d: request_dma() %d failed %d\n",
						boardp->id, shp->dma_channel, ret);
					release_region(shp->io_port, shp->n_io_port);
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
					kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
					scsi_unregister(shp);
					asc_board_count--;
					continue;
				}
				AscEnableIsaDma(shp->dma_channel);
			}

			/* Register IRQ Number. */
			ASC_DBG1(2, "advansys_detect: request_irq() %d\n", shp->irq);
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,70)
			if ((ret = request_irq(shp->irq, advansys_interrupt,
							SA_INTERRUPT, "advansys")) != 0) {
#else /* version >= v1.3.70 */
			if ((ret = request_irq(shp->irq, advansys_interrupt,
							SA_INTERRUPT | (share_irq == TRUE ? SA_SHIRQ : 0),
							"advansys", boardp)) != 0) {
#endif /* version >= v1.3.70 */
				ASC_PRINT2(
"advansys_detect: Board %d: request_irq() failed %d\n",
					boardp->id, ret);
				release_region(shp->io_port, shp->n_io_port);
				if (shp->dma_channel != NO_ISA_DMA) {
					free_dma(shp->dma_channel);
				}
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
				kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
				scsi_unregister(shp);
				asc_board_count--;
				continue;
			}

			/*
			 * Initialize board RISC chip and enable interrupts.
			 */
			ASC_DBG(2, "advansys_detect: AscInitAsc1000Driver()\n");
			if (AscInitAsc1000Driver(asc_dvc_varp)) {
				ASC_PRINT3(
"AscInitAsc1000Driver: Board %d error: init_state %x, err_code %x\n",
					boardp->id, asc_dvc_varp->init_state,
					asc_dvc_varp->err_code);
				release_region(shp->io_port, shp->n_io_port);
				if (shp->dma_channel != NO_ISA_DMA) {
					free_dma(shp->dma_channel);
				}
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
				kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,70)
				free_irq(shp->irq);
#else /* version >= v1.3.70 */
				free_irq(shp->irq, boardp);
#endif /* version >= v1.3.70 */
				scsi_unregister(shp);
				asc_board_count--;
				continue;
			}
			ASC_DBG_PRT_SCSI_HOST(2, shp);
		}
	}
	ASC_DBG1(1, "advansys_detect: done: asc_board_count %d\n", asc_board_count);
	return asc_board_count;
}

/*
 * advansys_release()
 *
 * Release resources allocated for a single AdvanSys adapter.
 */
int
advansys_release(struct Scsi_Host *shp)
{
	struct asc_board	*boardp;

	ASC_DBG(1, "advansys_release: begin\n");
	boardp = ASC_BOARDP(shp);
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,70)
	free_irq(shp->irq);
#else /* version >= v1.3.70 */
	free_irq(shp->irq, boardp);
#endif /* version >= v1.3.70 */
	if (shp->dma_channel != NO_ISA_DMA) {
		ASC_DBG(1, "advansys_release: free_dma()\n");
		free_dma(shp->dma_channel);
	}
	release_region(shp->io_port, shp->n_io_port);
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
	ASC_ASSERT(boardp->prtbuf != NULL);
	kfree(boardp->prtbuf);
#endif /* version >= v1.3.0 */
	scsi_unregister(shp);
	ASC_DBG(1, "advansys_release: end\n");
	return 0;
}

/*
 * advansys_info()
 *
 * Return suitable for printing on the console with the argument
 * adapter's configuration information.
 *
 * Note: The information line should not exceed ASC_INFO_SIZE bytes,
 * otherwise the static 'info' array will be overrun.
 */
const char *
advansys_info(struct Scsi_Host *shp)
{
	static char 		info[ASC_INFO_SIZE];
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	char				*busname;

	boardp = ASC_BOARDP(shp);
	asc_dvc_varp = &boardp->asc_dvc_var;
	ASC_DBG(1, "advansys_info: begin\n");
	if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
		sprintf(info,
			"AdvanSys SCSI %s: ISA (%u CDB): BIOS %X, IO %X-%X, IRQ %u, DMA %u",
			ASC_VERSION, boardp->asc_dvc_var.max_total_qng,
			(unsigned) shp->base, shp->io_port,
			shp->io_port + (shp->n_io_port - 1), shp->irq, shp->dma_channel);
	} else {
		if (asc_dvc_varp->bus_type & ASC_IS_VL) {
			busname = "VL";
		} else if (asc_dvc_varp->bus_type & ASC_IS_EISA) {
			busname = "EISA";
		} else if (asc_dvc_varp->bus_type & ASC_IS_PCI) {
			busname = "PCI";
		} else {
			busname = "?";
			ASC_PRINT2(
"advansys_info: Board %d: unknown bus type %d\n",
				boardp->id, asc_dvc_varp->bus_type);
		}
		/* No DMA channel for non-ISA busses. */
		sprintf(info,
			"AdvanSys SCSI %s: %s (%u CDB): BIOS %X, IO %X-%X, IRQ %u",
			ASC_VERSION, busname, boardp->asc_dvc_var.max_total_qng,
			(unsigned) shp->base, shp->io_port,
			shp->io_port + (shp->n_io_port - 1), shp->irq);
	}
#ifdef ADVANSYS_DEBUG
	ASC_ASSERT(strlen(info) < ASC_INFO_SIZE);
#endif /* ADVANSYS_DEBUG */
	ASC_DBG(1, "advansys_info: end\n");
	return info;
}

/*
 * advansys_command()
 *
 * Polled-I/O. Apparently host drivers shouldn't return until
 * command is finished.
 *
 * XXX - Can host drivers block here instead of spinning on command status?
 */
int
advansys_command(Scsi_Cmnd *scp)
{
	ASC_DBG1(1, "advansys_command: scp %x\n", (unsigned) scp);
	ASC_STATS(scp->host, command);
	scp->SCp.Status = 0; /* Set to a known state */
	advansys_queuecommand(scp, advansys_command_done);
	while (scp->SCp.Status == 0) {
		continue;
	}
	ASC_DBG1(1, "advansys_command: result %x\n", scp->result);
	return scp->result;
}

/*
 * advansys_queuecommand()
 *
 * This function always returns 0. Command return status is saved
 * in the 'scp' result field.
 */
int
advansys_queuecommand(Scsi_Cmnd *scp, void (*done)(Scsi_Cmnd *))
{
	struct Scsi_Host		*shp;
	struct asc_board		*boardp;
	int						flags = 0;
	int						interrupts_disabled;

	shp = scp->host;
	boardp = ASC_BOARDP(shp);
	ASC_STATS(shp, queuecommand);

	/*
	 * If there are any pending commands for this board before trying
	 * to execute them, disable interrupts to preserve request ordering.
	 *
	 * The typical case will be no pending commands and interrupts
	 * not disabled.
	 */
	if (boardp->pending.tidmask == 0) {
		interrupts_disabled = ASC_FALSE;
	} else {
		/* Disable interrupts */
		interrupts_disabled = ASC_TRUE;
		save_flags(flags);
		cli();
		ASC_DBG1(1, "advansys_queuecommand: asc_execute_queue() %x\n",
			boardp->pending.tidmask);
		asc_execute_queue(&boardp->pending);
	}

	/*
	 * Save the function pointer to Linux mid-level 'done' function and
	 * execute the command.
	 */
	scp->scsi_done = done;
	if (asc_execute_scsi_cmnd(scp) == ASC_BUSY) {
		if (interrupts_disabled == ASC_FALSE) {
			save_flags(flags);
			cli();
			interrupts_disabled = ASC_TRUE;
		}
		asc_enqueue(&boardp->pending, scp, ASC_BACK);
	}

	if (interrupts_disabled == ASC_TRUE) {
		restore_flags(flags);
	}

	return 0;
}

/*
 * advansys_abort()
 *
 * Abort the command specified by 'scp'.
 */
int
advansys_abort(Scsi_Cmnd *scp)
{
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	int					flags;
	int					abort;
	int					ret;

	ASC_DBG1(1, "advansys_abort: scp %x\n", (unsigned) scp);
	ASC_STATS(scp->host, abort);

	/* Save current flags and disable interrupts. */
	save_flags(flags);
	cli();

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
	if (scp->serial_number != scp->serial_number_at_timeout) {
		ret = SCSI_ABORT_NOT_RUNNING;
	} else
#endif /* version >= v1.3.89 */
	if (scp->host == NULL) {
		scp->result = HOST_BYTE(DID_ERROR);
		ret = SCSI_ABORT_ERROR;
	} else {
		boardp = ASC_BOARDP(scp->host);
		if (asc_rmqueue(&boardp->pending, scp) == ASC_TRUE) {
			/*
		 	 * If asc_rmqueue() found the command on the pending
			 * queue, it had not been sent to the Asc Library.
			 * After the queue is removed, no other handling is required.
		 	 */
			scp->result = HOST_BYTE(DID_ABORT);
			ret = SCSI_ABORT_SUCCESS;
		} else if (asc_isqueued(&boardp->active, scp) == ASC_TRUE) {
			/*
		 	 * If asc_isqueued() found the command on the active
			 * queue, it has been sent to the Asc Library. The
			 * command should be returned through the interrupt
			 * handler after calling AscAbortSRB().
		 	 */
			asc_dvc_varp = &boardp->asc_dvc_var;
			scp->result = HOST_BYTE(DID_ABORT);
			/* Must enable interrupts for AscAbortSRB() */
			sti();
			switch (abort = AscAbortSRB(asc_dvc_varp, (ulong) scp)) {
			case ASC_TRUE:
				/* asc_isr_callback() will be called */
				ASC_DBG(1, "advansys_abort: AscAbortSRB() TRUE\n");
				ret = SCSI_ABORT_PENDING;
				break;
			case ASC_FALSE:
				/* Request has apparently already completed. */
				ASC_DBG(1, "advansys_abort: AscAbortSRB() FALSE\n");
				ret = SCSI_ABORT_NOT_RUNNING;
				break;
			case ASC_ERROR:
			default:
				ASC_DBG(1, "advansys_abort: AscAbortSRB() ERROR\n");
				ret = SCSI_ABORT_ERROR;
				break;
			}
			cli();
			/*
			 * If the abort failed, remove the request from the
			 * active list and complete it.
			 */
			if (abort != ASC_TRUE) {
				if (asc_rmqueue(&boardp->active, scp) == ASC_TRUE) {
					scp->result = HOST_BYTE(DID_ABORT);
					scp->scsi_done(scp);
				}
			}
		} else {
			/*
			 * The command was not found on the active or pending queues.
			 */
			ret = SCSI_ABORT_NOT_RUNNING;
		}
	}
	restore_flags(flags);
	ASC_DBG1(1, "advansys_abort: ret %d\n", ret);
	return ret;
}

/*
 * advansys_reset()
 *
 * Reset the device associated with the command 'scp'.
 */
int
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,89)
advansys_reset(Scsi_Cmnd *scp)
#else /* version >= v1.3.89 */
advansys_reset(Scsi_Cmnd *scp, unsigned int reset_flags)
#endif /* version >= v1.3.89 */
{
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	int					flags;
	Scsi_Cmnd			*tscp;
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
	int					scp_found = ASC_FALSE;
#endif /* version >= v1.3.89 */
	int					i;
	int					ret;

	ASC_DBG1(1, "advansys_reset: %x\n", (unsigned) scp);
	ASC_STATS(scp->host, reset);

	/* Save current flags and disable interrupts. */
	save_flags(flags);
	cli();

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
	if (scp->serial_number != scp->serial_number_at_timeout) {
		ret = SCSI_RESET_NOT_RUNNING;
	} else
#endif /* version >= v1.3.89 */
	if (scp->host == NULL) {
		scp->result = HOST_BYTE(DID_ERROR);
		ret = SCSI_RESET_ERROR;
	} else {
		boardp = ASC_BOARDP(scp->host);

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
		/*
	 	 * If the request is on the target pending or active queue,
		 * note that it was found.
		 */
		if ((asc_isqueued(&boardp->pending, scp) == ASC_TRUE) ||
		    (asc_isqueued(&boardp->active, scp) == ASC_TRUE)) {
			scp_found = ASC_TRUE;
		}
#endif /* version >= v1.3.89 */

		/*
		 * If the suggest reset bus flags are set, reset the bus.
		 * Otherwise only reset the device.
		 */
		asc_dvc_varp = &boardp->asc_dvc_var;
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
		if (reset_flags &
			(SCSI_RESET_SUGGEST_BUS_RESET | SCSI_RESET_SUGGEST_HOST_RESET)) {
#endif /* version >= v1.3.89 */

			/*
			 * Done all pending requests for all targets with DID_RESET.
			 */
			for (i = 0; i <= ASC_MAX_TID; i++) {
				while ((tscp = asc_dequeue(&boardp->pending, i)) != NULL) {
					tscp->result = HOST_BYTE(DID_RESET);
					tscp->scsi_done(tscp);
				}
			}

			/*
			 * Reset the target's SCSI bus.
			 */
			sti();	/* Enable interrupts for AscResetSB(). */
			switch (AscResetSB(asc_dvc_varp)) {
			case ASC_TRUE:
				ASC_DBG(1, "advansys_reset: AscResetSB() TRUE\n");
				ret = SCSI_RESET_SUCCESS;
				break;
			case ASC_ERROR:
			default:
				ASC_DBG(1, "advansys_reset: AscResetSB() ERROR\n");
				ret = SCSI_RESET_ERROR;
				break;
			}
			cli();

			/*
			 * Done all active requests for all targets with DID_RESET.
			 */
			for (i = 0; i <= ASC_MAX_TID; i++) {
				while ((tscp = asc_dequeue(&boardp->active, i)) != NULL) {
					tscp->result = HOST_BYTE(DID_RESET);
					tscp->scsi_done(tscp);
				}
			}
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
		} else {
			/*
			 * Done all pending requests for the target with DID_RESET.
			 */
			while ((tscp = asc_dequeue(&boardp->pending, scp->target))
					!= NULL) {
				tscp->result = HOST_BYTE(DID_RESET);
				tscp->scsi_done(tscp);
			}

			sti();	/* Enabled interrupts for AscResetDevice(). */
			ASC_DBG(1, "advansys_reset: AscResetDevice()\n");
			(void) AscResetDevice(asc_dvc_varp, scp->target);
			cli();

			/*
			 * Done all active requests for the target with DID_RESET.
			 */
			while ((tscp = asc_dequeue(&boardp->active, scp->target))
					!= NULL) {
				tscp->result = HOST_BYTE(DID_RESET);
				tscp->scsi_done(tscp);
			}
		}
#endif /* version >= v1.3.89 */

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
		/*
		 * If the command was not on the active or pending request
		 * queues and the SCSI_RESET_SYNCHRONOUS flag is set, then
		 * done the command now. If the command had been on the
		 * active or pending request queues it would have already
		 * been completed.
		 */
		if (scp_found == ASC_FALSE && (reset_flags & SCSI_RESET_SYNCHRONOUS)) {
			scp->result = HOST_BYTE(DID_RESET);
			scp->scsi_done(tscp);
		}
#endif /* version >= v1.3.89 */
		ret = SCSI_RESET_SUCCESS;
	}
	restore_flags(flags);
	ASC_DBG1(1, "advansys_reset: ret %d", ret);
	return ret;
}

/*
 * advansys_biosparam()
 *
 * Translate disk drive geometry if the "BIOS greater than 1 GB"
 * support is enabled for a drive.
 *
 * ip (information pointer) is an int array with the following definition:
 * ip[0]: heads
 * ip[1]: sectors
 * ip[2]: cylinders
 */
int
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,0)
advansys_biosparam(Disk *dp, int dep, int ip[])
#else /* version >= v1.3.0 */
advansys_biosparam(Disk *dp, kdev_t dep, int ip[])
#endif /* version >= v1.3.0 */
{
	ASC_DBG(1, "advansys_biosparam: begin\n");
	ASC_STATS(dp->device->host, biosparam);
	if ((ASC_BOARDP(dp->device->host)->asc_dvc_var.dvc_cntl &
	     ASC_CNTL_BIOS_GT_1GB) && dp->capacity > 0x200000) {
			ip[0] = 255;
			ip[1] = 63;
	} else {
			ip[0] = 64;
			ip[1] = 32;
	}
	ip[2] = dp->capacity / (ip[0] * ip[1]);
	ASC_DBG(1, "advansys_biosparam: end\n");
	return 0;
}

/*
 * advansys_setup()
 *
 * This function is called from init/main.c at boot time.
 * It it passed LILO parameters that can be set from the
 * LILO command line or in /etc/lilo.conf.
 *
 * It is used by the AdvanSys driver to either disable I/O
 * port scanning or to limit scanning to 1 - 4 I/O ports.
 * Regardless of the option setting EISA and PCI boards
 * will still be searched for and detected. This option
 * only affects searching for ISA and VL boards.
 *
 * If ADVANSYS_DEBUG is defined the driver debug level may
 * be set using the 5th (ASC_NUM_BOARD_SUPPORTED + 1) I/O Port.
 *
 * Examples:
 * 1. Eliminate I/O port scanning:
 * 		boot: linux advansys=
 *       or
 * 		boot: linux advansys=0x0
 * 2. Limit I/O port scanning to one I/O port:
 *		boot: linux advansys=0x110
 * 3. Limit I/O port scanning to four I/O ports:
 *		boot: linux advansys=0x110,0x210,0x230,0x330
 * 4. If ADVANSYS_DEBUG, limit I/O port scanning to four I/O ports and
 *    set the driver debug level to 2.
 *		boot: linux advansys=0x110,0x210,0x230,0x330,0xdeb2
 *
 * ints[0] - number of arguments
 * ints[1] - first argument
 * ints[2] - second argument
 * ...
 */
void
advansys_setup(char *str, int *ints)
{
	int	i;

	if (asc_iopflag == ASC_TRUE) {
		printk("AdvanSys SCSI: 'advansys' LILO option may appear only once\n");
		return;
	}

	asc_iopflag = ASC_TRUE;

	if (ints[0] > ASC_NUM_BOARD_SUPPORTED) {
#ifdef ADVANSYS_DEBUG
		if ((ints[0] == ASC_NUM_BOARD_SUPPORTED + 1) &&
		    (ints[ASC_NUM_BOARD_SUPPORTED + 1] >> 4 == 0xdeb)) {
			asc_dbglvl = ints[ASC_NUM_BOARD_SUPPORTED + 1] & 0xf;
		} else {
#endif /* ADVANSYS_DEBUG */
			printk("AdvanSys SCSI: only %d I/O ports accepted\n",
				ASC_NUM_BOARD_SUPPORTED);
#ifdef ADVANSYS_DEBUG
		}
#endif /* ADVANSYS_DEBUG */
	}

#ifdef ADVANSYS_DEBUG
	ASC_DBG1(1, "advansys_setup: ints[0] %d\n", ints[0]);
	for (i = 1; i < ints[0]; i++) {
		ASC_DBG2(1, " ints[%d] %x", i, ints[i]);
	}
	ASC_DBG(1, "\n");
#endif /* ADVANSYS_DEBUG */

	for (i = 1; i <= ints[0] && i <= ASC_NUM_BOARD_SUPPORTED; i++) {
		asc_ioport[i-1] = ints[i];
		ASC_DBG2(1, "advansys_setup: asc_ioport[%d] %x\n",
			i - 1, asc_ioport[i-1]);
	}
}


/*
 * --- Loadable Driver Support
 */

#ifdef MODULE
Scsi_Host_Template driver_template = ADVANSYS;
# include "scsi_module.c"
#endif /* MODULE */


/*
 * --- Miscellaneous Driver Functions
 */

/*
 * First-level interrupt handler.
 *
 * For versions > v1.3.70, 'dev_id' is a pointer to the interrupting
 * adapter's struct asc_board. Because all boards are currently checked
 * for interrupts on each interrupt, 'dev_id' is not referenced. 'dev_id'
 * could be used to identify an interrupt passed to the AdvanSys driver
 * but actually for a device sharing an interrupt with an AdvanSys adapter.
 */
STATIC void
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,70)
advansys_interrupt(int irq, struct pt_regs *regs)
#else /* version >= v1.3.70 */
advansys_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#endif /* version >= v1.3.70 */
{
	int			i;
	int			flags;
	Scsi_Cmnd	*scp;
	Scsi_Cmnd	*tscp;

	/* Disable interrupts, if the aren't already disabled. */
	save_flags(flags);
	cli();

	ASC_DBG(1, "advansys_interrupt: begin\n");
	/*
	 * Check for interrupts on all boards.
	 * AscISR() will call asc_isr_callback().
	 */
	for (i = 0; i < asc_board_count; i++) {
		ASC_STATS(asc_host[i], check_interrupt);
		while (AscIsIntPending(asc_host[i]->io_port)) {
			ASC_STATS(asc_host[i], interrupt);
			ASC_DBG(1, "advansys_interrupt: before AscISR()\n");
			AscISR(&ASC_BOARDP(asc_host[i])->asc_dvc_var);
		}
	}

	/*
	 * While interrupts are still disabled save the list of requests that
	 * need their done function called. After re-enabling interrupts call
	 * the done function which may re-enable interrupts anyway.
	 */
	if ((scp = asc_scsi_done) != NULL) {
		asc_scsi_done = NULL;
	}

	/* Re-enable interrupts, if they were enabled on entry. */
	restore_flags(flags);

	while (scp) {
		tscp = (Scsi_Cmnd *) scp->host_scribble;
		scp->scsi_done(scp);
		scp = tscp;
	}

	ASC_DBG(1, "advansys_interrupt: end\n");
	return;
}

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,89)
/*
 * Set the number of commands to queue per device for the
 * specified host adapter.
 */
STATIC void
advansys_select_queue_depths(struct Scsi_Host *shp, Scsi_Device *devicelist)
{
	Scsi_Device			*device;
	struct asc_board	*boardp;

	boardp = ASC_BOARDP(shp);
	for (device = devicelist; device != NULL; device = device->next) {
		if (device->host != shp) {
			continue;
		}
		device->queue_depth = boardp->asc_dvc_var.max_dvc_qng[device->id];
		ASC_DBG3(1, "advansys_select_queue_depths: shp %x, id %d, depth %d\n",
			(unsigned) shp, device->id, device->queue_depth);
	}
}
#endif /* version >= v1.3.89 */

/*
 * Function used only with polled I/O requests that are initiated by
 * advansys_command().
 */
STATIC void
advansys_command_done(Scsi_Cmnd *scp)
{
	ASC_DBG1(1, "advansys_command_done: scp %x\n", (unsigned) scp);
	scp->SCp.Status = 1;
}

/*
 * Execute a single 'Scsi_Cmnd'.
 *
 * The function 'done' is called when the request has been completed.
 *
 * Scsi_Cmnd:
 *
 *  host - board controlling device
 *  device - device to send command
 *  target - target of device
 *  lun - lun of device
 *  cmd_len - length of SCSI CDB
 *	cmnd - buffer for SCSI 8, 10, or 12 byte CDB
 *  use_sg - if non-zero indicates scatter-gather request with use_sg elements
 *
 *  if (use_sg == 0)
 *		request_buffer - buffer address for request
 *		request_bufflen - length of request buffer
 *  else
 *		request_buffer - pointer to scatterlist structure
 *
 *  sense_buffer - sense command buffer
 *
 *  result (4 bytes of an int):
 *   Byte Meaning
 *   0	  SCSI Status Byte Code
 *   1	  SCSI One Byte Message Code
 *   2 	  Host Error Code
 *   3	  Mid-Level Error Code
 *
 *  host driver fields:
 *  SCp - Scsi_Pointer used for command processing status
 *  scsi_done - used to save caller's done function
 * 	host_scribble - used for pointer to another Scsi_Cmnd
 *
 * If this function returns ASC_NOERROR or ASC_ERROR the done
 * function has been called. If ASC_BUSY is returned the request
 * must be enqueued by the caller and re-tried later.
 */
STATIC int
asc_execute_scsi_cmnd(Scsi_Cmnd *scp)
{
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	ASC_SCSI_Q			scsiq;
	ASC_SG_HEAD			sghead;
	int					flags;
	int					ret;

	ASC_DBG2(1, "asc_execute_scsi_cmnd: scp %x, done %x\n",
		(unsigned) scp, (unsigned) scp->scsi_done);

	boardp = ASC_BOARDP(scp->host);
	asc_dvc_varp = &boardp->asc_dvc_var;

	/*
	 * If this is the first command, then initialize the device. If
	 * no device is found set 'DID_BAD_TARGET' and return.
	 */
	if ((boardp->init_tidmask & ASC_TIX_TO_TARGET_ID(scp->target)) == 0) {
		if (asc_init_dev(asc_dvc_varp, scp) == ASC_FALSE) {
			scp->result = HOST_BYTE(DID_BAD_TARGET);
			scp->scsi_done(scp);
			return ASC_ERROR;
		}
		boardp->init_tidmask |= ASC_TIX_TO_TARGET_ID(scp->target);
	}

	memset(&scsiq, 0, sizeof(ASC_SCSI_Q));

	/*
	 * Point the ASC_SCSI_Q to the 'Scsi_Cmnd'.
	 */
	scsiq.q2.srb_ptr = (ulong) scp;

	/*
	 * Build the ASC_SCSI_Q request.
	 */
	scsiq.cdbptr = &scp->cmnd[0];
	scsiq.q2.cdb_len = scp->cmd_len;
	scsiq.q1.target_id = ASC_TID_TO_TARGET_ID(scp->target);
	scsiq.q1.target_lun = scp->lun;
	scsiq.q2.target_ix = ASC_TIDLUN_TO_IX(scp->target, scp->lun);
	scsiq.q1.sense_addr = (ulong) &scp->sense_buffer[0];
	scsiq.q1.sense_len = sizeof(scp->sense_buffer);
	scsiq.q2.tag_code = M2_QTAG_MSG_SIMPLE;

	/*
	 * Build ASC_SCSI_Q for a contiguous buffer or a scatter-gather
	 * buffer command.
	 */
	if (scp->use_sg == 0) {
		/*
		 * CDB request of single contiguous buffer.
		 */
		ASC_STATS(scp->host, cont_cnt);
 		/* request_buffer is already a real address. */
		scsiq.q1.data_addr = (ulong) scp->request_buffer;
		scsiq.q1.data_cnt = scp->request_bufflen;
		ASC_STATS_ADD(scp->host, cont_xfer,
					  ASC_CEILING(scp->request_bufflen, 512));
		scsiq.q1.sg_queue_cnt = 0;
		scsiq.sg_head = NULL;
	} else {
		/*
		 * CDB scatter-gather request list.
		 */
		int					sgcnt;
		struct scatterlist	*slp;

		if (scp->use_sg > ASC_MAX_SG_LIST) {
			ASC_PRINT3("asc_execute_scsi_cmnd: Board %d: use_sg %d > %d\n",
				boardp->id, scp->use_sg, ASC_MAX_SG_LIST);
			scp->result = HOST_BYTE(DID_ERROR);
			scp->scsi_done(scp);
			return ASC_ERROR;
		}

		ASC_STATS(scp->host, sg_cnt);

		/*
		 * Allocate a ASC_SG_HEAD structure and set the ASC_SCSI_Q
		 * to point to it.
		 */
		memset(&sghead, 0, sizeof(ASC_SG_HEAD));

		scsiq.q1.cntl |= QC_SG_HEAD;
		scsiq.sg_head = &sghead;
		scsiq.q1.data_cnt = 0;
		scsiq.q1.data_addr = 0;
		sghead.entry_cnt = scsiq.q1.sg_queue_cnt = scp->use_sg;
		ASC_STATS_ADD(scp->host, sg_elem, sghead.entry_cnt);

		/*
		 * Convert scatter-gather list into ASC_SG_HEAD list.
		 */
		slp = (struct scatterlist *) scp->request_buffer;
		for (sgcnt = 0; sgcnt < scp->use_sg; sgcnt++, slp++) {
			sghead.sg_list[sgcnt].addr = (ulong) slp->address;
			sghead.sg_list[sgcnt].bytes = slp->length;
			ASC_STATS_ADD(scp->host, sg_xfer, ASC_CEILING(slp->length, 512));
		}
	}

	ASC_DBG_PRT_SCSI_Q(2, &scsiq);
	ASC_DBG_PRT_CDB(1, scp->cmnd, scp->cmd_len);

	/*
	 * Disable interrupts to issue the command and add the
	 * command to the active queue if it is started.
	 */
	save_flags(flags);
	cli();

	switch (ret = AscExeScsiQueue(asc_dvc_varp, &scsiq)) {
	case ASC_NOERROR:
		asc_enqueue(&boardp->active, scp, ASC_BACK);
		ASC_STATS(scp->host, asc_noerror);
		ASC_DBG(1, "asc_execute_scsi_cmnd: AscExeScsiQueue(), ASC_NOERROR\n");
		break;
	case ASC_BUSY:
		/* Caller must enqueue request and retry later. */
		ASC_STATS(scp->host, asc_busy);
		break;
	case ASC_ERROR:
		ASC_PRINT2(
"asc_execute_scsi_cmnd: Board %d: AscExeScsiQueue() ASC_ERROR, err_code %x\n",
			boardp->id, asc_dvc_varp->err_code);
		ASC_STATS(scp->host, asc_error);
		scp->result = HOST_BYTE(DID_ERROR);
		scp->scsi_done(scp);
		break;
	default:
		ASC_PRINT2(
"asc_execute_scsi_cmnd: Board %d: AscExeScsiQueue() unknown, err_code %x\n",
			boardp->id, asc_dvc_varp->err_code);
		ASC_STATS(scp->host, asc_unknown);
		scp->result = HOST_BYTE(DID_ERROR);
		scp->scsi_done(scp);
		break;
	}
	restore_flags(flags);

	ASC_DBG(1, "asc_execute_scsi_cmnd: end\n");
	return ret;
}

/*
 * asc_isr_callback() - Second Level Interrupt Handler called by AscISR().
 */
void
asc_isr_callback(ASC_DVC_VAR *asc_dvc_varp, ASC_QDONE_INFO *qdonep)
{
	struct asc_board	*boardp;
	Scsi_Cmnd			*scp;
	struct Scsi_Host	*shp;
	Scsi_Cmnd			**scpp;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ASC_DBG2(1, "asc_isr_callback: asc_dvc_varp %x, qdonep %x\n",
		(unsigned) asc_dvc_varp, (unsigned) qdonep);
	ASC_DBG_PRT_QDONE_INFO(2, qdonep);

	/*
	 * Get the Scsi_Cmnd structure and Scsi_Host structure for the
	 * command that has been completed.
	 */
	scp = (Scsi_Cmnd *) qdonep->d2.srb_ptr;
	ASC_DBG1(1, "asc_isr_callback: scp %x\n", (unsigned) scp);
	ASC_DBG_PRT_CDB(2, scp->cmnd, scp->cmd_len);

	shp = scp->host;
	ASC_ASSERT(shp);
	ASC_STATS(shp, callback);
	ASC_DBG1(1, "asc_isr_callback: shp %x\n", (unsigned) shp);

	boardp = ASC_BOARDP(shp);
	if (asc_rmqueue(&boardp->active, scp) == ASC_FALSE) {
		ASC_PRINT2(
"asc_isr_callback: Board %d: scp %x not on active queue\n",
			boardp->id, (unsigned) scp);
	}

	/*
	 * 'qdonep' contains the command's ending status.
	 */
	switch (qdonep->d3.done_stat) {
	case QD_NO_ERROR:
		ASC_DBG(2, "asc_isr_callback: QD_NO_ERROR\n");
		switch (qdonep->d3.host_stat) {
		case QHSTA_NO_ERROR:
			scp->result = 0;
			break;
		default:
			/* QHSTA error occurred */
			scp->result = HOST_BYTE(DID_ERROR);
			break;
		}
		break;

	case QD_WITH_ERROR:
		ASC_DBG(2, "asc_isr_callback: QD_WITH_ERROR\n");
		switch (qdonep->d3.host_stat) {
		case QHSTA_NO_ERROR:
			if (qdonep->d3.scsi_stat == SS_CHK_CONDITION) {
				ASC_DBG(2, "asc_isr_callback: SS_CHK_CONDITION\n");
				ASC_DBG_PRT_SENSE(2, scp->sense_buffer,
					sizeof(scp->sense_buffer));
				/*
				 * Note: The 'status_byte()' macro used by target drivers
				 * defined in scsi.h shifts the status byte returned by
				 * host drivers right by 1 bit. This is why target drivers
				 * also use right shifted status byte definitions. For
				 * instance target drivers use CHECK_CONDITION, defined to
				 * 0x1, instead of the SCSI defined check condition value
				 * of 0x2. Host drivers are supposed to return the status
				 * byte as it is defined by SCSI.
				 */
				scp->result = DRIVER_BYTE(DRIVER_SENSE) |
					STATUS_BYTE(qdonep->d3.scsi_stat); 
			} else {
				scp->result = STATUS_BYTE(qdonep->d3.scsi_stat); 
			}
			break;

		default:
			/* QHSTA error occurred */
			ASC_DBG1(2, "asc_isr_callback: host_stat %x\n",
				qdonep->d3.host_stat);
			scp->result = HOST_BYTE(DID_ERROR) | MSG_BYTE(qdonep->d3.scsi_msg) |
				STATUS_BYTE(qdonep->d3.scsi_stat);
			break;
		}
		break;

	case QD_ABORTED_BY_HOST:
		ASC_DBG(1, "asc_isr_callback: QD_ABORTED_BY_HOST\n");
		scp->result = HOST_BYTE(DID_ABORT) | MSG_BYTE(qdonep->d3.scsi_msg) |
				STATUS_BYTE(qdonep->d3.scsi_stat);
		break;

	default:
		ASC_PRINT1("asc_isr_callback: done_stat %x\n", qdonep->d3.done_stat );
		scp->result = HOST_BYTE(DID_ERROR) | MSG_BYTE(qdonep->d3.scsi_msg) |
				STATUS_BYTE(qdonep->d3.scsi_stat);
		break;
	}

	/*
	 * Before calling 'scsi_done' for the current 'Scsi_Cmnd' and possibly
	 * triggering more commands to be issued, try to start any pending
	 * commands.
	 */
	if (boardp->pending.tidmask != 0) {
	 	/*
		 * If there are any pending commands for this board before trying
	 	 * to execute them, disable interrupts to preserve request ordering.
		 */
		ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
		ASC_DBG1(1, "asc_isr_callback: asc_execute_queue() %x\n",
			boardp->pending.tidmask);
		asc_execute_queue(&boardp->pending);
	}

	/* 
	 * Because interrupts may be enabled by the 'Scsi_Cmnd' done function,
	 * add the command to the end of the global done list. The done function
	 * for the command will be called in advansys_interrupt().
	 */
	for (scpp = &asc_scsi_done; *scpp;
	     scpp = (Scsi_Cmnd **) &(*scpp)->host_scribble) {
		;
	}
	*scpp = scp;
	scp->host_scribble = NULL;
	return;
}

/*
 * asc_init_dev()
 *
 * Perform one-time initialization of a device.
 */
STATIC int
asc_init_dev(ASC_DVC_VAR *asc_dvc_varp, Scsi_Cmnd *scp)
{
	struct asc_board		*boardp;
	ASC_SCSI_REQ_Q			*scsireqq;
	ASC_CAP_INFO			*cap_info;
	ASC_SCSI_INQUIRY		*inquiry;
	int						found;
	ASC_SCSI_BIT_ID_TYPE	save_use_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE	save_can_tagged_qng;
	int						ret;
#ifdef ADVANSYS_DEBUG
	ASC_SCSI_BIT_ID_TYPE	tidmask; /* target id bit mask: 1 - 128 */
#endif /* ADVANSYS_DEBUG */

	ASC_DBG1(1, "asc_init_dev: target %d\n", (unsigned) scp->target);

	/* The hosts's target id is set in init_tidmask during initialization. */
	ASC_ASSERT(asc_dvc_varp->cfg->chip_scsi_id != scp->target);

	boardp = ASC_BOARDP(scp->host);

	/*
	 * XXX - Host drivers should not modify the timeout field.
	 * But on the first command only add some extra time to
	 * allow the driver to complete its initialization for the
	 * device.
	 */
	scp->timeout += 2000;	/* Add 5 seconds to the request timeout. */

	/* Set-up AscInitPollTarget() arguments. */
	scsireqq = &boardp->scsireqq;
	memset(scsireqq, 0, sizeof(ASC_SCSI_REQ_Q));
	cap_info = &boardp->cap_info;
	memset(cap_info, 0, sizeof(ASC_CAP_INFO));
	inquiry = &boardp->inquiry;
	memset(inquiry, 0, sizeof(ASC_SCSI_INQUIRY));

	/*
	 * XXX - AscInitPollBegin() re-initializes these fields to
	 * zero. 'Or' in the new values and restore them before calling 
	 * AscInitPollEnd(). Normally all targets are initialized within
	 * a call to AscInitPollBegin() and AscInitPollEnd().
	 */
	save_use_tagged_qng = asc_dvc_varp->use_tagged_qng;
    save_can_tagged_qng = asc_dvc_varp->cfg->can_tagged_qng;

	ASC_DBG(2, "asc_init_dev: AscInitPollBegin()\n");
	if (AscInitPollBegin(asc_dvc_varp)) {
		ASC_PRINT1("asc_init_dev: Board %d: AscInitPollBegin() failed\n",
			boardp->id);
		return ASC_FALSE;
	}

	scsireqq->sense_ptr = &scsireqq->sense[0];
	scsireqq->r1.sense_len = ASC_MIN_SENSE_LEN;
	scsireqq->r1.target_id = ASC_TID_TO_TARGET_ID(scp->target);
	scsireqq->r1.target_lun = 0;
	scsireqq->r2.target_ix = ASC_TIDLUN_TO_IX(scp->target, 0);

	found = ASC_FALSE;
	ASC_DBG(2, "asc_init_dev: AscInitPollTarget()\n");
	switch (ret = AscInitPollTarget(asc_dvc_varp, scsireqq, inquiry, cap_info)) {
	case ASC_TRUE:
		found = ASC_TRUE;
#ifdef ADVANSYS_DEBUG
		tidmask = ASC_TIX_TO_TARGET_ID(scp->target);
		ASC_DBG2(1, "asc_init_dev: lba %lu, blk_size %lu\n",
			cap_info->lba, cap_info->blk_size);
		ASC_DBG1(1, "asc_init_dev: peri_dvc_type %x\n",
			inquiry->byte0.peri_dvc_type);
		if (asc_dvc_varp->use_tagged_qng & tidmask) {
			ASC_DBG1(1, "asc_init_dev: command queuing enabled: %d\n",
				asc_dvc_varp->max_dvc_qng[scp->target]);
		} else {
			ASC_DBG(1, "asc_init_dev: command queuing disabled\n");
		}
		if (asc_dvc_varp->init_sdtr & tidmask) {
			ASC_DBG(1, "asc_init_dev: synchronous transfers enabled\n");
		} else {
			ASC_DBG(1, "asc_init_dev: synchronous transfers disabled\n");
		}
		/* Set bit means fix disabled. */
		if (asc_dvc_varp->pci_fix_asyn_xfer & tidmask) {
			ASC_DBG(1, "asc_init_dev: synchronous transfer fix disabled\n");
		} else {
			ASC_DBG(1, "asc_init_dev: synchronous transfer fix enabled\n");
		}
#endif /* ADVANSYS_DEBUG */
		break;
	case ASC_FALSE:
		ASC_DBG(1, "asc_init_dev: no device found\n");
		break;
	case ASC_ERROR:
		ASC_PRINT1("asc_init_dev: Board %d: AscInitPollTarget() ASC_ERROR\n",
				boardp->id);
		break;
	default:
		ASC_PRINT2(
"asc_init_dev: Board %d: AscInitPollTarget() unknown ret %d\n",
				boardp->id, ret);
		break;
	}

	/* XXX - 'Or' in original tag bits. */
	asc_dvc_varp->use_tagged_qng |= save_use_tagged_qng;
	asc_dvc_varp->cfg->can_tagged_qng |= save_can_tagged_qng;

	ASC_DBG(2, "asc_init_dev: AscInitPollEnd()\n");
	AscInitPollEnd(asc_dvc_varp);

	return found;
}

/*
 * Search for an AdvanSys PCI device in the PCI configuration space.
 */
STATIC int
asc_srch_pci_dev(PCI_DEVICE *pciDevice)
{
	int ret;

	ASC_DBG(2, "asc_srch_pci_dev: begin\n");

	if (pci_scan_method == -1) {
		pci_scan_method = asc_scan_method();
	}
	pciDevice->type = pci_scan_method;
	ASC_DBG1(2, "asc_srch_pci_dev: type %d\n", pciDevice->type);

	ret = asc_pci_find_dev(pciDevice);
	ASC_DBG1(2, "asc_srch_pci_dev: asc_pci_find_dev() return %d\n", ret);
	if (ret == PCI_DEVICE_FOUND) {
		pciDevice->slotNumber = pciDevice->slotFound + 1;
		pciDevice->startSlot = pciDevice->slotFound + 1;
	} else {
		if (pciDevice->bridge > pciDevice->busNumber) {
			ASC_DBG2(2, "asc_srch_pci_dev: bridge %x, busNumber %x\n",
				pciDevice->bridge, pciDevice->busNumber);
			pciDevice->busNumber++;
			pciDevice->slotNumber = 0;
			pciDevice->startSlot = 0;
			pciDevice->endSlot = 0x0f;
			ret = asc_srch_pci_dev(pciDevice);
			ASC_DBG1(2, "asc_srch_pci_dev: recursive call return %d\n", ret);
		}
	}
	ASC_DBG1(2, "asc_srch_pci_dev: return %d\n", ret);
	return ret;
}

/*
 * Determine the access method to be used for 'pciDevice'.
 */
STATIC uchar
asc_scan_method(void)
{
	ushort data;
	PCI_DATA pciData;
	uchar type;
	uchar slot;

	ASC_DBG(2, "asc_scan_method: begin\n");
	memset(&pciData, 0, sizeof(pciData));
	for (type = 1; type < 3; type++) {
		pciData.type = type;
		for (slot = 0; slot < PCI_MAX_SLOT; slot++) {
			pciData.slot = slot;
			data = asc_get_cfg_word(&pciData);
			if ((data != 0xFFFF) && (data != 0x0000)) {
				ASC_DBG2(4, "asc_scan_method: data %x, type %d\n", data, type);
				return (type);
			}
		}
	}
	ASC_DBG1(4, "asc_scan_method: type %d\n", type);
	return (type);
}

/*
 * Check for an AdvanSys PCI device in 'pciDevice'.
 *
 * Return PCI_DEVICE_FOUND if found, otherwise return PCI_DEVICE_NOT_FOUND.
 */
STATIC int
asc_pci_find_dev(PCI_DEVICE *pciDevice)
{
	PCI_DATA pciData;
	ushort vendorid, deviceid;
	uchar classcode, subclass;
	uchar lslot;

	ASC_DBG(3, "asc_pci_find_dev: begin\n");
	pciData.type = pciDevice->type;
	pciData.bus = pciDevice->busNumber;
	pciData.func = pciDevice->devFunc;
	lslot = pciDevice->startSlot;
	for (; lslot < pciDevice->endSlot; lslot++) {
		pciData.slot = lslot;
		pciData.offset = VENDORID_OFFSET;
		vendorid = asc_get_cfg_word(&pciData);
		ASC_DBG1(3, "asc_pci_find_dev: vendorid %x\n", vendorid);
		if (vendorid != 0xffff) {
			pciData.offset = DEVICEID_OFFSET;
			deviceid = asc_get_cfg_word(&pciData);
			ASC_DBG1(3, "asc_pci_find_dev: deviceid %x\n", deviceid);
			if ((vendorid == ASC_PCI_VENDORID) &&
				((deviceid == ASC_PCI_DEVICE_ID_1100) ||
				 (deviceid == ASC_PCI_DEVICE_ID_1200) ||
				 (deviceid == ASC_PCI_DEVICE_ID_1300))) {
				pciDevice->slotFound = lslot;
				ASC_DBG(3, "asc_pci_find_dev: PCI_DEVICE_FOUND\n");
				return PCI_DEVICE_FOUND;
			} else {
				pciData.offset = SUBCLASS_OFFSET;
				subclass = asc_get_cfg_byte(&pciData);
				pciData.offset = CLASSCODE_OFFSET;
				classcode = asc_get_cfg_byte(&pciData);
				if ((classcode & PCI_BASE_CLASS_BRIDGE_DEVICE) &&
					(subclass & PCI_SUB_CLASS_PCI_TO_PCI_BRIDGE_CONTROLLER)) {
					pciDevice->bridge++;
				}
				ASC_DBG2(3, "asc_pci_find_dev: subclass %x, classcode %x\n",
					subclass, classcode);
			}
		}
	}
	return PCI_DEVICE_NOT_FOUND;
}

/*
 * Read PCI configuration data into 'pciConfig'.
 */
STATIC void
asc_get_pci_cfg(PCI_DEVICE *pciDevice, PCI_CONFIG_SPACE *pciConfig)
{
	PCI_DATA pciData;
	uchar counter;
	uchar *localConfig;

	ASC_DBG1(4, "asc_get_pci_cfg: slot found - %d\n ",
		pciDevice->slotFound);

	pciData.type = pciDevice->type;
	pciData.bus = pciDevice->busNumber;
	pciData.slot = pciDevice->slotFound;
	pciData.func = pciDevice->devFunc;
	localConfig = (uchar *) pciConfig;

	for (counter = 0; counter < sizeof(PCI_CONFIG_SPACE); counter++) {
		pciData.offset = counter;
		*localConfig = asc_get_cfg_byte(&pciData);
		ASC_DBG1(4, "asc_get_pci_cfg: byte %x\n", *localConfig);
		localConfig++;
	}
	ASC_DBG1(4, "asc_get_pci_cfg: counter %d\n", counter);
}

/*
 * Read a word (16 bits) from the PCI configuration space.
 *
 * The configuration mechanism is checked for the correct access method.
 */
STATIC ushort
asc_get_cfg_word(PCI_DATA *pciData)
{
	ushort	tmp;
	ulong	address;
	ulong	lbus = pciData->bus;
	ulong	lslot = pciData->slot;
	ulong	lfunc = pciData->func;
	uchar	t2CFA, t2CF8;
	ulong	t1CF8, t1CFC;

	ASC_DBG4(4, "asc_get_cfg_word: type %d, bus %lu, slot %lu, func %lu\n",
		pciData->type, lbus, lslot, lfunc);

	/*
	 * Check type of configuration mechanism.
	 */
	if (pciData->type == 2) {
		/*
		 * Save registers to be restored later.
		 */
		t2CFA = inp(0xCFA);	/* save PCI bus register */
		t2CF8 = inp(0xCF8);	/* save config space enable register */

		/*
		 * Write the bus and enable registers.
		 */
		/* set for type 1 cycle, if needed */
		outp(0xCFA, pciData->bus);
		/* set the function number */
		outp(0xCF8, 0x10 | (pciData->func << 1)) ;

		/*
		 * Read the configuration space type 2 locations.
		 */
		tmp = (ushort) inpw(0xC000 | ((pciData->slot << 8) + pciData->offset));

		outp(0xCFA, t2CFA);	/* save PCI bus register */
		outp(0xCF8, t2CF8);	/* save config space enable register */
	} else {
		/*
		 * Type 1 or 3 configuration mechanism.
		 *
		 * Save the CONFIG_ADDRESS and CONFIG_DATA register values.
		 */
		t1CF8 = inpl(0xCF8);
		t1CFC = inpl(0xCFC);

		/*
		 * enable <31>, bus = <23:16>, slot = <15:11>,
		 * func = <10:8>, reg = <7:2>
		 */
		address = (ulong) ((lbus << 16) | (lslot << 11) |
			(lfunc << 8) | (pciData->offset & 0xFC) | 0x80000000L);

		/*
		 * Write out the address to CONFIG_ADDRESS.
		 */
		outpl(0xCF8, address);

		/*
		 * Read in word from CONFIG_DATA.
		 */
		tmp = (ushort) ((inpl(0xCFC) >>
				 ((pciData->offset & 2) * 8)) & 0xFFFF);

		/*
		 * Restore registers.
		 */
		outpl(0xCF8, t1CF8);
		outpl(0xCFC, t1CFC);
	}
	ASC_DBG1(4, "asc_get_cfg_word: config data: %x\n", tmp);
	return tmp;
}

/*
 * Reads a byte from the PCI configuration space.
 *
 * The configuration mechanism is checked for the correct access method.
 */
STATIC uchar
asc_get_cfg_byte(PCI_DATA *pciData)
{
	uchar tmp;
	ulong address;
	ulong lbus = pciData->bus, lslot = pciData->slot, lfunc = pciData->func;
	uchar t2CFA, t2CF8;
	ulong t1CF8, t1CFC;

	ASC_DBG1(4, "asc_get_cfg_byte: type: %d\n", pciData->type);

	/*
	 * Check type of configuration mechanism.
	 */
	if (pciData->type == 2) {
		/*
		 * Save registers to be restored later.
		 */
		t2CFA = inp(0xCFA);	/* save PCI bus register */
		t2CF8 = inp(0xCF8);	/* save config space enable register */

		/*
		 * Write the bus and enable registers.
		 */
		/* set for type 1 cycle, if needed */
		outp(0xCFA, pciData->bus);
		/* set the function number */
		outp(0xCF8, 0x10 | (pciData->func << 1));

		/*
		 * Read configuration space type 2 locations.
		 */
		tmp = inp(0xC000 | ((pciData->slot << 8) + pciData->offset));

		/*
		 * Restore registers.
		 */
		outp(0xCF8, t2CF8);	/* restore the enable register */
		outp(0xCFA, t2CFA);	/* restore PCI bus register */
	} else {
		/*
		 * Type 1 or 3 configuration mechanism.
		 *
		 * Save CONFIG_ADDRESS and CONFIG_DATA register values.
		 */
		t1CF8 = inpl(0xCF8);
		t1CFC = inpl(0xCFC);

		/*
		 * enable <31>, bus = <23:16>, slot = <15:11>, func = <10:8>,
		 * reg = <7:2>
		 */
		address = (ulong) ((lbus << 16) | (lslot << 11) |
			(lfunc << 8) | (pciData->offset & 0xFC) | 0x80000000L);

		/*
		 * Write out address to CONFIG_ADDRESS.
		 */
		outpl(0xCF8, address);

		/*
		 * Read in word from CONFIG_DATA.
		 */
		tmp = (uchar) ((inpl(0xCFC) >> ((pciData->offset & 3) * 8)) & 0xFF);

		/*
		 * Restore registers.
		 */
		outpl(0xCF8, t1CF8);
		outpl(0xCFC, t1CFC);
	}
	ASC_DBG1(4, "asc_get_cfg_byte: config data: %x\n", tmp);
	return tmp;
}

/*
 * Write a byte to the PCI configuration space.
 */
void
asc_put_cfg_byte(PCI_DATA *pciData, uchar byte_data)
{
	ulong tmpl;
	ulong address;
	ulong lbus = pciData->bus, lslot = pciData->slot, lfunc = pciData->func;
	uchar t2CFA, t2CF8;
	ulong t1CF8, t1CFC;

	ASC_DBG2(4, "asc_put_cfg_byte: type: %d, byte_data %x\n",
		pciData->type, byte_data);

	/*
	 * Check type of configuration mechanism.
	 */
	if (pciData->type == 2) {

		/*
		 * Save registers to be restored later.
		 */
		t2CFA = inp(0xCFA);	/* save PCI bus register */
		t2CF8 = inp(0xCF8);	/* save config space enable register */

		/*
		 * Write bus and enable registers.
		 */
		outp(0xCFA, pciData->bus);

		/*
		 * Set the function number.
		 */
		outp(0xCF8, 0x10 | (pciData->func << 1));

		/*
		 * Write the configuration space type 2 locations.
		 */
		outp(0xC000 | ((pciData->slot << 8) + pciData->offset), byte_data);

		/*
		 * Restore registers.
		 */
		outp(0xCF8, t2CF8);	/* restore the enable register */
		outp(0xCFA, t2CFA);	/* restore PCI bus register	*/
	} else {

		/*
		 * Type 1 or 3 configuration mechanism.
		 *
		 * Save the CONFIG_ADDRESS and CONFIG_DATA register values.
		 */
		t1CF8 = inpl(0xCF8);
		t1CFC = inpl(0xCFC);

		/*
		 * enable <31>, bus = <23:16>, slot = <15:11>, func = <10:8>,
		 * reg = <7:2>
		 */
		address = (ulong) ((lbus << 16) | (lslot << 11) | (lfunc << 8) |
				(pciData->offset & 0xFC) | 0x80000000L);
		/*
		 * Write out address to CONFIG_ADDRESS.
		 */
		outpl(0xCF8, address);

		/*
		 * Write double word to CONFIG_DATA preserving the bytes
		 * in the double not written.
		 */
		tmpl = inpl(0xCFC) & ~(0xFF << ((pciData->offset & 3) * 8));
		outpl(0xCFC, tmpl | (byte_data << ((pciData->offset & 3) * 8)));

		/*
		 * Restore registers.
		 */
		outpl(0xCF8, t1CF8);
		outpl(0xCFC, t1CFC);
	}
	ASC_DBG(4, "asc_put_cfg_byte: end\n");
}

/*
 * Add a 'REQP' to the end of specified queue. Set 'tidmask'
 * to indicate a command is queued for the device.
 *
 * 'flag' may be either ASC_FRONT or ASC_BACK.
 *
 * 'REQPNEXT(reqp)' returns reqp's next pointer.
 */
void
asc_enqueue(asc_queue_t *ascq, REQP reqp, int flag)
{
	REQP	*reqpp;
	int		tid;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ASC_DBG3(2, "asc_enqueue: ascq %x, reqp %x, flag %d\n",
		(unsigned) ascq, (unsigned) reqp, flag);
	tid = REQPTID(reqp);
	ASC_ASSERT(flag == ASC_FRONT || flag == ASC_BACK);
	if (flag == ASC_FRONT) {
		REQPNEXT(reqp) = ascq->queue[tid];
		ascq->queue[tid] = reqp;
	} else { /* ASC_BACK */
		for (reqpp = &ascq->queue[tid]; *reqpp; reqpp = REQPNEXTP(*reqpp)) {
			ASC_ASSERT(ascq->tidmask & ASC_TIX_TO_TARGET_ID(tid));
			;
		}
		*reqpp = reqp;
		REQPNEXT(reqp) = NULL;
	}
	/* The queue has at least one entry, set its bit. */
	ascq->tidmask |= ASC_TIX_TO_TARGET_ID(tid);
#ifdef ADVANSYS_STATS
	/*
	 * Maintain request queue statistics.
	 */
	ascq->cur_count[tid]++;
	if (ascq->cur_count[tid] > ascq->max_count[tid]) {
		ascq->max_count[tid] = ascq->cur_count[tid];
		ASC_DBG2(1, "asc_enqueue: new max_count[%d] %d\n",
			tid, ascq->max_count[tid]);
	}
#endif /* ADVANSYS_STATS */
	ASC_DBG1(1, "asc_enqueue: reqp %x\n", (unsigned) reqp);
	return;
}

/*
 * Return first queued 'REQP' on the specified queue for
 * the specified target device. Clear the 'tidmask' bit for
 * the device if no more commands are left queued for it.
 *
 * 'REQPNEXT(reqp)' returns reqp's next pointer.
 */
REQP
asc_dequeue(asc_queue_t *ascq, int tid)
{
	REQP	reqp;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ASC_DBG2(1, "asc_dequeue: ascq %x, tid %d\n", (unsigned) ascq, tid);
	if ((reqp = ascq->queue[tid]) != NULL) {
		ASC_ASSERT(ascq->tidmask & ASC_TIX_TO_TARGET_ID(tid));
		ascq->queue[tid] = REQPNEXT(reqp);
		/* If the queue is empty, clear its bit. */
		if (ascq->queue[tid] == NULL) {
			ascq->tidmask &= ~ASC_TIX_TO_TARGET_ID(tid);
		}
	}
#ifdef ADVANSYS_STATS
	/*
	 * Maintain request queue statistics.
	 */
	if (reqp != NULL) {
		ascq->cur_count[tid]--;
	}
	ASC_ASSERT(ascq->cur_count[tid] >= 0);
#endif /* ADVANSYS_STATS */
	ASC_DBG1(1, "asc_dequeue: reqp %x\n", (unsigned) reqp);
	return reqp;
}

/*
 * Remove the specified 'REQP' from the specified queue for
 * the specified target device. Clear the 'tidmask' bit for the
 * device if no more commands are left queued for it.
 *
 * 'REQPNEXT(reqp)' returns reqp's the next pointer.
 *
 * Return ASC_TRUE if the command was found and removed,
 * otherwise return ASC_FALSE.
 */
int
asc_rmqueue(asc_queue_t *ascq, REQP reqp)
{
	REQP		*reqpp;
	int			tid;
	int			ret;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ret = ASC_FALSE;
	tid = REQPTID(reqp);
	for (reqpp = &ascq->queue[tid]; *reqpp; reqpp = REQPNEXTP(*reqpp)) {
		ASC_ASSERT(ascq->tidmask & ASC_TIX_TO_TARGET_ID(tid));
		if (*reqpp == reqp) {
			ret = ASC_TRUE;
			*reqpp = REQPNEXT(reqp);
			REQPNEXT(reqp) = NULL;
			/* If the queue is now empty, clear its bit. */
			if (ascq->queue[tid] == NULL) {
				ascq->tidmask &= ~ASC_TIX_TO_TARGET_ID(tid);
			}
			break; /* Note: *reqpp may now be NULL, don't iterate. */
		}
	}
#ifdef ADVANSYS_STATS
	/*
	 * Maintain request queue statistics.
	 */
	if (ret == ASC_TRUE) {
		ascq->cur_count[tid]--;
	}
	ASC_ASSERT(ascq->cur_count[tid] >= 0);
#endif /* ADVANSYS_STATS */
	ASC_DBG2(1, "asc_rmqueue: reqp %x, ret %d\n", (unsigned) reqp, ret);
	return ret;
}

/*
 * If the specified 'REQP' is queued on the specified queue for
 * the specified target device, return ASC_TRUE.
 */
int
asc_isqueued(asc_queue_t *ascq, REQP reqp)
{
	REQP		*reqpp;
	int			tid;
	int			ret;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ret = ASC_FALSE;
	tid = REQPTID(reqp);
	for (reqpp = &ascq->queue[tid]; *reqpp; reqpp = REQPNEXTP(*reqpp)) {
		ASC_ASSERT(ascq->tidmask & ASC_TIX_TO_TARGET_ID(tid));
		if (*reqpp == reqp) {
			ret = ASC_TRUE;
			break;
		}
	}
	return ret;
}

/*
 * Execute as many queued requests as possible for the specified queue.
 *
 * Calls asc_execute_scsi_cmnd() to execute a REQP/Scsi_Cmnd.
 */
void
asc_execute_queue(asc_queue_t *ascq)
{
	ASC_SCSI_BIT_ID_TYPE	scan_tidmask;
	REQP					reqp;
	int						i;

	ASC_ASSERT(interrupts_enabled() == ASC_FALSE);
	ASC_DBG1(1, "asc_execute_queue: ascq %x\n", (unsigned) ascq);
	/*
	 * Execute queued commands for devices attached to
	 * the current board in round-robin fashion.
	 */
	scan_tidmask = ascq->tidmask;
	do {
		for (i = 0; i <= ASC_MAX_TID; i++) {
			if (scan_tidmask & ASC_TIX_TO_TARGET_ID(i)) {
				if ((reqp = asc_dequeue(ascq, i)) == NULL) {
					scan_tidmask &= ~ASC_TIX_TO_TARGET_ID(i);
				} else if (asc_execute_scsi_cmnd((Scsi_Cmnd *) reqp)
							== ASC_BUSY) {
					scan_tidmask &= ~ASC_TIX_TO_TARGET_ID(i);
					/* Put the request back at front of the list. */
					asc_enqueue(ascq, reqp, ASC_FRONT);
				}
			}
		}
	} while (scan_tidmask);
	return;
}

/*
 * asc_prt_board_devices()
 *
 * Print driver information for devices attached to the board.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_devices(struct Scsi_Host *shp, char *cp, int cplen)
{
	struct asc_board	*boardp;
	int					leftlen;
	int					totlen;
	int					len;
	int					i;

	boardp = ASC_BOARDP(shp);
	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
"\nDevice Information for AdvanSys SCSI Host %d:\n", shp->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, "Target Ids Detected:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if (boardp->asc_dvc_cfg.chip_scsi_id == i) {
			continue;
		} else if (boardp->init_tidmask & (1 << i)) {
			len = asc_prt_line(cp, leftlen, " %d,", i);
			ASC_PRT_NEXT();
		}
	}
	len = asc_prt_line(cp, leftlen, " (%d=Host Adapter)\n",
					   boardp->asc_dvc_cfg.chip_scsi_id);
	ASC_PRT_NEXT();

 	return totlen;
}

/*
 * asc_prt_board_eeprom()
 *
 * Print board EEPROM configuration.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_eeprom(struct Scsi_Host *shp, char *cp, int cplen)
{
	struct asc_board	*boardp;
	ASC_DVC_VAR			*asc_dvc_varp;
	int					leftlen;
	int					totlen;
	int					len;
	ASCEEP_CONFIG       *ep;
	int					i;
	int					isa_dma_speed[] = { 10, 8, 7, 6, 5, 4, 3, 2 };

	boardp = ASC_BOARDP(shp);
	asc_dvc_varp = &boardp->asc_dvc_var;
	ep = &boardp->eep_config;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
"\nEEPROM Settings for AdvanSys SCSI Host %d:\n", shp->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
		ep->chip_scsi_id, ep->max_total_qng, ep->max_tag_qng);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Disconnects:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (ep->disc_enable & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Command Queuing:     ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (ep->use_cmd_qng & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Start Motor:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (ep->start_motor & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Synchronous Transfer:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (ep->init_sdtr & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
		len = asc_prt_line(cp, leftlen,
" Host ISA DMA speed:   %d MB/S\n",
			isa_dma_speed[ep->isa_dma_speed]);
		ASC_PRT_NEXT();
	}

 	return totlen;
}

/*
 * asc_prt_board_info()
 *
 * Print dynamic board configuration information.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_info(struct Scsi_Host *shp, char *cp, int cplen)
{
	struct asc_board	*boardp;
	int					leftlen;
	int					totlen;
	int					len;
	ASC_DVC_VAR			*v;
	ASC_DVC_CFG			*c;
	int					i;
#ifdef ADVANSYS_STATS
	struct asc_stats	*s;
#endif /* ADVANSYS_STATS */

	boardp = ASC_BOARDP(shp);
	v = &boardp->asc_dvc_var;
	c = &boardp->asc_dvc_cfg;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
"\nAsc Library Configuration and Statistics for AdvanSys SCSI Host %d:\n",
	shp->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" chip_version %u, lib_version %u, lib_serial_no %u mcode_date %u\n",
		c->chip_version, c->lib_version, c->lib_serial_no, c->mcode_date);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" mcode_version %u, err_code %u\n",
		 c->mcode_version, v->err_code);
	ASC_PRT_NEXT();

	/* Current number of commands pending for the host. */
	len = asc_prt_line(cp, leftlen,
" Total Command Pending:    %d\n", v->cur_total_qng);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Synchronous Transfer:    ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((boardp->asc_dvc_cfg.chip_scsi_id == i) ||
		    ((boardp->init_tidmask & (1 << i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (v->sdtr_done & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" Command Queuing:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((boardp->asc_dvc_cfg.chip_scsi_id == i) ||
		    ((boardp->init_tidmask & (1 << i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %d:%c",
			i, (v->use_tagged_qng & (1 << i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	/* Current number of commands pending for a device. */
	len = asc_prt_line(cp, leftlen,
" Command Queue Pending:   ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((boardp->asc_dvc_cfg.chip_scsi_id == i) ||
		    ((boardp->init_tidmask & (1 << i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %d:%u", i, v->cur_dvc_qng[i]);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	/* Current limit on number of commands that can be sent to a device. */
	len = asc_prt_line(cp, leftlen,
" Command Queue Limit:     ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((boardp->asc_dvc_cfg.chip_scsi_id == i) ||
		    ((boardp->init_tidmask & (1 << i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %d:%u", i, v->max_dvc_qng[i]);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

#ifdef ADVANSYS_STATS
	s = &boardp->asc_stats;

	/* Indicate whether the device has returned queue full status. */
	len = asc_prt_line(cp, leftlen,
" Command Queue Full:      ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((boardp->asc_dvc_cfg.chip_scsi_id == i) ||
		    ((boardp->init_tidmask & (1 << i)) == 0)) {
			continue;
		}
		if (s->queue_full & (1 << i)) {
			len = asc_prt_line(cp, leftlen, " %d:Y-%d",
				i, s->queue_full_cnt[i]);
		} else {
			len = asc_prt_line(cp, leftlen, " %d:N", i);
		}
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();
#endif /* ADVANSYS_STATS */

 	return totlen;
}

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(1,3,0)
/*
 * asc_proc_copy() 
 *
 * Copy proc information to a read buffer taking into account the current
 * read offset in the file and the remaining space in the read buffer.
 */
STATIC int
asc_proc_copy(off_t advoffset, off_t offset, char *curbuf, int leftlen,
			  char *cp, int cplen)
{
	int cnt = 0;
	
	ASC_DBG3(2, "asc_proc_copy: offset %d, advoffset %d, cplen %d\n",
			(unsigned) offset, (unsigned) advoffset, cplen);
	if (offset <= advoffset) {
		/* Read offset below current offset, copy everything. */
		cnt = ASC_MIN(cplen, leftlen);
		ASC_DBG3(2, "asc_proc_copy: curbuf %x, cp %x, cnt %d\n",
				(unsigned) curbuf, (unsigned) cp, cnt);
		memcpy(curbuf, cp, cnt);
	} else if (offset < advoffset + cplen) {
		/* Read offset within current range, partial copy. */
		cnt = (advoffset + cplen) - offset;
		cp = (cp + cplen) - cnt;
		cnt = ASC_MIN(cnt, leftlen);
		ASC_DBG3(2, "asc_proc_copy: curbuf %x, cp %x, cnt %d\n",
				(unsigned) curbuf, (unsigned) cp, cnt);
		memcpy(curbuf, cp, cnt);
	}
	return cnt;
}
#endif /* version >= v1.3.0 */

/*
 * asc_prt_line()
 *
 * If 'cp' is NULL print to the console, otherwise print to a buffer.
 *
 * Return 0 if printing to the console, otherwise return the number of
 * bytes written to the buffer.
 *
 * Note: If any single line is greater than ASC_PRTLINE_SIZE bytes the stack
 * will be corrupted. 's[]' is defined to be ASC_PRTLINE_SIZE bytes.
 */
int
asc_prt_line(char *buf, int buflen, char *fmt, ...)
{
	va_list		args;
	int			ret;
	char		s[ASC_PRTLINE_SIZE];

	va_start(args, fmt);
	ret = vsprintf(s, fmt, args);
	ASC_ASSERT(ret < ASC_PRTLINE_SIZE);
	if (buf == NULL) {
		(void) printk(s);
		ret = 0;
	} else {
		ret = ASC_MIN(buflen, ret);
		memcpy(buf, s, ret);
	}
	va_end(args);
	return ret;
}


/*
 * --- Functions Required by the Asc Library
 */

/*
 * Delay for 'n' milliseconds. Don't use the 'jiffies'
 * global variable which is incremented once every 5 ms
 * from a timer interrupt, because this function may be
 * called when interrupts are disabled.
 */
void
DvcSleepMilliSecond(ulong n)
{
	ulong i;

	ASC_DBG1(4, "DvcSleepMilliSecond: %lu\n", n);
	for (i = 0; i < n; i++) {
		udelay(1000);
	}
}

void
DvcDisplayString(uchar *s)
{
	printk(s);
}

int
DvcEnterCritical(void)
{
	int	flags;

	save_flags(flags);
	cli();
	return flags;
}

void
DvcLeaveCritical(int flags)
{
	restore_flags(flags);
}

/*
 * Convert a virtual address to a virtual address.
 *
 * Apparently Linux is loaded V=R (virtual equals real). Just return
 * the virtual address.
 */
ulong
DvcGetPhyAddr(uchar *buf_addr, ulong buf_len)
{
	ulong phys_addr;

	phys_addr = (ulong) buf_addr;
	return phys_addr;
}

ulong
DvcGetSGList(ASC_DVC_VAR *asc_dvc_sg, uchar *buf_addr, ulong buf_len,
			 ASC_SG_HEAD *asc_sg_head_ptr)
{
	ulong buf_size;

	buf_size = buf_len;
	asc_sg_head_ptr->entry_cnt = 1;
	asc_sg_head_ptr->sg_list[0].addr = (ulong) buf_addr;
	asc_sg_head_ptr->sg_list[0].bytes = buf_size;
	return buf_size;
}

/*
 * void
 * DvcPutScsiQ(PortAddr iop_base, ushort s_addr, ushort *outbuf, int words)
 *
 * Calling/Exit State:
 *	none
 *
 * Description:
 * 	Output an ASC_SCSI_Q structure to the chip
 */
void
DvcPutScsiQ(PortAddr iop_base, ushort s_addr, ushort *outbuf, int words)
{
	int	i;

	ASC_DBG_PRT_HEX(2, "DvcPutScsiQ", (uchar *) outbuf, 2 * words);
	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < words; i++, outbuf++) {
		if (i == 2 || i == 10) {
			continue;
		}
		AscSetChipLramDataNoSwap(iop_base, *outbuf);
	}
}

/*
 * void
 * DvcGetQinfo(PortAddr iop_base, ushort s_addr, ushort *inbuf, int words)
 *
 * Calling/Exit State:
 *	none
 *
 * Description:
 * 	Input an ASC_QDONE_INFO structure from the chip
 */
void
DvcGetQinfo(PortAddr iop_base, ushort s_addr, ushort *inbuf, int words)
{
	int	i;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < words; i++, inbuf++) {
		if (i == 5) {
			continue;
		}
		*inbuf = AscGetChipLramDataNoSwap(iop_base);
	}
	ASC_DBG_PRT_HEX(2, "DvcGetQinfo", (uchar *) inbuf, 2 * words);
}

/*
 * void	DvcOutPortWords(ushort iop_base, ushort &outbuf, int words)
 *
 * Calling/Exit State:
 *	none
 *
 * Description:
 * 	output a buffer to an i/o port address
 */
void
DvcOutPortWords(ushort iop_base, ushort *outbuf, int words)
{
	int	i;

	for (i = 0; i < words; i++, outbuf++)
		outpw(iop_base, *outbuf);
}

/*
 * void	DvcInPortWords(ushort iop_base, ushort &outbuf, int words)
 *
 * Calling/Exit State:
 *	none
 *
 * Description:
 * 	input a buffer from an i/o port address
 */
void
DvcInPortWords(ushort iop_base, ushort *inbuf, int words)
{
	int	i;

	for (i = 0; i < words; i++, inbuf++)
		*inbuf = inpw(iop_base);
}


/*
 * void DvcOutPortDWords(PortAddr port, ulong *pdw, int dwords)
 *
 * Calling/Exit State:
 *	none
 *
 * Description:
 * 	output a buffer of 32-bit integers to an i/o port address in
 *  16 bit integer units
 */
void  
DvcOutPortDWords(PortAddr port, ulong *pdw, int dwords)
{
	int		i;
	int		words;
	ushort	*pw;

	pw = (ushort *) pdw;
	words = dwords << 1;
	for(i = 0; i < words; i++, pw++) {
		outpw(port, *pw);
	}
	return;
}

/*
 * Read a PCI configuration byte.
 */
uchar
DvcReadPCIConfigByte( 
        ASC_DVC_VAR asc_ptr_type *asc_dvc, 
        ushort offset )
{
    PCI_DATA	pciData;

    pciData.bus = ASC_PCI_ID2BUS(asc_dvc->cfg->pci_slot_info);
    pciData.slot = ASC_PCI_ID2DEV(asc_dvc->cfg->pci_slot_info);
    pciData.func = ASC_PCI_ID2FUNC(asc_dvc->cfg->pci_slot_info);
    pciData.offset = offset;
    pciData.type = pci_scan_method;
    return asc_get_cfg_byte(&pciData);
}

/*
 * Write a PCI configuration byte.
 */
void
DvcWritePCIConfigByte(
        ASC_DVC_VAR asc_ptr_type *asc_dvc, 
        ushort offset, 
        uchar  byte_data )
{
    PCI_DATA	pciData;

    pciData.bus = ASC_PCI_ID2BUS(asc_dvc->cfg->pci_slot_info);
    pciData.slot = ASC_PCI_ID2DEV(asc_dvc->cfg->pci_slot_info);
    pciData.func = ASC_PCI_ID2FUNC(asc_dvc->cfg->pci_slot_info);
    pciData.offset = offset;
    pciData.type = pci_scan_method;
    asc_put_cfg_byte(&pciData, byte_data);
}

/*
 * Return the BIOS address of the adatper at the specified
 * I/O port and with the specified bus type.
 *
 * This function was formerly supplied by the library.
 */
ushort
AscGetChipBiosAddress(
        PortAddr iop_base,
        ushort bus_type
    )
{
    ushort  cfg_lsw ;
    ushort  bios_addr ;

    /*
     *   We can't get the BIOS address for PCI
     */
    if ( bus_type & ASC_IS_PCI )
    {
        return( 0 );
    }

    if( ( bus_type & ASC_IS_EISA ) != 0 )
    {
        cfg_lsw = AscGetEisaChipCfg( iop_base ) ;
        cfg_lsw &= 0x000F ;
        bios_addr = ( ushort )( ASC_BIOS_MIN_ADDR  +
                                ( cfg_lsw * ASC_BIOS_BANK_SIZE ) ) ;
        return( bios_addr ) ;
    }/* if */

    cfg_lsw = AscGetChipCfgLsw( iop_base ) ;

    /*
    *  ISA PnP uses the top bit as the 32K BIOS flag
    */
    if ( bus_type == ASC_IS_ISAPNP )
    {
        cfg_lsw &= 0x7FFF;
    }/* if */

    bios_addr = ( ushort )( ( ( cfg_lsw >> 12 ) * ASC_BIOS_BANK_SIZE ) +
			ASC_BIOS_MIN_ADDR ) ;
    return( bios_addr ) ;
}


/*
 * --- Tracing and Debugging Functions
 */

#ifdef ADVANSYS_STATS
/*
 * asc_prt_board_stats()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_stats(struct Scsi_Host *shp, char *cp, int cplen)
{
	int					leftlen;
	int					totlen;
	int					len;
	struct asc_stats	*s;
	int					i;
	asc_queue_t			*active;
	asc_queue_t			*pending;

	leftlen = cplen;
	totlen = len = 0;

	s = &ASC_BOARDP(shp)->asc_stats;
	len = asc_prt_line(cp, leftlen,
"\nLinux Driver Statistics for AdvanSys SCSI Host %d:\n", shp->host_no);
	ASC_PRT_NEXT();
	
	len = asc_prt_line(cp, leftlen,
" command %lu, queuecommand %lu, abort %lu, reset %lu, biosparam %lu\n",
		s->command, s->queuecommand, s->abort, s->reset, s->biosparam);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" check_interrupt %lu, interrupt %lu, callback %lu\n",
		s->check_interrupt, s->interrupt, s->callback);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
" asc_noerror %lu, asc_busy %lu, asc_error %lu, asc_unknown %lu\n",
		s->asc_noerror, s->asc_busy, s->asc_error, s->asc_unknown);
	ASC_PRT_NEXT();

	/*
	 * Display request queuing statistics.
	 */
	len = asc_prt_line(cp, leftlen,
" Active and Pending Request Queues:\n");
	ASC_PRT_NEXT();

	active = &ASC_BOARDP(shp)->active;
	pending = &ASC_BOARDP(shp)->pending;
	for (i = 0; i < ASC_MAX_TID + 1; i++) {
		if (active->max_count[i] > 0 || pending->max_count[i] > 0) {
			len = asc_prt_line(cp, leftlen,
"  target %d: active [cur %d, max %d], pending [cur %d, max %d]\n",
				i, active->cur_count[i], active->max_count[i],
				pending->cur_count[i], pending->max_count[i]);
			ASC_PRT_NEXT();
		}
	}

	/*
	 * Display data transfer statistics.
	 */
	if (s->cont_cnt > 0) {
		len = asc_prt_line(cp, leftlen, " cont_cnt %lu, ", s->cont_cnt);
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "cont_xfer %lu.%01lu kb ",
					s->cont_xfer/2,
					ASC_TENTHS(s->cont_xfer, 2));
		ASC_PRT_NEXT();

		/* Contiguous transfer average size */
		len = asc_prt_line(cp, leftlen, "avg_xfer %lu.%01lu kb\n",
					(s->cont_xfer/2)/s->cont_cnt,
					ASC_TENTHS((s->cont_xfer/2), s->cont_cnt));
		ASC_PRT_NEXT();
	}

	if (s->sg_cnt > 0) {

		len = asc_prt_line(cp, leftlen, " sg_cnt %lu, sg_elem %lu, ",
					s->sg_cnt, s->sg_elem);
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "sg_xfer %lu.%01lu kb\n",
					s->sg_xfer/2,
					ASC_TENTHS(s->sg_xfer, 2));
		ASC_PRT_NEXT();

		/* Scatter gather transfer statistics */
		len = asc_prt_line(cp, leftlen, " avg_num_elem %lu.%01lu, ",
					s->sg_elem/s->sg_cnt,
					ASC_TENTHS(s->sg_elem, s->sg_cnt));
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "avg_elem_size %lu.%01lu kb, ",
					(s->sg_xfer/2)/s->sg_elem,
					ASC_TENTHS((s->sg_xfer/2), s->sg_elem));
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "avg_xfer_size %lu.%01lu kb\n",
					(s->sg_xfer/2)/s->sg_cnt,
					ASC_TENTHS((s->sg_xfer/2), s->sg_cnt));
		ASC_PRT_NEXT();
	}

 	return totlen;
}
#endif /* ADVANSYS_STATS */

#ifdef ADVANSYS_DEBUG
/*
 * asc_prt_scsi_host()
 */
STATIC void 
asc_prt_scsi_host(struct Scsi_Host *s)
{
	printk("Scsi_Host at addr %x\n", (unsigned) s);
	printk(
" next %x, extra_bytes %u, host_busy %u, host_no %d, last_reset %d,\n",
		(unsigned) s->next, s->extra_bytes, s->host_busy, s->host_no,
		(unsigned) s->last_reset);

	printk(
" host_wait %x, host_queue %x, hostt %x, block %x,\n",
		(unsigned) s->host_wait, (unsigned) s->host_queue,
		(unsigned) s->hostt, (unsigned) s->block);

	printk(
" wish_block %d, base %x, io_port %d, n_io_port %d, irq %d, dma_channel %d,\n",
		s->wish_block, (unsigned) s->base, s->io_port, s->n_io_port,
		s->irq, s->dma_channel);

	printk(
" this_id %d, can_queue %d,\n", s->this_id, s->can_queue);

	printk(
" cmd_per_lun %d, sg_tablesize %d, unchecked_isa_dma %d, loaded_as_module %d\n",
		s->cmd_per_lun, s->sg_tablesize, s->unchecked_isa_dma,
		s->loaded_as_module);

	asc_prt_dvc_var(&ASC_BOARDP(s)->asc_dvc_var);
	asc_prt_dvc_cfg(&ASC_BOARDP(s)->asc_dvc_cfg);
}

/*
 * asc_prt_dvc_var()
 */
STATIC void 
asc_prt_dvc_var(ASC_DVC_VAR *h)
{
	printk("ASC_DVC_VAR at addr %x\n", (unsigned) h);

	printk(
" iop_base %x, err_code %x, dvc_cntl %x, bug_fix_cntl %d,\n",
		h->iop_base, h->err_code, h->dvc_cntl, h->bug_fix_cntl);

	printk(
" bus_type %d, isr_callback %x, exe_callback %x, init_sdtr %x,\n",
		h->bus_type, (unsigned) h->isr_callback, (unsigned) h->exe_callback,
		(unsigned) h->init_sdtr);

	printk(
" sdtr_done %x, use_tagged_qng %x, unit_not_ready %x, chip_no %x,\n",
		(unsigned) h->sdtr_done, (unsigned) h->use_tagged_qng,
		(unsigned) h->unit_not_ready, (unsigned) h->chip_no);
		
	printk(
" queue_full_or_busy %x, start_motor %x, scsi_reset_wait %x, irq_no %x,\n",
		(unsigned) h->queue_full_or_busy, (unsigned) h->start_motor,
		(unsigned) h->scsi_reset_wait, (unsigned) h->irq_no);

	printk(
" is_in_int %x, max_total_qng %x, cur_total_qng %x, in_critical_cnt %x,\n",
		(unsigned) h->is_in_int, (unsigned) h->max_total_qng,
		(unsigned) h->cur_total_qng, (unsigned) h->in_critical_cnt);

	printk(
" last_q_shortage %x, init_state %x, no_scam %x, pci_fix_asyn_xfer %x,\n",
		(unsigned) h->last_q_shortage, (unsigned) h->init_state,
		(unsigned) h->no_scam, (unsigned) h->pci_fix_asyn_xfer);

	printk(
" cfg %x, saved_ptr2func %x\n",
		(unsigned) h->cfg, (unsigned) h->saved_ptr2func);
}

/*
 * asc_prt_dvc_cfg()
 */
STATIC void 
asc_prt_dvc_cfg(ASC_DVC_CFG *h)
{
	printk("ASC_DVC_CFG at addr %x\n", (unsigned) h);

	printk(
" can_tagged_qng %x, cmd_qng_enabled %x, disc_enable %x, res %x,\n",
			h->can_tagged_qng, h->cmd_qng_enabled, h->disc_enable, h->res);

	printk(
" chip_scsi_id %d, isa_dma_speed %d, isa_dma_channel %d, chip_version %d,\n",
			 h->chip_scsi_id, h->isa_dma_speed, h->isa_dma_channel,
			 h->chip_version);

	printk(
" pci_device_id %d, lib_serial_no %d, lib_version %d, mcode_date %d,\n",
		  h->pci_device_id, h->lib_serial_no, h->lib_version, h->mcode_date);

	printk(
" mcode_version %d, overrun_buf %x\n",
			h->mcode_version, (unsigned) h->overrun_buf);
}

/*
 * asc_prt_scsi_q()
 */
STATIC void 
asc_prt_scsi_q(ASC_SCSI_Q *q)
{
	ASC_SG_HEAD	*sgp;
	int i;

	printk("ASC_SCSI_Q at addr %x\n", (unsigned) q);

	printk(
" target_ix %u, target_lun %u, srb_ptr %x, tag_code %u,\n",
			q->q2.target_ix, q->q1.target_lun,
			(unsigned) q->q2.srb_ptr, q->q2.tag_code);

	printk(
" data_addr %x, data_cnt %lu, sense_addr %x, sense_len %u,\n",
			(unsigned) q->q1.data_addr, q->q1.data_cnt,
			(unsigned) q->q1.sense_addr, q->q1.sense_len);

	printk(
" cdbptr %x, cdb_len %u, sg_head %x, sg_queue_cnt %u\n",
			(unsigned) q->cdbptr, q->q2.cdb_len,
			(unsigned) q->sg_head, q->q1.sg_queue_cnt);

	if (q->sg_head) {
		sgp = q->sg_head;
		printk("ASC_SG_HEAD at addr %x\n", (unsigned) sgp);
		printk(" entry_cnt %u, queue_cnt %u\n", sgp->entry_cnt, sgp->queue_cnt);
		for (i = 0; i < sgp->entry_cnt; i++) {
			printk(" [%u]: addr %x, bytes %lu\n",
				i, (unsigned) sgp->sg_list[i].addr, sgp->sg_list[i].bytes);
		}

	}
}

/*
 * asc_prt_qdone_info()
 */
STATIC void 
asc_prt_qdone_info(ASC_QDONE_INFO *q)
{
	printk("ASC_QDONE_INFO at addr %x\n", (unsigned) q);
	printk(
" srb_ptr %x, target_ix %u, cdb_len %u, tag_code %u, done_stat %x\n",
			(unsigned) q->d2.srb_ptr, q->d2.target_ix, q->d2.cdb_len,
			q->d2.tag_code, q->d3.done_stat);
	printk(
" host_stat %x, scsi_stat %x, scsi_msg %x\n",
			q->d3.host_stat, q->d3.scsi_stat, q->d3.scsi_msg);
}

/*
 * asc_prt_hex()
 *
 * Print hexadecimal output in 4 byte groupings 32 bytes 
 * or 8 double-words per line.
 */
STATIC void 
asc_prt_hex(char *f, uchar *s, int l)
{
	int			i;
	int			j;
	int			k;
	int			m;

	printk("%s: (%d bytes)\n", f, l);

	for (i = 0; i < l; i += 32) {
		
		/* Display a maximum of 8 double-words per line. */
		if ((k = (l - i) / 4) >= 8) {
			k = 8;
			m = 0;
		} else {
			m = (l - i) % 4 ;
		}

		for (j = 0; j < k; j++) {
			printk(" %2.2X%2.2X%2.2X%2.2X",
				(unsigned) s[i+(j*4)], (unsigned) s[i+(j*4)+1],
				(unsigned) s[i+(j*4)+2], (unsigned) s[i+(j*4)+3]);
		}

		switch (m) {
		case 0:
		default:
			break;
		case 1:
			printk(" %2.2X",
				(unsigned) s[i+(j*4)]);
			break;
		case 2:
			printk(" %2.2X%2.2X",
				(unsigned) s[i+(j*4)],
				(unsigned) s[i+(j*4)+1]);
			break;
		case 3:
			printk(" %2.2X%2.2X%2.2X",
				(unsigned) s[i+(j*4)+1],
				(unsigned) s[i+(j*4)+2],
				(unsigned) s[i+(j*4)+3]);
			break;
		}

		printk("\n");
	}
}

/*
 * interrupts_enabled()
 *
 * Return 1 if interrupts are enabled, otherwise return 0.
 */
STATIC int
interrupts_enabled(void)
{
	int flags;

	save_flags(flags);
	if (flags & 0x0200) {
		return ASC_TRUE;
	} else {
		return ASC_FALSE;
	}
}

#endif /* ADVANSYS_DEBUG */


/*
 * --- Asc Library Functions
 */

ushort
AscGetEisaChipCfg(
					 PortAddr iop_base
)
{
	PortAddr            eisa_cfg_iop;
	eisa_cfg_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
	  (PortAddr) (ASC_EISA_CFG_IOP_MASK);
	return (inpw(eisa_cfg_iop));
}

uchar
AscSetChipScsiID(
					PortAddr iop_base,
					uchar new_host_id
)
{
	ushort              cfg_lsw;
	if (AscGetChipScsiID(iop_base) == new_host_id) {
		return (new_host_id);
	}
	cfg_lsw = AscGetChipCfgLsw(iop_base);
	cfg_lsw &= 0xF8FF;
	cfg_lsw |= (ushort) ((new_host_id & ASC_MAX_TID) << 8);
	AscSetChipCfgLsw(iop_base, cfg_lsw);
	return (AscGetChipScsiID(iop_base));
}

uchar
AscGetChipScsiCtrl(
					  PortAddr iop_base
)
{
	uchar               sc;
	AscSetBank(iop_base, 1);
	sc = inp(iop_base + IOP_REG_SC);
	AscSetBank(iop_base, 0);
	return (sc);
}

uchar
AscGetChipVersion(
					 PortAddr iop_base,
					 ushort bus_type
)
{
	if ((bus_type & ASC_IS_EISA) != 0) {
		PortAddr            eisa_iop;
		uchar               revision;
		eisa_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
		  (PortAddr) ASC_EISA_REV_IOP_MASK;
		revision = inp(eisa_iop);
		return ((uchar) ((ASC_CHIP_MIN_VER_EISA - 1) + revision));
	}
	return (AscGetChipVerNo(iop_base));
}

ushort
AscGetChipBusType(
					 PortAddr iop_base
)
{
	ushort              chip_ver;
	chip_ver = AscGetChipVerNo(iop_base);
	if (
		   (chip_ver >= ASC_CHIP_MIN_VER_VL)
		   && (chip_ver <= ASC_CHIP_MAX_VER_VL)
	  ) {
		if (
			   ((iop_base & 0x0C30) == 0x0C30)
			   || ((iop_base & 0x0C50) == 0x0C50)
		  ) {
			return (ASC_IS_EISA);
		}
		return (ASC_IS_VL);
	}
	if ((chip_ver >= ASC_CHIP_MIN_VER_ISA) &&
		(chip_ver <= ASC_CHIP_MAX_VER_ISA)) {
		if (chip_ver >= ASC_CHIP_MIN_VER_ISA_PNP) {
			return (ASC_IS_ISAPNP);
		}
		return (ASC_IS_ISA);
	} else if ((chip_ver >= ASC_CHIP_MIN_VER_PCI) &&
			   (chip_ver <= ASC_CHIP_MAX_VER_PCI)) {
		return (ASC_IS_PCI);
	}
	return (0);
}

ulong
AscLoadMicroCode(
					PortAddr iop_base,
					ushort s_addr,
					ushort dosfar * mcode_buf,
					ushort mcode_size
)
{
	ulong               chksum;
	ushort              mcode_word_size;
	ushort              mcode_chksum;
	mcode_word_size = (ushort) (mcode_size >> 1);
	AscMemWordSetLram(iop_base, s_addr, 0, mcode_word_size);
	AscMemWordCopyToLram(iop_base, s_addr, mcode_buf, mcode_word_size);
	chksum = AscMemSumLramWord(iop_base, s_addr, mcode_word_size);
	mcode_chksum = (ushort) AscMemSumLramWord(iop_base,
											  (ushort) ASC_CODE_SEC_BEG,
		  (ushort) ((mcode_size - s_addr - (ushort) ASC_CODE_SEC_BEG) / 2));
	AscWriteLramWord(iop_base, ASCV_MCODE_CHKSUM_W, mcode_chksum);
	AscWriteLramWord(iop_base, ASCV_MCODE_SIZE_W, mcode_size);
	return (chksum);
}

int
AscFindSignature(
					PortAddr iop_base
)
{
	ushort              sig_word;
	if (AscGetChipSignatureByte(iop_base) == (uchar) ASC_1000_ID1B) {
		sig_word = AscGetChipSignatureWord(iop_base);
		if ((sig_word == (ushort) ASC_1000_ID0W) ||
			(sig_word == (ushort) ASC_1000_ID0W_FIX)) {
			return (1);
		}
	}
	return (0);
}

uchar               _isa_pnp_inited = 0;
PortAddr            _asc_def_iop_base[ASC_IOADR_TABLE_MAX_IX] =
{
	0x100, ASC_IOADR_1, 0x120, ASC_IOADR_2, 0x140, ASC_IOADR_3, ASC_IOADR_4,
	ASC_IOADR_5, ASC_IOADR_6, ASC_IOADR_7, ASC_IOADR_8
};

PortAddr
AscSearchIOPortAddr(
					   PortAddr iop_beg,
					   ushort bus_type
)
{
	if (bus_type & ASC_IS_VL) {
		while ((iop_beg = AscSearchIOPortAddr11(iop_beg)) != 0) {
			if (AscGetChipVersion(iop_beg, bus_type) <= ASC_CHIP_MAX_VER_VL) {
				return (iop_beg);
			}
		}
		return (0);
	}
	if (bus_type & ASC_IS_ISA) {
		if (_isa_pnp_inited == 0) {
			AscSetISAPNPWaitForKey();
			_isa_pnp_inited++;
		}
		while ((iop_beg = AscSearchIOPortAddr11(iop_beg)) != 0) {
			if ((AscGetChipVersion(iop_beg, bus_type) & ASC_CHIP_VER_ISA_BIT) != 0) {
				return (iop_beg);
			}
		}
		return (0);
	}
	if (bus_type & ASC_IS_EISA) {
		if ((iop_beg = AscSearchIOPortAddrEISA(iop_beg)) != 0) {
			return (iop_beg);
		}
		return (0);
	}
	return (0);
}

PortAddr
AscSearchIOPortAddr11(
						 PortAddr s_addr
)
{
	int                 i;
	PortAddr            iop_base;
	for (i = 0; i < ASC_IOADR_TABLE_MAX_IX; i++) {
		if (_asc_def_iop_base[i] > s_addr) {
			break;
		}
	}
	for (; i < ASC_IOADR_TABLE_MAX_IX; i++) {
		iop_base = _asc_def_iop_base[i];
		if (check_region(iop_base, ASC_IOADR_GAP) != 0) {
			ASC_DBG1(1,
			   "AscSearchIOPortAddr11: check_region() failed I/O port %x\n",
					 iop_base);
			continue;
		}
		ASC_DBG1(1, "AscSearchIOPortAddr11: probing I/O port %x\n", iop_base);
		if (AscFindSignature(iop_base)) {
			return (iop_base);
		}
	}
	return (0);
}

void
AscToggleIRQAct(
				   PortAddr iop_base
)
{
	AscSetChipStatus(iop_base, CIW_IRQ_ACT);
	AscSetChipStatus(iop_base, 0);
	return;
}

#if CC_INIT_INQ_DISPLAY
uchar               _hextbl_[16] =
{
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F'
};
#endif

void
AscSetISAPNPWaitForKey(
	void)
{
	outp(ASC_ISA_PNP_PORT_ADDR, 0x02);
	outp(ASC_ISA_PNP_PORT_WRITE, 0x02);
	return;
}

uchar
AscGetChipIRQ(
				 PortAddr iop_base,
				 ushort bus_type
)
{
	ushort              cfg_lsw;
	uchar               chip_irq;
	if ((bus_type & ASC_IS_EISA) != 0) {
		cfg_lsw = AscGetEisaChipCfg(iop_base);
		chip_irq = (uchar) (((cfg_lsw >> 8) & 0x07) + 10);
		if ((chip_irq == 13) || (chip_irq > 15)) {
			return (0);
		}
		return (chip_irq);
	}
	if ((bus_type & ASC_IS_VL) != 0) {
		cfg_lsw = AscGetChipCfgLsw(iop_base);
		chip_irq = (uchar) (((cfg_lsw >> 2) & 0x07));
		if ((chip_irq == 0) ||
			(chip_irq == 4) ||
			(chip_irq == 7)) {
			return (0);
		}
#if CC_PLEXTOR_VL
		if (chip_irq == 5) {
			return (9);
		}
#endif
		return ((uchar) (chip_irq + (ASC_MIN_IRQ_NO - 1)));
	}
	cfg_lsw = AscGetChipCfgLsw(iop_base);
	chip_irq = (uchar) (((cfg_lsw >> 2) & 0x03));
	if (chip_irq == 3)
		chip_irq += (uchar) 2;
	return ((uchar) (chip_irq + ASC_MIN_IRQ_NO));
}

uchar
AscSetChipIRQ(
				 PortAddr iop_base,
				 uchar irq_no,
				 ushort bus_type
)
{
	ushort              cfg_lsw;
	if ((bus_type & ASC_IS_VL) != 0) {
		if (irq_no != 0) {
#if CC_PLEXTOR_VL
			if (irq_no == 9) {
				irq_no = 14;
			}
#endif
			if ((irq_no < ASC_MIN_IRQ_NO) || (irq_no > ASC_MAX_IRQ_NO)) {
				irq_no = 0;
			} else {
				irq_no -= (uchar) ((ASC_MIN_IRQ_NO - 1));
			}
		}
		cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFE3);
		cfg_lsw |= (ushort) 0x0010;
		AscSetChipCfgLsw(iop_base, cfg_lsw);
		AscToggleIRQAct(iop_base);
		cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFE0);
		cfg_lsw |= (ushort) ((irq_no & 0x07) << 2);
		AscSetChipCfgLsw(iop_base, cfg_lsw);
		AscToggleIRQAct(iop_base);
		return (AscGetChipIRQ(iop_base, bus_type));
	}
	if ((bus_type & (ASC_IS_ISA)) != 0) {
		if (irq_no == 15)
			irq_no -= (uchar) 2;
		irq_no -= (uchar) ASC_MIN_IRQ_NO;
		cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFF3);
		cfg_lsw |= (ushort) ((irq_no & 0x03) << 2);
		AscSetChipCfgLsw(iop_base, cfg_lsw);
		return (AscGetChipIRQ(iop_base, bus_type));
	}
	return (0);
}

void
AscEnableIsaDma(
				   uchar dma_channel
)
{
	if (dma_channel < 4) {
		outp(0x000B, (ushort) (0xC0 | dma_channel));
		outp(0x000A, dma_channel);
	} else if (dma_channel < 8) {
		outp(0x00D6, (ushort) (0xC0 | (dma_channel - 4)));
		outp(0x00D4, (ushort) (dma_channel - 4));
	}
	return;
}

int
AscIsrChipHalted(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	SDTR_XMSG           sdtr_xmsg;
	SDTR_XMSG           out_msg;
	ushort              halt_q_addr;
	int                 sdtr_accept;
	ushort              int_halt_code;
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;
	PortAddr            iop_base;
	uchar               tag_code;
	uchar               q_status;
	uchar               halt_qp;
	uchar               sdtr_data = 0;
	uchar               target_ix;
	uchar               q_cntl, tid_no;
	uchar               cur_dvc_qng;
	uchar               asyn_sdtr;
	uchar               scsi_status;
	iop_base = asc_dvc->iop_base;
	int_halt_code = AscReadLramWord(iop_base, ASCV_HALTCODE_W);
	halt_qp = AscReadLramByte(iop_base, ASCV_CURCDB_B);
	halt_q_addr = ASC_QNO_TO_QADDR(halt_qp);
	target_ix = AscReadLramByte(iop_base,
				   (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TARGET_IX));
	q_cntl = AscReadLramByte(iop_base,
						(ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL));
	tid_no = ASC_TIX_TO_TID(target_ix);
	target_id = (uchar) ASC_TID_TO_TARGET_ID(tid_no);
	if (asc_dvc->pci_fix_asyn_xfer & target_id) {
		asyn_sdtr = ASYN_SDTR_DATA_FIX_PCI_REV_AB;
	} else {
		asyn_sdtr = 0;
	}
	if (int_halt_code == ASC_HALT_DISABLE_ASYN_USE_SYN_FIX) {
		if (asc_dvc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iop_base, 0, tid_no);
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_ENABLE_ASYN_USE_SYN_FIX) {
		if (asc_dvc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_EXTMSG_IN) {
		AscMemWordCopyFromLram(iop_base,
							   ASCV_MSGIN_BEG,
							   (ushort dosfar *) & sdtr_xmsg,
							   (ushort) (sizeof (SDTR_XMSG) >> 1));
		if (
			   (sdtr_xmsg.msg_type == MS_EXTEND)
			   && (sdtr_xmsg.msg_len == MS_SDTR_LEN)
		  ) {
			sdtr_accept = TRUE;
			if (sdtr_xmsg.msg_req == MS_SDTR_CODE) {
				if (
					   (sdtr_xmsg.req_ack_offset > ASC_SYN_MAX_OFFSET)
				  ) {
					sdtr_accept = FALSE;
					sdtr_xmsg.req_ack_offset = ASC_SYN_MAX_OFFSET;
				}
				if (
					   (sdtr_xmsg.xfer_period < asc_dvc->sdtr_period_tbl[0])
					   || (sdtr_xmsg.xfer_period > asc_dvc->sdtr_period_tbl[asc_dvc->max_sdtr_index])
				  ) {
					sdtr_accept = FALSE;
				}
				if (sdtr_accept) {
					sdtr_data = AscCalSDTRData(asc_dvc, sdtr_xmsg.xfer_period,
											   sdtr_xmsg.req_ack_offset);
					if ((sdtr_data == 0xFF)) {
						q_cntl |= QC_MSG_OUT;
						asc_dvc->init_sdtr &= ~target_id;
						asc_dvc->sdtr_done &= ~target_id;
						AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
					}
				}
				if (sdtr_xmsg.req_ack_offset == 0) {
					q_cntl &= ~QC_MSG_OUT;
					asc_dvc->init_sdtr &= ~target_id;
					asc_dvc->sdtr_done &= ~target_id;
					AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
				} else {
					if (
						   sdtr_accept
						   && (q_cntl & QC_MSG_OUT)
					  ) {
						q_cntl &= ~QC_MSG_OUT;
						asc_dvc->sdtr_done |= target_id;
						asc_dvc->init_sdtr |= target_id;
						asc_dvc->pci_fix_asyn_xfer &= ~target_id;
						AscSetChipSDTR(iop_base, sdtr_data, tid_no);
					} else {
						q_cntl |= QC_MSG_OUT;
						AscMsgOutSDTR(asc_dvc,
									  sdtr_xmsg.xfer_period,
									  sdtr_xmsg.req_ack_offset);
						asc_dvc->pci_fix_asyn_xfer &= ~target_id;
						AscSetChipSDTR(iop_base, sdtr_data, tid_no);
						asc_dvc->sdtr_done |= target_id;
						asc_dvc->init_sdtr |= target_id;
					}
				}
				AscWriteLramByte(iop_base,
						 (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
								 q_cntl);
				AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
				return (0);
			}
		}
	} else if (int_halt_code == ASC_HALT_CHK_CONDITION) {
		q_cntl |= QC_REQ_SENSE;
#if CC_CHK_COND_REDO_SDTR
		if (((asc_dvc->init_sdtr & target_id) != 0) &&
			((asc_dvc->sdtr_done & target_id) != 0)) {
			asc_dvc->sdtr_done &= ~target_id;
			sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
			q_cntl |= QC_MSG_OUT;
			AscMsgOutSDTR(asc_dvc,
						  asc_dvc->sdtr_period_tbl[(sdtr_data >> 4) & (uchar) (ASC_SYN_XFER_NO - 1)],
						  (uchar) (sdtr_data & (uchar) ASC_SYN_MAX_OFFSET));
		}
#endif
		AscWriteLramByte(iop_base,
						 (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
						 q_cntl);
		tag_code = AscReadLramByte(iop_base,
					(ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TAG_CODE));
		tag_code &= 0xDC;
		if (
			   (asc_dvc->pci_fix_asyn_xfer & target_id)
			   && !(asc_dvc->pci_fix_asyn_xfer_always & target_id)
		  ) {
			tag_code |= (ASC_TAG_FLAG_DISABLE_DISCONNECT
						 | ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX);
		}
		AscWriteLramByte(iop_base,
					 (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TAG_CODE),
						 tag_code);
		q_status = AscReadLramByte(iop_base,
					  (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_STATUS));
		q_status |= (QS_READY | QS_BUSY);
		AscWriteLramByte(iop_base,
					   (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_STATUS),
						 q_status);
		scsi_busy = AscReadLramByte(iop_base,
									(ushort) ASCV_SCSIBUSY_B);
		scsi_busy &= ~target_id;
		AscWriteLramByte(iop_base, (ushort) ASCV_SCSIBUSY_B, scsi_busy);
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_SDTR_REJECTED) {
		AscMemWordCopyFromLram(iop_base,
							   ASCV_MSGOUT_BEG,
							   (ushort dosfar *) & out_msg,
							   (ushort) (sizeof (SDTR_XMSG) >> 1));
		if ((out_msg.msg_type == MS_EXTEND) &&
			(out_msg.msg_len == MS_SDTR_LEN) &&
			(out_msg.msg_req == MS_SDTR_CODE)) {
			asc_dvc->init_sdtr &= ~target_id;
			asc_dvc->sdtr_done &= ~target_id;
			AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
		} else {
		}
		q_cntl &= ~QC_MSG_OUT;
		AscWriteLramByte(iop_base,
						 (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
						 q_cntl);
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_SS_QUEUE_FULL) {
		scsi_status = AscReadLramByte(iop_base,
		  (ushort) ((ushort) halt_q_addr + (ushort) ASC_SCSIQ_SCSI_STATUS));
		cur_dvc_qng = AscReadLramByte(iop_base,
					 (ushort) ((ushort) ASC_QADR_BEG + (ushort) target_ix));
		if ((cur_dvc_qng > 0) &&
			(asc_dvc->cur_dvc_qng[tid_no] > 0)) {
			scsi_busy = AscReadLramByte(iop_base,
										(ushort) ASCV_SCSIBUSY_B);
			scsi_busy |= target_id;
			AscWriteLramByte(iop_base,
							 (ushort) ASCV_SCSIBUSY_B, scsi_busy);
			asc_dvc->queue_full_or_busy |= target_id;
			if (scsi_status == SS_QUEUE_FULL) {
				if (cur_dvc_qng > ASC_MIN_TAGGED_CMD) {
					cur_dvc_qng -= 1;
					asc_dvc->max_dvc_qng[tid_no] = cur_dvc_qng;
					AscWriteLramByte(iop_base,
									 (ushort) ((ushort) ASCV_MAX_DVC_QNG_BEG + (ushort) tid_no),
									 cur_dvc_qng);
				}
#ifdef ADVANSYS_STATS
				{
					struct asc_board   *boardp;
					int                 i;
					for (i = 0; i < ASC_NUM_BOARD_SUPPORTED; i++) {
						if (asc_host[i] == NULL) {
							continue;
						}
						boardp = ASC_BOARDP(asc_host[i]);
						if (&boardp->asc_dvc_var == asc_dvc) {
							boardp->asc_stats.queue_full |= target_id;
							boardp->asc_stats.queue_full_cnt[tid_no] =
							  cur_dvc_qng;
							break;
						}
					}
				}
#endif
			}
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	}
	return (0);
}

uchar
_AscCopyLramScsiDoneQ(
						 PortAddr iop_base,
						 ushort q_addr,
						 REG ASC_QDONE_INFO dosfar * scsiq,
						 ulong max_dma_count
)
{
	ushort              _val;
	uchar               sg_queue_cnt;
	DvcGetQinfo(iop_base,
				(ushort) (q_addr + (ushort) ASC_SCSIQ_DONE_INFO_BEG),
				(ushort dosfar *) scsiq,
			  (ushort) ((sizeof (ASC_SCSIQ_2) + sizeof (ASC_SCSIQ_3)) / 2));
#if !CC_LITTLE_ENDIAN_HOST
	AscAdjEndianQDoneInfo(scsiq);
#endif
	_val = AscReadLramWord(iop_base,
						   (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS));
	scsiq->q_status = (uchar) _val;
	scsiq->q_no = (uchar) (_val >> 8);
	_val = AscReadLramWord(iop_base,
						   (ushort) (q_addr + (ushort) ASC_SCSIQ_B_CNTL));
	scsiq->cntl = (uchar) _val;
	sg_queue_cnt = (uchar) (_val >> 8);
	_val = AscReadLramWord(iop_base,
						(ushort) (q_addr + (ushort) ASC_SCSIQ_B_SENSE_LEN));
	scsiq->sense_len = (uchar) _val;
	scsiq->extra_bytes = (uchar) (_val >> 8);
	scsiq->remain_bytes = AscReadLramDWord(iop_base,
				 (ushort) (q_addr + (ushort) ASC_SCSIQ_DW_REMAIN_XFER_CNT));
	scsiq->remain_bytes &= max_dma_count;
	return (sg_queue_cnt);
}

int
AscIsrQDone(
			   REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	uchar               next_qp;
	uchar               n_q_used;
	uchar               sg_list_qp;
	uchar               sg_queue_cnt;
	uchar               q_cnt;
	uchar               done_q_tail;
	uchar               tid_no;
#if CC_LINK_BUSY_Q
	uchar               exe_tid_no;
#endif
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;
	PortAddr            iop_base;
	ushort              q_addr;
	ushort              sg_q_addr;
	uchar               cur_target_qng;
	ASC_QDONE_INFO      scsiq_buf;
	REG ASC_QDONE_INFO dosfar *scsiq;
	int                 false_overrun;
	ASC_ISR_CALLBACK    asc_isr_callback;
#if CC_LINK_BUSY_Q
	ushort              n_busy_q_done;
#endif
	iop_base = asc_dvc->iop_base;
	asc_isr_callback = (ASC_ISR_CALLBACK) asc_dvc->isr_callback;
	n_q_used = 1;
	scsiq = (ASC_QDONE_INFO dosfar *) & scsiq_buf;
	done_q_tail = (uchar) AscGetVarDoneQTail(iop_base);
	q_addr = ASC_QNO_TO_QADDR(done_q_tail);
	next_qp = AscReadLramByte(iop_base,
							  (ushort) (q_addr + (ushort) ASC_SCSIQ_B_FWD));
	if (next_qp != ASC_QLINK_END) {
		AscPutVarDoneQTail(iop_base, next_qp);
		q_addr = ASC_QNO_TO_QADDR(next_qp);
		sg_queue_cnt = _AscCopyLramScsiDoneQ(iop_base, q_addr, scsiq, asc_dvc->max_dma_count);
		AscWriteLramByte(iop_base,
						 (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
			 (uchar) (scsiq->q_status & (uchar) ~ (QS_READY | QS_ABORTED)));
		tid_no = ASC_TIX_TO_TID(scsiq->d2.target_ix);
		target_id = ASC_TIX_TO_TARGET_ID(scsiq->d2.target_ix);
		if ((scsiq->cntl & QC_SG_HEAD) != 0) {
			sg_q_addr = q_addr;
			sg_list_qp = next_qp;
			for (q_cnt = 0; q_cnt < sg_queue_cnt; q_cnt++) {
				sg_list_qp = AscReadLramByte(iop_base,
						   (ushort) (sg_q_addr + (ushort) ASC_SCSIQ_B_FWD));
				sg_q_addr = ASC_QNO_TO_QADDR(sg_list_qp);
				if (sg_list_qp == ASC_QLINK_END) {
					AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SG_Q_LINKS);
					scsiq->d3.done_stat = QD_WITH_ERROR;
					scsiq->d3.host_stat = QHSTA_D_QDONE_SG_LIST_CORRUPTED;
					goto FATAL_ERR_QDONE;
				}
				AscWriteLramByte(iop_base,
						 (ushort) (sg_q_addr + (ushort) ASC_SCSIQ_B_STATUS),
								 QS_FREE);
			}
			n_q_used = sg_queue_cnt + 1;
			AscPutVarDoneQTail(iop_base, sg_list_qp);
		}
		if (asc_dvc->queue_full_or_busy & target_id) {
			cur_target_qng = AscReadLramByte(iop_base,
			(ushort) ((ushort) ASC_QADR_BEG + (ushort) scsiq->d2.target_ix));
			if (cur_target_qng < asc_dvc->max_dvc_qng[tid_no]) {
				scsi_busy = AscReadLramByte(iop_base,
											(ushort) ASCV_SCSIBUSY_B);
				scsi_busy &= ~target_id;
				AscWriteLramByte(iop_base,
								 (ushort) ASCV_SCSIBUSY_B, scsi_busy);
				asc_dvc->queue_full_or_busy &= ~target_id;
			}
		}
		if (asc_dvc->cur_total_qng >= n_q_used) {
			asc_dvc->cur_total_qng -= n_q_used;
			if (asc_dvc->cur_dvc_qng[tid_no] != 0) {
				asc_dvc->cur_dvc_qng[tid_no]--;
			}
		} else {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CUR_QNG);
			scsiq->d3.done_stat = QD_WITH_ERROR;
			goto FATAL_ERR_QDONE;
		}
		if ((scsiq->d2.srb_ptr == 0UL) ||
			((scsiq->q_status & QS_ABORTED) != 0)) {
			return (0x11);
		} else if (scsiq->q_status == QS_DONE) {
			false_overrun = FALSE;
			if (scsiq->extra_bytes != 0) {
				scsiq->remain_bytes += (ulong) scsiq->extra_bytes;
			}
			if (scsiq->d3.done_stat == QD_WITH_ERROR) {
				if (scsiq->d3.host_stat == QHSTA_M_DATA_OVER_RUN) {
					if ((scsiq->cntl & (QC_DATA_IN | QC_DATA_OUT)) == 0) {
						scsiq->d3.done_stat = QD_NO_ERROR;
						scsiq->d3.host_stat = QHSTA_NO_ERROR;
					} else if (false_overrun) {
						scsiq->d3.done_stat = QD_NO_ERROR;
						scsiq->d3.host_stat = QHSTA_NO_ERROR;
					}
				} else if (scsiq->d3.host_stat == QHSTA_M_HUNG_REQ_SCSI_BUS_RESET) {
					AscStopChip(iop_base);
					AscSetChipControl(iop_base, (uchar) (CC_SCSI_RESET | CC_HALT));
					DvcDelayNanoSecond(asc_dvc, 30000);
					AscSetChipControl(iop_base, CC_HALT);
					AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
					AscSetChipStatus(iop_base, 0);
					AscSetChipControl(iop_base, 0);
				}
			}
#if CC_CLEAR_LRAM_SRB_PTR
			AscWriteLramDWord(iop_base,
							(ushort) (q_addr + (ushort) ASC_SCSIQ_D_SRBPTR),
							  asc_dvc->int_count);
#endif
			if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
				(*asc_isr_callback) (asc_dvc, scsiq);
			} else {
				if ((AscReadLramByte(iop_base,
						  (ushort) (q_addr + (ushort) ASC_SCSIQ_CDB_BEG)) ==
					 SCSICMD_StartStopUnit)) {
					asc_dvc->unit_not_ready &= ~target_id;
					if (scsiq->d3.done_stat != QD_NO_ERROR) {
						asc_dvc->start_motor &= ~target_id;
					}
				}
			}
#if CC_LINK_BUSY_Q
			n_busy_q_done = AscIsrExeBusyQueue(asc_dvc, tid_no);
			if (n_busy_q_done == 0) {
				exe_tid_no = (uint) tid_no + 1;
				while (TRUE) {
					if (exe_tid_no > ASC_MAX_TID)
						exe_tid_no = 0;
					if (exe_tid_no == (uint) tid_no)
						break;
					n_busy_q_done = AscIsrExeBusyQueue(asc_dvc, exe_tid_no);
					if (n_busy_q_done != 0)
						break;
					exe_tid_no++;
				}
			}
			if (n_busy_q_done == 0xFFFF)
				return (0x80);
#endif
			return (1);
		} else {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_Q_STATUS);
		  FATAL_ERR_QDONE:
			if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
				(*asc_isr_callback) (asc_dvc, scsiq);
			}
			return (0x80);
		}
	}
	return (0);
}

#if CC_LINK_BUSY_Q
#endif

int
AscISR(
		  REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	ASC_CS_TYPE         chipstat;
	PortAddr            iop_base;
	ushort              saved_ram_addr;
	uchar               ctrl_reg;
	uchar               saved_ctrl_reg;
	int                 int_pending;
	int                 status;
	uchar               host_flag;
	iop_base = asc_dvc->iop_base;
	int_pending = FALSE;
	if (((asc_dvc->init_state & ASC_INIT_STATE_END_LOAD_MC) == 0)
		|| (asc_dvc->isr_callback == 0)
	  ) {
		return (ERR);
	}
	if (asc_dvc->in_critical_cnt != 0) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_ON_CRITICAL);
		return (ERR);
	}
	if (asc_dvc->is_in_int) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_RE_ENTRY);
		return (ERR);
	}
	asc_dvc->is_in_int = TRUE;
	ctrl_reg = AscGetChipControl(iop_base);
	saved_ctrl_reg = ctrl_reg & (~(CC_SCSI_RESET | CC_CHIP_RESET |
								   CC_SINGLE_STEP | CC_DIAG | CC_TEST));
	chipstat = AscGetChipStatus(iop_base);
	if (chipstat & CSW_SCSI_RESET_LATCH) {
		if (
			   !(asc_dvc->bus_type & (ASC_IS_VL | ASC_IS_EISA))
		  ) {
			int_pending = TRUE;
			asc_dvc->sdtr_done = 0;
			saved_ctrl_reg &= (uchar) (~CC_HALT);
			while (AscGetChipStatus(iop_base) & CSW_SCSI_RESET_ACTIVE) ;
			AscSetChipControl(iop_base, (CC_CHIP_RESET | CC_HALT));
			AscSetChipControl(iop_base, CC_HALT);
			AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
			AscSetChipStatus(iop_base, 0);
			chipstat = AscGetChipStatus(iop_base);
		}
	}
	saved_ram_addr = AscGetChipLramAddr(iop_base);
	host_flag = AscReadLramByte(iop_base, ASCV_HOST_FLAG_B) & (uchar) (~ASC_HOST_FLAG_IN_ISR);
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
					 (uchar) (host_flag | (uchar) ASC_HOST_FLAG_IN_ISR));
#if CC_ASCISR_CHECK_INT_PENDING
	if ((chipstat & CSW_INT_PENDING)
		|| (int_pending)
	  ) {
		AscAckInterrupt(iop_base);
#endif
		int_pending = TRUE;
		if ((chipstat & CSW_HALTED) &&
			(ctrl_reg & CC_SINGLE_STEP)) {
			if (AscIsrChipHalted(asc_dvc) == ERR) {
				goto ISR_REPORT_QDONE_FATAL_ERROR;
			} else {
				saved_ctrl_reg &= (uchar) (~CC_HALT);
			}
		} else {
		  ISR_REPORT_QDONE_FATAL_ERROR:
			if ((asc_dvc->dvc_cntl & ASC_CNTL_INT_MULTI_Q) != 0) {
				while (((status = AscIsrQDone(asc_dvc)) & 0x01) != 0) {
				}
			} else {
				do {
					if ((status = AscIsrQDone(asc_dvc)) == 1) {
						break;
					}
				} while (status == 0x11);
			}
			if ((status & 0x80) != 0)
				int_pending = ERR;
		}
#if CC_ASCISR_CHECK_INT_PENDING
	}
#endif
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
	AscSetChipLramAddr(iop_base, saved_ram_addr);
	AscSetChipControl(iop_base, saved_ctrl_reg);
	asc_dvc->is_in_int = FALSE;
	return (int_pending);
}

int
AscScsiSetupCmdQ(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					REG ASC_SCSI_REQ_Q dosfar * scsiq,
					uchar dosfar * buf_addr,
					ulong buf_len
)
{
	ulong               phy_addr;
	scsiq->r1.cntl = 0;
	scsiq->r1.sg_queue_cnt = 0;
	scsiq->r1.q_no = 0;
	scsiq->r1.extra_bytes = 0;
	scsiq->r3.scsi_stat = 0;
	scsiq->r3.scsi_msg = 0;
	scsiq->r3.host_stat = 0;
	scsiq->r3.done_stat = 0;
	scsiq->r2.vm_id = 0;
	scsiq->cdbptr = (uchar dosfar *) scsiq->cdb;
	scsiq->r1.data_cnt = buf_len;
	scsiq->r2.tag_code = (uchar) M2_QTAG_MSG_SIMPLE;
	scsiq->r2.flag = (uchar) ASC_FLAG_SCSIQ_REQ;
	scsiq->r2.srb_ptr = (ulong) scsiq;
	scsiq->r1.status = (uchar) QS_READY;
	scsiq->r1.data_addr = 0L;
	if (buf_len != 0L) {
		if ((phy_addr = AscGetOnePhyAddr(asc_dvc,
					(uchar dosfar *) buf_addr, scsiq->r1.data_cnt)) == 0L) {
			return (ERR);
		}
		scsiq->r1.data_addr = phy_addr;
	}
	return (0);
}

uchar               _mcode_buf[] =
{
	0x01, 0x03, 0x01, 0x19, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xC4, 0x0C, 0x08, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xFF, 0x80, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x23, 0x00, 0x20, 0x00, 0x00, 0x00, 0x07, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE4, 0x88, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x73, 0x48, 0x04, 0x36, 0x00, 0x00, 0xA2, 0xC6, 0x00, 0x80, 0x73, 0x03, 0x23, 0x36, 0x40,
	0xB6, 0x00, 0x36, 0x00, 0x05, 0xD6, 0x0C, 0xD2, 0x12, 0xDA, 0x00, 0xA2, 0xC6, 0x00, 0x92, 0x80,
	0x20, 0x98, 0x50, 0x00, 0xF5, 0x00, 0x4A, 0x98, 0xDF, 0x23, 0x36, 0x60, 0xB6, 0x00, 0x92, 0x80,
	0x4F, 0x00, 0xF5, 0x00, 0x4A, 0x98, 0xEF, 0x23, 0x36, 0x60, 0xB6, 0x00, 0x92, 0x80, 0x80, 0x62,
	0x92, 0x80, 0x00, 0x62, 0x92, 0x80, 0x00, 0x46, 0x17, 0xEE, 0x13, 0xEA, 0x02, 0x01, 0x09, 0xD8,
	0xCD, 0x04, 0x4D, 0x00, 0x00, 0xA3, 0xDA, 0x00, 0xA8, 0x97, 0x7F, 0x23, 0x04, 0x61, 0x84, 0x01,
	0xD2, 0x84, 0xD0, 0xC1, 0x80, 0x73, 0xCD, 0x04, 0x4D, 0x00, 0x00, 0xA3, 0xE6, 0x01, 0xA8, 0x97,
	0xD2, 0x81, 0x00, 0x33, 0x02, 0x00, 0xC2, 0x88, 0x80, 0x73, 0x80, 0x77, 0x00, 0x01, 0x01, 0xA1,
	0x06, 0x01, 0x4F, 0x00, 0x86, 0x97, 0x07, 0xA6, 0x10, 0x01, 0x00, 0x33, 0x03, 0x00, 0xC2, 0x88,
	0x03, 0x03, 0x03, 0xDE, 0x00, 0x33, 0x05, 0x00, 0xC2, 0x88, 0xCE, 0x00, 0x69, 0x60, 0xCE, 0x00,
	0x02, 0x03, 0x4A, 0x60, 0x00, 0xA2, 0x84, 0x01, 0x80, 0x63, 0x07, 0xA6, 0x30, 0x01, 0x84, 0x81,
	0x03, 0x03, 0x80, 0x63, 0xE2, 0x00, 0x07, 0xA6, 0x40, 0x01, 0x00, 0x33, 0x04, 0x00, 0xC2, 0x88,
	0x03, 0x07, 0x02, 0x01, 0x04, 0xCA, 0x0D, 0x23, 0x6A, 0x98, 0x4D, 0x04, 0xF0, 0x84, 0x05, 0xD8,
	0x0D, 0x23, 0x6A, 0x98, 0xCD, 0x04, 0x15, 0x23, 0xF8, 0x88, 0xFB, 0x23, 0x02, 0x61, 0x82, 0x01,
	0x80, 0x63, 0x02, 0x03, 0x06, 0xA3, 0x6E, 0x01, 0x00, 0x33, 0x0A, 0x00, 0xC2, 0x88, 0x4E, 0x00,
	0x07, 0xA3, 0x7A, 0x01, 0x00, 0x33, 0x0B, 0x00, 0xC2, 0x88, 0xCD, 0x04, 0x36, 0x2D, 0x00, 0x33,
	0x1A, 0x00, 0xC2, 0x88, 0x50, 0x04, 0x94, 0x81, 0x06, 0xAB, 0x8E, 0x01, 0x94, 0x81, 0x4E, 0x00,
	0x07, 0xA3, 0x9E, 0x01, 0x50, 0x00, 0x00, 0xA3, 0x48, 0x01, 0x00, 0x05, 0x88, 0x81, 0x48, 0x97,
	0x02, 0x01, 0x05, 0xC6, 0x04, 0x23, 0xA0, 0x01, 0x15, 0x23, 0xA1, 0x01, 0xCA, 0x81, 0xFD, 0x23,
	0x02, 0x61, 0x82, 0x01, 0x0A, 0xDA, 0x4A, 0x00, 0x06, 0x61, 0x00, 0xA0, 0xC0, 0x01, 0x80, 0x63,
	0xCD, 0x04, 0x36, 0x2D, 0x00, 0x33, 0x1B, 0x00, 0xC2, 0x88, 0x06, 0x23, 0x6A, 0x98, 0xCD, 0x04,
	0xD2, 0x84, 0x06, 0x01, 0x00, 0xA2, 0xE0, 0x01, 0x57, 0x60, 0x00, 0xA0, 0xE6, 0x01, 0xD2, 0x84,
	0x80, 0x23, 0xA0, 0x01, 0xD2, 0x84, 0x80, 0x73, 0x4B, 0x00, 0x06, 0x61, 0x00, 0xA2, 0x0E, 0x02,
	0x04, 0x01, 0x0D, 0xDE, 0x02, 0x01, 0x03, 0xCC, 0x4F, 0x00, 0x86, 0x97, 0x08, 0x82, 0x08, 0x23,
	0x02, 0x41, 0x82, 0x01, 0x4F, 0x00, 0x64, 0x97, 0x48, 0x04, 0xFF, 0x23, 0x84, 0x80, 0xF2, 0x97,
	0x00, 0x46, 0x56, 0x00, 0x03, 0xC0, 0x01, 0x23, 0xE8, 0x00, 0x81, 0x73, 0x06, 0x29, 0x03, 0x42,
	0x06, 0xE2, 0x03, 0xEE, 0x66, 0xEB, 0x11, 0x23, 0xF8, 0x88, 0x06, 0x98, 0xF8, 0x80, 0x80, 0x73,
	0x80, 0x77, 0x06, 0xA6, 0x3C, 0x02, 0x00, 0x33, 0x31, 0x00, 0xC2, 0x88, 0x04, 0x01, 0x03, 0xD8,
	0xB4, 0x98, 0x3E, 0x96, 0x4E, 0x82, 0xCE, 0x95, 0x80, 0x67, 0x83, 0x03, 0x80, 0x63, 0xB6, 0x2D,
	0x02, 0xA6, 0x78, 0x02, 0x07, 0xA6, 0x66, 0x02, 0x06, 0xA6, 0x6A, 0x02, 0x03, 0xA6, 0x6E, 0x02,
	0x00, 0x33, 0x10, 0x00, 0xC2, 0x88, 0x6A, 0x95, 0x50, 0x82, 0x34, 0x96, 0x50, 0x82, 0x04, 0x23,
	0xA0, 0x01, 0x14, 0x23, 0xA1, 0x01, 0x28, 0x84, 0x04, 0x01, 0x0C, 0xDC, 0xE0, 0x23, 0x25, 0x61,
	0xEF, 0x00, 0x14, 0x01, 0x4F, 0x04, 0xA8, 0x01, 0x6F, 0x00, 0xA5, 0x01, 0x03, 0x23, 0xA4, 0x01,
	0x06, 0x23, 0x9C, 0x01, 0x24, 0x2B, 0x1C, 0x01, 0x02, 0xA6, 0xAA, 0x02, 0x07, 0xA6, 0x66, 0x02,
	0x06, 0xA6, 0x6A, 0x02, 0x00, 0x33, 0x12, 0x00, 0xC2, 0x88, 0x00, 0x0E, 0x80, 0x63, 0x00, 0x43,
	0x00, 0xA0, 0x98, 0x02, 0x4D, 0x04, 0x04, 0x01, 0x0B, 0xDC, 0xE7, 0x23, 0x04, 0x61, 0x84, 0x01,
	0x10, 0x31, 0x12, 0x35, 0x14, 0x01, 0xEC, 0x00, 0x6C, 0x38, 0x00, 0x3F, 0x00, 0x00, 0xEA, 0x82,
	0x18, 0x23, 0x04, 0x61, 0x18, 0xA0, 0xE2, 0x02, 0x04, 0x01, 0x98, 0xC8, 0x00, 0x33, 0x1F, 0x00,
	0xC2, 0x88, 0x08, 0x31, 0x0A, 0x35, 0x0C, 0x39, 0x0E, 0x3D, 0x80, 0x98, 0xB6, 0x2D, 0x01, 0xA6,
	0x14, 0x03, 0x00, 0xA6, 0x14, 0x03, 0x07, 0xA6, 0x0C, 0x03, 0x06, 0xA6, 0x10, 0x03, 0x03, 0xA6,
	0x0C, 0x04, 0x02, 0xA6, 0x78, 0x02, 0x00, 0x33, 0x33, 0x00, 0xC2, 0x88, 0x6A, 0x95, 0xEE, 0x82,
	0x34, 0x96, 0xEE, 0x82, 0x84, 0x98, 0x80, 0x42, 0x80, 0x98, 0x60, 0xE4, 0x04, 0x01, 0x29, 0xC8,
	0x31, 0x05, 0x07, 0x01, 0x00, 0xA2, 0x54, 0x03, 0x00, 0x43, 0x87, 0x01, 0x05, 0x05, 0x88, 0x98,
	0x80, 0x98, 0x00, 0xA6, 0x16, 0x03, 0x07, 0xA6, 0x4C, 0x03, 0x03, 0xA6, 0x28, 0x04, 0x06, 0xA6,
	0x50, 0x03, 0x01, 0xA6, 0x16, 0x03, 0x00, 0x33, 0x25, 0x00, 0xC2, 0x88, 0x6A, 0x95, 0x32, 0x83,
	0x34, 0x96, 0x32, 0x83, 0x04, 0x01, 0x0C, 0xCE, 0x03, 0xC8, 0x00, 0x33, 0x42, 0x00, 0xC2, 0x88,
	0x00, 0x01, 0x05, 0x05, 0xFF, 0xA2, 0x72, 0x03, 0xB1, 0x01, 0x08, 0x23, 0xB2, 0x01, 0x2E, 0x83,
	0x05, 0x05, 0x15, 0x01, 0x00, 0xA2, 0x92, 0x03, 0xEC, 0x00, 0x6E, 0x00, 0x95, 0x01, 0x6C, 0x38,
	0x00, 0x3F, 0x00, 0x00, 0x01, 0xA6, 0x8E, 0x03, 0x00, 0xA6, 0x8E, 0x03, 0x02, 0x84, 0x80, 0x42,
	0x80, 0x98, 0x01, 0xA6, 0x9C, 0x03, 0x00, 0xA6, 0xB4, 0x03, 0x02, 0x84, 0xA8, 0x98, 0x80, 0x42,
	0x01, 0xA6, 0x9C, 0x03, 0x07, 0xA6, 0xAA, 0x03, 0xCC, 0x83, 0x6A, 0x95, 0xA0, 0x83, 0x00, 0x33,
	0x2F, 0x00, 0xC2, 0x88, 0xA8, 0x98, 0x80, 0x42, 0x00, 0xA6, 0xB4, 0x03, 0x07, 0xA6, 0xC2, 0x03,
	0xCC, 0x83, 0x6A, 0x95, 0xB8, 0x83, 0x00, 0x33, 0x26, 0x00, 0xC2, 0x88, 0x38, 0x2B, 0x80, 0x32,
	0x80, 0x36, 0x04, 0x23, 0xA0, 0x01, 0x12, 0x23, 0xA1, 0x01, 0x02, 0x84, 0x04, 0xF0, 0x80, 0x6B,
	0x00, 0x33, 0x20, 0x00, 0xC2, 0x88, 0x03, 0xA6, 0x00, 0x04, 0x07, 0xA6, 0xF8, 0x03, 0x06, 0xA6,
	0xFC, 0x03, 0x00, 0x33, 0x17, 0x00, 0xC2, 0x88, 0x6A, 0x95, 0xE6, 0x83, 0x34, 0x96, 0xE6, 0x83,
	0x0C, 0x84, 0x04, 0xF0, 0x80, 0x6B, 0x00, 0x33, 0x20, 0x00, 0xC2, 0x88, 0xB6, 0x2D, 0x03, 0xA6,
	0x28, 0x04, 0x07, 0xA6, 0x20, 0x04, 0x06, 0xA6, 0x24, 0x04, 0x00, 0x33, 0x30, 0x00, 0xC2, 0x88,
	0x6A, 0x95, 0x0C, 0x84, 0x34, 0x96, 0x0C, 0x84, 0x1D, 0x01, 0x06, 0xCC, 0x00, 0x33, 0x00, 0x84,
	0xC0, 0x20, 0x00, 0x23, 0xEA, 0x00, 0x81, 0x62, 0xA2, 0x0D, 0x80, 0x63, 0x07, 0xA6, 0x46, 0x04,
	0x00, 0x33, 0x18, 0x00, 0xC2, 0x88, 0x03, 0x03, 0x80, 0x63, 0xA3, 0x01, 0x07, 0xA4, 0x50, 0x04,
	0x23, 0x01, 0x00, 0xA2, 0x72, 0x04, 0x0A, 0xA0, 0x62, 0x04, 0xE0, 0x00, 0x00, 0x33, 0x1D, 0x00,
	0xC2, 0x88, 0x0B, 0xA0, 0x6E, 0x04, 0xE0, 0x00, 0x00, 0x33, 0x1E, 0x00, 0xC2, 0x88, 0x42, 0x23,
	0xF8, 0x88, 0x00, 0x23, 0x22, 0xA3, 0xD2, 0x04, 0x08, 0x23, 0x22, 0xA3, 0x8E, 0x04, 0x28, 0x23,
	0x22, 0xA3, 0x9A, 0x04, 0x02, 0x23, 0x22, 0xA3, 0xB0, 0x04, 0x42, 0x23, 0xF8, 0x88, 0x4A, 0x00,
	0x06, 0x61, 0x00, 0xA0, 0x9A, 0x04, 0x45, 0x23, 0xF8, 0x88, 0x06, 0x98, 0x00, 0xA2, 0xAC, 0x04,
	0xB4, 0x98, 0x00, 0x33, 0x00, 0x82, 0xC0, 0x20, 0x81, 0x62, 0xF4, 0x81, 0x47, 0x23, 0xF8, 0x88,
	0x04, 0x01, 0x0B, 0xDE, 0x06, 0x98, 0xB4, 0x98, 0x00, 0x33, 0x00, 0x81, 0xC0, 0x20, 0x81, 0x62,
	0x14, 0x01, 0x00, 0xA0, 0x0E, 0x02, 0x43, 0x23, 0xF8, 0x88, 0x04, 0x23, 0xA0, 0x01, 0x44, 0x23,
	0xA1, 0x01, 0x80, 0x73, 0x4D, 0x00, 0x03, 0xA3, 0xE0, 0x04, 0x00, 0x33, 0x27, 0x00, 0xC2, 0x88,
	0x04, 0x01, 0x04, 0xDC, 0x02, 0x23, 0xA2, 0x01, 0x04, 0x23, 0xA0, 0x01, 0x06, 0x98, 0x14, 0x95,
	0x4B, 0x00, 0xF6, 0x00, 0x4F, 0x04, 0x4F, 0x00, 0x00, 0xA3, 0x0E, 0x05, 0x00, 0x05, 0x76, 0x00,
	0x06, 0x61, 0x00, 0xA2, 0x08, 0x05, 0xF6, 0x84, 0x48, 0x97, 0xCD, 0x04, 0x12, 0x85, 0x48, 0x04,
	0xFF, 0x23, 0x84, 0x80, 0x02, 0x01, 0x03, 0xDA, 0x80, 0x23, 0x82, 0x01, 0x22, 0x85, 0x02, 0x23,
	0xA0, 0x01, 0x4A, 0x00, 0x06, 0x61, 0x00, 0xA2, 0x2E, 0x05, 0x1D, 0x01, 0x04, 0xD6, 0xFF, 0x23,
	0x86, 0x41, 0x4B, 0x60, 0xCB, 0x00, 0xFF, 0x23, 0x80, 0x01, 0x49, 0x00, 0x81, 0x01, 0x04, 0x01,
	0x02, 0xC8, 0x30, 0x01, 0x80, 0x01, 0xF7, 0x04, 0x03, 0x01, 0x49, 0x04, 0x80, 0x01, 0xC9, 0x00,
	0x00, 0x05, 0x00, 0x01, 0xFF, 0xA0, 0x4E, 0x05, 0x77, 0x04, 0x01, 0x23, 0xEA, 0x00, 0x5D, 0x00,
	0xFE, 0xC7, 0x00, 0x62, 0x00, 0x23, 0xEA, 0x00, 0x00, 0x63, 0x07, 0xA4, 0xCC, 0x05, 0x03, 0x03,
	0x02, 0xA0, 0x7C, 0x05, 0xC8, 0x85, 0x00, 0x33, 0x2D, 0x00, 0xC2, 0x88, 0x04, 0xA0, 0xA2, 0x05,
	0x80, 0x63, 0x4A, 0x00, 0x06, 0x61, 0x00, 0xA2, 0x8E, 0x05, 0x1D, 0x01, 0x06, 0xD6, 0x02, 0x23,
	0x02, 0x41, 0x82, 0x01, 0x50, 0x00, 0x64, 0x97, 0xF0, 0x84, 0x04, 0x23, 0x02, 0x41, 0x82, 0x01,
	0xF0, 0x84, 0x08, 0xA0, 0xA8, 0x05, 0xC8, 0x85, 0x03, 0xA0, 0xAE, 0x05, 0xC8, 0x85, 0x01, 0xA0,
	0xBA, 0x05, 0x88, 0x00, 0x80, 0x63, 0xB8, 0x96, 0x6A, 0x85, 0x07, 0xA0, 0xC6, 0x05, 0x06, 0x23,
	0x6A, 0x98, 0x48, 0x23, 0xF8, 0x88, 0xC8, 0x86, 0x80, 0x63, 0x6A, 0x85, 0x00, 0x63, 0x4A, 0x00,
	0x06, 0x61, 0x00, 0xA2, 0x0A, 0x06, 0x1D, 0x01, 0x18, 0xD4, 0xC0, 0x23, 0x07, 0x41, 0x83, 0x03,
	0x80, 0x63, 0x06, 0xA6, 0xEC, 0x05, 0x00, 0x33, 0x37, 0x00, 0xC2, 0x88, 0x1D, 0x01, 0x02, 0xD6,
	0x46, 0x23, 0xF8, 0x88, 0x63, 0x60, 0x83, 0x03, 0x80, 0x63, 0x06, 0xA6, 0x04, 0x06, 0x00, 0x33,
	0x38, 0x00, 0xC2, 0x88, 0xEF, 0x04, 0x6F, 0x00, 0x00, 0x63, 0x4B, 0x00, 0x06, 0x41, 0xCB, 0x00,
	0x52, 0x00, 0x06, 0x61, 0x00, 0xA2, 0x22, 0x06, 0x1D, 0x01, 0x03, 0xCA, 0xC0, 0x23, 0x07, 0x41,
	0x00, 0x63, 0x1D, 0x01, 0x04, 0xCC, 0x00, 0x33, 0x00, 0x83, 0xC0, 0x20, 0x81, 0x62, 0x80, 0x23,
	0x07, 0x41, 0x00, 0x63, 0x80, 0x67, 0x08, 0x23, 0x83, 0x03, 0x80, 0x63, 0x00, 0x63, 0x06, 0xA6,
	0x50, 0x06, 0x07, 0xA6, 0xA4, 0x06, 0x02, 0xA6, 0xFC, 0x06, 0x00, 0x33, 0x39, 0x00, 0xC2, 0x88,
	0x00, 0x00, 0x01, 0xA0, 0x16, 0x07, 0xCE, 0x95, 0x83, 0x03, 0x80, 0x63, 0x06, 0xA6, 0x64, 0x06,
	0x07, 0xA6, 0xA4, 0x06, 0x00, 0x00, 0x01, 0xA0, 0x16, 0x07, 0x00, 0x2B, 0x40, 0x0E, 0x80, 0x63,
	0x01, 0x00, 0x06, 0xA6, 0x80, 0x06, 0x07, 0xA6, 0xA4, 0x06, 0x00, 0x33, 0x3A, 0x00, 0xC2, 0x88,
	0x40, 0x0E, 0x80, 0x63, 0x00, 0x43, 0x00, 0xA0, 0x72, 0x06, 0x06, 0xA6, 0x98, 0x06, 0x07, 0xA6,
	0xA4, 0x06, 0x00, 0x33, 0x3B, 0x00, 0xC2, 0x88, 0x80, 0x67, 0x40, 0x0E, 0x80, 0x63, 0x07, 0xA6,
	0xA4, 0x06, 0x00, 0x63, 0x03, 0x03, 0x80, 0x63, 0x88, 0x00, 0x01, 0xA2, 0xB8, 0x06, 0x07, 0xA2,
	0xFC, 0x06, 0x00, 0x33, 0x35, 0x00, 0xC2, 0x88, 0x07, 0xA6, 0xC2, 0x06, 0x00, 0x33, 0x2A, 0x00,
	0xC2, 0x88, 0x03, 0x03, 0x03, 0xA2, 0xCE, 0x06, 0x07, 0x23, 0x80, 0x00, 0x08, 0x87, 0x80, 0x63,
	0x89, 0x00, 0x0A, 0x2B, 0x07, 0xA6, 0xDE, 0x06, 0x00, 0x33, 0x29, 0x00, 0xC2, 0x88, 0x00, 0x43,
	0x00, 0xA2, 0xEA, 0x06, 0xC0, 0x0E, 0x80, 0x63, 0xD4, 0x86, 0xC0, 0x0E, 0x00, 0x33, 0x00, 0x80,
	0xC0, 0x20, 0x81, 0x62, 0x04, 0x01, 0x08, 0xDA, 0x80, 0x63, 0x00, 0x63, 0x80, 0x67, 0x00, 0x33,
	0x00, 0x40, 0xC0, 0x20, 0x81, 0x62, 0x00, 0x63, 0x80, 0x7B, 0x80, 0x63, 0x06, 0xA6, 0x5C, 0x06,
	0x00, 0x33, 0x2C, 0x00, 0xC2, 0x88, 0x0C, 0xA2, 0x30, 0x07, 0xCE, 0x95, 0x83, 0x03, 0x80, 0x63,
	0x06, 0xA6, 0x2E, 0x07, 0x07, 0xA6, 0xA4, 0x06, 0x00, 0x33, 0x3D, 0x00, 0xC2, 0x88, 0x00, 0x00,
	0x80, 0x67, 0x83, 0x03, 0x80, 0x63, 0x0C, 0xA0, 0x46, 0x07, 0x07, 0xA6, 0xA4, 0x06, 0xBF, 0x23,
	0x04, 0x61, 0x84, 0x01, 0xD2, 0x84, 0x00, 0x63, 0xF0, 0x04, 0x01, 0x01, 0xF1, 0x00, 0x00, 0x01,
	0xF2, 0x00, 0x01, 0x05, 0x80, 0x01, 0x72, 0x04, 0x71, 0x00, 0x81, 0x01, 0x70, 0x04, 0x80, 0x05,
	0x81, 0x05, 0x00, 0x63, 0xF0, 0x04, 0xF2, 0x00, 0x72, 0x04, 0x01, 0x01, 0xF1, 0x00, 0x70, 0x00,
	0x81, 0x01, 0x70, 0x04, 0x71, 0x00, 0x81, 0x01, 0x72, 0x00, 0x80, 0x01, 0x71, 0x04, 0x70, 0x00,
	0x80, 0x01, 0x70, 0x04, 0x00, 0x63, 0xF0, 0x04, 0xF2, 0x00, 0x72, 0x04, 0x00, 0x01, 0xF1, 0x00,
	0x70, 0x00, 0x80, 0x01, 0x70, 0x04, 0x71, 0x00, 0x80, 0x01, 0x72, 0x00, 0x81, 0x01, 0x71, 0x04,
	0x70, 0x00, 0x81, 0x01, 0x70, 0x04, 0x00, 0x63, 0x00, 0x23, 0xB3, 0x01, 0x83, 0x05, 0xA3, 0x01,
	0xA2, 0x01, 0xA1, 0x01, 0x01, 0x23, 0xA0, 0x01, 0x00, 0x01, 0xC8, 0x00, 0x03, 0xA1, 0xC6, 0x07,
	0x00, 0x33, 0x07, 0x00, 0xC2, 0x88, 0x80, 0x05, 0x81, 0x05, 0x04, 0x01, 0x11, 0xC8, 0x48, 0x00,
	0xB0, 0x01, 0xB1, 0x01, 0x08, 0x23, 0xB2, 0x01, 0x05, 0x01, 0x48, 0x04, 0x00, 0x43, 0x00, 0xA2,
	0xE6, 0x07, 0x00, 0x05, 0xDC, 0x87, 0x00, 0x01, 0xC8, 0x00, 0xFF, 0x23, 0x80, 0x01, 0x05, 0x05,
	0x00, 0x63, 0xF7, 0x04, 0x1A, 0x09, 0xF6, 0x08, 0x6E, 0x04, 0x00, 0x02, 0x80, 0x43, 0x76, 0x08,
	0x80, 0x02, 0x77, 0x04, 0x00, 0x63, 0xF7, 0x04, 0x1A, 0x09, 0xF6, 0x08, 0x6E, 0x04, 0x00, 0x02,
	0x00, 0xA0, 0x16, 0x08, 0x18, 0x88, 0x00, 0x43, 0x76, 0x08, 0x80, 0x02, 0x77, 0x04, 0x00, 0x63,
	0xF3, 0x04, 0x00, 0x23, 0xF4, 0x00, 0x74, 0x00, 0x80, 0x43, 0xF4, 0x00, 0xCF, 0x40, 0x00, 0xA2,
	0x46, 0x08, 0x74, 0x04, 0x02, 0x01, 0xF7, 0xC9, 0xF6, 0xD9, 0x00, 0x01, 0x01, 0xA1, 0x26, 0x08,
	0x06, 0x98, 0x14, 0x95, 0x26, 0x88, 0x73, 0x04, 0x00, 0x63, 0xF3, 0x04, 0x75, 0x04, 0x5C, 0x88,
	0x02, 0x01, 0x04, 0xD8, 0x48, 0x97, 0x06, 0x98, 0x14, 0x95, 0x4C, 0x88, 0x75, 0x00, 0x00, 0xA3,
	0x66, 0x08, 0x00, 0x05, 0x50, 0x88, 0x73, 0x04, 0x00, 0x63, 0x80, 0x7B, 0x80, 0x63, 0x06, 0xA6,
	0x78, 0x08, 0x00, 0x33, 0x3E, 0x00, 0xC2, 0x88, 0x80, 0x67, 0x83, 0x03, 0x80, 0x63, 0x00, 0x63,
	0x38, 0x2B, 0x9E, 0x88, 0x38, 0x2B, 0x94, 0x88, 0x32, 0x09, 0x31, 0x05, 0x94, 0x98, 0x05, 0x05,
	0xB2, 0x09, 0x00, 0x63, 0x00, 0x32, 0x00, 0x36, 0x00, 0x3A, 0x00, 0x3E, 0x00, 0x63, 0x80, 0x32,
	0x80, 0x36, 0x80, 0x3A, 0x80, 0x3E, 0x00, 0x63, 0x38, 0x2B, 0x40, 0x32, 0x40, 0x36, 0x40, 0x3A,
	0x40, 0x3E, 0x00, 0x63, 0x5A, 0x20, 0xC9, 0x40, 0x00, 0xA0, 0xB4, 0x08, 0x5D, 0x00, 0xFE, 0xC3,
	0x00, 0x63, 0x80, 0x73, 0xE6, 0x20, 0x02, 0x23, 0xE8, 0x00, 0x82, 0x73, 0xFF, 0xFD, 0x80, 0x73,
	0x13, 0x23, 0xF8, 0x88, 0x66, 0x20, 0xC0, 0x20, 0x04, 0x23, 0xA0, 0x01, 0xA1, 0x23, 0xA1, 0x01,
	0x81, 0x62, 0xE2, 0x88, 0x80, 0x73, 0x80, 0x77, 0x68, 0x00, 0x00, 0xA2, 0x80, 0x00, 0x03, 0xC2,
	0xF1, 0xC7, 0x41, 0x23, 0xF8, 0x88, 0x11, 0x23, 0xA1, 0x01, 0x04, 0x23, 0xA0, 0x01, 0xD2, 0x84,
};

ushort              _mcode_size = sizeof (_mcode_buf);
ulong               _mcode_chksum = 0x012CD3FFUL;
#define ASC_SYN_OFFSET_ONE_DISABLE_LIST  16
uchar               _syn_offset_one_disable_cmd[ASC_SYN_OFFSET_ONE_DISABLE_LIST] =
{
	SCSICMD_Inquiry,
	SCSICMD_RequestSense,
	SCSICMD_ReadCapacity,
	SCSICMD_ReadTOC,
	SCSICMD_ModeSelect6,
	SCSICMD_ModeSense6,
	SCSICMD_ModeSelect10,
	SCSICMD_ModeSense10,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF
};

int
AscExeScsiQueue(
				   REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				   REG ASC_SCSI_Q dosfar * scsiq
)
{
	PortAddr            iop_base;
	int                 last_int_level;
	int                 sta;
	int                 n_q_required;
	int                 disable_syn_offset_one_fix;
	int                 i;
	ulong               addr;
	ASC_EXE_CALLBACK    asc_exe_callback;
	ushort              sg_entry_cnt = 0;
	ushort              sg_entry_cnt_minus_one = 0;
	uchar               target_ix;
	uchar               tid_no;
	uchar               sdtr_data;
	uchar               extra_bytes;
	uchar               scsi_cmd;
	uchar               disable_cmd;
	ASC_SG_HEAD dosfar *sg_head;
	ulong               data_cnt;
#if CC_LINK_BUSY_Q
	ASC_SCSI_Q dosfar  *scsiq_tail;
	ASC_SCSI_Q dosfar  *scsiq_next;
	ASC_SCSI_Q dosfar  *scsiq_prev;
#endif
	iop_base = asc_dvc->iop_base;
	sg_head = scsiq->sg_head;
	asc_exe_callback = (ASC_EXE_CALLBACK) asc_dvc->exe_callback;
	if (asc_dvc->err_code != 0)
		return (ERR);
	if (scsiq == (ASC_SCSI_Q dosfar *) 0L) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SCSIQ_NULL_PTR);
		return (ERR);
	}
	scsiq->q1.q_no = 0;
	scsiq->q1.extra_bytes = 0;
	sta = 0;
	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	n_q_required = 1;
	if (scsiq->cdbptr[0] == SCSICMD_RequestSense) {
		if (((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) &&
			((asc_dvc->sdtr_done & scsiq->q1.target_id) != 0)) {
			sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
			AscMsgOutSDTR(asc_dvc,
						  asc_dvc->sdtr_period_tbl[(sdtr_data >> 4) & (uchar) (ASC_SYN_XFER_NO - 1)],
						  (uchar) (sdtr_data & (uchar) ASC_SYN_MAX_OFFSET));
			scsiq->q1.cntl |= (QC_MSG_OUT | QC_URGENT);
		}
	}
	last_int_level = DvcEnterCritical();
	if (asc_dvc->in_critical_cnt != 0) {
		DvcLeaveCritical(last_int_level);
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CRITICAL_RE_ENTRY);
		return (ERR);
	}
	asc_dvc->in_critical_cnt++;
	if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
		if ((sg_entry_cnt = sg_head->entry_cnt) == 0) {
			asc_dvc->in_critical_cnt--;
			DvcLeaveCritical(last_int_level);
			return (ERR);
		}
		if (sg_entry_cnt > ASC_MAX_SG_LIST) {
			return (ERR);
		}
		if (sg_entry_cnt == 1) {
			scsiq->q1.data_addr = sg_head->sg_list[0].addr;
			scsiq->q1.data_cnt = sg_head->sg_list[0].bytes;
			scsiq->q1.cntl &= ~(QC_SG_HEAD | QC_SG_SWAP_QUEUE);
		} else {
#if CC_CHK_AND_COALESCE_SG_LIST
			AscCoalesceSgList(scsiq);
			sg_entry_cnt = sg_head->entry_cnt;
#endif
		}
		sg_entry_cnt_minus_one = sg_entry_cnt - 1;
#if CC_DEBUG_SG_LIST
		if (asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL | ASC_IS_EISA)) {
			for (i = 0; i < sg_entry_cnt_minus_one; i++) {
				addr = sg_head->sg_list[i].addr + sg_head->sg_list[i].bytes;
				if (((ushort) addr & 0x0003) != 0) {
					asc_dvc->in_critical_cnt--;
					DvcLeaveCritical(last_int_level);
					AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SG_LIST_ODD_ADDRESS);
					return (ERR);
				}
			}
		}
#endif
	}
	scsi_cmd = scsiq->cdbptr[0];
	disable_syn_offset_one_fix = FALSE;
	if (
		   (asc_dvc->pci_fix_asyn_xfer & scsiq->q1.target_id)
		   && !(asc_dvc->pci_fix_asyn_xfer_always & scsiq->q1.target_id)
	  ) {
		if (scsiq->q1.cntl & QC_SG_HEAD) {
			data_cnt = 0;
			for (i = 0; i < sg_entry_cnt; i++) {
				data_cnt += sg_head->sg_list[i].bytes;
			}
		} else {
			data_cnt = scsiq->q1.data_cnt;
		}
		if (data_cnt != 0UL) {
			if (data_cnt < 512UL) {
				disable_syn_offset_one_fix = TRUE;
			} else {
				for (i = 0; i < ASC_SYN_OFFSET_ONE_DISABLE_LIST; i++) {
					disable_cmd = _syn_offset_one_disable_cmd[i];
					if (disable_cmd == 0xFF) {
						break;
					}
					if (scsi_cmd == disable_cmd) {
						disable_syn_offset_one_fix = TRUE;
						break;
					}
				}
			}
		}
	}
	if (disable_syn_offset_one_fix) {
		scsiq->q2.tag_code &= ~M2_QTAG_MSG_SIMPLE;
		scsiq->q2.tag_code |= (ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX |
							   ASC_TAG_FLAG_DISABLE_DISCONNECT);
	} else {
		scsiq->q2.tag_code &= 0x23;
	}
	if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
		if (asc_dvc->bug_fix_cntl) {
			if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if (
					   (scsi_cmd == SCSICMD_Read6)
					   || (scsi_cmd == SCSICMD_Read10)
				  ) {
					addr = sg_head->sg_list[sg_entry_cnt_minus_one].addr +
					  sg_head->sg_list[sg_entry_cnt_minus_one].bytes;
					extra_bytes = (uchar) ((ushort) addr & 0x0003);
					if (extra_bytes != 0) {
						scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
						scsiq->q1.extra_bytes = extra_bytes;
						sg_head->sg_list[sg_entry_cnt_minus_one].bytes -= (ulong) extra_bytes;
					}
				}
			}
		}
		sg_head->entry_to_copy = sg_head->entry_cnt;
		n_q_required = AscSgListToQueue(sg_entry_cnt);
#if CC_LINK_BUSY_Q
		scsiq_next = (ASC_SCSI_Q dosfar *) asc_dvc->scsiq_busy_head[tid_no];
		if (scsiq_next != (ASC_SCSI_Q dosfar *) 0L) {
			goto link_scisq_to_busy_list;
		}
#endif
		if (
			   (AscGetNumOfFreeQueue(asc_dvc, target_ix, n_q_required)
				>= (uint) n_q_required) ||
			   ((scsiq->q1.cntl & QC_URGENT) != 0)
		  ) {
			if ((sta = AscSendScsiQueue(asc_dvc, scsiq,
										n_q_required)) == 1) {
				asc_dvc->in_critical_cnt--;
				if (asc_exe_callback != 0) {
					(*asc_exe_callback) (asc_dvc, scsiq);
				}
				DvcLeaveCritical(last_int_level);
				return (sta);
			}
		}
	} else {
		if (asc_dvc->bug_fix_cntl) {
			if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if (
					   (scsi_cmd == SCSICMD_Read6)
					   || (scsi_cmd == SCSICMD_Read10)
				  ) {
					addr = scsiq->q1.data_addr + scsiq->q1.data_cnt;
					extra_bytes = (uchar) ((ushort) addr & 0x0003);
					if (extra_bytes != 0) {
						if (((ushort) scsiq->q1.data_cnt & 0x01FF) == 0) {
							scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
							scsiq->q1.data_cnt -= (ulong) extra_bytes;
							scsiq->q1.extra_bytes = extra_bytes;
						}
					}
				}
			}
		}
		n_q_required = 1;
#if CC_LINK_BUSY_Q
		scsiq_next = (ASC_SCSI_Q dosfar *) asc_dvc->scsiq_busy_head[tid_no];
		if (scsiq_next != (ASC_SCSI_Q dosfar *) 0L) {
			goto link_scisq_to_busy_list;
		}
#endif
		if ((AscGetNumOfFreeQueue(asc_dvc, target_ix, 1) >= 1) ||
			((scsiq->q1.cntl & QC_URGENT) != 0)) {
			if ((sta = AscSendScsiQueue(asc_dvc, scsiq,
										n_q_required)) == 1) {
				asc_dvc->in_critical_cnt--;
				if (asc_exe_callback != 0) {
					(*asc_exe_callback) (asc_dvc, scsiq);
				}
				DvcLeaveCritical(last_int_level);
				return (sta);
			}
		}
	}
#if CC_LINK_BUSY_Q
	if (sta == 0) {
	  link_scisq_to_busy_list:
		scsiq->ext.q_required = n_q_required;
		if (scsiq_next == (ASC_SCSI_Q dosfar *) 0L) {
			asc_dvc->scsiq_busy_head[tid_no] = (ASC_SCSI_Q dosfar *) scsiq;
			asc_dvc->scsiq_busy_tail[tid_no] = (ASC_SCSI_Q dosfar *) scsiq;
			scsiq->ext.next = (ASC_SCSI_Q dosfar *) 0L;
			scsiq->ext.join = (ASC_SCSI_Q dosfar *) 0L;
			scsiq->q1.status = QS_BUSY;
			sta = 1;
		} else {
			scsiq_tail = (ASC_SCSI_Q dosfar *) asc_dvc->scsiq_busy_tail[tid_no];
			if (scsiq_tail->ext.next == (ASC_SCSI_Q dosfar *) 0L) {
				if ((scsiq->q1.cntl & QC_URGENT) != 0) {
					asc_dvc->scsiq_busy_head[tid_no] = (ASC_SCSI_Q dosfar *) scsiq;
					scsiq->ext.next = scsiq_next;
					scsiq->ext.join = (ASC_SCSI_Q dosfar *) 0L;
				} else {
					if (scsiq->ext.cntl & QCX_SORT) {
						do {
							scsiq_prev = scsiq_next;
							scsiq_next = scsiq_next->ext.next;
							if (scsiq->ext.lba < scsiq_prev->ext.lba)
								break;
						} while (scsiq_next != (ASC_SCSI_Q dosfar *) 0L);
						scsiq_prev->ext.next = scsiq;
						scsiq->ext.next = scsiq_next;
						if (scsiq_next == (ASC_SCSI_Q dosfar *) 0L) {
							asc_dvc->scsiq_busy_tail[tid_no] = (ASC_SCSI_Q dosfar *) scsiq;
						}
						scsiq->ext.join = (ASC_SCSI_Q dosfar *) 0L;
					} else {
						scsiq_tail->ext.next = (ASC_SCSI_Q dosfar *) scsiq;
						asc_dvc->scsiq_busy_tail[tid_no] = (ASC_SCSI_Q dosfar *) scsiq;
						scsiq->ext.next = (ASC_SCSI_Q dosfar *) 0L;
						scsiq->ext.join = (ASC_SCSI_Q dosfar *) 0L;
					}
				}
				scsiq->q1.status = QS_BUSY;
				sta = 1;
			} else {
				AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SCSIQ_BAD_NEXT_PTR);
				sta = ERR;
			}
		}
	}
#endif
	asc_dvc->in_critical_cnt--;
	DvcLeaveCritical(last_int_level);
	return (sta);
}

int
AscSendScsiQueue(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					REG ASC_SCSI_Q dosfar * scsiq,
					uchar n_q_required
)
{
	PortAddr            iop_base;
	uchar               free_q_head;
	uchar               next_qp;
	uchar               tid_no;
	uchar               target_ix;
	int                 sta;
	iop_base = asc_dvc->iop_base;
	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	sta = 0;
	free_q_head = (uchar) AscGetVarFreeQHead(iop_base);
	if (n_q_required > 1) {
		if ((next_qp = AscAllocMultipleFreeQueue(iop_base,
									   free_q_head, (uchar) (n_q_required)))
			!= (uchar) ASC_QLINK_END) {
			asc_dvc->last_q_shortage = 0;
			scsiq->sg_head->queue_cnt = n_q_required - 1;
			scsiq->q1.q_no = free_q_head;
			if ((sta = AscPutReadySgListQueue(asc_dvc, scsiq,
											  free_q_head)) == 1) {
#if CC_WRITE_IO_COUNT
				asc_dvc->req_count++;
#endif
				AscPutVarFreeQHead(iop_base, next_qp);
				asc_dvc->cur_total_qng += (uchar) (n_q_required);
				asc_dvc->cur_dvc_qng[tid_no]++;
			}
			return (sta);
		}
	} else if (n_q_required == 1) {
		if ((next_qp = AscAllocFreeQueue(iop_base,
										 free_q_head)) != ASC_QLINK_END) {
			scsiq->q1.q_no = free_q_head;
			if ((sta = AscPutReadyQueue(asc_dvc, scsiq,
										free_q_head)) == 1) {
#if CC_WRITE_IO_COUNT
				asc_dvc->req_count++;
#endif
				AscPutVarFreeQHead(iop_base, next_qp);
				asc_dvc->cur_total_qng++;
				asc_dvc->cur_dvc_qng[tid_no]++;
			}
			return (sta);
		}
	}
	return (sta);
}

int
AscSgListToQueue(
					int sg_list
)
{
	int                 n_sg_list_qs;
	n_sg_list_qs = ((sg_list - 1) / ASC_SG_LIST_PER_Q);
	if (((sg_list - 1) % ASC_SG_LIST_PER_Q) != 0)
		n_sg_list_qs++;
	return (n_sg_list_qs + 1);
}


uint
AscGetNumOfFreeQueue(
						REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						uchar target_ix,
						uchar n_qs
)
{
	uint                cur_used_qs;
	uint                cur_free_qs;
	ASC_SCSI_BIT_ID_TYPE target_id;
	uchar               tid_no;
	target_id = ASC_TIX_TO_TARGET_ID(target_ix);
	tid_no = ASC_TIX_TO_TID(target_ix);
	if ((asc_dvc->unit_not_ready & target_id) ||
		(asc_dvc->queue_full_or_busy & target_id)) {
		return (0);
	}
	if (n_qs == 1) {
		cur_used_qs = (uint) asc_dvc->cur_total_qng +
		  (uint) asc_dvc->last_q_shortage +
		  (uint) ASC_MIN_FREE_Q;
	} else {
		cur_used_qs = (uint) asc_dvc->cur_total_qng +
		  (uint) ASC_MIN_FREE_Q;
	}
	if ((uint) (cur_used_qs + n_qs) <= (uint) asc_dvc->max_total_qng) {
		cur_free_qs = (uint) asc_dvc->max_total_qng - cur_used_qs;
		if (asc_dvc->cur_dvc_qng[tid_no] >=
			asc_dvc->max_dvc_qng[tid_no]) {
			return (0);
		}
		return (cur_free_qs);
	}
	if (n_qs > 1) {
		if (n_qs > asc_dvc->last_q_shortage) {
			asc_dvc->last_q_shortage = n_qs;
		}
	}
	return (0);
}

int
AscPutReadyQueue(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					REG ASC_SCSI_Q dosfar * scsiq,
					uchar q_no
)
{
	ushort              q_addr;
	uchar               tid_no;
	uchar               sdtr_data;
	uchar               syn_period_ix;
	uchar               syn_offset;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	if (((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) &&
		((asc_dvc->sdtr_done & scsiq->q1.target_id) == 0)) {
		tid_no = ASC_TIX_TO_TID(scsiq->q2.target_ix);
		sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
		syn_period_ix = (sdtr_data >> 4) & (ASC_SYN_XFER_NO - 1);
		syn_offset = sdtr_data & ASC_SYN_MAX_OFFSET;
		AscMsgOutSDTR(asc_dvc,
					  asc_dvc->sdtr_period_tbl[syn_period_ix],
					  syn_offset);
		scsiq->q1.cntl |= QC_MSG_OUT;
	}
	q_addr = ASC_QNO_TO_QADDR(q_no);
	if ((scsiq->q1.target_id & asc_dvc->use_tagged_qng) == 0) {
		scsiq->q2.tag_code &= ~M2_QTAG_MSG_SIMPLE;
	}
	scsiq->q1.status = QS_FREE;
	AscMemWordCopyToLram(iop_base,
						 (ushort) (q_addr + (ushort) ASC_SCSIQ_CDB_BEG),
						 (ushort dosfar *) scsiq->cdbptr,
						 (ushort) ((ushort) scsiq->q2.cdb_len >> 1));
#if !CC_LITTLE_ENDIAN_HOST
	AscAdjEndianScsiQ(scsiq);
#endif
	DvcPutScsiQ(iop_base,
				(ushort) (q_addr + (ushort) ASC_SCSIQ_CPY_BEG),
				(ushort dosfar *) & scsiq->q1.cntl,
	  (ushort) ((((sizeof (ASC_SCSIQ_1) + sizeof (ASC_SCSIQ_2)) / 2) - 1)));
#if CC_WRITE_IO_COUNT
	AscWriteLramWord(iop_base,
					 (ushort) (q_addr + (ushort) ASC_SCSIQ_W_REQ_COUNT),
					 (ushort) asc_dvc->req_count);
#endif
#if CC_VERIFY_LRAM_COPY
	if ((asc_dvc->dvc_cntl & ASC_CNTL_NO_VERIFY_COPY) == 0) {
		if (AscMemWordCmpToLram(iop_base,
							 (ushort) (q_addr + (ushort) ASC_SCSIQ_CDB_BEG),
								(ushort dosfar *) scsiq->cdbptr,
								(ushort) (scsiq->q2.cdb_len >> 1)) != 0) {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_LOCAL_MEM);
			return (ERR);
		}
		if (AscMemWordCmpToLram(iop_base,
							 (ushort) (q_addr + (ushort) ASC_SCSIQ_CPY_BEG),
								(ushort dosfar *) & scsiq->q1.cntl,
		 (ushort) (((sizeof (ASC_SCSIQ_1) + sizeof (ASC_SCSIQ_2)) / 2) - 1))
			!= 0) {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_LOCAL_MEM);
			return (ERR);
		}
	}
#endif
#if CC_CLEAR_DMA_REMAIN
	AscWriteLramDWord(iop_base,
		   (ushort) (q_addr + (ushort) ASC_SCSIQ_DW_REMAIN_XFER_ADDR), 0UL);
	AscWriteLramDWord(iop_base,
			(ushort) (q_addr + (ushort) ASC_SCSIQ_DW_REMAIN_XFER_CNT), 0UL);
#endif
	AscWriteLramWord(iop_base,
					 (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
			 (ushort) (((ushort) scsiq->q1.q_no << 8) | (ushort) QS_READY));
	return (1);
}

int
AscPutReadySgListQueue(
						  REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						  REG ASC_SCSI_Q dosfar * scsiq,
						  uchar q_no
)
{
	int                 sta;
	int                 i;
	ASC_SG_HEAD dosfar *sg_head;
	ASC_SG_LIST_Q       scsi_sg_q;
	ulong               saved_data_addr;
	ulong               saved_data_cnt;
	PortAddr            iop_base;
	ushort              sg_list_dwords;
	ushort              sg_index;
	ushort              sg_entry_cnt;
	ushort              q_addr;
	uchar               next_qp;
	iop_base = asc_dvc->iop_base;
	sg_head = scsiq->sg_head;
	saved_data_addr = scsiq->q1.data_addr;
	saved_data_cnt = scsiq->q1.data_cnt;
	scsiq->q1.data_addr = sg_head->sg_list[0].addr;
	scsiq->q1.data_cnt = sg_head->sg_list[0].bytes;
	sg_entry_cnt = sg_head->entry_cnt - 1;
	if (sg_entry_cnt != 0) {
		scsiq->q1.cntl |= QC_SG_HEAD;
		q_addr = ASC_QNO_TO_QADDR(q_no);
		sg_index = 1;
		scsiq->q1.sg_queue_cnt = sg_head->queue_cnt;
		scsi_sg_q.sg_head_qp = q_no;
		scsi_sg_q.cntl = QCSG_SG_XFER_LIST;
		for (i = 0; i < sg_head->queue_cnt; i++) {
			scsi_sg_q.seq_no = i + 1;
			if (sg_entry_cnt > ASC_SG_LIST_PER_Q) {
				sg_list_dwords = (uchar) (ASC_SG_LIST_PER_Q * 2);
				sg_entry_cnt -= ASC_SG_LIST_PER_Q;
				if (i == 0) {
					scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q;
					scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q;
				} else {
					scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q - 1;
					scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q - 1;
				}
			} else {
				scsi_sg_q.cntl |= QCSG_SG_XFER_END;
				sg_list_dwords = sg_entry_cnt << 1;
				if (i == 0) {
					scsi_sg_q.sg_list_cnt = sg_entry_cnt;
					scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt;
				} else {
					scsi_sg_q.sg_list_cnt = sg_entry_cnt - 1;
					scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt - 1;
				}
				sg_entry_cnt = 0;
			}
			next_qp = AscReadLramByte(iop_base,
									  (ushort) (q_addr + ASC_SCSIQ_B_FWD));
			scsi_sg_q.q_no = next_qp;
			q_addr = ASC_QNO_TO_QADDR(next_qp);
			AscMemWordCopyToLram(iop_base,
								 (ushort) (q_addr + ASC_SCSIQ_SGHD_CPY_BEG),
								 (ushort dosfar *) & scsi_sg_q,
								 (ushort) (sizeof (ASC_SG_LIST_Q) >> 1));
			AscMemDWordCopyToLram(iop_base,
								  (ushort) (q_addr + ASC_SGQ_LIST_BEG),
							  (ulong dosfar *) & sg_head->sg_list[sg_index],
								  (ushort) sg_list_dwords);
			sg_index += ASC_SG_LIST_PER_Q;
		}
	} else {
		scsiq->q1.cntl &= ~QC_SG_HEAD;
	}
	sta = AscPutReadyQueue(asc_dvc, scsiq, q_no);
	scsiq->q1.data_addr = saved_data_addr;
	scsiq->q1.data_cnt = saved_data_cnt;
	return (sta);
}

int
AscAbortSRB(
			   REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
			   ulong srb_ptr
)
{
	int                 sta;
	ASC_SCSI_BIT_ID_TYPE saved_unit_not_ready;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	sta = ERR;
	saved_unit_not_ready = asc_dvc->unit_not_ready;
	asc_dvc->unit_not_ready = 0xFF;
	AscWaitISRDone(asc_dvc);
	if (AscStopQueueExe(iop_base) == 1) {
		if (AscRiscHaltedAbortSRB(asc_dvc, srb_ptr) == 1) {
			sta = 1;
			AscCleanUpBusyQueue(iop_base);
			AscStartQueueExe(iop_base);
		} else {
			sta = 0;
			AscStartQueueExe(iop_base);
		}
	}
	asc_dvc->unit_not_ready = saved_unit_not_ready;
	return (sta);
}

int
AscResetDevice(
				  REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				  uchar target_ix
)
{
	PortAddr            iop_base;
	int                 sta;
	uchar               tid_no;
	ASC_SCSI_BIT_ID_TYPE target_id;
	int                 i;
	ASC_SCSI_REQ_Q      scsiq_buf;
	ASC_SCSI_REQ_Q dosfar *scsiq;
	uchar dosfar       *buf;
	ASC_SCSI_BIT_ID_TYPE saved_unit_not_ready;
	iop_base = asc_dvc->iop_base;
	tid_no = ASC_TIX_TO_TID(target_ix);
	target_id = ASC_TID_TO_TARGET_ID(tid_no);
	saved_unit_not_ready = asc_dvc->unit_not_ready;
	asc_dvc->unit_not_ready = target_id;
	sta = ERR;
	AscWaitTixISRDone(asc_dvc, target_ix);
	if (AscStopQueueExe(iop_base) == 1) {
		if (AscRiscHaltedAbortTIX(asc_dvc, target_ix) == 1) {
			AscCleanUpBusyQueue(iop_base);
			AscStartQueueExe(iop_base);
			AscWaitTixISRDone(asc_dvc, target_ix);
			sta = TRUE;
			scsiq = (ASC_SCSI_REQ_Q dosfar *) & scsiq_buf;
			buf = (uchar dosfar *) & scsiq_buf;
			for (i = 0; i < sizeof (ASC_SCSI_REQ_Q); i++) {
				*buf++ = 0x00;
			}
			scsiq->r1.status = (uchar) QS_READY;
			scsiq->r2.cdb_len = 6;
			scsiq->r2.tag_code = M2_QTAG_MSG_SIMPLE;
			scsiq->r1.target_id = target_id;
			scsiq->r2.target_ix = ASC_TIDLUN_TO_IX(tid_no, 0);
			scsiq->cdbptr = (uchar dosfar *) scsiq->cdb;
			scsiq->r1.cntl = QC_NO_CALLBACK | QC_MSG_OUT | QC_URGENT;
			AscWriteLramByte(asc_dvc->iop_base, ASCV_MSGOUT_BEG,
							 M1_BUS_DVC_RESET);
			asc_dvc->unit_not_ready &= ~target_id;
			asc_dvc->sdtr_done |= target_id;
			if (AscExeScsiQueue(asc_dvc, (ASC_SCSI_Q dosfar *) scsiq)
				== 1) {
				asc_dvc->unit_not_ready = target_id;
				DvcSleepMilliSecond(1000);
				_AscWaitQDone(iop_base, (ASC_SCSI_Q dosfar *) scsiq);
				if (AscStopQueueExe(iop_base) == 1) {
					AscCleanUpDiscQueue(iop_base);
					AscStartQueueExe(iop_base);
					if (asc_dvc->pci_fix_asyn_xfer & target_id) {
						AscSetRunChipSynRegAtID(iop_base, tid_no,
											 ASYN_SDTR_DATA_FIX_PCI_REV_AB);
					}
					AscWaitTixISRDone(asc_dvc, target_ix);
				}
			} else {
				sta = 0;
			}
			asc_dvc->sdtr_done &= ~target_id;
		} else {
			sta = ERR;
			AscStartQueueExe(iop_base);
		}
	}
	asc_dvc->unit_not_ready = saved_unit_not_ready;
	return (sta);
}

int
AscResetSB(
			  REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	int                 sta;
	int                 i;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	asc_dvc->unit_not_ready = 0xFF;
	sta = TRUE;
	AscWaitISRDone(asc_dvc);
	AscStopQueueExe(iop_base);
	asc_dvc->sdtr_done = 0;
	AscResetChipAndScsiBus(iop_base);
	DvcSleepMilliSecond((ulong) ((ushort) asc_dvc->scsi_reset_wait * 1000));
	AscReInitLram(asc_dvc);
	for (i = 0; i <= ASC_MAX_TID; i++) {
		asc_dvc->cur_dvc_qng[i] = 0;
		if (asc_dvc->pci_fix_asyn_xfer & (ASC_SCSI_BIT_ID_TYPE) (0x01 << i)) {
			AscSetChipSynRegAtID(iop_base, i, ASYN_SDTR_DATA_FIX_PCI_REV_AB);
		}
	}
	asc_dvc->err_code = 0;
	AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
	if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
		sta = ERR;
	}
	if (AscStartChip(iop_base) == 0) {
		sta = ERR;
	}
	AscStartQueueExe(iop_base);
	asc_dvc->unit_not_ready = 0;
	asc_dvc->queue_full_or_busy = 0;
	return (sta);
}

int
AscSetRunChipSynRegAtID(
						   PortAddr iop_base,
						   uchar tid_no,
						   uchar sdtr_data
)
{
	int                 sta = FALSE;
	if (AscHostReqRiscHalt(iop_base)) {
		sta = AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
		AscStartChip(iop_base);
		return (sta);
	}
	return (sta);
}

int
AscSetChipSynRegAtID(
						PortAddr iop_base,
						uchar id,
						uchar sdtr_data
)
{
	ASC_SCSI_BIT_ID_TYPE org_id;
	int                 i;
	int                 sta;
	sta = TRUE;
	AscSetBank(iop_base, 1);
	org_id = AscReadChipDvcID(iop_base);
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if (org_id == (0x01 << i))
			break;
	}
	org_id = i;
	AscWriteChipDvcID(iop_base, id);
	if (AscReadChipDvcID(iop_base) == (0x01 << id)) {
		AscSetBank(iop_base, 0);
		AscSetChipSyn(iop_base, sdtr_data);
		if (AscGetChipSyn(iop_base) != sdtr_data) {
			sta = FALSE;
		}
	} else {
		sta = FALSE;
	}
	AscSetBank(iop_base, 1);
	AscWriteChipDvcID(iop_base, org_id);
	AscSetBank(iop_base, 0);
	return (sta);
}

int
AscReInitLram(
				 REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	AscInitLram(asc_dvc);
	AscInitQLinkVar(asc_dvc);
	return (0);
}

ushort
AscInitLram(
			   REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	uchar               i;
	ushort              s_addr;
	PortAddr            iop_base;
	ushort              warn_code;
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	AscMemWordSetLram(iop_base, ASC_QADR_BEG, 0,
			   (ushort) (((int) (asc_dvc->max_total_qng + 2 + 1) * 64) >> 1)
	  );
	i = ASC_MIN_ACTIVE_QNO;
	s_addr = ASC_QADR_BEG + ASC_QBLK_SIZE;
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
					 (uchar) (i + 1));
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
					 (uchar) (asc_dvc->max_total_qng));
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
					 (uchar) i);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i < asc_dvc->max_total_qng; i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
						 (uchar) (i + 1));
		AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
						 (uchar) (i - 1));
		AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
						 (uchar) i);
	}
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
					 (uchar) ASC_QLINK_END);
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
					 (uchar) (asc_dvc->max_total_qng - 1));
	AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
					 (uchar) asc_dvc->max_total_qng);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i <= (uchar) (asc_dvc->max_total_qng + 3);
		 i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iop_base,
						 (ushort) (s_addr + (ushort) ASC_SCSIQ_B_FWD), i);
		AscWriteLramByte(iop_base,
						 (ushort) (s_addr + (ushort) ASC_SCSIQ_B_BWD), i);
		AscWriteLramByte(iop_base,
						 (ushort) (s_addr + (ushort) ASC_SCSIQ_B_QNO), i);
	}
	return (warn_code);
}

ushort
AscInitQLinkVar(
				   REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	int                 i;
	ushort              lram_addr;
	iop_base = asc_dvc->iop_base;
	AscPutRiscVarFreeQHead(iop_base, 1);
	AscPutRiscVarDoneQTail(iop_base, asc_dvc->max_total_qng);
	AscPutVarFreeQHead(iop_base, 1);
	AscPutVarDoneQTail(iop_base, asc_dvc->max_total_qng);
	AscWriteLramByte(iop_base, ASCV_BUSY_QHEAD_B,
					 (uchar) ((int) asc_dvc->max_total_qng + 1));
	AscWriteLramByte(iop_base, ASCV_DISC1_QHEAD_B,
					 (uchar) ((int) asc_dvc->max_total_qng + 2));
	AscWriteLramByte(iop_base, (ushort) ASCV_TOTAL_READY_Q_B,
					 asc_dvc->max_total_qng);
	AscWriteLramWord(iop_base, ASCV_ASCDVC_ERR_CODE_W, 0);
	AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, 0);
	AscWriteLramByte(iop_base, ASCV_SCSIBUSY_B, 0);
	AscWriteLramByte(iop_base, ASCV_WTM_FLAG_B, 0);
	AscPutQDoneInProgress(iop_base, 0);
	lram_addr = ASC_QADR_BEG;
	for (i = 0; i < 32; i++, lram_addr += 2) {
		AscWriteLramWord(iop_base, lram_addr, 0);
	}
	return (0);
}

int
AscSetLibErrorCode(
					  REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					  ushort err_code
)
{
	if (asc_dvc->err_code == 0) {
		asc_dvc->err_code = err_code;
		AscWriteLramWord(asc_dvc->iop_base, ASCV_ASCDVC_ERR_CODE_W,
						 err_code);
	}
	return (err_code);
}


int
_AscWaitQDone(
				 PortAddr iop_base,
				 REG ASC_SCSI_Q dosfar * scsiq
)
{
	ushort              q_addr;
	uchar               q_status;
	int                 count = 0;
	while (scsiq->q1.q_no == 0) ;
	q_addr = ASC_QNO_TO_QADDR(scsiq->q1.q_no);
	do {
		q_status = AscReadLramByte(iop_base, q_addr + ASC_SCSIQ_B_STATUS);
		DvcSleepMilliSecond(100L);
		if (count++ > 30) {
			return (0);
		}
	} while ((q_status & QS_READY) != 0);
	return (1);
}

uchar
AscMsgOutSDTR(
				 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				 uchar sdtr_period,
				 uchar sdtr_offset
)
{
	SDTR_XMSG           sdtr_buf;
	uchar               sdtr_period_index;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	sdtr_buf.msg_type = MS_EXTEND;
	sdtr_buf.msg_len = MS_SDTR_LEN;
	sdtr_buf.msg_req = MS_SDTR_CODE;
	sdtr_buf.xfer_period = sdtr_period;
	sdtr_offset &= ASC_SYN_MAX_OFFSET;
	sdtr_buf.req_ack_offset = sdtr_offset;
	if ((sdtr_period_index =
		 AscGetSynPeriodIndex(asc_dvc, sdtr_period)) <=
		asc_dvc->max_sdtr_index) {
		AscMemWordCopyToLram(iop_base,
							 ASCV_MSGOUT_BEG,
							 (ushort dosfar *) & sdtr_buf, SYN_XMSG_WLEN);
		return ((sdtr_period_index << 4) | sdtr_offset);
	} else {
		sdtr_buf.req_ack_offset = 0;
		AscMemWordCopyToLram(iop_base,
							 ASCV_MSGOUT_BEG,
							 (ushort dosfar *) & sdtr_buf, SYN_XMSG_WLEN);
		return (0);
	}
}

uchar
AscCalSDTRData(
				  REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				  uchar sdtr_period,
				  uchar syn_offset
)
{
	uchar               byte;
	uchar               sdtr_period_ix;
	sdtr_period_ix = AscGetSynPeriodIndex(asc_dvc, sdtr_period);
	if (
		   (sdtr_period_ix > asc_dvc->max_sdtr_index)
	  ) {
		return (0xFF);
	}
	byte = (sdtr_period_ix << 4) | (syn_offset & ASC_SYN_MAX_OFFSET);
	return (byte);
}

void
AscSetChipSDTR(
				  PortAddr iop_base,
				  uchar sdtr_data,
				  uchar tid_no
)
{
	AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
	AscPutMCodeSDTRDoneAtID(iop_base, tid_no, sdtr_data);
	return;
}

uchar
AscGetSynPeriodIndex(
						ASC_DVC_VAR asc_ptr_type * asc_dvc,
						ruchar syn_time
)
{
	ruchar             *period_table;
	int                 max_index;
	int                 i;
	period_table = asc_dvc->sdtr_period_tbl;
	max_index = (int) asc_dvc->max_sdtr_index;
	if (
		   (syn_time >= period_table[0])
		   && (syn_time <= period_table[max_index])
	  ) {
		for (i = 0; i < (max_index - 1); i++) {
			if (syn_time <= period_table[i]) {
				return (i);
			}
		}
		return (max_index);
	} else {
		return (max_index + 1);
	}
}

uchar
AscAllocFreeQueue(
					 PortAddr iop_base,
					 uchar free_q_head
)
{
	ushort              q_addr;
	uchar               next_qp;
	uchar               q_status;
	q_addr = ASC_QNO_TO_QADDR(free_q_head);
	q_status = (uchar) AscReadLramByte(iop_base,
									(ushort) (q_addr + ASC_SCSIQ_B_STATUS));
	next_qp = AscReadLramByte(iop_base,
							  (ushort) (q_addr + ASC_SCSIQ_B_FWD));
	if (
		   ((q_status & QS_READY) == 0)
		   && (next_qp != ASC_QLINK_END)
	  ) {
		return (next_qp);
	}
	return (ASC_QLINK_END);
}

uchar
AscAllocMultipleFreeQueue(
							 PortAddr iop_base,
							 uchar free_q_head,
							 uchar n_free_q
)
{
	uchar               i;
	for (i = 0; i < n_free_q; i++) {
		if ((free_q_head = AscAllocFreeQueue(iop_base, free_q_head))
			== ASC_QLINK_END) {
			return (ASC_QLINK_END);
		}
	}
	return (free_q_head);
}

int
AscRiscHaltedAbortSRB(
						 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						 ulong srb_ptr
)
{
	PortAddr            iop_base;
	ushort              q_addr;
	uchar               q_no;
	ASC_QDONE_INFO      scsiq_buf;
	ASC_QDONE_INFO dosfar *scsiq;
	ASC_ISR_CALLBACK    asc_isr_callback;
	int                 last_int_level;
	iop_base = asc_dvc->iop_base;
	asc_isr_callback = (ASC_ISR_CALLBACK) asc_dvc->isr_callback;
	last_int_level = DvcEnterCritical();
	scsiq = (ASC_QDONE_INFO dosfar *) & scsiq_buf;
#if CC_LINK_BUSY_Q
	_AscAbortSrbBusyQueue(asc_dvc, scsiq, srb_ptr);
#endif
	for (q_no = ASC_MIN_ACTIVE_QNO; q_no <= asc_dvc->max_total_qng;
		 q_no++) {
		q_addr = ASC_QNO_TO_QADDR(q_no);
		scsiq->d2.srb_ptr = AscReadLramDWord(iop_base,
						   (ushort) (q_addr + (ushort) ASC_SCSIQ_D_SRBPTR));
		if (scsiq->d2.srb_ptr == srb_ptr) {
			_AscCopyLramScsiDoneQ(iop_base, q_addr, scsiq, asc_dvc->max_dma_count);
			if (
				   ((scsiq->q_status & QS_READY) != 0)
				   && ((scsiq->q_status & QS_ABORTED) == 0)
				   && ((scsiq->cntl & QCSG_SG_XFER_LIST) == 0)
			  ) {
				scsiq->q_status |= QS_ABORTED;
				scsiq->d3.done_stat = QD_ABORTED_BY_HOST;
				AscWriteLramDWord(iop_base,
							(ushort) (q_addr + (ushort) ASC_SCSIQ_D_SRBPTR),
								  0L);
				AscWriteLramByte(iop_base,
							(ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
								 scsiq->q_status);
				(*asc_isr_callback) (asc_dvc, scsiq);
				return (1);
			}
		}
	}
	DvcLeaveCritical(last_int_level);
	return (0);
}

int
AscRiscHaltedAbortTIX(
						 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						 uchar target_ix
)
{
	PortAddr            iop_base;
	ushort              q_addr;
	uchar               q_no;
	ASC_QDONE_INFO      scsiq_buf;
	ASC_QDONE_INFO dosfar *scsiq;
	ASC_ISR_CALLBACK    asc_isr_callback;
	int                 last_int_level;
#if CC_LINK_BUSY_Q
	uchar               tid_no;
#endif
	iop_base = asc_dvc->iop_base;
	asc_isr_callback = (ASC_ISR_CALLBACK) asc_dvc->isr_callback;
	last_int_level = DvcEnterCritical();
	scsiq = (ASC_QDONE_INFO dosfar *) & scsiq_buf;
#if CC_LINK_BUSY_Q
	tid_no = ASC_TIX_TO_TID(target_ix);
	_AscAbortTidBusyQueue(asc_dvc, scsiq, tid_no);
#endif
	for (q_no = ASC_MIN_ACTIVE_QNO; q_no <= asc_dvc->max_total_qng;
		 q_no++) {
		q_addr = ASC_QNO_TO_QADDR(q_no);
		_AscCopyLramScsiDoneQ(iop_base, q_addr, scsiq, asc_dvc->max_dma_count);
		if (
			   ((scsiq->q_status & QS_READY) != 0)
			   && ((scsiq->q_status & QS_ABORTED) == 0)
			   && ((scsiq->cntl & QCSG_SG_XFER_LIST) == 0)
		  ) {
			if (scsiq->d2.target_ix == target_ix) {
				scsiq->q_status |= QS_ABORTED;
				scsiq->d3.done_stat = QD_ABORTED_BY_HOST;
				AscWriteLramDWord(iop_base,
							(ushort) (q_addr + (ushort) ASC_SCSIQ_D_SRBPTR),
								  0L);
				AscWriteLramByte(iop_base,
							(ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
								 scsiq->q_status);
				(*asc_isr_callback) (asc_dvc, scsiq);
			}
		}
	}
	DvcLeaveCritical(last_int_level);
	return (1);
}

#if CC_LINK_BUSY_Q

#endif

int
AscHostReqRiscHalt(
					  PortAddr iop_base
)
{
	int                 count = 0;
	int                 sta = 0;
	uchar               saved_stop_code;
	if (AscIsChipHalted(iop_base))
		return (1);
	saved_stop_code = AscReadLramByte(iop_base, ASCV_STOP_CODE_B);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
					 ASC_STOP_HOST_REQ_RISC_HALT | ASC_STOP_REQ_RISC_STOP
	  );
	do {
		if (AscIsChipHalted(iop_base)) {
			sta = 1;
			break;
		}
		DvcSleepMilliSecond(100);
	} while (count++ < 20);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, saved_stop_code);
	return (sta);
}

int
AscStopQueueExe(
				   PortAddr iop_base
)
{
	int                 count;
	count = 0;
	if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) == 0) {
		AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
						 ASC_STOP_REQ_RISC_STOP);
		do {
			if (
				   AscReadLramByte(iop_base, ASCV_STOP_CODE_B) &
				   ASC_STOP_ACK_RISC_STOP) {
				return (1);
			}
			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
	return (0);
}

int
AscStartQueueExe(
					PortAddr iop_base
)
{
	if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) != 0) {
		AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, 0);
	}
	return (1);
}

int
AscCleanUpBusyQueue(
					   PortAddr iop_base
)
{
	int                 count;
	uchar               stop_code;
	count = 0;
	if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) != 0) {
		AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
						 ASC_STOP_CLEAN_UP_BUSY_Q);
		do {
			stop_code = AscReadLramByte(iop_base, ASCV_STOP_CODE_B);
			if ((stop_code & ASC_STOP_CLEAN_UP_BUSY_Q) == 0)
				break;
			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
	return (1);
}

int
AscCleanUpDiscQueue(
					   PortAddr iop_base
)
{
	int                 count;
	uchar               stop_code;
	count = 0;
	if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) != 0) {
		AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
						 ASC_STOP_CLEAN_UP_DISC_Q);
		do {
			stop_code = AscReadLramByte(iop_base, ASCV_STOP_CODE_B);
			if ((stop_code & ASC_STOP_CLEAN_UP_DISC_Q) == 0)
				break;
			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
	return (1);
}

int
AscWaitTixISRDone(
					 ASC_DVC_VAR asc_ptr_type * asc_dvc,
					 uchar target_ix
)
{
	uchar               cur_req;
	uchar               tid_no;
	tid_no = ASC_TIX_TO_TID(target_ix);
	while (TRUE) {
		if ((cur_req = asc_dvc->cur_dvc_qng[tid_no]) == 0) {
			break;
		}
		DvcSleepMilliSecond(1000L);
		if (asc_dvc->cur_dvc_qng[tid_no] == cur_req) {
			break;
		}
	}
	return (1);
}

int
AscWaitISRDone(
				  REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	int                 tid;
	for (tid = 0; tid <= ASC_MAX_TID; tid++) {
		AscWaitTixISRDone(asc_dvc, ASC_TID_TO_TIX(tid));
	}
	return (1);
}

ulong
AscGetOnePhyAddr(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					uchar dosfar * buf_addr,
					ulong buf_size
)
{
	ASC_MIN_SG_HEAD     sg_head;
	sg_head.entry_cnt = ASC_MIN_SG_LIST;
	if (DvcGetSGList(asc_dvc, (uchar dosfar *) buf_addr,
				  buf_size, (ASC_SG_HEAD dosfar *) & sg_head) != buf_size) {
		return (0L);
	}
	if (sg_head.entry_cnt > 1) {
		return (0L);
	}
	return (sg_head.sg_list[0].addr);
}

void
DvcDelayNanoSecond(
					  ASC_DVC_VAR asc_ptr_type * asc_dvc,
					  ulong nano_sec
)
{
	ulong               loop;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	loop = nano_sec / 90;
	loop++;
	while (loop-- != 0) {
		inp(iop_base);
	}
	return;
}

ulong
AscGetEisaProductID(
					   PortAddr iop_base
)
{
	PortAddr            eisa_iop;
	ushort              product_id_high, product_id_low;
	ulong               product_id;
	eisa_iop = ASC_GET_EISA_SLOT(iop_base) | ASC_EISA_PID_IOP_MASK;
	product_id_low = inpw(eisa_iop);
	product_id_high = inpw(eisa_iop + 2);
	product_id = ((ulong) product_id_high << 16) | (ulong) product_id_low;
	return (product_id);
}

PortAddr
AscSearchIOPortAddrEISA(
						   PortAddr iop_base
)
{
	ulong               eisa_product_id;
	if (iop_base == 0) {
		iop_base = ASC_EISA_MIN_IOP_ADDR;
	} else {
		if (iop_base == ASC_EISA_MAX_IOP_ADDR)
			return (0);
		if ((iop_base & 0x0050) == 0x0050) {
			iop_base += ASC_EISA_BIG_IOP_GAP;
		} else {
			iop_base += ASC_EISA_SMALL_IOP_GAP;
		}
	}
	while (iop_base <= ASC_EISA_MAX_IOP_ADDR) {
		eisa_product_id = AscGetEisaProductID(iop_base);
		if (
			   (eisa_product_id == ASC_EISA_ID_740)
			   || (eisa_product_id == ASC_EISA_ID_750)
		  ) {
			if (AscFindSignature(iop_base)) {
				inpw(iop_base + 4);
				return (iop_base);
			}
		}
		if (iop_base == ASC_EISA_MAX_IOP_ADDR)
			return (0);
		if ((iop_base & 0x0050) == 0x0050) {
			iop_base += ASC_EISA_BIG_IOP_GAP;
		} else {
			iop_base += ASC_EISA_SMALL_IOP_GAP;
		}
	}
	return (0);
}

int
AscStartChip(
				PortAddr iop_base
)
{
	AscSetChipControl(iop_base, 0);
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
		return (0);
	}
	return (1);
}

int
AscStopChip(
			   PortAddr iop_base
)
{
	uchar               cc_val;
	cc_val = AscGetChipControl(iop_base) & (~(CC_SINGLE_STEP | CC_TEST | CC_DIAG));
	AscSetChipControl(iop_base, (uchar) (cc_val | CC_HALT));
	AscSetChipIH(iop_base, INS_HALT);
	AscSetChipIH(iop_base, INS_RFLAG_WTM);
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) == 0) {
		return (0);
	}
	return (1);
}

int
AscIsChipHalted(
				   PortAddr iop_base
)
{
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
		if ((AscGetChipControl(iop_base) & CC_HALT) != 0) {
			return (1);
		}
	}
	return (0);
}

void
AscSetChipIH(
				PortAddr iop_base,
				ushort ins_code
)
{
	AscSetBank(iop_base, 1);
	AscWriteChipIH(iop_base, ins_code);
	AscSetBank(iop_base, 0);
	return;
}

void
AscAckInterrupt(
				   PortAddr iop_base
)
{
	uchar               host_flag;
	uchar               risc_flag;
	ushort              loop;
	loop = 0;
	do {
		risc_flag = AscReadLramByte(iop_base, ASCV_RISC_FLAG_B);
		if (loop++ > 0x7FFF) {
			break;
		}
	} while ((risc_flag & ASC_RISC_FLAG_GEN_INT) != 0);
	host_flag = AscReadLramByte(iop_base, ASCV_HOST_FLAG_B) & (~ASC_HOST_FLAG_ACK_INT);
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
					 (uchar) (host_flag | ASC_HOST_FLAG_ACK_INT));
	AscSetChipStatus(iop_base, CIW_INT_ACK);
	loop = 0;
	while (AscGetChipStatus(iop_base) & CSW_INT_PENDING) {
		AscSetChipStatus(iop_base, CIW_INT_ACK);
		if (loop++ > 3) {
			break;
		}
	}
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
	return;
}

void
AscDisableInterrupt(
					   PortAddr iop_base
)
{
	ushort              cfg;
	cfg = AscGetChipCfgLsw(iop_base);
	AscSetChipCfgLsw(iop_base, cfg & (~ASC_CFG0_HOST_INT_ON));
	return;
}

void
AscEnableInterrupt(
					  PortAddr iop_base
)
{
	ushort              cfg;
	cfg = AscGetChipCfgLsw(iop_base);
	AscSetChipCfgLsw(iop_base, cfg | ASC_CFG0_HOST_INT_ON);
	return;
}



void
AscSetBank(
			  PortAddr iop_base,
			  uchar bank
)
{
	uchar               val;
	val = AscGetChipControl(iop_base) &
	  (~(CC_SINGLE_STEP | CC_TEST | CC_DIAG | CC_SCSI_RESET | CC_CHIP_RESET));
	if (bank == 1) {
		val |= CC_BANK_ONE;
	} else if (bank == 2) {
		val |= CC_DIAG | CC_BANK_ONE;
	} else {
		val &= ~CC_BANK_ONE;
	}
	AscSetChipControl(iop_base, val);
	return;
}



int
AscResetChipAndScsiBus(
						  PortAddr iop_base
)
{
	while (AscGetChipStatus(iop_base) & CSW_SCSI_RESET_ACTIVE) ;
	AscStopChip(iop_base);
	AscSetChipControl(iop_base, CC_CHIP_RESET | CC_SCSI_RESET | CC_HALT);
	DvcSleepMilliSecond(200);
	AscSetChipIH(iop_base, INS_RFLAG_WTM);
	AscSetChipIH(iop_base, INS_HALT);
	AscSetChipControl(iop_base, CC_CHIP_RESET | CC_HALT);
	AscSetChipControl(iop_base, CC_HALT);
	DvcSleepMilliSecond(200);
	AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
	AscSetChipStatus(iop_base, 0);
	return (AscIsChipHalted(iop_base));
}



ulong
AscGetMaxDmaCount(
					 ushort bus_type
)
{
	if (bus_type & ASC_IS_ISA)
		return (ASC_MAX_ISA_DMA_COUNT);
	else if (bus_type & (ASC_IS_EISA | ASC_IS_VL))
		return (ASC_MAX_VL_DMA_COUNT);
	return (ASC_MAX_PCI_DMA_COUNT);
}

ushort
AscGetIsaDmaChannel(
					   PortAddr iop_base
)
{
	ushort              channel;
	channel = AscGetChipCfgLsw(iop_base) & 0x0003;
	if (channel == 0x03)
		return (0);
	else if (channel == 0x00)
		return (7);
	return (channel + 4);
}

ushort
AscSetIsaDmaChannel(
					   PortAddr iop_base,
					   ushort dma_channel
)
{
	ushort              cfg_lsw;
	uchar               value;
	if ((dma_channel >= 5) && (dma_channel <= 7)) {
		if (dma_channel == 7)
			value = 0x00;
		else
			value = dma_channel - 4;
		cfg_lsw = AscGetChipCfgLsw(iop_base) & 0xFFFC;
		cfg_lsw |= value;
		AscSetChipCfgLsw(iop_base, cfg_lsw);
		return (AscGetIsaDmaChannel(iop_base));
	}
	return (0);
}

uchar
AscSetIsaDmaSpeed(
					 PortAddr iop_base,
					 uchar speed_value
)
{
	speed_value &= 0x07;
	AscSetBank(iop_base, 1);
	AscWriteChipDmaSpeed(iop_base, speed_value);
	AscSetBank(iop_base, 0);
	return (AscGetIsaDmaSpeed(iop_base));
}

uchar
AscGetIsaDmaSpeed(
					 PortAddr iop_base
)
{
	uchar               speed_value;
	AscSetBank(iop_base, 1);
	speed_value = AscReadChipDmaSpeed(iop_base);
	speed_value &= 0x07;
	AscSetBank(iop_base, 0);
	return (speed_value);
}

ushort
AscInitGetConfig(
					ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	ushort              warn_code;
	warn_code = 0;
	asc_dvc->init_state = ASC_INIT_STATE_BEG_GET_CFG;
	if (asc_dvc->err_code != 0)
		return (UW_ERR);
	if (AscFindSignature(asc_dvc->iop_base)) {
		warn_code |= AscInitAscDvcVar(asc_dvc);
#if CC_INCLUDE_EEP_CONFIG
		if (asc_dvc->init_state & ASC_INIT_STATE_WITHOUT_EEP) {
			warn_code |= AscInitWithoutEEP(asc_dvc);
		} else {
			warn_code |= AscInitFromEEP(asc_dvc);
		}
#else
		warn_code |= AscInitWithoutEEP(asc_dvc);
#endif
		asc_dvc->init_state |= ASC_INIT_STATE_END_GET_CFG;
		if (asc_dvc->scsi_reset_wait > ASC_MAX_SCSI_RESET_WAIT) {
			asc_dvc->scsi_reset_wait = ASC_MAX_SCSI_RESET_WAIT;
		}
	} else {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
	}
	return (warn_code);
}

ushort
AscInitSetConfig(
					ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	ushort              warn_code;
	warn_code = 0;
	asc_dvc->init_state |= ASC_INIT_STATE_BEG_SET_CFG;
	if (asc_dvc->err_code != 0)
		return (UW_ERR);
	if (AscFindSignature(asc_dvc->iop_base)) {
		warn_code |= AscInitFromAscDvcVar(asc_dvc);
		asc_dvc->init_state |= ASC_INIT_STATE_END_SET_CFG;
	} else {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
	}
	return (warn_code);
}

ushort
AscInitFromAscDvcVar(
						ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	ushort              cfg_msw;
	ushort              warn_code;
	ushort              pci_device_id;
	iop_base = asc_dvc->iop_base;
	pci_device_id = asc_dvc->cfg->pci_device_id;
	warn_code = 0;
	cfg_msw = AscGetChipCfgMsw(iop_base);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		AscSetChipCfgMsw(iop_base, cfg_msw);
	}
	if ((asc_dvc->cfg->cmd_qng_enabled & asc_dvc->cfg->disc_enable) !=
		asc_dvc->cfg->cmd_qng_enabled) {
		asc_dvc->cfg->disc_enable = asc_dvc->cfg->cmd_qng_enabled;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
	}
	if ((asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL)) != 0) {
		if (AscSetChipIRQ(iop_base, asc_dvc->irq_no, asc_dvc->bus_type)
			!= asc_dvc->irq_no) {
			asc_dvc->err_code |= ASC_IERR_SET_IRQ_NO;
		}
	}
	if (asc_dvc->bus_type & ASC_IS_PCI) {
#if CC_DISABLE_PCI_PARITY_INT
		cfg_msw &= 0xFFC0;
		AscSetChipCfgMsw(iop_base, cfg_msw);
#endif
		if ((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) {
		} else {
			if ((pci_device_id == ASC_PCI_DEVICE_ID_REV_A) ||
				(pci_device_id == ASC_PCI_DEVICE_ID_REV_B)) {
				asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_IF_NOT_DWB;
				asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
#if CC_SET_PCI_LATENCY_TIMER_ZERO
				DvcWritePCIConfigByte(asc_dvc,
									  AscPCIConfigCommandRegister,
									  AscPCICmdRegBits_BusMastering
				  );
				if ((DvcReadPCIConfigByte(asc_dvc,
										  AscPCIConfigCommandRegister
					 )
					 & AscPCICmdRegBits_BusMastering)
					!= AscPCICmdRegBits_BusMastering) {
					warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
				}
				DvcWritePCIConfigByte(asc_dvc,
									  AscPCIConfigLatencyTimer,
									  0x00
				  );
				if (DvcReadPCIConfigByte(asc_dvc,
										 AscPCIConfigLatencyTimer
					) != 0x00) {
					warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
				}
#endif
			}
		}
	} else if (asc_dvc->bus_type == ASC_IS_ISAPNP) {
		if (AscGetChipVersion(iop_base, asc_dvc->bus_type)
			== ASC_CHIP_VER_ASYN_BUG) {
			asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
		}
	}
	if (AscSetChipScsiID(iop_base, asc_dvc->cfg->chip_scsi_id) !=
		asc_dvc->cfg->chip_scsi_id) {
		asc_dvc->err_code |= ASC_IERR_SET_SCSI_ID;
	}
	if (asc_dvc->bus_type & ASC_IS_ISA) {
		AscSetIsaDmaChannel(iop_base, asc_dvc->cfg->isa_dma_channel);
		AscSetIsaDmaSpeed(iop_base, asc_dvc->cfg->isa_dma_speed);
	}
	return (warn_code);
}

ushort
AscInitAsc1000Driver(
						ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	ushort              warn_code;
	PortAddr            iop_base;
	extern ushort       _mcode_size;
	extern ulong        _mcode_chksum;
	extern uchar        _mcode_buf[];
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	if ((asc_dvc->dvc_cntl & ASC_CNTL_RESET_SCSI) &&
		!(asc_dvc->init_state & ASC_INIT_RESET_SCSI_DONE)) {
		AscResetChipAndScsiBus(iop_base);
		DvcSleepMilliSecond((ulong) ((ushort) asc_dvc->scsi_reset_wait * 1000));
	}
	asc_dvc->init_state |= ASC_INIT_STATE_BEG_LOAD_MC;
	if (asc_dvc->err_code != 0)
		return (UW_ERR);
	if (!AscFindSignature(asc_dvc->iop_base)) {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
		return (warn_code);
	}
	AscDisableInterrupt(iop_base);
	warn_code |= AscInitLram(asc_dvc);
	if (asc_dvc->err_code != 0)
		return (UW_ERR);
	if (AscLoadMicroCode(iop_base, 0, (ushort dosfar *) _mcode_buf,
						 _mcode_size) != _mcode_chksum) {
		asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
		return (warn_code);
	}
	warn_code |= AscInitMicroCodeVar(asc_dvc);
	asc_dvc->init_state |= ASC_INIT_STATE_END_LOAD_MC;
	AscEnableInterrupt(iop_base);
	return (warn_code);
}

ushort
AscInitAscDvcVar(
					ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	int                 i;
	PortAddr            iop_base;
	ushort              warn_code;
	uchar               chip_version;
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	asc_dvc->err_code = 0;
	if (
		   (asc_dvc->bus_type &
			(ASC_IS_ISA | ASC_IS_PCI | ASC_IS_EISA | ASC_IS_VL)) == 0
	  ) {
		asc_dvc->err_code |= ASC_IERR_NO_BUS_TYPE;
	}
	AscSetChipControl(iop_base, CC_HALT);
	AscSetChipStatus(iop_base, 0);
#if CC_LINK_BUSY_Q
	for (i = 0; i <= ASC_MAX_TID; i++) {
		asc_dvc->scsiq_busy_head[i] = (ASC_SCSI_Q dosfar *) 0L;
		asc_dvc->scsiq_busy_tail[i] = (ASC_SCSI_Q dosfar *) 0L;
	}
#endif
	asc_dvc->bug_fix_cntl = 0;
	asc_dvc->pci_fix_asyn_xfer = 0;
	asc_dvc->pci_fix_asyn_xfer_always = 0;
	asc_dvc->init_state = 0;
	asc_dvc->sdtr_done = 0;
	asc_dvc->cur_total_qng = 0;
	asc_dvc->is_in_int = 0;
	asc_dvc->in_critical_cnt = 0;
	asc_dvc->last_q_shortage = 0;
	asc_dvc->use_tagged_qng = 0;
	asc_dvc->no_scam = 0;
	asc_dvc->unit_not_ready = 0;
	asc_dvc->queue_full_or_busy = 0;
	asc_dvc->dvc_cntl = ASC_DEF_DVC_CNTL;
	asc_dvc->init_sdtr = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->max_total_qng = ASC_DEF_MAX_TOTAL_QNG;
	asc_dvc->scsi_reset_wait = 3;
	asc_dvc->start_motor = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->max_dma_count = AscGetMaxDmaCount(asc_dvc->bus_type);
	asc_dvc->redo_scam = 0;
	asc_dvc->res2 = 0;
	asc_dvc->res4 = 0;
	asc_dvc->res6 = 0;
	asc_dvc->res7 = 0;
	asc_dvc->res8 = 0;
	asc_dvc->cfg->disc_enable = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->cfg->can_tagged_qng = 0;
	asc_dvc->cfg->cmd_qng_enabled = 0;
	asc_dvc->cfg->chip_scsi_id = ASC_DEF_CHIP_SCSI_ID;
	asc_dvc->cfg->lib_serial_no = ASC_LIB_SERIAL_NUMBER;
	asc_dvc->cfg->lib_version = (ASC_LIB_VERSION_MAJOR << 8) |
	  ASC_LIB_VERSION_MINOR;
	chip_version = AscGetChipVersion(iop_base, asc_dvc->bus_type);
	asc_dvc->cfg->chip_version = chip_version;
	asc_dvc->sdtr_period_tbl[0] = SYN_XFER_NS_0;
	asc_dvc->sdtr_period_tbl[1] = SYN_XFER_NS_1;
	asc_dvc->sdtr_period_tbl[2] = SYN_XFER_NS_2;
	asc_dvc->sdtr_period_tbl[3] = SYN_XFER_NS_3;
	asc_dvc->sdtr_period_tbl[4] = SYN_XFER_NS_4;
	asc_dvc->sdtr_period_tbl[5] = SYN_XFER_NS_5;
	asc_dvc->sdtr_period_tbl[6] = SYN_XFER_NS_6;
	asc_dvc->sdtr_period_tbl[7] = SYN_XFER_NS_7;
	asc_dvc->max_sdtr_index = 7;
	if (
		   (asc_dvc->bus_type & ASC_IS_PCI) &&
		   (chip_version >= ASC_CHIP_VER_1ST_PCI_ULTRA)
	  ) {
		asc_dvc->sdtr_period_tbl[0] = SYN_ULTRA_XFER_NS_0;
		asc_dvc->sdtr_period_tbl[1] = SYN_ULTRA_XFER_NS_1;
		asc_dvc->sdtr_period_tbl[2] = SYN_ULTRA_XFER_NS_2;
		asc_dvc->sdtr_period_tbl[3] = SYN_ULTRA_XFER_NS_3;
		asc_dvc->sdtr_period_tbl[4] = SYN_ULTRA_XFER_NS_4;
		asc_dvc->sdtr_period_tbl[5] = SYN_ULTRA_XFER_NS_5;
		asc_dvc->sdtr_period_tbl[6] = SYN_ULTRA_XFER_NS_6;
		asc_dvc->sdtr_period_tbl[7] = SYN_ULTRA_XFER_NS_7;
		asc_dvc->sdtr_period_tbl[8] = SYN_ULTRA_XFER_NS_8;
		asc_dvc->sdtr_period_tbl[9] = SYN_ULTRA_XFER_NS_9;
		asc_dvc->sdtr_period_tbl[10] = SYN_ULTRA_XFER_NS_10;
		asc_dvc->sdtr_period_tbl[11] = SYN_ULTRA_XFER_NS_11;
		asc_dvc->sdtr_period_tbl[12] = SYN_ULTRA_XFER_NS_12;
		asc_dvc->sdtr_period_tbl[13] = SYN_ULTRA_XFER_NS_13;
		asc_dvc->sdtr_period_tbl[14] = SYN_ULTRA_XFER_NS_14;
		asc_dvc->sdtr_period_tbl[15] = SYN_ULTRA_XFER_NS_15;
		asc_dvc->max_sdtr_index = 15;
		AscSetExtraControl(iop_base, (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
		asc_dvc->bus_type = ASC_IS_PCI_ULTRA;
	}
	asc_dvc->cfg->isa_dma_speed = ASC_DEF_ISA_DMA_SPEED;
	if (AscGetChipBusType(iop_base) == ASC_IS_ISAPNP) {
		AscSetChipIFC(iop_base, IFC_INIT_DEFAULT);
		asc_dvc->bus_type = ASC_IS_ISAPNP;
	}
	if ((asc_dvc->bus_type & ASC_IS_ISA) != 0) {
		asc_dvc->cfg->isa_dma_channel = (uchar) AscGetIsaDmaChannel(iop_base);
	}
	for (i = 0; i <= ASC_MAX_TID; i++) {
		asc_dvc->cur_dvc_qng[i] = 0;
		asc_dvc->max_dvc_qng[i] = ASC_MAX_SCSI1_QNG;
		asc_dvc->scsiq_busy_head[i] = (ASC_SCSI_Q dosfar *) 0L;
		asc_dvc->scsiq_busy_tail[i] = (ASC_SCSI_Q dosfar *) 0L;
		asc_dvc->cfg->max_tag_qng[i] = ASC_MAX_INRAM_TAG_QNG;
		asc_dvc->cfg->sdtr_period_offset[i] = (uchar) (ASC_DEF_SDTR_OFFSET | (ASC_DEF_SDTR_INDEX << 4));
	}
	return (warn_code);
}

#if CC_INCLUDE_EEP_CONFIG
ushort
AscInitFromEEP(
				  ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	ASCEEP_CONFIG       eep_config_buf;
	ASCEEP_CONFIG dosfar *eep_config;
	PortAddr            iop_base;
	ushort              chksum;
	ushort              warn_code;
	ushort              cfg_msw, cfg_lsw;
	int                 i;
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0x00FE);
	AscStopQueueExe(iop_base);
	if ((AscStopChip(iop_base) == FALSE) ||
		(AscGetChipScsiCtrl(iop_base) != 0)) {
		asc_dvc->init_state |= ASC_INIT_RESET_SCSI_DONE;
		AscResetChipAndScsiBus(iop_base);
		DvcSleepMilliSecond((ulong) ((ushort) asc_dvc->scsi_reset_wait * 1000));
	}
	if (AscIsChipHalted(iop_base) == FALSE) {
		asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
		return (warn_code);
	}
	AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
	if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
		asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
		return (warn_code);
	}
	eep_config = (ASCEEP_CONFIG dosfar *) & eep_config_buf;
	cfg_msw = AscGetChipCfgMsw(iop_base);
	cfg_lsw = AscGetChipCfgLsw(iop_base);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		AscSetChipCfgMsw(iop_base, cfg_msw);
	}
	chksum = AscGetEEPConfig(iop_base, eep_config, asc_dvc->bus_type);
	eep_config->cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
	if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
		if (asc_dvc->cfg->chip_version == 3) {
			if (eep_config->cfg_lsw != cfg_lsw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_lsw = AscGetChipCfgLsw(iop_base);
			}
			if (eep_config->cfg_msw != cfg_msw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_msw = AscGetChipCfgMsw(iop_base);
			}
		}
	}
	eep_config->cfg_lsw |= ASC_CFG0_HOST_INT_ON;
	if (chksum != eep_config->chksum) {
		warn_code |= ASC_WARN_EEPROM_CHKSUM;
	}
	asc_dvc->init_sdtr = eep_config->init_sdtr;
	asc_dvc->cfg->disc_enable = eep_config->disc_enable;
	asc_dvc->cfg->cmd_qng_enabled = eep_config->use_cmd_qng;
	asc_dvc->cfg->isa_dma_speed = eep_config->isa_dma_speed;
	asc_dvc->start_motor = eep_config->start_motor;
	asc_dvc->dvc_cntl = eep_config->cntl;
	asc_dvc->no_scam = eep_config->no_scam;
	if (!AscTestExternalLram(asc_dvc)) {
		if (
			   ((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA)
		  ) {
			eep_config->max_total_qng = ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng = ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG;
		} else {
			eep_config->cfg_msw |= 0x0800;
			cfg_msw |= 0x0800;
			AscSetChipCfgMsw(iop_base, cfg_msw);
			eep_config->max_total_qng = ASC_MAX_PCI_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng = ASC_MAX_INRAM_TAG_QNG;
		}
	} else {
	}
	if (eep_config->max_total_qng < ASC_MIN_TOTAL_QNG) {
		eep_config->max_total_qng = ASC_MIN_TOTAL_QNG;
	}
	if (eep_config->max_total_qng > ASC_MAX_TOTAL_QNG) {
		eep_config->max_total_qng = ASC_MAX_TOTAL_QNG;
	}
	if (eep_config->max_tag_qng > eep_config->max_total_qng) {
		eep_config->max_tag_qng = eep_config->max_total_qng;
	}
	if (eep_config->max_tag_qng < ASC_MIN_TAG_Q_PER_DVC) {
		eep_config->max_tag_qng = ASC_MIN_TAG_Q_PER_DVC;
	}
	asc_dvc->max_total_qng = eep_config->max_total_qng;
	if ((eep_config->use_cmd_qng & eep_config->disc_enable) !=
		eep_config->use_cmd_qng) {
		eep_config->disc_enable = eep_config->use_cmd_qng;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	if (asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL | ASC_IS_EISA)) {
		asc_dvc->irq_no = AscGetChipIRQ(iop_base, asc_dvc->bus_type);
	}
	eep_config->chip_scsi_id &= ASC_MAX_TID;
	asc_dvc->cfg->chip_scsi_id = eep_config->chip_scsi_id;
	for (i = 0; i <= ASC_MAX_TID; i++) {
#if CC_TMP_USE_EEP_SDTR
		asc_dvc->cfg->sdtr_period_offset[i] = eep_config->dos_int13_table[i];
#endif
		asc_dvc->dos_int13_table[i] = eep_config->dos_int13_table[i];
		asc_dvc->cfg->max_tag_qng[i] = eep_config->max_tag_qng;
	}
	eep_config->cfg_msw = AscGetChipCfgMsw(iop_base);
#if CC_CHK_FIX_EEP_CONTENT
	if (AscSetEEPConfig(iop_base, eep_config, asc_dvc->bus_type) != 0) {
		asc_dvc->err_code |= ASC_IERR_WRITE_EEPROM;
	}
#endif
	return (warn_code);
}
#endif

ushort
AscInitWithoutEEP(
					 ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	ushort              warn_code;
	ushort              cfg_msw;
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	cfg_msw = AscGetChipCfgMsw(iop_base);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		AscSetChipCfgMsw(iop_base, cfg_msw);
	}
	if (!AscTestExternalLram(asc_dvc)) {
		if (asc_dvc->bus_type & ASC_IS_PCI) {
			cfg_msw |= 0x0800;
			AscSetChipCfgMsw(iop_base, cfg_msw);
			asc_dvc->max_total_qng = ASC_MAX_PCI_INRAM_TOTAL_QNG;
		}
	} else {
	}
	if (asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL | ASC_IS_EISA)) {
		asc_dvc->irq_no = AscGetChipIRQ(iop_base, asc_dvc->bus_type);
	}
	return (warn_code);
}

ushort
AscInitMicroCodeVar(
					   ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	int                 i;
	ushort              warn_code;
	PortAddr            iop_base;
	ulong               phy_addr;
	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	for (i = 0; i <= ASC_MAX_TID; i++) {
		AscPutMCodeInitSDTRAtID(iop_base, i,
								asc_dvc->cfg->sdtr_period_offset[i]
		  );
	}
	AscInitQLinkVar(asc_dvc);
	AscWriteLramByte(iop_base, ASCV_DISC_ENABLE_B,
					 asc_dvc->cfg->disc_enable);
	AscWriteLramByte(iop_base, ASCV_HOSTSCSI_ID_B,
					 ASC_TID_TO_TARGET_ID(asc_dvc->cfg->chip_scsi_id));
	if ((phy_addr = AscGetOnePhyAddr(asc_dvc,
								 (uchar dosfar *) asc_dvc->cfg->overrun_buf,
									 ASC_OVERRUN_BSIZE)) == 0L) {
		asc_dvc->err_code |= ASC_IERR_GET_PHY_ADDR;
	} else {
		phy_addr = (phy_addr & 0xFFFFFFF8UL) + 8;
		AscWriteLramDWord(iop_base, ASCV_OVERRUN_PADDR_D, phy_addr);
		AscWriteLramDWord(iop_base, ASCV_OVERRUN_BSIZE_D,
						  ASC_OVERRUN_BSIZE - 8);
	}
	asc_dvc->cfg->mcode_date = AscReadLramWord(iop_base,
											   (ushort) ASCV_MC_DATE_W);
	asc_dvc->cfg->mcode_version = AscReadLramWord(iop_base,
												  (ushort) ASCV_MC_VER_W);
	AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
	if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
		asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
		return (warn_code);
	}
	if (AscStartChip(iop_base) != 1) {
		asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
		return (warn_code);
	}
	return (warn_code);
}

void                dosfar
AscInitPollIsrCallBack(
						  ASC_DVC_VAR asc_ptr_type * asc_dvc,
						  ASC_QDONE_INFO dosfar * scsi_done_q
)
{
	ASC_SCSI_REQ_Q dosfar *scsiq_req;
	ASC_ISR_CALLBACK    asc_isr_callback;
	uchar               cp_sen_len;
	uchar               i;
	if ((scsi_done_q->d2.flag & ASC_FLAG_SCSIQ_REQ) != 0) {
		scsiq_req = (ASC_SCSI_REQ_Q dosfar *) scsi_done_q->d2.srb_ptr;
		scsiq_req->r3.done_stat = scsi_done_q->d3.done_stat;
		scsiq_req->r3.host_stat = scsi_done_q->d3.host_stat;
		scsiq_req->r3.scsi_stat = scsi_done_q->d3.scsi_stat;
		scsiq_req->r3.scsi_msg = scsi_done_q->d3.scsi_msg;
		if ((scsi_done_q->d3.scsi_stat == SS_CHK_CONDITION) &&
			(scsi_done_q->d3.host_stat == 0)) {
			cp_sen_len = (uchar) ASC_MIN_SENSE_LEN;
			if (scsiq_req->r1.sense_len < ASC_MIN_SENSE_LEN) {
				cp_sen_len = (uchar) scsiq_req->r1.sense_len;
			}
			for (i = 0; i < cp_sen_len; i++) {
				scsiq_req->sense[i] = scsiq_req->sense_ptr[i];
			}
		}
	} else {
		if (asc_dvc->isr_callback != 0) {
			asc_isr_callback = (ASC_ISR_CALLBACK) asc_dvc->isr_callback;
			(*asc_isr_callback) (asc_dvc, scsi_done_q);
		}
	}
	return;
}

int
AscTestExternalLram(
					   ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	ushort              q_addr;
	ushort              saved_word;
	int                 sta;
	iop_base = asc_dvc->iop_base;
	sta = 0;
	q_addr = ASC_QNO_TO_QADDR(241);
	saved_word = AscReadLramWord(iop_base, q_addr);
	AscSetChipLramAddr(iop_base, q_addr);
	AscSetChipLramData(iop_base, 0x55AA);
	DvcSleepMilliSecond(10);
	AscSetChipLramAddr(iop_base, q_addr);
	if (AscGetChipLramData(iop_base) == 0x55AA) {
		sta = 1;
		AscWriteLramWord(iop_base, q_addr, saved_word);
	}
	return (sta);
}

#if CC_INCLUDE_EEP_CONFIG
int
AscWriteEEPCmdReg(
					 PortAddr iop_base,
					 uchar cmd_reg
)
{
	uchar               read_back;
	int                 retry;
	retry = 0;
	while (TRUE) {
		AscSetChipEEPCmd(iop_base, cmd_reg);
		DvcSleepMilliSecond(1);
		read_back = AscGetChipEEPCmd(iop_base);
		if (read_back == cmd_reg) {
			return (1);
		}
		if (retry++ > ASC_EEP_MAX_RETRY) {
			return (0);
		}
	}
}

int
AscWriteEEPDataReg(
					  PortAddr iop_base,
					  ushort data_reg
)
{
	ushort              read_back;
	int                 retry;
	retry = 0;
	while (TRUE) {
		AscSetChipEEPData(iop_base, data_reg);
		DvcSleepMilliSecond(1);
		read_back = AscGetChipEEPData(iop_base);
		if (read_back == data_reg) {
			return (1);
		}
		if (retry++ > ASC_EEP_MAX_RETRY) {
			return (0);
		}
	}
}

void
AscWaitEEPRead(
				  void
)
{
	DvcSleepMilliSecond(1);
	return;
}

void
AscWaitEEPWrite(
				   void
)
{
	DvcSleepMilliSecond(20);
	return;
}

ushort
AscReadEEPWord(
				  PortAddr iop_base,
				  uchar addr
)
{
	ushort              read_wval;
	uchar               cmd_reg;
	AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
	AscWaitEEPRead();
	cmd_reg = addr | ASC_EEP_CMD_READ;
	AscWriteEEPCmdReg(iop_base, cmd_reg);
	AscWaitEEPRead();
	read_wval = AscGetChipEEPData(iop_base);
	AscWaitEEPRead();
	return (read_wval);
}

ushort
AscWriteEEPWord(
				   PortAddr iop_base,
				   uchar addr,
				   ushort word_val
)
{
	ushort              read_wval;
	read_wval = AscReadEEPWord(iop_base, addr);
	if (read_wval != word_val) {
		AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_ABLE);
		AscWaitEEPRead();
		AscWriteEEPDataReg(iop_base, word_val);
		AscWaitEEPRead();
		AscWriteEEPCmdReg(iop_base,
						  (uchar) ((uchar) ASC_EEP_CMD_WRITE | addr));
		AscWaitEEPWrite();
		AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
		AscWaitEEPRead();
		return (AscReadEEPWord(iop_base, addr));
	}
	return (read_wval);
}

ushort
AscGetEEPConfig(
				   PortAddr iop_base,
				   ASCEEP_CONFIG dosfar * cfg_buf, ushort bus_type
)
{
	ushort              wval;
	ushort              sum;
	ushort dosfar      *wbuf;
	int                 cfg_beg;
	int                 cfg_end;
	int                 s_addr;
	int                 isa_pnp_wsize;
	wbuf = (ushort dosfar *) cfg_buf;
	sum = 0;
	isa_pnp_wsize = 0;
	for (s_addr = 0; s_addr < (2 + isa_pnp_wsize); s_addr++, wbuf++) {
		wval = AscReadEEPWord(iop_base, (uchar) s_addr);
		sum += wval;
		*wbuf = wval;
	}
	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}
	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1);
		 s_addr++, wbuf++) {
		wval = AscReadEEPWord(iop_base, (uchar) s_addr);
		sum += wval;
		*wbuf = wval;
	}
	*wbuf = AscReadEEPWord(iop_base, (uchar) s_addr);
	return (sum);
}

#if CC_CHK_FIX_EEP_CONTENT
int
AscSetEEPConfigOnce(
					   PortAddr iop_base,
					   ASCEEP_CONFIG dosfar * cfg_buf, ushort bus_type
)
{
	int                 n_error;
	ushort dosfar      *wbuf;
	ushort              sum;
	int                 s_addr;
	int                 cfg_beg;
	int                 cfg_end;
	wbuf = (ushort dosfar *) cfg_buf;
	n_error = 0;
	sum = 0;
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		sum += *wbuf;
		if (*wbuf != AscWriteEEPWord(iop_base, (uchar) s_addr, *wbuf)) {
			n_error++;
		}
	}
	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}
	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1);
		 s_addr++, wbuf++) {
		sum += *wbuf;
		if (*wbuf != AscWriteEEPWord(iop_base, (uchar) s_addr, *wbuf)) {
			n_error++;
		}
	}
	*wbuf = sum;
	if (sum != AscWriteEEPWord(iop_base, (uchar) s_addr, sum)) {
		n_error++;
	}
	wbuf = (ushort dosfar *) cfg_buf;
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		if (*wbuf != AscReadEEPWord(iop_base, (uchar) s_addr)) {
			n_error++;
		}
	}
	for (s_addr = cfg_beg; s_addr <= cfg_end;
		 s_addr++, wbuf++) {
		if (*wbuf != AscReadEEPWord(iop_base, (uchar) s_addr)) {
			n_error++;
		}
	}
	return (n_error);
}

int
AscSetEEPConfig(
				   PortAddr iop_base,
				   ASCEEP_CONFIG dosfar * cfg_buf, ushort bus_type
)
{
	int                 retry;
	int                 n_error;
	retry = 0;
	while (TRUE) {
		if ((n_error = AscSetEEPConfigOnce(iop_base, cfg_buf,
										   bus_type)) == 0) {
			break;
		}
		if (++retry > ASC_EEP_MAX_RETRY) {
			break;
		}
	}
	return (n_error);
}
#endif
#endif

int
AscInitPollBegin(
					REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
#if CC_INIT_INQ_DISPLAY
	DvcDisplayString((uchar dosfar *) "\r\n");
#endif
	AscDisableInterrupt(iop_base);
	asc_dvc->init_state |= ASC_INIT_STATE_BEG_INQUIRY;
	AscWriteLramByte(iop_base, ASCV_DISC_ENABLE_B, 0x00);
	asc_dvc->use_tagged_qng = 0;
	asc_dvc->cfg->can_tagged_qng = 0;
	asc_dvc->saved_ptr2func = (ulong) asc_dvc->isr_callback;
	asc_dvc->isr_callback = ASC_GET_PTR2FUNC(AscInitPollIsrCallBack);
	return (0);
}

int
AscInitPollEnd(
				  REG ASC_DVC_VAR asc_ptr_type * asc_dvc
)
{
	PortAddr            iop_base;
	rint                i;
	iop_base = asc_dvc->iop_base;
	asc_dvc->isr_callback = (Ptr2Func) asc_dvc->saved_ptr2func;
	AscWriteLramByte(iop_base, ASCV_DISC_ENABLE_B,
					 asc_dvc->cfg->disc_enable);
	AscWriteLramByte(iop_base, ASCV_USE_TAGGED_QNG_B,
					 asc_dvc->use_tagged_qng);
	AscWriteLramByte(iop_base, ASCV_CAN_TAGGED_QNG_B,
					 asc_dvc->cfg->can_tagged_qng);
	for (i = 0; i <= ASC_MAX_TID; i++) {
		AscWriteLramByte(iop_base,
					  (ushort) ((ushort) ASCV_MAX_DVC_QNG_BEG + (ushort) i),
						 asc_dvc->max_dvc_qng[i]);
	}
	AscAckInterrupt(iop_base);
	AscEnableInterrupt(iop_base);
#if CC_INIT_INQ_DISPLAY
	DvcDisplayString((uchar dosfar *) "\r\n");
#endif
	asc_dvc->init_state |= ASC_INIT_STATE_END_INQUIRY;
	return (0);
}

int                 _asc_wait_slow_device_ = FALSE;

int
AscInitPollTarget(
					 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					 REG ASC_SCSI_REQ_Q dosfar * scsiq,
					 REG ASC_SCSI_INQUIRY dosfar * inq,
					 REG ASC_CAP_INFO dosfar * cap_info
)
{
	uchar               tid_no, lun;
	uchar               dvc_type;
	ASC_SCSI_BIT_ID_TYPE tid_bits;
	int                 dvc_found;
	int                 support_read_cap;
	int                 tmp_disable_init_sdtr;
	int                 sta;
	ulong               phy_addr;
	dvc_found = 0;
	tmp_disable_init_sdtr = FALSE;
	tid_bits = scsiq->r1.target_id;
	lun = scsiq->r1.target_lun;
	tid_no = ASC_TIX_TO_TID(scsiq->r2.target_ix);
	if (
		   (phy_addr = AscGetOnePhyAddr(asc_dvc,
										(uchar dosfar *) scsiq->sense_ptr,
										(ulong) scsiq->r1.sense_len)) == 0L
	  ) {
		return (ERR);
	}
	scsiq->r1.sense_addr = phy_addr;
	if (
		   ((asc_dvc->init_sdtr & tid_bits) != 0)
		   && ((asc_dvc->sdtr_done & tid_bits) == 0)
	  ) {
		asc_dvc->init_sdtr &= ~tid_bits;
		tmp_disable_init_sdtr = TRUE;
	}
	if (
		   PollScsiInquiry(asc_dvc, scsiq, (uchar dosfar *) inq,
						   sizeof (ASC_SCSI_INQUIRY)) == 1
	  ) {
		dvc_found = 1;
		support_read_cap = TRUE;
		dvc_type = inq->byte0.peri_dvc_type;
		if (dvc_type != SCSI_TYPE_UNKNOWN) {
			if (
				   (dvc_type != SCSI_TYPE_DASD)
				   && (dvc_type != SCSI_TYPE_WORM)
				   && (dvc_type != SCSI_TYPE_CDROM)
				   && (dvc_type != SCSI_TYPE_OPTMEM)
			  ) {
				asc_dvc->start_motor &= ~tid_bits;
				support_read_cap = FALSE;
			}
			if (
				   (dvc_type != SCSI_TYPE_DASD)
				   || inq->byte1.rmb
			  ) {
				if (!_asc_wait_slow_device_) {
					DvcSleepMilliSecond(3000 - ((int) tid_no * 250));
					_asc_wait_slow_device_ = TRUE;
				}
			}
#if CC_INIT_INQ_DISPLAY
			AscDispInquiry(tid_no, lun, inq);
#endif
			if (lun == 0) {
				if (
					   (inq->byte3.rsp_data_fmt >= 2)
					   || (inq->byte2.ansi_apr_ver >= 2)
				  ) {
					if (inq->byte7.CmdQue) {
						asc_dvc->cfg->can_tagged_qng |= tid_bits;
						if (asc_dvc->cfg->cmd_qng_enabled & tid_bits) {
#if CC_FIX_QUANTUM_XP34301_1071
							if (
								   (inq->add_len >= 32)
								   && (AscCompareString(inq->vendor_id, (uchar *) "QUANTUM XP34301", 15) == 0)
								   && (AscCompareString(inq->product_rev_level, (uchar *) "1071", 4) == 0)
							  ) {
							} else {
#endif
								asc_dvc->use_tagged_qng |= tid_bits;
								asc_dvc->max_dvc_qng[tid_no] = asc_dvc->cfg->max_tag_qng[tid_no];
#if CC_FIX_QUANTUM_XP34301_1071
							}
#endif
						}
					}
					if (!inq->byte7.Sync) {
						asc_dvc->init_sdtr &= ~tid_bits;
						asc_dvc->sdtr_done &= ~tid_bits;
					} else if (tmp_disable_init_sdtr) {
						asc_dvc->init_sdtr |= tid_bits;
					}
				} else {
					asc_dvc->init_sdtr &= ~tid_bits;
					asc_dvc->sdtr_done &= ~tid_bits;
					asc_dvc->use_tagged_qng &= ~tid_bits;
				}
			}
			if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_ASYN_USE_SYN) {
				if (!(asc_dvc->init_sdtr & tid_bits)) {
					if (
						   (dvc_type == SCSI_TYPE_CDROM)
						   && (AscCompareString((uchar *) inq->vendor_id, (uchar *) "HP ", 3) == 0)
					  ) {
						asc_dvc->pci_fix_asyn_xfer_always |= tid_bits;
					}
					asc_dvc->pci_fix_asyn_xfer |= tid_bits;
#if CC_DISABLE_ASYN_FIX_WANGTEK_TAPE
					if (
						   (dvc_type == SCSI_TYPE_SASD)
						   && (AscCompareString((uchar *) inq->vendor_id, (uchar *) "WANGTEK ", 8) == 0)
					  ) {
						asc_dvc->pci_fix_asyn_xfer &= ~tid_bits;
					}
#endif
					if (asc_dvc->pci_fix_asyn_xfer & tid_bits) {
						AscSetRunChipSynRegAtID(asc_dvc->iop_base, tid_no,
											 ASYN_SDTR_DATA_FIX_PCI_REV_AB);
					}
				}
			}
			sta = 1;
#if CC_INIT_TARGET_TEST_UNIT_READY
			sta = InitTestUnitReady(asc_dvc, scsiq);
#endif
#if CC_INIT_TARGET_READ_CAPACITY
			if (sta == 1) {
				if ((cap_info != 0L) && support_read_cap) {
					if (PollScsiReadCapacity(asc_dvc, scsiq,
											 cap_info) != 1) {
						cap_info->lba = 0L;
						cap_info->blk_size = 0x0000;
					} else {
					}
				}
			}
#endif
		} else {
			asc_dvc->start_motor &= ~tid_bits;
		}
	} else {
	}
	return (dvc_found);
}

int
PollQueueDone(
				 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				 REG ASC_SCSI_REQ_Q dosfar * scsiq,
				 int timeout_sec
)
{
	int                 status;
	int                 retry;
	retry = 0;
	do {
		if (
			   (status = AscExeScsiQueue(asc_dvc,
										 (ASC_SCSI_Q dosfar *) scsiq)) == 1
		  ) {
			if ((status = AscPollQDone(asc_dvc, scsiq,
									   timeout_sec)) != 1) {
				if (status == 0x80) {
					if (retry++ > ASC_MAX_INIT_BUSY_RETRY) {
						break;
					}
					scsiq->r3.done_stat = 0;
					scsiq->r3.host_stat = 0;
					scsiq->r3.scsi_stat = 0;
					scsiq->r3.scsi_msg = 0;
					DvcSleepMilliSecond(2000);
					continue;
				}
				scsiq->r3.done_stat = 0;
				scsiq->r3.host_stat = 0;
				scsiq->r3.scsi_stat = 0;
				scsiq->r3.scsi_msg = 0;
				AscAbortSRB(asc_dvc, (ulong) scsiq);
			}
			return (scsiq->r3.done_stat);
		}
	} while ((status == 0) || (status == 0x80));
	return (scsiq->r3.done_stat = QD_WITH_ERROR);
}

int
PollScsiInquiry(
				   REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				   REG ASC_SCSI_REQ_Q dosfar * scsiq,
				   uchar dosfar * buf,
				   int buf_len
)
{
	if (AscScsiInquiry(asc_dvc, scsiq, buf, buf_len) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	return (PollQueueDone(asc_dvc, (ASC_SCSI_REQ_Q dosfar *) scsiq, 4));
}

#if CC_INIT_TARGET_START_UNIT
int
PollScsiStartUnit(
					 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					 REG ASC_SCSI_REQ_Q dosfar * scsiq
)
{
	if (AscScsiStartStopUnit(asc_dvc, scsiq, 1) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	return (PollQueueDone(asc_dvc, (ASC_SCSI_REQ_Q dosfar *) scsiq, 40));
}
#endif

#if CC_INIT_TARGET_READ_CAPACITY
int
PollScsiReadCapacity(
						REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						REG ASC_SCSI_REQ_Q dosfar * scsiq,
						REG ASC_CAP_INFO dosfar * cap_info
)
{
	ASC_CAP_INFO        scsi_cap_info;
	int                 status;
	if (AscScsiReadCapacity(asc_dvc, scsiq,
							(uchar dosfar *) & scsi_cap_info) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	status = PollQueueDone(asc_dvc, (ASC_SCSI_REQ_Q dosfar *) scsiq, 8);
	if (status == 1) {
#if CC_LITTLE_ENDIAN_HOST
		cap_info->lba = (ulong) * swapfarbuf4((uchar dosfar *) & scsi_cap_info.lba);
		cap_info->blk_size = (ulong) * swapfarbuf4((uchar dosfar *) & scsi_cap_info.blk_size);
#else
		cap_info->lba = scsi_cap_info.lba;
		cap_info->blk_size = scsi_cap_info.blk_size;
#endif
		return (scsiq->r3.done_stat);
	}
	return (scsiq->r3.done_stat = QD_WITH_ERROR);
}
#endif

ulong dosfar       *
swapfarbuf4(
			   uchar dosfar * buf
)
{
	uchar               tmp;

	tmp = buf[3];
	buf[3] = buf[0];
	buf[0] = tmp;

	tmp = buf[1];
	buf[1] = buf[2];
	buf[2] = tmp;

	return ((ulong dosfar *) buf);
}

#if CC_INIT_TARGET_TEST_UNIT_READY
int
PollScsiTestUnitReady(
						 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						 REG ASC_SCSI_REQ_Q dosfar * scsiq
)
{
	if (AscScsiTestUnitReady(asc_dvc, scsiq) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	return (PollQueueDone(asc_dvc, (ASC_SCSI_REQ_Q dosfar *) scsiq, 12));
}

int
InitTestUnitReady(
					 REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					 REG ASC_SCSI_REQ_Q dosfar * scsiq
)
{
	ASC_SCSI_BIT_ID_TYPE tid_bits;
	int                 retry;
	ASC_REQ_SENSE dosfar *sen;
	retry = 0;
	tid_bits = scsiq->r1.target_id;
	while (retry++ < 2) {
		PollScsiTestUnitReady(asc_dvc, scsiq);
		if (scsiq->r3.done_stat == 0x01) {
			return (1);
		} else if (scsiq->r3.done_stat == QD_WITH_ERROR) {
			DvcSleepMilliSecond(100);
			sen = (ASC_REQ_SENSE dosfar *) scsiq->sense_ptr;
			if ((scsiq->r3.scsi_stat == SS_CHK_CONDITION) &&
				((sen->err_code & 0x70) != 0)) {
				if (sen->sense_key == SCSI_SENKEY_NOT_READY) {
					if (asc_dvc->start_motor & tid_bits) {
						if (PollScsiStartUnit(asc_dvc, scsiq) == 1) {
							retry = 0;
							continue;
						} else {
							asc_dvc->start_motor &= ~tid_bits;
							break;
						}
					} else {
						DvcSleepMilliSecond(5000);
					}
				} else if (sen->sense_key == SCSI_SENKEY_ATTENSION) {
					DvcSleepMilliSecond(500);
				} else {
					break;
				}
			} else {
				break;
			}
		} else if (scsiq->r3.done_stat == QD_ABORTED_BY_HOST) {
			break;
		} else {
			break;
		}
	}
	return (0);
}
#endif

int
AscPollQDone(
				REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				REG ASC_SCSI_REQ_Q dosfar * scsiq,
				int timeout_sec
)
{
	int                 loop, loop_end;
	int                 sta;
	PortAddr            iop_base;
	iop_base = asc_dvc->iop_base;
	loop = 0;
	loop_end = timeout_sec * 100;
	sta = 1;
	while (TRUE) {
		if (asc_dvc->err_code != 0) {
			scsiq->r3.done_stat = QD_WITH_ERROR;
			sta = ERR;
			break;
		}
		if (scsiq->r3.done_stat != QD_IN_PROGRESS) {
			if ((scsiq->r3.done_stat == QD_WITH_ERROR) &&
				(scsiq->r3.scsi_stat == SS_TARGET_BUSY)) {
				sta = 0x80;
			}
			break;
		}
		DvcSleepMilliSecond(10);
		if (loop++ > loop_end) {
			sta = 0;
			break;
		}
		if (AscIsChipHalted(iop_base)) {
#if !CC_ASCISR_CHECK_INT_PENDING
			AscAckInterrupt(iop_base);
#endif
			AscISR(asc_dvc);
			loop = 0;
		} else {
			if (AscIsIntPending(iop_base)) {
#if !CC_ASCISR_CHECK_INT_PENDING
				AscAckInterrupt(iop_base);
#endif
				AscISR(asc_dvc);
			}
		}
	}
	return (sta);
}

int
AscCompareString(
					ruchar * str1,
					ruchar * str2,
					int len
)
{
	int                 i;
	int                 diff;
	for (i = 0; i < len; i++) {
		diff = (int) (str1[i] - str2[i]);
		if (diff != 0)
			return (diff);
	}
	return (0);
}

uchar
AscReadLramByte(
				   PortAddr iop_base,
				   ushort addr
)
{
	uchar               byte_data;
	ushort              word_data;
	if (isodd_word(addr)) {
		AscSetChipLramAddr(iop_base, addr - 1);
		word_data = AscGetChipLramData(iop_base);
		byte_data = (uchar) ((word_data >> 8) & 0xFF);
	} else {
		AscSetChipLramAddr(iop_base, addr);
		word_data = AscGetChipLramData(iop_base);
		byte_data = (uchar) (word_data & 0xFF);
	}
	return (byte_data);
}

ushort
AscReadLramWord(
				   PortAddr iop_base,
				   ushort addr
)
{
	ushort              word_data;
	AscSetChipLramAddr(iop_base, addr);
	word_data = AscGetChipLramData(iop_base);
	return (word_data);
}

ulong
AscReadLramDWord(
					PortAddr iop_base,
					ushort addr
)
{
	ushort              val_low, val_high;
	ulong               dword_data;
	AscSetChipLramAddr(iop_base, addr);
	val_low = AscGetChipLramData(iop_base);
	val_high = AscGetChipLramData(iop_base);
	dword_data = ((ulong) val_high << 16) | (ulong) val_low;
	return (dword_data);
}

void
AscWriteLramWord(
					PortAddr iop_base,
					ushort addr,
					ushort word_val
)
{
	AscSetChipLramAddr(iop_base, addr);
	AscSetChipLramData(iop_base, word_val);
	return;
}

void
AscWriteLramDWord(
					 PortAddr iop_base,
					 ushort addr,
					 ulong dword_val
)
{
	ushort              word_val;
	AscSetChipLramAddr(iop_base, addr);
	word_val = (ushort) dword_val;
	AscSetChipLramData(iop_base, word_val);
	word_val = (ushort) (dword_val >> 16);
	AscSetChipLramData(iop_base, word_val);
	return;
}

void
AscWriteLramByte(
					PortAddr iop_base,
					ushort addr,
					uchar byte_val
)
{
	ushort              word_data;
	if (isodd_word(addr)) {
		addr--;
		word_data = AscReadLramWord(iop_base, addr);
		word_data &= 0x00FF;
		word_data |= (((ushort) byte_val << 8) & 0xFF00);
	} else {
		word_data = AscReadLramWord(iop_base, addr);
		word_data &= 0xFF00;
		word_data |= ((ushort) byte_val & 0x00FF);
	}
	AscWriteLramWord(iop_base, addr, word_data);
	return;
}




void
AscMemWordCopyToLram(
						PortAddr iop_base,
						ushort s_addr,
						ushort dosfar * s_buffer,
						int words
)
{
	AscSetChipLramAddr(iop_base, s_addr);
	DvcOutPortWords(iop_base + IOP_RAM_DATA, s_buffer, words);
	return;
}

void
AscMemDWordCopyToLram(
						 PortAddr iop_base,
						 ushort s_addr,
						 ulong dosfar * s_buffer,
						 int dwords
)
{
	AscSetChipLramAddr(iop_base, s_addr);
	DvcOutPortDWords(iop_base + IOP_RAM_DATA, s_buffer, dwords);
	return;
}

void
AscMemWordCopyFromLram(
						  PortAddr iop_base,
						  ushort s_addr,
						  ushort dosfar * d_buffer,
						  int words
)
{
	AscSetChipLramAddr(iop_base, s_addr);
	DvcInPortWords(iop_base + IOP_RAM_DATA, d_buffer, words);
	return;
}

ulong
AscMemSumLramWord(
					 PortAddr iop_base,
					 ushort s_addr,
					 rint words
)
{
	ulong               sum;
	int                 i;
	sum = 0L;
	for (i = 0; i < words; i++, s_addr += 2) {
		sum += AscReadLramWord(iop_base, s_addr);
	}
	return (sum);
}

void
AscMemWordSetLram(
					 PortAddr iop_base,
					 ushort s_addr,
					 ushort set_wval,
					 rint words
)
{
	rint                i;
	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < words; i++) {
		AscSetChipLramData(iop_base, set_wval);
	}
	return;
}


int
AscScsiInquiry(
				  REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
				  REG ASC_SCSI_REQ_Q dosfar * scsiq,
				  uchar dosfar * buf,
				  int buf_len
)
{
	if (AscScsiSetupCmdQ(asc_dvc, scsiq, buf,
						 (ulong) buf_len) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	scsiq->cdb[0] = (uchar) SCSICMD_Inquiry;
	scsiq->cdb[1] = scsiq->r1.target_lun << 5;
	scsiq->cdb[2] = 0;
	scsiq->cdb[3] = 0;
	scsiq->cdb[4] = buf_len;
	scsiq->cdb[5] = 0;
	scsiq->r2.cdb_len = 6;
	return (0);
}

#if CC_INIT_TARGET_READ_CAPACITY
int
AscScsiReadCapacity(
					   REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
					   REG ASC_SCSI_REQ_Q dosfar * scsiq,
					   uchar dosfar * info
)
{
	if (AscScsiSetupCmdQ(asc_dvc, scsiq, info, 8L) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	scsiq->cdb[0] = (uchar) SCSICMD_ReadCapacity;
	scsiq->cdb[1] = scsiq->r1.target_lun << 5;
	scsiq->cdb[2] = 0;
	scsiq->cdb[3] = 0;
	scsiq->cdb[4] = 0;
	scsiq->cdb[5] = 0;
	scsiq->cdb[6] = 0;
	scsiq->cdb[7] = 0;
	scsiq->cdb[8] = 0;
	scsiq->cdb[9] = 0;
	scsiq->r2.cdb_len = 10;
	return (0);
}
#endif

#if CC_INIT_TARGET_TEST_UNIT_READY
int
AscScsiTestUnitReady(
						REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						REG ASC_SCSI_REQ_Q dosfar * scsiq
)
{
	if (AscScsiSetupCmdQ(asc_dvc, scsiq, FNULLPTR,
						 (ulong) 0L) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	scsiq->r1.cntl = (uchar) ASC_SCSIDIR_NODATA;
	scsiq->cdb[0] = (uchar) SCSICMD_TestUnitReady;
	scsiq->cdb[1] = scsiq->r1.target_lun << 5;
	scsiq->cdb[2] = 0;
	scsiq->cdb[3] = 0;
	scsiq->cdb[4] = 0;
	scsiq->cdb[5] = 0;
	scsiq->r2.cdb_len = 6;
	return (0);
}
#endif

#if CC_INIT_TARGET_START_UNIT
int
AscScsiStartStopUnit(
						REG ASC_DVC_VAR asc_ptr_type * asc_dvc,
						REG ASC_SCSI_REQ_Q dosfar * scsiq,
						uchar op_mode
)
{
	if (AscScsiSetupCmdQ(asc_dvc, scsiq, FNULLPTR, (ulong) 0L) == ERR) {
		return (scsiq->r3.done_stat = QD_WITH_ERROR);
	}
	scsiq->r1.cntl = (uchar) ASC_SCSIDIR_NODATA;
	scsiq->cdb[0] = (uchar) SCSICMD_StartStopUnit;
	scsiq->cdb[1] = scsiq->r1.target_lun << 5;
	scsiq->cdb[2] = 0;
	scsiq->cdb[3] = 0;
	scsiq->cdb[4] = op_mode;
	scsiq->cdb[5] = 0;
	scsiq->r2.cdb_len = 6;
	return (0);
}
#endif
