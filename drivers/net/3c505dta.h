/* This header file defines some data structures used by the 3c505 driver */

/* Data units */
typedef unsigned char         byte;
typedef unsigned short int    word;
typedef unsigned long int     dword;


/* Data structures */
struct Memconf {
	word	cmd_q,
		rcv_q,
		mcast,
		frame,
		rcv_b,
		progs;
};

struct Rcv_pkt {
	word	buf_ofs,
		buf_seg,
		buf_len,
		timeout;
};

struct Xmit_pkt {
	word	buf_ofs,
		buf_seg,
		pkt_len;
};

struct Rcv_resp {
	word	buf_ofs,
		buf_seg,
		buf_len,
		pkt_len,
		timeout,
		status;
	dword	timetag;
};

struct Xmit_resp {
	word	buf_ofs,
		buf_seg,
		c_stat,
		status;
};


struct Netstat {
	dword	tot_recv,
		tot_xmit;
	word	err_CRC,
		err_align,
		err_res,
		err_ovrrun;
};


struct Selftest {
	word	error;
	union {
		word ROM_cksum;
		struct {
			word ofs, seg;
		} RAM;
		word i82586;
	} failure;
};

struct Info {
	byte	minor_vers,
		major_vers;
	word	ROM_cksum,
		RAM_sz,
		free_ofs,
		free_seg;
};

struct Memdump {
       word size,
            off,
            seg;
};

/*
Primary Command Block. The most important data structure. All communication
between the host and the adapter is done with these. (Except for the ethernet
data, which has different packaging.)
*/
typedef struct {
	byte	command;
	byte	length;
	union	{
		struct Memconf		memconf;
		word			configure;
		struct Rcv_pkt		rcv_pkt;
		struct Xmit_pkt		xmit_pkt;
		byte			multicast[10][6];
		byte			eth_addr[6];
		byte			failed;
		struct Rcv_resp		rcv_resp;
		struct Xmit_resp	xmit_resp;
		struct Netstat		netstat;
		struct Selftest		selftest;
		struct Info		info;
		struct Memdump    	memdump;
		byte			raw[62];
	} data;
} pcb_struct;

/* These defines for 'configure' */
#define RECV_STATION	0x00
#define RECV_BROAD	0x01
#define RECV_MULTI	0x02
#define RECV_ALL	0x04
#define NO_LOOPBACK	0x00
#define INT_LOOPBACK	0x08
#define EXT_LOOPBACK	0x10
