
#include <stdio.h>
#include <string.h>

#include "bootp.h"
#include "ethlance.h"


struct {
	volatile unsigned short	*memaddr;
	volatile unsigned short	*ioaddr;
} lance_addr_list[] = {
	{ (void *)0xfe010000, (void *)0xfe00fff0 },	/* RieblCard VME in TT */
	{ (void *)0xfec10000, (void *)0xfec0fff0 },	/* RieblCard VME in MegaSTE
												   (highest byte stripped) */
	{ (void *)0xfee00000, (void *)0xfeff7000 },	/* RieblCard in ST
												   (highest byte stripped) */
	{ (void *)0xfecf0000, (void *)0xfecffff0 },	/* PAMCard VME in TT and MSTE
												   (highest byte stripped) */
};

#define	N_LANCE_ADDR	(sizeof(lance_addr_list)/sizeof(*lance_addr_list))

#define TX_RING_SIZE			1
#define TX_RING_LEN_BITS		0

#define RX_RING_SIZE			16
#define RX_RING_LEN_BITS		(4 << 5)

#define	offsetof(type,elt)	((unsigned long)(&(((type *)0)->elt)))

/* The LANCE Rx and Tx ring descriptors. */
struct lance_rx_head {
	unsigned short			base;		/* Low word of base addr */
	volatile unsigned char	flag;
	unsigned char			base_hi;	/* High word of base addr (unused) */
	short					buf_length;	/* This length is 2s complement! */
	short					msg_length;	/* This length is "normal". */
};

struct lance_tx_head {
	unsigned short			base;		/* Low word of base addr */
	volatile unsigned char	flag;
	unsigned char			base_hi;	/* High word of base addr (unused) */
	short					length;		/* Length is 2s complement! */
	volatile short			misc;
};

struct ringdesc {
	unsigned short	adr_lo;		/* Low 16 bits of address */
	unsigned char	len;		/* Length bits */
	unsigned char	adr_hi;		/* High 8 bits of address (unused) */
};

struct lance_packet {
	volatile unsigned char	data[PKTLEN];
};

/* The LANCE initialization block, described in databook. */
struct lance_init_block {
	unsigned short	mode;		/* Pre-set mode */
	unsigned char	hwaddr[6];	/* Physical ethernet address */
	unsigned		filter[2];	/* Multicast filter (unused). */
	/* Receive and transmit ring base, along with length bits. */
	struct ringdesc	rx_ring;
	struct ringdesc	tx_ring;
};

/* The whole layout of the Lance shared memory */
struct lance_memory {
	struct lance_init_block	init;
	struct lance_tx_head	tx_head[TX_RING_SIZE];
	struct lance_rx_head	rx_head[RX_RING_SIZE];
	struct lance_packet		tx_packet[TX_RING_SIZE];
	struct lance_packet		rx_packet[TX_RING_SIZE];
};

#define RIEBL_MAGIC			0x09051990
#define RIEBL_MAGIC_ADDR	((unsigned long *)(((char *)MEM) + 0xee8a))
#define RIEBL_HWADDR_ADDR	((unsigned char *)(((char *)MEM) + 0xee8e))
#define RIEBL_IVEC_ADDR		((unsigned short *)(((char *)MEM) + 0xfffe))

struct lance_ioreg {
/* base+0x0 */	volatile unsigned short	data;
/* base+0x2 */	volatile unsigned short	addr;
				unsigned char			_dummy1[3];
/* base+0x7 */	volatile unsigned char	ivec;
				unsigned char			_dummy2[5];
/* base+0xd */	volatile unsigned char	eeprom;
				unsigned char			_dummy3;
/* base+0xf */	volatile unsigned char	mem;
};

enum lance_type {
	OLD_RIEBL,		/* old Riebl card without battery */
	NEW_RIEBL,		/* new Riebl card with battery */
	PAM_CARD		/* PAM card with EEPROM */
} CardType;

HWADDR	dev_addr;

