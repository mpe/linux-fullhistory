#define PF_NETROM	AF_NETROM
#define NETROM_MTU	236

#define NETROM_T1	1
#define NETROM_T2	2
#define NETROM_N2	3
#define	NETROM_HDRINCL	4
#define	NETROM_PACLEN	5

#define	NETROM_KILL	99

#define	SIOCNRGETPARMS		(SIOCPROTOPRIVATE+0)
#define	SIOCNRSETPARMS		(SIOCPROTOPRIVATE+1)
#define	SIOCNRDECOBS		(SIOCPROTOPRIVATE+2)
#define	SIOCNRRTCTL		(SIOCPROTOPRIVATE+3)
#define	SIOCNRCTLCON		(SIOCPROTOPRIVATE+4)

struct nr_route_struct {
#define	NETROM_NEIGH	0
#define	NETROM_NODE	1
	int type;
	ax25_address callsign;
	char device[16];
	unsigned int quality;
	char mnemonic[7];
	ax25_address neighbour;
	unsigned int obs_count;
};

struct nr_parms_struct {
	unsigned int quality;
	unsigned int obs_count;
	unsigned int ttl;
	unsigned int timeout;
	unsigned int ack_delay;
	unsigned int busy_delay;
	unsigned int tries;
	unsigned int window;
	unsigned int paclen;
};

struct nr_ctl_struct {
	unsigned char index;
	unsigned char id;
	unsigned int  cmd;
	unsigned long arg;
};
