/********************************************************
* Header file for eata_dma.c Linux EATA-DMA SCSI driver *
* (c) 1993,94,95 Michael Neuffer                        *
*********************************************************
* last change: 95/04/10                                 *
********************************************************/


#ifndef _EATA_DMA_H
#define _EATA_DMA_H

#define VER_MAJOR 2
#define VER_MINOR 3
#define VER_SUB   "5r"

/************************************************************************
 * Here you can configure your drives that are using a non-standard     *
 * geometry.                                                            *
 * To enable this set HARDCODED to 1                                    *
 * If you have only one drive that need reconfiguration, set ID1 to -1  *
 ************************************************************************/
#define HARDCODED     0          /* Here are drives running in emu. mode   */

#define ID0           0          /* SCSI ID of "IDE" drive mapped to C:    
                                  * If you're not sure check your config
				  * utility that came with your controller
				  */
#define HEADS0       13          /* Number of emulated heads of this drive */  
#define SECTORS0     38          /* Number of emulated sectors             */ 
#define CYLINDER0   719          /* Number of emulated cylinders           */
   
#define ID1           1          /* SCSI ID of "IDE" drive mapped to D:    */
#define HEADS1       16          /* Number of emulated heads of this drive */ 
#define SECTORS1     62          /* Number of emulated sectors             */
#define CYLINDER1  1024          /* Number of emulated cylinders           */

/************************************************************************
 * Here you can switch parts of the code on and of                      *
 ************************************************************************/

#define CHECKPAL        0        /* EISA pal checking on/off            */
#define EATA_DMA_PROC   0        /* proc-fs support                     */

/************************************************************************
 * Debug options.                                                       * 
 * Enable DEBUG and whichever options you require.                      *
 ************************************************************************/
#define DEBUG_EATA	1	/* Enable debug code. 			*/
#define DPT_DEBUG       0       /* Bobs special                         */
#define DBG_DELAY       0	/* Build in delays so debug messages can be
				 * be read before they vanish of the top of
				 * the screen!
				 */
#define DBG_PROBE	0	/* Debug probe routines. 		*/
#define DBG_PCI         0       /* Trace PCI routines                   */
#define DBG_EISA        0       /* Trace EISA routines                  */
#define DBG_ISA         0       /* Trace ISA routines                   */ 
#define DBG_BLINK       0       /* Trace Blink check                    */
#define DBG_PIO         0       /* Trace get_config_PIO                 */
#define DBG_COM 	0	/* Trace command call   		*/
#define DBG_QUEUE	0	/* Trace command queueing. 		*/
#define DBG_INTR	0       /* Trace interrupt service routine. 	*/
#define DBG_INTR2	0       /* Trace interrupt service routine. 	*/
#define DBG_INTR3       0       /* Trace interrupt service routine.     */
#define DBG_PROC        0       /* Debug proc-fs related statistics     */
#define DBG_REGISTER    0       /* */
#define DBG_ABNORM	1	/* Debug abnormal actions (reset, abort)*/

#if DEBUG_EATA 
#define DBG(x, y)	if ((x)) {y;} 
#else
#define DBG(x, y)
#endif


#define EATA_DMA {                   \
	NULL, NULL,                  \
        "EATA (Extended Attachment) driver", \
        eata_detect,                 \
        eata_release,                \
        eata_info,                   \
        eata_command,                \
        eata_queue,                  \
        eata_abort,                  \
        eata_reset,                  \
        NULL, /* Slave attach */     \
	scsicam_bios_param,          \
        0,      /* Canqueue     */   \
        0,      /* this_id      */   \
        0,      /* sg_tablesize */   \
        0,      /* cmd_per_lun  */   \
        0,      /* present      */   \
        1,      /* True if ISA  */   \
	ENABLE_CLUSTERING }

int eata_detect(Scsi_Host_Template *);
const char *eata_info(struct Scsi_Host *);
int eata_command(Scsi_Cmnd *);
int eata_queue(Scsi_Cmnd *, void *(done)(Scsi_Cmnd *));
int eata_abort(Scsi_Cmnd *);
int eata_reset(Scsi_Cmnd *);
int eata_release(struct Scsi_Host *);

/*********************************************
 * Misc. definitions                         *
 *********************************************/

#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#define R_LIMIT 0x20000