/* This is a default address for the old RieblCards without a battery
 * that have no ethernet address at boot time. 00:00:36:04 is the
 * prefix for Riebl cards, the 00:00 at the end is arbitrary.
 */

HWADDR OldRieblDefHwaddr = {
	0x00, 0x00, 0x36, 0x04, 0x00, 0x00
};

struct lance_ioreg	*IO;
struct lance_memory	*MEM;

#define	DREG	IO->data
#define	AREG	IO->addr
#define	REGA(a)	( AREG = (a), DREG )

int CurRx;


/* Definitions for the Lance */

/* tx_head flags */
#define	TMD1_ENP		0x01
#define TMD1_STP		0x02
#define	TMD1_DEF		0x04
#define TMD1_ONE		0x08
#define	TMD1_MORE		0x10
#define	TMD1_ERR		0x40
#define TMD1_OWN 		0x80

#define TMD1_OWN_CHIP	TMD1_OWN
#define TMD1_OWN_HOST	0

/* tx_head misc field */
#define TMD3_TDR		0x03FF
#define TMD3_RTRY		0x0400
#define TMD3_LCAR		0x0800
#define TMD3_LCOL		0x1000
#define TMD3_UFLO		0x4000
#define TMD3_BUFF3		0x8000

/* rx_head flags */
#define	RMD1_ENP		0x01
#define RMD1_STP		0x02
#define RMD1_BUFF		0x04
#define RMD1_CRC		0x08
#define RMD1_OFLO		0x10
#define RMD1_FRAM		0x20
#define	RMD1_ERR		0x40
#define RMD1_OWN 		0x80

#define RMD1_OWN_CHIP	RMD1_OWN
#define RMD1_OWN_HOST	0

/* register names */
#define CSR0	0
#define CSR1	1
#define CSR2	2
#define CSR3	3

/* CSR0 */
#define CSR0_INIT	0x0001		/* initialize */
#define CSR0_STRT	0x0002		/* start */
#define CSR0_STOP	0x0004		/* stop */
#define CSR0_TDMD	0x0008		/* transmit demand */
#define CSR0_TXON	0x0010		/* transmitter on */
#define CSR0_RXON	0x0020		/* receiver on */
#define CSR0_INEA	0x0040		/* interrupt enable */
#define CSR0_INTR	0x0080		/* interrupt active */
#define CSR0_IDON	0x0100		/* initialization done */
#define CSR0_TINT	0x0200		/* transmitter interrupt */
#define CSR0_RINT	0x0400		/* receiver interrupt */
#define CSR0_MERR	0x0800		/* memory error */
#define CSR0_MISS	0x1000		/* missed frame */
#define CSR0_CERR	0x2000		/* carrier error (no heartbeat :-) */
#define CSR0_BABL	0x4000		/* babble: tx-ed too many bits */
#define CSR0_ERR	0x8000		/* error */

/* CSR3 */
#define CSR3_BCON	0x0001
#define CSR3_ACON	0x0002
#define CSR3_BSWP	0x0004


#define	HZ	200
#define	_hz_200	(*(volatile unsigned long *)0x4ba)




/***************************** Prototypes *****************************/

static int lance_probe( void );
static int addr_readable( volatile void *regp, int wordflag );
static int lance_init( void );
static void lance_get_hwaddr( HWADDR *addr );
static int lance_snd( Packet *pkt, int len );
static int lance_rcv( Packet *pkt, int *len );

/************************* End of Prototypes **************************/



ETHIF_SWITCH LanceSwitch = {
	lance_probe, lance_init, lance_get_hwaddr, 
	lance_snd, lance_rcv
};


static int lance_probe( void )

{	int		i;
	
	for( i = 0; i < N_LANCE_ADDR; ++i ) {
		if (addr_readable( lance_addr_list[i].memaddr, 1 ) &&
			(lance_addr_list[i].memaddr[0] = 1,
			 lance_addr_list[i].memaddr[0] == 1) &&
			(lance_addr_list[i].memaddr[0] = 0,
			 lance_addr_list[i].memaddr[0] == 0) &&
			addr_readable( lance_addr_list[i].ioaddr, 1 )) {
			break;
		}
	}
	if (i == N_LANCE_ADDR) return( -1 );

	IO = (struct lance_ioreg *)lance_addr_list[i].ioaddr;
	MEM = (struct lance_memory *)lance_addr_list[i].memaddr;
	REGA( CSR0 ) = CSR0_STOP;

	return( 0 );
}


