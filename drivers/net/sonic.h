/*
 * Helpfile for sonic.c
 *
 * (C) Waldorf Electronics, Germany
 * Written by Andreas Busse
 *
 * NOTE: most of the structure definitions here are endian dependent.
 * If you want to use this driver on big endian machines, the data
 * and pad structure members must be exchanged. Also, the structures
 * need to be changed accordingly to the bus size. 
 *
 */

#ifndef SONIC_H
#define SONIC_H

/*
 * Macros to access SONIC registers
 */
#define SONIC_READ(reg) \
	*((volatile unsigned int *)base_addr+reg)

#define SONIC_WRITE(reg,val) \
	*((volatile unsigned int *)base_addr+reg) = val


/*
 * SONIC register offsets
 */
 
#define SONIC_CMD              0x00
#define SONIC_DCR              0x01
#define SONIC_RCR              0x02
#define SONIC_TCR              0x03
#define SONIC_IMR              0x04
#define SONIC_ISR              0x05

#define SONIC_UTDA             0x06
#define SONIC_CTDA             0x07

#define SONIC_URDA             0x0d
#define SONIC_CRDA             0x0e
#define SONIC_EOBC             0x13
#define SONIC_URRA             0x14
#define SONIC_RSA              0x15
#define SONIC_REA              0x16
#define SONIC_RRP              0x17
#define SONIC_RWP              0x18
#define SONIC_RSC              0x2b

#define SONIC_CEP              0x21
#define SONIC_CAP2             0x22
#define SONIC_CAP1             0x23
#define SONIC_CAP0             0x24
#define SONIC_CE               0x25
#define SONIC_CDP              0x26
#define SONIC_CDC              0x27

#define SONIC_WT0              0x29
#define SONIC_WT1              0x2a

#define SONIC_SR               0x28


/* test-only registers */

#define SONIC_TPS		0x08
#define SONIC_TFC		0x09
#define SONIC_TSA0		0x0a
#define SONIC_TSA1		0x0b
#define SONIC_TFS		0x0c

#define SONIC_CRBA0		0x0f
#define SONIC_CRBA1		0x10
#define SONIC_RBWC0		0x11
#define SONIC_RBWC1		0x12
#define SONIC_TTDA		0x20
#define SONIC_MDT		0x2f

#define SONIC_TRBA0		0x19
#define SONIC_TRBA1		0x1a
#define SONIC_TBWC0		0x1b
#define SONIC_TBWC1		0x1c
#define SONIC_LLFA		0x1f

#define SONIC_ADDR0		0x1d
#define SONIC_ADDR1		0x1e

/*
 * Error counters
 */
#define SONIC_CRCT              0x2c
#define SONIC_FAET              0x2d
#define SONIC_MPT               0x2e


/*
 * SONIC command bits
 */

#define SONIC_CR_LCAM           0x0200
#define SONIC_CR_RRRA           0x0100
#define SONIC_CR_RST            0x0080
#define SONIC_CR_ST             0x0020
#define SONIC_CR_STP            0x0010
#define SONIC_CR_RXEN           0x0008
#define SONIC_CR_RXDIS          0x0004
#define SONIC_CR_TXP            0x0002
#define SONIC_CR_HTX            0x0001

/*
 * SONIC data configuration bits
 */

#define SONIC_DCR_EXBUS         0x8000
#define SONIC_DCR_LBR           0x2000
#define SONIC_DCR_PO1           0x1000
#define SONIC_DCR_PO0           0x0800
#define SONIC_DCR_SBUS          0x0400
#define SONIC_DCR_USR1          0x0200
#define SONIC_DCR_USR0          0x0100
#define SONIC_DCR_WC1           0x0080
#define SONIC_DCR_WC0           0x0040
#define SONIC_DCR_DW            0x0020
#define SONIC_DCR_BMS           0x0010
#define SONIC_DCR_RFT1          0x0008
#define SONIC_DCR_RFT0          0x0004
#define SONIC_DCR_TFT1          0x0002
#define SONIC_DCR_TFT0          0x0001

/*
 * Constants for the SONIC receive control register.
 */

#define SONIC_RCR_ERR           0x8000
#define SONIC_RCR_RNT           0x4000
#define SONIC_RCR_BRD           0x2000
#define SONIC_RCR_PRO           0x1000
#define SONIC_RCR_AMC           0x0800
#define SONIC_RCR_LB1           0x0400
#define SONIC_RCR_LB0           0x0200

#define SONIC_RCR_MC            0x0100
#define SONIC_RCR_BC            0x0080
#define SONIC_RCR_LPKT          0x0040
#define SONIC_RCR_CRS           0x0020
#define SONIC_RCR_COL           0x0010
#define SONIC_RCR_CRCR          0x0008
#define SONIC_RCR_FAER          0x0004
#define SONIC_RCR_LBK           0x0002
#define SONIC_RCR_PRX           0x0001

#define SONIC_RCR_LB_OFF        0
#define SONIC_RCR_LB_MAC        SONIC_RCR_LB0
#define SONIC_RCR_LB_ENDEC      SONIC_RCR_LB1
#define SONIC_RCR_LB_TRANS      (SONIC_RCR_LB0 | SONIC_RCR_LB1)

