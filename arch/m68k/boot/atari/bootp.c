
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bootp.h"


/* --------------------------------------------------------------------- */
/*						  Protocol Header Structures					 */

struct etherhdr {
	HWADDR			dst_addr;
	HWADDR			src_addr;
	unsigned short	type;
};

struct arphdr {
	unsigned short	hrd;		/* format of hardware address	*/
	unsigned short	pro;		/* format of protocol address	*/
	unsigned char	hln;		/* length of hardware address	*/
	unsigned char	pln;		/* length of protocol address	*/
	unsigned short	op;			/* ARP opcode (command)			*/
	unsigned char	addr[0];	/* addresses (var len)			*/
};
	
struct iphdr {
	unsigned char	version : 4;
	unsigned char	ihl : 4;
	unsigned char	tos;
	unsigned short	tot_len;
	unsigned short	id;
	unsigned short	frag_off;
	unsigned char	ttl;
	unsigned char	protocol;
	unsigned short	chksum;
	IPADDR			src_addr;
	IPADDR			dst_addr;
};

struct udphdr {
	unsigned short	src_port;
	unsigned short	dst_port;
	unsigned short	len;
	unsigned short	chksum;
};

struct bootp {
	unsigned char	op;			/* packet opcode type */
	unsigned char	htype;		/* hardware addr type */
	unsigned char	hlen;		/* hardware addr length */
	unsigned char	hops;		/* gateway hops */
	unsigned long	xid;		/* transaction ID */
	unsigned short	secs;		/* seconds since boot began */
	unsigned short	unused;
	IPADDR			ciaddr;		/* client IP address */
	IPADDR			yiaddr;		/* 'your' IP address */
	IPADDR			siaddr;		/* server IP address */
	IPADDR			giaddr; 	/* gateway IP address */
	unsigned char	chaddr[16];	/* client hardware address */
	unsigned char	sname[64];	/* server host name */
	unsigned char	file[128];	/* boot file name */
	unsigned char	vend[64];	/* vendor-specific area */
};

struct tftp_req {
	unsigned short	opcode;
	char			name[512];
};

struct tftp_data {
	unsigned short	opcode;
	unsigned short	nr;
	unsigned char	data[512];
};

struct tftp_ack {
	unsigned short	opcode;
	unsigned short	nr;
};

struct tftp_error {
	unsigned short	opcode;
	unsigned short	errcode;
	char			str[512];
};


typedef struct {
	struct etherhdr		ether;
	struct arphdr		arp;
} ARP;

typedef struct {
	struct etherhdr		ether;
	struct iphdr		ip;
	struct udphdr		udp;
} UDP;

#define	UDP_BOOTPS	67
#define	UDP_BOOTPC	68
#define	UDP_TFTP	69

typedef struct {
	struct etherhdr		ether;
	struct iphdr		ip;
	struct udphdr		udp;
	struct bootp		bootp;
} BOOTP;

#define	BOOTREQUEST		1
#define	BOOTREPLY		2
#define	BOOTP_RETRYS	5

typedef struct {
	struct etherhdr		ether;
	struct iphdr		ip;
	struct udphdr		udp;
	union tftp {
		unsigned short		opcode;
		struct tftp_req		req;
		struct tftp_data	data;
		struct tftp_ack		ack;
		struct tftp_error	error;
	} tftp;
} TFTP;
	
#define	TFTP_RRQ	1
#define	TFTP_WRQ	2
#define	TFTP_DATA	3
#define	TFTP_ACK	4
#define	TFTP_ERROR	5


/* --------------------------------------------------------------------- */
/*								  Addresses								 */

static HWADDR	MyHwaddr;
static HWADDR	ServerHwaddr;
static IPADDR	MyIPaddr;
static IPADDR	ServerIPaddr;

static IPADDR	IP_Unknown_Addr =   0x00000000;
static IPADDR	IP_Broadcast_Addr = 0xffffffff;
static HWADDR 	Eth_Broadcast_Addr = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };


#define	HZ	200
#define	_hz_200	(*(volatile unsigned long *)0x4ba)


/* --------------------------------------------------------------------- */
/*								Error Strings							 */