static int addr_readable( volatile void *regp, int wordflag )

{	int		ret;
	long	*vbr, save_berr;

	__asm__ __volatile__ ( "movec	%/vbr,%0" : "=r" (vbr) : );
	save_berr = vbr[2];
	
	__asm__ __volatile__
	(	"movel	%/sp,%/d1\n\t"
		"movel	#Lberr,%2@\n\t"
		"moveq	#0,%0\n\t"
		"tstl   %3\n\t"
		"bne	1f\n\t"
		"tstb	%1@\n\t"
		"bra	2f\n"
"1:		 tstw	%1@\n"
"2:		 moveq	#1,%0\n"
"Lberr:	 movel	%/d1,%/sp"
		: "=&d" (ret)
		: "a" (regp), "a" (&vbr[2]), "rm" (wordflag)
		: "d1", "memory"
	);

	vbr[2] = save_berr;
	
	return( ret );
}


static int lance_init( void )

{	int		i;
	
	/* Now test for type: If the eeprom I/O port is readable, it is a
	 * PAM card */
	if (addr_readable( &(IO->eeprom), 0 )) {
		/* Switch back to Ram */
		i = IO->mem;
		CardType = PAM_CARD;
	}
	else if (*RIEBL_MAGIC_ADDR == RIEBL_MAGIC) {
		CardType = NEW_RIEBL;
	}
	else
		CardType = OLD_RIEBL;
		
	/* Get the ethernet address */
	switch( CardType ) {
	  case OLD_RIEBL:
		/* No ethernet address! (Set some default address) */
		memcpy( dev_addr, OldRieblDefHwaddr, ETHADDRLEN );
		break;
	  case NEW_RIEBL:
		memcpy( dev_addr, RIEBL_HWADDR_ADDR, ETHADDRLEN );
		break;
	  case PAM_CARD:
		i = IO->eeprom;
		for( i = 0; i < ETHADDRLEN; ++i )
			dev_addr[i] = 
				((((unsigned short *)MEM)[i*2] & 0x0f) << 4) |
				((((unsigned short *)MEM)[i*2+1] & 0x0f));
		i = IO->mem;
		break;
	}

	MEM->init.mode = 0x0000;		/* Disable Rx and Tx. */
	for( i = 0; i < ETHADDRLEN; i++ )
		MEM->init.hwaddr[i] = dev_addr[i^1]; /* <- 16 bit swap! */
	MEM->init.filter[0] = 0x00000000;
	MEM->init.filter[1] = 0x00000000;
	MEM->init.rx_ring.adr_lo = offsetof( struct lance_memory, rx_head );
	MEM->init.rx_ring.adr_hi = 0;
	MEM->init.rx_ring.len    = RX_RING_LEN_BITS;
	MEM->init.tx_ring.adr_lo = offsetof( struct lance_memory, tx_head );
	MEM->init.tx_ring.adr_hi = 0;
	MEM->init.tx_ring.len    = TX_RING_LEN_BITS;
	
	REGA( CSR3 ) = CSR3_BSWP | (CardType == PAM_CARD ? CSR3_ACON : 0); 
	REGA( CSR2 ) = 0;
	REGA( CSR1 ) = 0;
	REGA( CSR0 ) = CSR0_INIT | CSR0_STRT;

	i = 1000000;
	while( i-- > 0 )
		if (DREG & CSR0_IDON)
			break;
	if (i < 0 || (DREG & CSR0_ERR)) {
		DREG = CSR0_STOP;
		return( -1 );
	}
	DREG = CSR0_IDON;

	for (i = 0; i < TX_RING_SIZE; i++) {
		MEM->tx_head[i].base = offsetof( struct lance_memory, tx_packet[i] );
		MEM->tx_head[i].flag = TMD1_OWN_HOST;
 		MEM->tx_head[i].base_hi = 0;
		MEM->tx_head[i].length = 0;
		MEM->tx_head[i].misc = 0;
	}

	for (i = 0; i < RX_RING_SIZE; i++) {
		MEM->rx_head[i].base = offsetof( struct lance_memory, rx_packet[i] );
		MEM->rx_head[i].flag = TMD1_OWN_CHIP;
		MEM->rx_head[i].base_hi = 0;
		MEM->rx_head[i].buf_length = -PKTLEN;
		MEM->rx_head[i].msg_length = 0;
	}
	CurRx = 0;
	
	return( 0 );
}