#define MAXISA     4
#define MAXEISA   16  
#define MAXPCI    16
#define MAXIRQ    16 
#define MAXTARGET  8

#define MAX_PCI_DEVICES   32             /* Maximum # Of Devices Per Bus   */
#define MAX_METHOD_2      16             /* Max Devices For Method 2       */
#define MAX_PCI_BUS       16             /* Maximum # Of Busses Allowed    */

#define SG_SIZE           64 

#define C_P_L_CURRENT_MAX 16  /* Until this limit in the mm is removed    
			       * Kernels < 1.1.86 died horrible deaths
			       * if you used values >2. The memory management
			       * since pl1.1.86 seems to cope with up to 10
			       * queued commands per device. 
                               * Since 1.2.0 the memory management seems to 
                               * have no more problems......
			       */
#define C_P_L_DIV          3  /* 1 <= C_P_L_DIV <= 8            
			       * You can use this parameter to fine-tune
			       * the driver. Depending on the number of 
			       * devices and their speed and ability to queue 
			       * commands, you will get the best results with a
			       * value
			       * ~= numdevices-(devices_unable_to_queue_commands/2)
			       * The reason for this is that the disk driver 
			       * tends to flood the queue, so that other 
			       * drivers have problems to queue commands 
			       * themselves. This can for example result in 
			       * the effect that the tape stops during disk 
			       * accesses. 
			       */

#define FREE       0
#define USED       1
#define TIMEOUT    2
#define RESET      4
#define LOCKED     8

#define HD(cmd)  ((hostdata *)&(cmd->host->hostdata))
#define CD(cmd)  ((struct eata_ccb *)(cmd->host_scribble))
#define SD(host) ((hostdata *)&(host->hostdata))

#define DELAY(x) { int i; i = jiffies + x; while (jiffies < i); }
#define DEL2(x)  { ulong i; for (i = 0; i < 0xffff*x; i++); }

/***********************************************
 *    EATA Command & Register definitions      *
 ***********************************************/
#define PCI_REG_DPTconfig        0x40    
#define PCI_REG_PumpModeAddress  0x44    
#define PCI_REG_PumpModeData     0x48    
#define PCI_REG_ConfigParam1     0x50    
#define PCI_REG_ConfigParam2     0x54    


#define EATA_CMD_PIO_READ_CONFIG 0xf0
#define EATA_CMD_PIO_SET_CONFIG  0xf1
#define EATA_CMD_PIO_SEND_CP     0xf2
#define EATA_CMD_PIO_RECEIVE_SP  0xf3
#define EATA_CMD_PIO_TRUNC       0xf4

#define EATA_CMD_RESET           0xf9

#define EATA_CMD_DMA_READ_CONFIG 0xfd
#define EATA_CMD_DMA_SET_CONFIG  0xfe
#define EATA_CMD_DMA_SEND_CP     0xff

#define ECS_EMULATE_SENSE        0xd4

#define HA_WCOMMAND 0x07        /* command register offset   */
#define HA_WDMAADDR 0x02        /* DMA address LSB offset    */  
#define HA_RAUXSTAT 0x08        /* aux status register offset*/
#define HA_RSTATUS  0x07        /* status register offset    */
#define HA_RDATA    0x00        /* data register (16bit)     */

#define HA_ABUSY    0x01        /* aux busy bit              */
#define HA_AIRQ     0x02        /* aux IRQ pending bit       */
#define HA_SERROR   0x01        /* pr. command ended in error*/
#define HA_SMORE    0x02        /* more data soon to come    */
#define HA_SCORR    0x04        /* data corrected            */
#define HA_SDRQ     0x08        /* data request active       */
#define HA_SSC      0x10        /* seek complete             */
#define HA_SFAULT   0x20        /* write fault               */
#define HA_SREADY   0x40        /* drive ready               */
#define HA_SBUSY    0x80        /* drive busy                */
#define HA_SDRDY    HA_SSC+HA_SREADY+HA_SDRQ 