static char *ErrStr[] = {
	"timeout",
	"general Ethernet transmit error",
	"general Ethernet receive error",
	"Ethernet framing error",
	"Ethernet overflow error",
	"Ethernet CRC error"
};


/* --------------------------------------------------------------------- */
/*						 Kfile Emulation Definitions					 */

#define	KFILE_CHUNK_BITS	16  /* chunk is 64 KB */
#define	KFILE_CHUNK_SIZE	(1 << KFILE_CHUNK_BITS)
#define	KFILE_CHUNK_MASK	(KFILE_CHUNK_SIZE-1)
#define	KFILE_N_CHUNKS		(2*1024*1024/KFILE_CHUNK_SIZE)

char *KFile[KFILE_N_CHUNKS];
int  KFileSize = 0;
int	 KFpos = 0;




/***************************** Prototypes *****************************/

static void free_kfile( void );
static int bootp( char *image_name );
static int tftp( char *image_name );
static int udp_send( UDP *pkt, int len, int fromport, int toport );
static unsigned short ip_checksum( struct iphdr *buf );
static int udp_rcv( UDP *pkt, int *len, int fromport, int atport );
static void print_ip( IPADDR addr );
static void print_hw( HWADDR addr );
static int check_ethif( void );
static int eth_send( Packet *pkt, int len );
static int eth_rcv( Packet *pkt, int *len );

/************************* End of Prototypes **************************/




/* --------------------------------------------------------------------- */
/*						   Interface to bootstrap.c						 */

/* get_remote_kernel():
 * Perform all necessary steps to get the kernel image
 * from the boot server. If successfull (retval == 0), subsequent calls to
 * kread() can access the data.
 */

int get_remote_kernel( const char *kname /* optional */ )

{	char	image_name[256];
	
	/* Check if a Ethernet interface is present and determine the Ethernet
	 * address */
	if (check_ethif() < 0) {
		printf( "No Ethernet interface found -- no remote boot possible.\n" );
		return( -1 );
	}
	
	/* Do a BOOTP request to find out our IP address and the kernel image's
	 * name; we also learn the IP and Ethernet address of our server */
	if (kname)
		strcpy( image_name, kname );
	else
		*image_name = 0;
	if (bootp( image_name ) < 0)
		return( -1 );
	
	/* Now start a TFTP connection to receive the kernel image */
	if (tftp( image_name ) < 0)
		return( -1 );

	return( 0 );
}


/* kread(), klseek(), kclose():
 * Functions for accessing the received kernel image like with read(),
 * lseek(), close().
 */

int kread( int fd, void *buf, unsigned cnt )

{	unsigned done = 0;
	
	if (!KFileSize)
		return( read( fd, buf, cnt ) );

	if (KFpos + cnt > KFileSize)
		cnt = KFileSize - KFpos;
	
	while( cnt > 0 ) {
		unsigned chunk = KFpos >> KFILE_CHUNK_BITS;
		unsigned endchunk = (chunk+1) << KFILE_CHUNK_BITS;
		unsigned n = cnt;

		if (KFpos + n > endchunk)
			n = endchunk - KFpos;
		memcpy( buf, KFile[chunk] + (KFpos & KFILE_CHUNK_MASK), n );
		cnt -= n;
		buf += n;
		done += n;
		KFpos += n;
	}

	return( done );
}


int klseek( int fd, int where, int whence )

{
	if (!KFileSize)
		return( lseek( fd, where, whence ) );

	switch( whence ) {
	  case SEEK_SET:
		KFpos = where;
		break;
	  case SEEK_CUR:
		KFpos += where;
		break;
	  case SEEK_END:
		KFpos = KFileSize + where;
		break;
	  default:
		return( -1 );
	}
	if (KFpos < 0) {
		KFpos = 0;
		return( -1 );
	}
	else if (KFpos > KFileSize) {
		KFpos = KFileSize;
		return( -1 );
	}

	return( KFpos );
}


int kclose( int fd )

{
	if (!KFileSize)
		return( close( fd ) );

	free_kfile();
	return( 0 );
}


static void free_kfile( void )

{	int		i;

	for( i = 0; i < KFILE_N_CHUNKS; ++i )
		if (KFile[i]) free( KFile[i] );
}



