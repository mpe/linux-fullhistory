/*
 * mac_scsi.h -- Header file for the Macintosh native SCSI driver
 *
 * based on Roman Hodeks atari_scsi.h
 */

/*
 * atari_scsi.h -- Header file for the Atari native SCSI driver
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * (Loosely based on the work of Robert De Vries' team)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


#ifndef MAC_SCSI_H
#define MAC_SCSI_H

/* (I_HAVE_OVERRUNS stuff removed) */

#ifndef ASM
int mac_scsi_abort (Scsi_Cmnd *);
int mac_scsi_detect (Scsi_Host_Template *);
const char * mac_scsi_info (struct Scsi_Host *host);
int mac_scsi_queue_command (Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int mac_scsi_reset (Scsi_Cmnd *, unsigned int);
int mac_scsi_proc_info (char *, char **, off_t, int, int, int);
#ifdef MODULE
int mac_scsi_release (struct Scsi_Host *);
#else
#define mac_scsi_release NULL
#endif

/* The values for CMD_PER_LUN and CAN_QUEUE are somehow arbitrary. Higher
 * values should work, too; try it! (but cmd_per_lun costs memory!) */

/* But there seems to be a bug somewhere that requires CAN_QUEUE to be
 * 2*CMD_PER_LUN. At least on a TT, no spurious timeouts seen since
 * changed CMD_PER_LUN... */

/* Note: The Falcon currently uses 8/1 setting due to unsolved problems with
 * cmd_per_lun != 1 */

#define MAC_SCSI_CAN_QUEUE		        16
#define MAC_SCSI_CMD_PER_LUN		        8
#define MAC_SCSI_SG_TABLESIZE		SG_ALL

#define	DEFAULT_USE_TAGGED_QUEUING	0


#if defined (HOSTS_C) || defined (MODULE)

#define MAC_SCSI { NULL, NULL, NULL,				\
  mac_scsi_proc_info,						\
  "Macintosh NCR5380 SCSI",					\
  mac_scsi_detect,						\
  mac_scsi_release,						\
  mac_scsi_info,						\
  /* command */ NULL,						\
  mac_scsi_queue_command,					\
  mac_scsi_abort,						\
  mac_scsi_reset,						\
  /* slave_attach */	NULL,					\
  /* bios_param */	NULL,					\
  /* can queue */	0, /* initialized at run-time */	\
  /* host_id */		0, /* initialized at run-time */	\
  /* scatter gather */	0, /* initialized at run-time */	\
  /* cmd per lun */	0, /* initialized at run-time */	\
  /* present */		0,					\
  /* unchecked ISA DMA */ 0,					\
  /* use_clustering */	DISABLE_CLUSTERING }

#endif

#ifndef HOSTS_C

#define	NCR5380_implementation_fields	/* none */

#define NCR5380_read(reg)		  mac_scsi_reg_read( reg )
#define NCR5380_write(reg, value) mac_scsi_reg_write( reg, value )

#define NCR5380_intr mac_scsi_intr
#define NCR5380_queue_command mac_scsi_queue_command
#define NCR5380_abort mac_scsi_abort
#define NCR5380_proc_info mac_scsi_proc_info
#define NCR5380_dma_read_setup(inst,d,c) mac_scsi_dma_setup (inst, d, c, 0)
#define NCR5380_dma_write_setup(inst,d,c) mac_scsi_dma_setup (inst, d, c, 1)
#define NCR5380_dma_residual(inst) mac_scsi_dma_residual( inst )
#define	NCR5380_dma_xfer_len(i,cmd,phase) \
	mac_dma_xfer_len(cmd->SCp.this_residual,cmd,((phase) & SR_IO) ? 0 : 1)
#ifdef PSEUDO_DMA
#define NCR5380_pread(inst,d,l) mac_pdma_read (inst, d, l)
#define NCR5380_pwrite(inst,d,l) mac_pdma_write (inst, d, l)
#endif

/* Debugging printk definitions:
 *
 *  ARB  -> arbitration
 *  ASEN -> auto-sense
 *  DMA  -> DMA
 *  HSH  -> PIO handshake
 *  INF  -> information transfer
 *  INI  -> initialization
 *  INT  -> interrupt
 *  LNK  -> linked commands
 *  MAIN -> NCR5380_main() control flow
 *  NDAT -> no data-out phase
 *  NWR  -> no write commands
 *  PIO  -> PIO transfers
 *  PDMA -> pseudo DMA (unused on MAC)
 *  QU   -> queues
 *  RSL  -> reselections
 *  SEL  -> selections
 *  USL  -> usleep cpde (unused on MAC)
 *  LBS  -> last byte sent (unused on MAC)
 *  RSS  -> restarting of selections
 *  EXT  -> extended messages
 *  ABRT -> aborting and resetting
 *  TAG  -> queue tag handling
 *  MER  -> merging of consec. buffers
 *
 */

#if NDEBUG & NDEBUG_ARBITRATION
#define ARB_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ARB_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_AUTOSENSE
#define ASEN_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ASEN_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_DMA
#define DMA_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define DMA_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_HANDSHAKE
#define HSH_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define HSH_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INFORMATION
#define INF_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INF_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INIT
#define INI_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INI_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INTR
#define INT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_LINKED
#define LNK_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define LNK_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_MAIN
#define MAIN_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define MAIN_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_NO_DATAOUT
#define NDAT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define NDAT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_NO_WRITE
#define NWR_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define NWR_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_PIO
#define PIO_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define PIO_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_PSEUDO_DMA
#define PDMA_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define PDMA_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_QUEUES
#define QU_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define QU_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_RESELECTION
#define RSL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define RSL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_SELECTION
#define SEL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define SEL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_USLEEP
#define USL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define USL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_LAST_BYTE_SENT
#define LBS_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define LBS_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_RESTART_SELECT
#define RSS_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define RSS_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_EXTENDED
#define EXT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define EXT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_ABORT
#define ABRT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ABRT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_TAGS
#define TAG_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define TAG_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_MERGING
#define MER_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define MER_PRINTK(format, args...)
#endif

/* conditional macros for NCR5380_print_{,phase,status} */

#define NCR_PRINT(mask)	\
	((NDEBUG & (mask)) ? NCR5380_print(instance) : (void)0)

#define NCR_PRINT_PHASE(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_phase(instance) : (void)0)

#define NCR_PRINT_STATUS(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_status(instance) : (void)0)

#define NDEBUG_ANY	0xffffffff


#endif /* else def HOSTS_C */
#endif /* ndef ASM */
#endif /* MAC_SCSI_H */


