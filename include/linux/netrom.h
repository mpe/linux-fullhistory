#define PF_NETROM	AF_NETROM
#define NETROM_MTU	236

#define NETROM_T1	1
#define NETROM_T2	2
#define NETROM_N2	3

#define SIOCNRADDNODE		(SIOCPROTOPRIVATE)
#define SIOCNRDELNODE		(SIOCPROTOPRIVATE+1)
#define	SIOCNRADDNEIGH		(SIOCPROTOPRIVATE+2)
#define	SIOCNRDELNEIGH		(SIOCPROTOPRIVATE+3)
#define	SIOCNRGETPARMS		(SIOCPROTOPRIVATE+4)
#define	SIOCNRSETPARMS		(SIOCPROTOPRIVATE+5)
#define	SIOCNRDECOBS		(SIOCPROTOPRIVATE+6)

struct nr_node_struct {
	ax25_address callsign;
	char mnemonic[7];
	ax25_address neighbour;
	char device[16];
	unsigned int quality;
	unsigned int obs_count;
};

struct nr_neigh_struct {
	ax25_address callsign;
	char device[16];
	unsigned int quality;
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
};