/* --------------------------------------------------------------------- */
/*							   BOOTP Procedure							 */


static int bootp( char *image_name )

{	BOOTP	req;
	Packet	_reply;
	BOOTP	*reply = (BOOTP *)_reply;
	static unsigned char mincookie[] = { 99, 130, 83, 99, 255 };
	unsigned long 	starttime, rancopy;
	int				err, len, retry;
	
	memset( (char *)&req, 0, sizeof(req) );
	/* Now fill in the packet... */
	req.bootp.op = BOOTREQUEST;
	req.bootp.htype = 1; /* 10Mb/s Ethernet */
	req.bootp.hlen = 6;
	memcpy( req.bootp.chaddr, &MyHwaddr, ETHADDRLEN );

	/* Put in the minimal RFC1497 Magic cookie */
	memcpy( req.bootp.vend, mincookie, sizeof(mincookie) );
	/* Put the user precified bootfile name in place */
	memcpy( req.bootp.file, image_name, strlen(image_name)+1);

	starttime = _hz_200;
	for( retry = 0; retry < BOOTP_RETRYS; ++retry ) {

		/* Initialize server addresses and own IP to defaults */
		ServerIPaddr = IP_Broadcast_Addr;  /* 255.255.255.255 */
		MyIPaddr     = IP_Unknown_Addr;    /* 0.0.0.0 */
		memcpy( ServerHwaddr, Eth_Broadcast_Addr, ETHADDRLEN );

		if (retry)
			sleep( 3 );
		
		req.bootp.xid = rancopy = _hz_200;
		req.bootp.secs = (_hz_200 - starttime) / HZ;

		if ((err = udp_send( (UDP *)&req, sizeof(req.bootp),
							 UDP_BOOTPC, UDP_BOOTPS )) < 0) {
			printf( "bootp send: %s\n", ErrStr[-err-1] );
			continue;
		}
		
		if ((err = udp_rcv( (UDP *)reply, &len,
							UDP_BOOTPS, UDP_BOOTPC )) < 0) {
			printf( "bootp rcv: %s\n", ErrStr[-err-1] );
			continue;
		}
		if (len < sizeof(struct bootp)) {
			printf( "received short BOOTP packet (%d bytes)\n", len );
			continue;
		}

		if (reply->bootp.xid == rancopy)
			/* Ok, got the answer */
			break;
		printf( "bootp: xid mismatch\n" );
	}
	if (retry >= BOOTP_RETRYS) {
		printf( "No response from a bootp server\n" );
		return( -1 );
	}
	
	ServerIPaddr = reply->bootp.siaddr;
	memcpy( ServerHwaddr, reply->ether.src_addr, ETHADDRLEN );
	printf( "\nBoot server is " );
	if (strlen(reply->bootp.sname) > 0)
		printf( "%s, IP ", reply->bootp.sname );
	print_ip( ServerIPaddr );
	printf( ", HW address " );
	print_hw( ServerHwaddr );
	printf( "\n" );

	MyIPaddr = reply->bootp.yiaddr;
	printf( "My IP address is " );
	print_ip( MyIPaddr );
	printf( "\n" );

	strcpy( image_name, reply->bootp.file );
	return( 0 );
}


/* --------------------------------------------------------------------- */
/*								TFTP Procedure							 */


static int tftp( char *image_name )

