/*
 * linux/include/asm-arm/ecard.h
 *
 * definitions for expansion cards
 *
 * This is a new system as from Linux 1.2.3
 *
 * Changelog:
 *  11-12-1996	RMK	Further minor improvements
 *  12-09-1997	RMK	Added interrupt enable/disable for card level
 *
 * Reference: Acorns Risc OS 3 Programmers Reference Manuals.
 */

#ifndef __ASM_ECARD_H
#define __ASM_ECARD_H

/*
 * Currently understood cards (but not necessarily
 * supported):
 *                        Manufacturer  Product ID
 */
#define MANU_ACORN		0x0000
#define PROD_ACORN_SCSI			0x0002
#define PROD_ACORN_ETHER1		0x0003
#define PROD_ACORN_MFM			0x000b

#define MANU_ANT2		0x0011
#define PROD_ANT_ETHER3			0x00a4

#define MANU_ATOMWIDE		0x0017
#define PROD_ATOMWIDE_3PSERIAL		0x0090

#define MANU_OAK		0x0021
#define PROD_OAK_SCSI			0x0058

#define MANU_MORLEY		0x002b
#define PROD_MORLEY_SCSI_UNCACHED	0x0067

#define MANU_CUMANA		0x003a
#define PROD_CUMANA_SCSI_1		0x00a0
#define PROD_CUMANA_SCSI_2		0x003a

#define MANU_ICS		0x003c
#define PROD_ICS_IDE			0x00ae

#define MANU_SERPORT		0x003f
#define PROD_SERPORT_DSPORT		0x00b9

#define MANU_I3			0x0046
#define PROD_I3_ETHERLAN500		0x00d4
#define PROD_I3_ETHERLAN600		0x00ec
#define PROD_I3_ETHERLAN600A		0x011e

#define MANU_ANT		0x0053
#define PROD_ANT_ETHERB			0x00e4

#define MANU_ALSYSTEMS		0x005b
#define PROD_ALSYS_SCSIATAPI		0x0107

#define MANU_MCS		0x0063
#define PROD_MCS_CONNECT32		0x0125

#define MANU_EESOX		0x0064
#define PROD_EESOX_SCSI2		0x008c


#ifdef ECARD_C
#define CONST
#else
#define CONST const
#endif

#define MAX_ECARDS	8

/* Type of card's address space */
typedef enum {
	ECARD_IOC,
	ECARD_MEMC,
	ECARD_DEBI
} card_type_t;

/* Speed of card for ECARD_IOC address space */
typedef enum {
	ECARD_SLOW	 = 0,
	ECARD_MEDIUM	 = 1,
	ECARD_FAST	 = 2,
	ECARD_SYNC	 = 3
} card_speed_t;

/* Card ID structure */
typedef struct  {
	unsigned short manufacturer;
	unsigned short product;
} card_ids;

/* External view of card ID information */
struct in_ecld {
	unsigned short	product;
	unsigned short	manufacturer;
	unsigned char	ecld;
	unsigned char	country;
	unsigned char	fiqmask;
	unsigned char	irqmask;
	unsigned long	fiqaddr;
	unsigned long	irqaddr;
};

typedef struct expansion_card ecard_t;

/* Card handler routines */
typedef struct {
	void (*irqenable)(ecard_t *ec, int irqnr);
	void (*irqdisable)(ecard_t *ec, int irqnr);
	void (*fiqenable)(ecard_t *ec, int fiqnr);
	void (*fiqdisable)(ecard_t *ec, int fiqnr);
} expansioncard_ops_t;

typedef unsigned long *loader_t;

/*
 * This contains all the info needed on an expansion card
 */
struct expansion_card {
	/* Public data */
	volatile unsigned char *irqaddr;	/* address of IRQ register	*/
	volatile unsigned char *fiqaddr;	/* address of FIQ register	*/
	unsigned char		irqmask;	/* IRQ mask			*/
	unsigned char		fiqmask;	/* FIQ mask			*/
	unsigned char  		claimed;	/* Card claimed?		*/

	CONST unsigned char	slot_no;	/* Slot number			*/
	CONST unsigned char	dma;		/* DMA number (for request_dma)	*/
	CONST unsigned char	irq;		/* IRQ number (for request_irq)	*/
	CONST unsigned char	fiq;		/* FIQ number (for request_irq)	*/

	CONST struct in_ecld	cld;		/* Card Identification		*/
	void			*irq_data;	/* Data for use for IRQ by card	*/
	void			*fiq_data;	/* Data for use for FIQ by card	*/
	expansioncard_ops_t	*ops;		/* Enable/Disable Ops for card	*/

	/* Private internal data */
	CONST unsigned int	podaddr;	/* Base Linux address for card	*/
	CONST loader_t		loader;		/* loader program */
};

struct in_chunk_dir {
	unsigned int start_offset;
	union {
		unsigned char string[256];
		unsigned char data[1];
	} d;
};

/*
 * ecard_claim: claim an expansion card entry
 */
#define ecard_claim(ec) ((ec)->claimed = 1)

/*
 * ecard_release: release an expansion card entry
 */
#define ecard_release(ec) ((ec)->claimed = 0)

/*
 * Start finding cards from the top of the list
 */
extern void ecard_startfind (void);

/*
 * Find an expansion card with the correct cld, product and manufacturer code
 */
extern struct expansion_card *ecard_find (int cld, const card_ids *ids);
 
/*
 * Read a chunk from an expansion card
 * cd : where to put read data
 * ec : expansion card info struct
 * id : id number to find
 * num: (n+1)'th id to find.
 */
extern int ecard_readchunk (struct in_chunk_dir *cd, struct expansion_card *ec, int id, int num);

/*
 * Obtain the address of a card
 */
extern unsigned int ecard_address (struct expansion_card *ec, card_type_t card_type, card_speed_t speed);

#ifdef ECARD_C
/* Definitions internal to ecard.c - for it's use only!!
 *
 * External expansion card header as read from the card
 */
struct ex_ecld {
	unsigned char  r_ecld;
	unsigned char  r_reserved[2];
	unsigned char  r_product[2];
	unsigned char  r_manufacturer[2];
	unsigned char  r_country;
	         long  r_fiqs;
	         long  r_irqs;
#define e_ecld(x)	((x)->r_ecld)
#define e_cd(x)		((x)->r_reserved[0] & 1)
#define e_is(x)		((x)->r_reserved[0] & 2)
#define e_w(x)		(((x)->r_reserved[0] & 12)>>2)
#define e_prod(x)	((x)->r_product[0]|((x)->r_product[1]<<8))
#define e_manu(x)	((x)->r_manufacturer[0]|((x)->r_manufacturer[1]<<8))
#define e_country(x)	((x)->r_country)
#define e_fiqmask(x)	((x)->r_fiqs & 0xff)
#define e_fiqaddr(x)	((x)->r_fiqs >> 8)
#define e_irqmask(x)	((x)->r_irqs & 0xff)
#define e_irqaddr(x)	((x)->r_irqs >> 8)
};

/*
 * Chunk directory entry as read from the card
 */
struct ex_chunk_dir {
	unsigned char r_id;
	unsigned char r_len[3];
	unsigned long r_start;
	union {
		char string[256];
		char data[1];
	} d;
#define c_id(x)		((x)->r_id)
#define c_len(x)	((x)->r_len[0]|((x)->r_len[1]<<8)|((x)->r_len[2]<<16))
#define c_start(x)	((x)->r_start)
};

#endif

#endif
