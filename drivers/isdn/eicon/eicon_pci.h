/* $Id: eicon_pci.h,v 1.4 2000/01/23 21:21:23 armin Exp $
 *
 * ISDN low-level module for Eicon active ISDN-Cards (PCI part).
 *
 * Copyright 1998-2000 by Armin Schindler (mac@melware.de)
 * Copyright 1999,2000 Cytronics & Melware (info@melware.de)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: eicon_pci.h,v $
 * Revision 1.4  2000/01/23 21:21:23  armin
 * Added new trace capability and some updates.
 * DIVA Server BRI now supports data for ISDNLOG.
 *
 * Revision 1.3  1999/03/29 11:19:51  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.2  1999/03/02 12:37:50  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.1  1999/01/01 18:09:46  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#ifndef eicon_pci_h
#define eicon_pci_h

#ifdef __KERNEL__


#define PCI_VENDOR_EICON        0x1133
#define PCI_DIVA_PRO20          0xe001	/* Not supported */
#define PCI_DIVA20              0xe002	/* Not supported */
#define PCI_DIVA_PRO20_U        0xe003	/* Not supported */
#define PCI_DIVA20_U            0xe004	/* Not supported */
#define PCI_MAESTRA             0xe010
#define PCI_MAESTRAQ            0xe012
#define PCI_MAESTRAQ_U          0xe013
#define PCI_MAESTRAP            0xe014

#define DIVA_PRO20          1
#define DIVA20              2
#define DIVA_PRO20_U        3
#define DIVA20_U            4
#define MAESTRA             5
#define MAESTRAQ            6
#define MAESTRAQ_U          7
#define MAESTRAP            8

#define TRUE  1
#define FALSE 0

#define DIVAS_SIGNATURE 0x4447


/* MAESTRA BRI PCI */

#define M_RESET		0x10		/* offset of reset register */
#define M_DATA		0x00		/* offset of data register */
#define M_ADDR		0x04		/* offset of address register */
#define M_ADDRH		0x0c		/* offset of high address register */

#define M_DSP_CODE_LEN            0xbf7d0000
#define M_DSP_CODE                0xbf7d0004  /* max 128K DSP-Code */ 
#define M_DSP_CODE_BASE           0xbf7a0000  
#define M_MAX_DSP_CODE_SIZE       0x00050000  /* max 320K DSP-Code (Telindus) */



/* MAESTRA PRI PCI */

#define MP_SHARED_RAM_OFFSET 0x1000  /* offset of shared RAM base in the DRAM memory bar */

#define MP_IRQ_RESET     0xc18       /* offset of interrupt status register in the CONFIG memory bar */
#define MP_IRQ_RESET_VAL 0xfe        /* value to clear an interrupt */

#define MP_PROTOCOL_ADDR 0xa0011000  /* load address of protocol code */
#define MP_DSP_ADDR      0xa03c0000  /* load address of DSP code */
#define MP_MAX_PROTOCOL_CODE_SIZE  0x000a0000   /* max 640K Protocol-Code */
#define MP_DSP_CODE_BASE           0xa03a0000
#define MP_MAX_DSP_CODE_SIZE       0x00060000   /* max 384K DSP-Code */

#define MP_RESET         0x20        /* offset of RESET register in the DEVICES memory bar */

/* RESET register bits */
#define _MP_S2M_RESET    0x10        /* active lo   */
#define _MP_LED2         0x08        /* 1 = on      */
#define _MP_LED1         0x04        /* 1 = on      */
#define _MP_DSP_RESET    0x02        /* active lo   */
#define _MP_RISC_RESET   0x81        /* active hi, bit 7 for compatibility with old boards */

/* boot interface structure */
typedef struct {
	__u32 cmd	__attribute__ ((packed));
	__u32 addr	__attribute__ ((packed));
	__u32 len	__attribute__ ((packed));
	__u32 err	__attribute__ ((packed));
	__u32 live	__attribute__ ((packed));
	__u32 reserved[(0x1020>>2)-6] __attribute__ ((packed));
	__u32 signature	__attribute__ ((packed));
	__u8 data[1];    /* real interface description */
} eicon_pci_boot;


#define DL_PARA_IO_TYPE   0
#define DL_PARA_MEM_TYPE  1

typedef struct tag_dsp_download_space
{
  __u16 type;  /* see definitions above to differ union elements */
  union
  {
    struct
    {
      __u32               r3addr;
      __u16               ioADDR;
      __u16               ioADDRH;
      __u16               ioDATA;
      __u16               BadData;  /* in case of verify error */
      __u16               GoodData;
    } io;     /* for io based adapters */
    struct
    {
      __u32               r3addr;
      eicon_pci_boot	  *boot;
      __u32               BadData;  /* in case of verify error */
      __u32               GoodData;
      __u16               timeout;
    } mem;    /* for memory based adapters */
  } dat;
} t_dsp_download_space;


/* Shared memory */
typedef union {
	eicon_pci_boot boot;
} eicon_pci_shmem;

/*
 * card's description
 */
typedef struct {
	int		  ramsize;
	int   		  irq;	    /* IRQ		          */
	unsigned int      PCIram;
	unsigned int	  PCIreg;
	unsigned int	  PCIcfg;
	long int   	  serial;   /* Serial No.		  */
	int		  channels; /* No. of supported channels  */
        void*             card;
        eicon_pci_shmem*  shmem;    /* Shared-memory area         */
        unsigned char*    intack;   /* Int-Acknowledge            */
        unsigned char*    stopcpu;  /* Writing here stops CPU     */
        unsigned char*    startcpu; /* Writing here starts CPU    */
        unsigned char     type;     /* card type                  */
        unsigned char     irqprobe; /* Flag: IRQ-probing          */
        unsigned char     mvalid;   /* Flag: Memory is valid      */
        unsigned char     ivalid;   /* Flag: IRQ is valid         */
        unsigned char     master;   /* Flag: Card is Quadro 1/4   */
        void*             generic;  /* Ptr to generic card struct */
} eicon_pci_card;



extern int eicon_pci_load_pri(eicon_pci_card *card, eicon_pci_codebuf *cb);
extern int eicon_pci_load_bri(eicon_pci_card *card, eicon_pci_codebuf *cb);
extern void eicon_pci_release(eicon_pci_card *card);
extern void eicon_pci_printpar(eicon_pci_card *card);
extern int eicon_pci_find_card(char *ID);

#endif  /* __KERNEL__ */

#endif	/* eicon_pci_h */