{	TFTP	spkt;
	Packet	_rpkt;
	TFTP	*rpkt = (TFTP *)&_rpkt;
	unsigned short	mytid, rtid = 0;
	int				blk, retries, i, wpos, err, len, datalen;
	static char		rotchar[4] = { '|', '/', '-', '\\' };
	
	retries = 5;
	/* Construct and send a read request */
  repeat_req:
	spkt.tftp.req.opcode = TFTP_RRQ;
	strcpy( spkt.tftp.req.name, image_name );
	strcpy( spkt.tftp.req.name + strlen(spkt.tftp.req.name) + 1, "octet" );
	mytid = _hz_200 & 0xffff;
	
	if ((err = udp_send( (UDP *)&spkt, sizeof(spkt.tftp.req.opcode) +
									   strlen(image_name) + 1 +
									   strlen( "octect" ) +1,
						 mytid, UDP_TFTP )) < 0) {
		printf( "TFTP RREQ: %s\n", ErrStr[-err-1] );
		if (--retries > 0)
			goto repeat_req;
		return( -1 );
	}

	retries = 5;
	for( i = 0; i < KFILE_N_CHUNKS; ++i )
		KFile[i] = NULL;
	wpos = 0;
	printf( "Receiving kernel image %s:\n", image_name );

	for( blk = 1; ; ++blk ) {

	  repeat_data:
		if ((err = udp_rcv( (UDP *)rpkt, &len, rtid, mytid )) < 0) {
			printf( "TFTP rcv: %s\n", ErrStr[-err-1] );
			if (--retries > 0)
				goto repeat_data;
			goto err;
		}
		if (rtid == 0)
			/* Store the remote port at the first packet received */
			rtid = rpkt->udp.src_port;

		if (rpkt->tftp.opcode == TFTP_ERROR) {
			if (strlen(rpkt->tftp.error.str) > 0)
				printf( "TFTP error: %s\n", rpkt->tftp.error.str );
			else
				printf( "TFTP error #%d (no description)\n",
						rpkt->tftp.error.errcode );
			goto err;
		}
		else if (rpkt->tftp.opcode != TFTP_DATA) {
			printf( "Bad TFTP packet type: %d\n", rpkt->tftp.opcode );
			if (--retries > 0)
				goto repeat_data;
			goto err;
		}

		if (rpkt->tftp.data.nr != blk) {
			/* doubled data packet; ignore it */
			goto repeat_data;
		}
		datalen = len - sizeof(rpkt->tftp.data.opcode) -
			            sizeof(rpkt->tftp.data.nr);
		
		/* store data */
		if (datalen > 0) {
			int chunk = wpos >> KFILE_CHUNK_BITS;
			if (chunk >= KFILE_N_CHUNKS) {
				printf( "TFTP: file too large! Aborting.\n" );
			  out_of_mem:
				spkt.tftp.error.opcode = TFTP_ERROR;
				spkt.tftp.error.errcode = 3;
				strcpy( spkt.tftp.error.str, "Out of memory" );
				udp_send( (UDP *)&spkt, sizeof(spkt.tftp.ack), mytid, rtid );
				goto err;
			}
			if (!KFile[chunk]) {
				if (!(KFile[chunk] = malloc( KFILE_CHUNK_SIZE ))) {
					printf( "TFTP: Out of memory for kernel image\n" );
					goto out_of_mem;
				}
			}
			memcpy( KFile[chunk] + (wpos & KFILE_CHUNK_MASK),
					rpkt->tftp.data.data, datalen );
			wpos += datalen;

#define	DISPLAY_BITS 13
			if ((wpos & ((1 << DISPLAY_BITS)-1)) == 0) {
				printf( "\r %c %7d Bytes ",
						rotchar[(wpos>>DISPLAY_BITS)&3], wpos );
				fflush( stdout );
			}
		}

		/* Send ACK packet */
	  repeat_ack:
		spkt.tftp.ack.opcode = TFTP_ACK;
		spkt.tftp.ack.nr = blk;
		if ((err = udp_send( (UDP *)&spkt, sizeof(spkt.tftp.ack),
							 mytid, rtid )) < 0) {
			printf( "TFTP ACK: %s\n", ErrStr[-err-1] );
			if (--retries > 0)
				goto repeat_ack;
			goto err;
		}

		if (datalen < 512) {
			/* This was the last packet */
			printf( "\r   %7d Bytes done\n\n", wpos );
			break;
		}

		retries = 5;
	}

	KFileSize = wpos;
	return( 0 );

  err:
	free_kfile();
	return( -1 );
}



/* --------------------------------------------------------------------- */
/*				  UDP/IP Protocol Quick Hack Implementation				 */


static int udp_send( UDP *pkt, int len, int fromport, int toport )