#define HA_NO_ERROR      0x00
#define HA_ERR_SEL_TO    0x01
#define HA_ERR_CMD_TO    0x02
#define HA_ERR_RESET     0x03
#define HA_INIT_POWERUP  0x04
#define HA_UNX_BUSPHASE  0x05
#define HA_UNX_BUS_FREE  0x06
#define HA_BUS_PARITY    0x07
#define HA_SCSI_HUNG     0x08
#define HA_UNX_MSGRJCT   0x09
#define HA_RESET_STUCK   0x0a
#define HA_RSENSE_FAIL   0x0b
#define HA_PARITY_ERR    0x0c
#define HA_CP_ABORT_NA   0x0d
#define HA_CP_ABORTED    0x0e
#define HA_CP_RESET_NA   0x0f
#define HA_CP_RESET      0x10

/**********************************************
 * Message definitions                        *
 **********************************************/

struct reg_bit {        /* reading this one will clear the interrupt 	*/
  unchar error:1;     /* previous command ended in an error           */
  unchar more:1;      /* more DATA coming soon, poll BSY & DRQ (PIO) */
  unchar corr:1;      /* data read was successfully corrected with ECC*/
  unchar drq:1;       /* data request active  */     
  unchar sc:1;        /* seek complete        */
  unchar fault:1;     /* write fault          */
  unchar ready:1;     /* drive ready          */
  unchar busy:1;      /* controller busy      */
};

struct reg_abit {       /* reading this won't clear the interrupt */
  unchar abusy:1;     /* auxiliary busy                         */
  unchar irq:1;       /* set when drive interrupt is asserted   */
  unchar dummy:6;
};

struct eata_register {      	    /* EATA register set */
  unchar data_reg[2];     	/* R, couldn't figure this one out          */
  unchar cp_addr[4];      	/* W, CP address register                   */
  union { 
    unchar command;     	/* W, command code: [read|set] conf, send CP*/
    struct reg_bit status;	/* R, see register_bit1                     */
    unchar statusunchar;
  } ovr;   
  struct reg_abit aux_stat; 	/* R, see register_bit2               	    */
};

/**********************************************
 *  Other  definitions                        *
 **********************************************/

struct eata_sg_list
{
  ulong data;
  ulong len;
};

struct get_conf {          /* Read Configuration Array  */
  ulong  len;                 /* Should return 0x22 			*/
  unchar sig[4];              /* Signature MUST be "EATA" 		*/
  unchar    version2:4,
             version:4;       /* EATA Version level          	        */
  unchar OCS_enabled:1,	      /* Overlap Command Support enabled  	*/
         TAR_support:1,	      /* SCSI Target Mode supported		*/
              TRNXFR:1,	      /* Truncate Transfer Cmd not necessary	*/
                              /* Only used in PIO Mode 		        */
        MORE_support:1,	      /* MORE supported (only PIO Mode) 	*/
         DMA_support:1,	      /* DMA supported Driver uses only 	*/
                              /* this mode				*/
           DMA_valid:1,       /* DRQ value in Byte 30 is valid	        */
                 ATA:1,       /* ATA device connected (not supported)	*/
           HAA_valid:1;       /* Hostadapter Address is valid     	*/

  ushort cppadlen;	      /* Number of pad unchars send after CD data */
                              /* set to zero for DMA commands		*/
  unchar scsi_id[4];          /* SCSI ID of controller 2-0 Byte 0 res.  */
                              /* if not, zero is returned 		*/
  ulong  cplen;	              /* CP length: number of valid cp unchars 	*/
  ulong  splen;	              /* Number of unchars returned after	*/ 
                              /* Receive SP command			*/
  ushort queuesiz;	      /* max number of queueable CPs		*/
  ushort dummy;
  ushort SGsiz;	              /* max number of SG table entries	        */
  unchar    IRQ:4,            /* IRQ used this HA			*/
         IRQ_TR:1,            /* IRQ Trigger: 0=edge, 1=level	        */
         SECOND:1,            /* This is a secondary controller	        */  
    DMA_channel:2;            /* DRQ index, DRQ is 2comp of DRQX	*/
  unchar sync;                /* device at ID 7 tru 0 is running in 	*/
                              /* synchronous mode, this will disappear  */
  unchar   DSBLE:1,           /* ISA i/o addressing is disabled         */
         FORCADR:1,           /* i/o address has been forced            */
                :6;
  unchar  MAX_ID:5,           /* Max number of SCSI target IDs          */
        MAX_CHAN:3;           /* Number of SCSI busses on HBA           */
  unchar MAX_LUN;             /* Max number of LUNs                     */
  unchar        :5,          
         ID_qest:1,           /* Raidnum ID is questionable             */
          is_PCI:1,           /* HBA is PCI                             */
         is_EISA:1;           /* HBA is EISA                            */
  unchar unused[478]; 
};

