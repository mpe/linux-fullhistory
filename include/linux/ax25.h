#define PF_AX25		AF_AX25
#define AX25_MTU	256
#define AX25_MAX_DIGIS	8

typedef struct
{
	char ax25_call[7];	/* 6 call + SSID (shifted ascii!) */
}
ax25_address;

struct sockaddr_ax25
{
	short sax25_family;
	ax25_address sax25_call;
	int sax25_ndigis;
	/* Digipeater ax25_address sets follow */
};

#define sax25_uid	sax25_ndigis

struct full_sockaddr_ax25
{
	struct sockaddr_ax25 fsa_ax25;
	ax25_address fsa_digipeater[AX25_MAX_DIGIS];
};

#define AX25_WINDOW	1
#define AX25_T1		2
#define AX25_N2		3
#define AX25_T3		4
#define AX25_T2		5

#define SIOCAX25GETUID		(SIOCPROTOPRIVATE)
#define SIOCAX25ADDUID		(SIOCPROTOPRIVATE+1)
#define SIOCAX25DELUID		(SIOCPROTOPRIVATE+2)
#define SIOCAX25NOUID		(SIOCPROTOPRIVATE+3)

#define AX25_NOUID_DEFAULT	0
#define AX25_NOUID_BLOCK	1