{
	/* UDP layer */
	pkt->udp.src_port = fromport;
	pkt->udp.dst_port = toport;
	pkt->udp.len      = (len += sizeof(struct udphdr));
	pkt->udp.chksum   = 0; /* Too lazy to calculate :-) */

	/* IP layer */
	pkt->ip.version  = 4;
	pkt->ip.ihl      = 5;
	pkt->ip.tos      = 0;
	pkt->ip.tot_len  = (len += sizeof(struct iphdr));
	pkt->ip.id       = 0;
	pkt->ip.frag_off = 0;
	pkt->ip.ttl      = 255;
	pkt->ip.protocol = 17; /* UDP */
	pkt->ip.src_addr = MyIPaddr;
	pkt->ip.dst_addr = ServerIPaddr;
	pkt->ip.chksum   = 0;
	pkt->ip.chksum   = ip_checksum( &pkt->ip );

	/* Ethernet layer */
	memcpy( &pkt->ether.dst_addr, ServerHwaddr, ETHADDRLEN );
	memcpy( &pkt->ether.src_addr, MyHwaddr, ETHADDRLEN );
	pkt->ether.type     = 0x0800;
	len += sizeof(struct etherhdr);

	return( eth_send( (Packet *)pkt, len ) );
}


static unsigned short ip_checksum( struct iphdr *buf )

{	unsigned long sum = 0, wlen = 5;

	__asm__ ("subqw #1,%2\n"
			 "1:\t"
			 "movel %1@+,%/d0\n\t"
			 "addxl %/d0,%0\n\t"
			 "dbra %2,1b\n\t"
			 "movel %0,%/d0\n\t"
			 "swap %/d0\n\t"
			 "addxw %/d0,%0\n\t"
			 "clrw %/d0\n\t"
			 "addxw %/d0,%0"
			 : "=d" (sum), "=a" (buf), "=d" (wlen)
			 : "0" (sum), "1" (buf), "2" (wlen)
			 : "d0");
	return( (~sum) & 0xffff );
}


static int udp_rcv( UDP *pkt, int *len, int fromport, int atport )

{	int				err;

  repeat:
	if ((err = eth_rcv( (Packet *)pkt, len )))
		return( err );
	
	/* Ethernet layer */
	if (pkt->ether.type == 0x0806) {
		/* ARP */
		ARP *pk = (ARP *)pkt;
		unsigned char *shw, *sip, *thw, *tip;

		if (pk->arp.hrd != 1 || pk->arp.pro != 0x0800 ||
			pk->arp.op != 1 || MyIPaddr == IP_Unknown_Addr)
			/* Wrong hardware type or protocol; or reply -> ignore */
			goto repeat;
		shw = pk->arp.addr;
		sip = shw + pk->arp.hln;
		thw = sip + pk->arp.pln;
		tip = thw + pk->arp.hln;
		
		if (memcmp( tip, &MyIPaddr, pk->arp.pln ) == 0) {
			memcpy( thw, shw, pk->arp.hln );
			memcpy( tip, sip, pk->arp.pln );
			memcpy( shw, &MyHwaddr, pk->arp.hln );
			memcpy( sip, &MyIPaddr, pk->arp.pln );

			memcpy( &pk->ether.dst_addr, thw, ETHADDRLEN );
			memcpy( &pk->ether.src_addr, &MyHwaddr, ETHADDRLEN );
			eth_send( (Packet *)pk, *len );
		}
		goto repeat;
	}
	else if (pkt->ether.type != 0x0800) {
		printf( "Unknown Ethernet packet type %04x received\n",
				pkt->ether.type );
		goto repeat;
	}

	/* IP layer */
	if (MyIPaddr != IP_Unknown_Addr && pkt->ip.dst_addr != MyIPaddr) {
		printf( "Received packet for wrong IP address\n" );
		goto repeat;
	}
	if (ServerIPaddr != IP_Unknown_Addr &&
		ServerIPaddr != IP_Broadcast_Addr &&
		pkt->ip.src_addr != ServerIPaddr) {
		printf( "Received packet from wrong server\n" );
		goto repeat;
	}
	/* If IP header is longer than 5 longs, delete the options */
	if (pkt->ip.ihl > 5) {
		char *udpstart = (char *)((long *)&pkt->ip + pkt->ip.ihl);
		memmove( &pkt->udp, udpstart, *len - (udpstart-(char *)pkt) );
	}
	
	/* UDP layer */
	if (fromport != 0 && pkt->udp.src_port != fromport) {
		printf( "Received packet from wrong port %d\n", pkt->udp.src_port );
		goto repeat;
	}
	if (pkt->udp.dst_port != atport) {
		printf( "Received packet at wrong port %d\n", pkt->udp.dst_port );
		goto repeat;
	}

	*len = pkt->udp.len - sizeof(struct udphdr);
	return( 0 );
}