/* default RCR setup */

#define SONIC_RCR_DEFAULT       (SONIC_RCR_BRD)


/*
 * SONIC Transmit Control register bits
 */

#define SONIC_TCR_PINTR         0x8000
#define SONIC_TCR_POWC          0x4000
#define SONIC_TCR_CRCI          0x2000
#define SONIC_TCR_EXDIS         0x1000
#define SONIC_TCR_EXD           0x0400
#define SONIC_TCR_DEF           0x0200
#define SONIC_TCR_NCRS          0x0100
#define SONIC_TCR_CRLS          0x0080
#define SONIC_TCR_EXC           0x0040
#define SONIC_TCR_PMB           0x0008
#define SONIC_TCR_FU            0x0004
#define SONIC_TCR_BCM           0x0002
#define SONIC_TCR_PTX           0x0001

#define SONIC_TCR_DEFAULT       0x0000

/* 
 * Constants for the SONIC_INTERRUPT_MASK and
 * SONIC_INTERRUPT_STATUS registers.
 */

#define SONIC_INT_BR		0x4000
#define SONIC_INT_HBL		0x2000
#define SONIC_INT_LCD           0x1000
#define SONIC_INT_PINT          0x0800
#define SONIC_INT_PKTRX         0x0400
#define SONIC_INT_TXDN          0x0200
#define SONIC_INT_TXER          0x0100
#define SONIC_INT_TC            0x0080
#define SONIC_INT_RDE           0x0040
#define SONIC_INT_RBE           0x0020
#define SONIC_INT_RBAE		0x0010
#define SONIC_INT_CRC		0x0008
#define SONIC_INT_FAE		0x0004
#define SONIC_INT_MP		0x0002
#define SONIC_INT_RFO		0x0001


/*
 * The interrupts we allow.
 */

#define SONIC_IMR_DEFAULT	(SONIC_INT_BR | \
				SONIC_INT_LCD | \
                                SONIC_INT_PINT | \
                                SONIC_INT_PKTRX | \
                                SONIC_INT_TXDN | \
                                SONIC_INT_TXER | \
                                SONIC_INT_RDE | \
                                SONIC_INT_RBE | \
                                SONIC_INT_RBAE | \
                                SONIC_INT_CRC | \
                                SONIC_INT_FAE | \
                                SONIC_INT_MP)


#define	SONIC_END_OF_LINKS	0x0001


/*
 * structure definitions
 */

typedef struct {
  u32 rx_bufadr_l;	/* receive buffer ptr */
  u32 rx_bufadr_h;

  u32 rx_bufsize_l;	/* no. of words in the receive buffer */
  u32 rx_bufsize_h;
} sonic_rr_t;

/*
 * Sonic receive descriptor. Receive descriptors are
 * kept in a linked list of these structures.
 */

typedef struct {
  u16 rx_status;	/* status after reception of a packet */
  u16 pad0;
  u16 rx_pktlen;	/* length of the packet incl. CRC */
  u16 pad1;
  
  /*
   * Pointers to the location in the receive buffer area (RBA)
   * where the packet resides. A packet is always received into
   * a contiguous piece of memory.
   */
  u16 rx_pktptr_l;
  u16 pad2;
  u16 rx_pktptr_h;
  u16 pad3;

  u16 rx_seqno;	/* sequence no. */
  u16 pad4;

  u16 link;		/* link to next RDD (end if EOL bit set) */
  u16 pad5;

  /*
   * Owner of this descriptor, 0= driver, 1=sonic
   */
  
  u16 in_use;	
  u16 pad6;

  caddr_t rda_next;		/* pointer to next RD */
} sonic_rd_t;


/*
 * Describes a Transmit Descriptor
 */
typedef struct {
  u16 tx_status;	/* status after transmission of a packet */
  u16 pad0;		
  u16 tx_config;	/* transmit configuration for this packet */
  u16 pad1;		
  u16 tx_pktsize;	/* size of the packet to be transmitted */
  u16 pad2;		
  u16 tx_frag_count;	/* no. of fragments */
  u16 pad3;		

  u16 tx_frag_ptr_l;
  u16 pad4;		
  u16 tx_frag_ptr_h;
  u16 pad5;		
  u16 tx_frag_size;
  u16 pad6;		
  
  u16 link;		/* ptr to next descriptor */
  u16 pad7;		
} sonic_td_t;


/*
 * Describes an entry in the CAM Descriptor Area.
 */

typedef struct {
  u16 cam_entry_pointer;
  u16 pad;
  u16 cam_frag2;
  u16 pad2;
  u16 cam_frag1;
  u16 pad1;
  u16 cam_frag0;
  u16 pad0;
} sonic_cd_t;

#define CAM_DESCRIPTORS 16


typedef struct {
  sonic_cd_t cam_desc[CAM_DESCRIPTORS];
  u16 cam_enable;
  u16 pad;
} sonic_cda_t;


#endif /* SONIC_H */