struct eata_ccb {             /* Send Command Packet structure      */
 
  unchar SCSI_Reset:1,        /* Cause a SCSI Bus reset on the cmd  */
           HBA_Init:1,        /* Cause Controller to reinitialize   */
       Auto_Req_Sen:1,        /* Do Auto Request Sense on errors    */
            scatter:1,        /* Data Ptr points to a SG Packet     */
             Resrvd:1,        /* RFU                                */
          Interpret:1,        /* Interpret the SCSI cdb of own use  */
            DataOut:1,        /* Data Out phase with command        */
             DataIn:1;        /* Data In phase with command         */
  unchar reqlen;     	      /* Request Sense Length               */ 
                              /* Valid if Auto_Req_Sen=1            */
  unchar unused[3];
  unchar  FWNEST:1,           /* send cmd to phys RAID component*/
         unused2:7;
  unchar Phsunit:1,           /* physical unit on mirrored pair	*/
            I_AT:1,           /* inhibit address translation    */
         I_HBA_C:1,           /* HBA Inhibit caching            */
         unused3:5;

  unchar cp_id;               /* SCSI Device ID of target       */ 
  unchar    cp_lun:3,
                  :2,
         cp_luntar:1,         /* CP is for target ROUTINE       */
         cp_dispri:1,         /* Grant disconnect privilege     */
       cp_identify:1;         /* Always TRUE                    */         
  unchar cp_msg1;             /* Message bytes 0-3              */
  unchar cp_msg2;
  unchar cp_msg3;
  unchar cp_cdb[12];   	      /* Command Descriptor Block       */
  ulong  cp_datalen; 	      /* Data Transfer Length           */
                              /* If scatter=1 len of sg package */
  void *cp_viraddr;           /* address of this ccb            */
  ulong cp_dataDMA; 	      /* Data Address, if scatter=1     */
                              /* address of scatter packet      */  
  ulong cp_statDMA;           /* address for Status Packet      */ 
  ulong cp_reqDMA;            /* Request Sense Address, used if */
                              /* CP command ends with error     */
 
  ulong timeout;
  unchar retries;
  unchar status;              /* status of this queueslot       */
  struct eata_sg_list sg_list[SG_SIZE];
  Scsi_Cmnd *cmd;             /* address of cmd                 */
};


struct eata_sp {
  unchar hba_stat:7,          /* HBA status                     */
              EOC:1;          /* True if command finished       */
  unchar scsi_stat;           /* Target SCSI status             */       
  unchar reserved[2];
  ulong  residue_len;         /* Number of unchars not transferred */
  struct eata_ccb *ccb;       /* Address set in COMMAND PACKET  */
  unchar msg[12];
};

typedef struct hstd {
  char   vendor[9];
  char   name[18];
  char   revision[6];
  char   EATA_revision;
  unchar bustype;              /* bustype of HBA             */
  unchar channel;              /* no. of scsi channel        */
  unchar state;                /* state of HBA               */
  unchar primary;              /* true if primary            */
  ulong  reads[13];
  ulong  writes[13];
  unchar t_state[MAXTARGET];   /* state of Target (RESET,..) */
  uint   t_timeout[MAXTARGET]; /* timeouts on target         */
  uint   last_ccb;             /* Last used ccb              */
  struct Scsi_Host *next;         
  struct Scsi_Host *prev;
  struct eata_sp sp;           /* status packet              */ 
  struct eata_ccb ccb[0];      /* ccb array begins here      */
}hostdata;



/* structure for max. 2 emulated drives */
struct drive_geom_emul {
  unchar trans;                 /* translation flag 1=transl */
  unchar channel;               /* SCSI channel number       */
  unchar HBA;                   /* HBA number (prim/sec)     */
  unchar id;                    /* drive id                  */
  unchar lun;                   /* drive lun                 */
  uint   heads;                 /* number of heads           */
  uint   sectors;               /* number of sectors         */
  uint   cylinder;              /* number of cylinders       */
};

struct geom_emul {
  int bios_drives;               /* number of emulated drives */
  struct drive_geom_emul drv[2]; /* drive structures          */
};

#endif /* _EATA_H */