/* --------------------------------------------------------------------- */
/*							   Address Printing							 */


static void print_ip( IPADDR addr )

{
	printf( "%ld.%ld.%ld.%ld",
			(addr >> 24) & 0xff,
			(addr >> 16) & 0xff,
			(addr >>  8) & 0xff,
			addr & 0xff );
}


static void print_hw( HWADDR addr )

{
	printf( "%02x:%02x:%02x:%02x:%02x:%02x",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5] );
}


/* --------------------------------------------------------------------- */
/*					 Ethernet Interface Abstraction Layer				 */


#ifdef ETHLL_LANCE
#include "ethlance.h"
#endif

static ETHIF_SWITCH *PossibleInterfaces[] = {
#ifdef ETHLL_LANCE
	&LanceSwitch,
#endif
};

#define	N_PossibleInterfaces (sizeof(PossibleInterfaces)/sizeof(*PossibleInterfaces))

/* Detected interface */
static ETHIF_SWITCH *Ethif = NULL;


static int check_ethif( void )

{	int i;

	/* Check for configured interfaces */
	Ethif = NULL;
	for( i = 0; i < N_PossibleInterfaces; ++i ) {
		if (PossibleInterfaces[i]->probe() >= 0) {
			Ethif = PossibleInterfaces[i];
			break;
		}
	}
	if (!Ethif)
		return( -1 );

	if (Ethif->init() < 0) {
		printf( "Ethernet interface initialization failed\n" );
		return( -1 );
	}
	Ethif->get_hwaddr( &MyHwaddr );
	return( 0 );
}


static int eth_send( Packet *pkt, int len )

{
	return( Ethif->snd( pkt, len ));
}


static int eth_rcv( Packet *pkt, int *len )

{
	return( Ethif->rcv( pkt, len ));
}


#if 0
static void dump_packet( UDP *pkt )

{	int		i, l;
	unsigned char *p;
	
	printf( "Packet dump:\n" );

	printf( "Ethernet header:\n" );
	printf( "  dst addr: " ); print_hw( pkt->ether.dst_addr ); printf( "\n" );
	printf( "  src addr: " ); print_hw( pkt->ether.src_addr ); printf( "\n" );
	printf( "  type: %04x\n", pkt->ether.type );

	printf( "IP header:\n" );
	printf( "  version: %d\n", pkt->ip.version );
	printf( "  hdr len: %d\n", pkt->ip.ihl );
	printf( "  tos: %d\n", pkt->ip.tos );
	printf( "  tot_len: %d\n", pkt->ip.tot_len );
	printf( "  id: %d\n", pkt->ip.id );
	printf( "  frag_off: %d\n", pkt->ip.frag_off );
	printf( "  ttl: %d\n", pkt->ip.ttl );
	printf( "  prot: %d\n", pkt->ip.protocol );
	printf( "  src addr: " ); print_ip( pkt->ip.src_addr ); printf( "\n" );
	printf( "  dst addr: " ); print_ip( pkt->ip.dst_addr ); printf( "\n" );

	printf( "UDP header:\n" );
	printf( "  src port: %d\n", pkt->udp.src_port );
	printf( "  dst port: %d\n", pkt->udp.dst_port );
	printf( "  len: %d\n", pkt->udp.len );

	printf( "Data:" );
	l = pkt->udp.len - sizeof(pkt->udp);
	p = (unsigned char *)&pkt->udp + sizeof(pkt->udp);
	for( i = 0; i < l; ++i ) {
		if ((i % 32) == 0)
			printf( "\n  %04x ", i );
		printf( "%02x ", *p );
	}
	printf( "\n" );
}
#endif
