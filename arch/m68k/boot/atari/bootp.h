#ifndef _bootp_h
#define _bootp_h

/* --------------------------------------------------------------------- */
/*							 Ethernet Definitions						 */

#define	PKTLEN			1544
typedef unsigned char	Packet[PKTLEN];

#define	ETHADDRLEN		6
typedef unsigned char	HWADDR[ETHADDRLEN];

typedef struct {
	int		(*probe)( void );
	int		(*init)( void );
	void	(*get_hwaddr)( HWADDR *addr );
	int		(*snd)( Packet *pkt, int len );
	int		(*rcv)( Packet *pkt, int *len );
} ETHIF_SWITCH;


/* error codes */
#define	ETIMEO	-1		/* Timeout */
#define	ESEND	-2		/* General send error (carrier, abort, ...) */
#define	ERCV	-3		/* General receive error */
#define	EFRAM	-4		/* Framing error */
#define	EOVERFL	-5		/* Overflow (too long packet) */
#define	ECRC	-6		/* CRC error */


typedef unsigned long IPADDR;


/***************************** Prototypes *****************************/

int get_remote_kernel( const char *kname );
int kread( int fd, void *buf, unsigned cnt );
int klseek( int fd, int where, int whence );
int kclose( int fd );

/************************* End of Prototypes **************************/

#endif  /* _bootp_h */

