/*
 * These are the public elements of the Linux kernel X.25 implementation.
 */

#ifndef	X25_KERNEL_H
#define	X25_KERNEL_H

#define PF_X25			AF_X25

#define	SIOCX25GSUBSCRIP	(SIOCPROTOPRIVATE + 0)
#define	SIOCX25SSUBSCRIP	(SIOCPROTOPRIVATE + 1)
#define	SIOCX25GFACILITIES	(SIOCPROTOPRIVATE + 2)
#define	SIOCX25SFACILITIES	(SIOCPROTOPRIVATE + 3)
#define	SIOCX25GCALLUSERDATA	(SIOCPROTOPRIVATE + 4)
#define	SIOCX25SCALLUSERDATA	(SIOCPROTOPRIVATE + 5)

/*
 *	Values for {get,set}sockopt.
 */
#define	X25_QBITINCL		1

/*
 *	X.25 Packet Size values.
 */
#define	X25_PS16		4
#define	X25_PS32		5
#define	X25_PS64		6
#define	X25_PS128		7
#define	X25_PS256		8
#define	X25_PS512		9
#define	X25_PS1024		10
#define	X25_PS2048		11
#define	X25_PS4096		12

/*
 *	X.25 Reset error and diagnostic codes.
 */
#define	X25_ERR_RESET		100	/* Call Reset			*/
#define	X25_ERR_ROUT		101	/* Out of Order			*/
#define	X25_ERR_RRPE		102	/* Remote Procedure Error	*/
#define	X25_ERR_RLPE		103	/* Local Procedure Error	*/
#define	X25_ERR_RNCG		104	/* Network Congestion		*/
#define	X25_ERR_RRDO		105	/* Remote DTE Operational	*/
#define	X25_ERR_RNOP		106	/* Network Operational		*/
#define	X25_ERR_RINV		107	/* Invalid Call			*/
#define	X25_ERR_RNOO		108	/* Network Out of Order		*/

/*
 *	X.25 Clear error and diagnostic codes.
 */
#define	X25_ERR_CLEAR		110	/* Call Cleared			*/
#define	X25_ERR_CBUSY		111	/* Number Busy			*/
#define	X25_ERR_COUT		112	/* Out of Order			*/
#define	X25_ERR_CRPE		113	/* Remote Procedure Error	*/
#define	X25_ERR_CRRC		114	/* Collect Call Refused		*/
#define	X25_ERR_CINV		115	/* Invalid Call			*/
#define	X25_ERR_CNFS		116	/* Invalid Fast Select		*/
#define	X25_ERR_CSA		117	/* Ship Absent			*/
#define	X25_ERR_CIFR		118	/* Invalid Facility Request	*/
#define	X25_ERR_CAB		119	/* Access Barred		*/
#define	X25_ERR_CLPE		120	/* Local Procedure Error	*/
#define	X25_ERR_CNCG		121	/* Network Congestion		*/
#define	X25_ERR_CNOB		122	/* Not Obtainable		*/
#define	X25_ERR_CROO		123	/* RPOA Out of Order		*/

/*
 * An X.121 address, it is held as ASCII text, null terminated, up to 15
 * digits and a null terminator.
 */
typedef struct {
	char x25_addr[16];
} x25_address;

/*
 *	Linux X.25 Address structure, used for bind, and connect mostly.
 */
struct sockaddr_x25 {
	sa_family_t	sx25_family;		/* Must be AF_X25 */
	x25_address	sx25_addr;		/* X.121 Address */
};

/*
 *	DTE/DCE subscription options.
 */
struct x25_subscrip_struct {
	char device[200];
	unsigned int	extended;
};

/*
 *	Routing table control structure.
 */
struct x25_route_struct {
	x25_address	address;
	unsigned int	sigdigits;
	char		device[200];
};

/*
 *	Facilities structure.
 */
struct x25_facilities {
	unsigned int	winsize_in, winsize_out;
	unsigned int	pacsize_in, pacsize_out;
	unsigned int	throughput;
	unsigned int	reverse;
};

/*
 *	Call User Data structure.
 */
struct x25_calluserdata {
	unsigned int	cudlength;
	unsigned char	cuddata[128];
};

#endif