static void lance_get_hwaddr( HWADDR *addr )

{
	memcpy( addr, dev_addr, ETHADDRLEN );
}


static int lance_snd( Packet *pkt, int len )

{	unsigned long timeout;
	
	/* The old LANCE chips doesn't automatically pad buffers to min. size. */
	len = (len < 60) ? 60 : len;
	/* PAM-Card has a bug: Can only send packets with even number of bytes! */
	if (CardType == PAM_CARD && (len & 1))
		++len;

	MEM->tx_head[0].length = -len;
	MEM->tx_head[0].misc = 0;
	memcpy( (void *)&MEM->tx_packet[0].data, pkt, len );
	MEM->tx_head[0].base = offsetof(struct lance_memory, tx_packet[0]);
	MEM->tx_head[0].base_hi = 0;
	MEM->tx_head[0].flag = TMD1_OWN_CHIP | TMD1_ENP | TMD1_STP;

	/* Trigger an immediate send poll. */
	REGA( CSR0 ) = CSR0_TDMD;

	/* Wait for packet being sent */
	timeout = _hz_200 + 3*HZ;
	while( (MEM->tx_head[0].flag & TMD1_OWN_CHIP) &&
		   !MEM->tx_head[0].misc &&
		   _hz_200 < timeout )
		;

	if ((MEM->tx_head[0].flag & TMD1_OWN) == TMD1_OWN_HOST &&
		!(MEM->tx_head[0].misc & TMD1_ERR))
		/* sent ok */
		return( 0 );

	/* failure */
	if (_hz_200 >= timeout)
		return( ETIMEO );
	if (MEM->tx_head[0].misc & TMD3_UFLO) {
		/* On FIFO errors, must re-turn on TX! */
		DREG = CSR0_STRT;
	}

	return( ESEND );
}


static int lance_rcv( Packet *pkt, int *len )

{	unsigned long	timeout;
	int				stat;
	
	/* Wait for a packet */
	timeout = _hz_200 + 4*HZ;
	while( (MEM->rx_head[CurRx].flag & TMD1_OWN_CHIP) &&
		   _hz_200 < timeout )
		;
	/* Not ours -> was a timeout */
	if (((stat = MEM->rx_head[CurRx].flag) & TMD1_OWN) == TMD1_OWN_CHIP)
		return( ETIMEO );

	/* Check for errors */
	if (stat != (RMD1_ENP|RMD1_STP)) {
		MEM->rx_head[CurRx].flag &= (RMD1_ENP|RMD1_STP);
		if (stat & RMD1_FRAM) return( EFRAM );
		if (stat & RMD1_OFLO) return( EOVERFL );
		if (stat & RMD1_CRC)  return( ECRC );
		return( ERCV );
	}

	/* Get the packet */
	*len = MEM->rx_head[CurRx].msg_length & 0xfff;
	memcpy( pkt, (void *)&MEM->rx_packet[CurRx].data, *len );

	/* Give the buffer back to the chip */
	MEM->rx_head[CurRx].buf_length = -PKTLEN;
	MEM->rx_head[CurRx].flag |= RMD1_OWN_CHIP;
	CurRx = (CurRx + 1) % RX_RING_SIZE;

	return( 0 );
}


