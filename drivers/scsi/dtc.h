/*
 * DTC controller, taken from T128 driver by...
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * DISTRIBUTION RELEASE 1. 
 *
 * For more information, please consult 
 *
 * 
 * 
 * and 
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

#ifndef DTC3280_H
#define DTC3280_H

#define DTC_PUBLIC_RELEASE 1

/*#define DTCDEBUG 0x1*/
#define DTCDEBUG_INIT	0x1
#define DTCDEBUG_TRANSFER 0x2

/*
 * The DTC3180 & 3280 boards are memory mapped.
 * 
 */

/*
 */ 
/* Offset from DTC_5380_OFFSET */
#define DTC_CONTROL_REG		0x100	/* rw */
#define D_CR_ACCESS		0x80	/* ro set=can access 3280 registers */
#define CSR_DIR_READ		0x40	/* rw direction, 1 = read 0 = write */

#define CSR_RESET              0x80    /* wo  Resets 53c400 */
#define CSR_5380_REG           0x80    /* ro  5380 registers can be accessed */
#define CSR_TRANS_DIR          0x40    /* rw  Data transfer direction */
#define CSR_SCSI_BUFF_INTR     0x20    /* rw  Enable int on transfer ready */
#define CSR_5380_INTR          0x10    /* rw  Enable 5380 interrupts */
#define CSR_SHARED_INTR        0x08    /* rw  Interrupt sharing */
#define CSR_HOST_BUF_NOT_RDY   0x04    /* ro  Host buffer not ready */
#define CSR_SCSI_BUF_RDY       0x02    /* ro  SCSI buffer ready */
#define CSR_GATED_5380_IRQ     0x01    /* ro  Last block xferred */
#define CSR_INT_BASE (CSR_SCSI_BUFF_INTR | CSR_5380_INTR)


#define DTC_BLK_CNT		0x101   /* rw 
					 * # of 128-byte blocks to transfer */


#define D_CR_ACCESS             0x80    /* ro set=can access 3280 registers */

#define DTC_SWITCH_REG		0x3982	/* ro - DIP switches */
#define DTC_RESUME_XFER		0x3982	/* wo - resume data xfer 
					   * after disconnect/reconnect*/

#define DTC_5380_OFFSET		0x3880	/* 8 registers here, see NCR5380.h */

/*!!!! for dtc, it's a 128 byte buffer at 3900 !!! */
#define DTC_DATA_BUF		0x3900  /* rw 128 bytes long */


#ifndef ASM
int dtc_abort(Scsi_Cmnd *);
int dtc_biosparam(Disk *, kdev_t, int*);
int dtc_detect(Scsi_Host_Template *);
int dtc_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int dtc_reset(Scsi_Cmnd *, unsigned int reset_flags);
int dtc_proc_info (char *buffer, char **start, off_t offset,
		   int length, int hostno, int inout);

#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 32 
#endif

/* 
 * I hadn't thought of this with the earlier drivers - but to prevent
 * macro definition conflicts, we shouldn't define all of the internal
 * macros when this is being used solely for the host stub.
 */

#if defined(HOSTS_C) || defined(MODULE)

#define DTC3x80 {NULL, NULL, NULL, NULL, \
	"DTC 3180/3280 ", dtc_detect, NULL,  \
	NULL,							\
	NULL, dtc_queue_command, dtc_abort, dtc_reset, NULL, 	\
	dtc_biosparam, 						\
	/* can queue */ CAN_QUEUE, /* id */ 7, SG_ALL,			\
	/* cmd per lun */ CMD_PER_LUN , 0, 0, DISABLE_CLUSTERING}

#endif

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    volatile unsigned char *base

#define NCR5380_local_declare() \
    volatile unsigned char *base

#define NCR5380_setup(instance) \
    base = (volatile unsigned char *) (instance)->base

#define DTC_address(reg) (base + DTC_5380_OFFSET + reg)

#define dbNCR5380_read(reg)                                              \
    (rval=*(DTC_address(reg)), \
     (((unsigned char) printk("DTC : read register %d at addr %08x is: %02x\n"\
    , (reg), (int)DTC_address(reg), rval)), rval ) )

#define dbNCR5380_write(reg, value) do {                                  \
    printk("DTC : write %02x to register %d at address %08x\n",         \
            (value), (reg), (int)DTC_address(reg));     \
    *(DTC_address(reg)) = (value);} while(0)


#if !(DTCDEBUG & DTCDEBUG_TRANSFER) 
#define NCR5380_read(reg) (*(DTC_address(reg)))
#define NCR5380_write(reg, value) (*(DTC_address(reg)) = (value))
#else
#define NCR5380_read(reg) (*(DTC_address(reg)))
#define xNCR5380_read(reg)						\
    (((unsigned char) printk("DTC : read register %d at address %08x\n"\
    , (reg), DTC_address(reg))), *(DTC_address(reg)))

#define NCR5380_write(reg, value) do {					\
    printk("DTC : write %02x to register %d at address %08x\n", 	\
	    (value), (reg), (int)DTC_address(reg));	\
    *(DTC_address(reg)) = (value);		} while(0)
#endif

#define NCR5380_intr dtc_intr
#define NCR5380_queue_command dtc_queue_command
#define NCR5380_abort dtc_abort
#define NCR5380_reset dtc_reset
#define NCR5380_proc_info dtc_proc_info 

/* 15 12 11 10
   1001 1100 0000 0000 */

#define DTC_IRQS 0x9c00


#endif /* else def HOSTS_C */
#endif /* ndef ASM */
#endif /* DTC3280_H */
