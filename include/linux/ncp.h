#ifndef _LINUX_NCP_H_
#define _LINUX_NCP_H_

#define NCP_OPEN	0x1111
#define NCP_CLOSE	0x5555
#define NCP_REQUEST	0x2222
#define NCP_REPLY	0x3333

struct ncp_request
{
	unsigned short	p_type			__attribute__ ((packed));
	unsigned char	seq			__attribute__ ((packed));
	unsigned char	c_low			__attribute__ ((packed));
	unsigned char	task			__attribute__ ((packed));
	unsigned char	c_high			__attribute__ ((packed));
	unsigned char	func			__attribute__ ((packed));
};

struct ncp_request_sf
{
	unsigned short	p_type			__attribute__ ((packed));
	unsigned char	seq			__attribute__ ((packed));
	unsigned char	c_low			__attribute__ ((packed));
	unsigned char	task			__attribute__ ((packed));
	unsigned char	c_high			__attribute__ ((packed));
	unsigned char	func			__attribute__ ((packed));
	unsigned short	s_len			__attribute__ ((packed));
	unsigned char	s_func			__attribute__ ((packed));
};

struct ncp_reply
{
	unsigned short	p_type			__attribute__ ((packed));
	unsigned char	seq			__attribute__ ((packed));
	unsigned char	c_low			__attribute__ ((packed));
	unsigned char	task			__attribute__ ((packed));
	unsigned char	c_high			__attribute__ ((packed));
	unsigned char	f_stat			__attribute__ ((packed));
	unsigned char	c_stat			__attribute__ ((packed));
};

#define OTYPE_USER		0x0001
#define OTYPE_GROUP		0x0002
#define OTYPE_PQUEUE		0x0003
#define OTYPE_FSERVER		0x0004
#define OTYPE_JSERVER		0x0005
#define OTYPE_PSERVER		0x0007
#define OTYPE_UNKNOWN_1		0x002E
#define OTYPE_ADV_PSERVER	0x0047
#define OTYPE_AFSERVER		0x0107
#define OTYPE_UNKNOWN_2		0x0143
#define OTYPE_UNKNOWN_3		0x01F5
#define OTYPE_UNKNOWN_4		0x023F

#define LIMIT_OBJNAME	47

struct bind_obj
{
	unsigned long	id			__attribute__ ((packed));
	unsigned short	type			__attribute__ ((packed));
	char		name[LIMIT_OBJNAME+1]	__attribute__ ((packed));
};

struct	get_bind_obj
{
	unsigned short	type			__attribute__ ((packed));
	unsigned char	n_len			__attribute__ ((packed));
	char		name[0]			__attribute__ ((packed));
};

struct	scan_bind_obj
{
	unsigned long	id			__attribute__ ((packed));
	unsigned short	type			__attribute__ ((packed));
	unsigned char	n_len			__attribute__ ((packed));
	char		name[0]			__attribute__ ((packed));
};

struct	login_req
{
	unsigned char	password[8]		__attribute__ ((packed));
	unsigned short	type			__attribute__ ((packed));
	unsigned char	n_len			__attribute__ ((packed));
	char		name[0]			__attribute__ ((packed));
};

struct	ncp_time
{
	unsigned char	year			__attribute__ ((packed));
	unsigned char	month			__attribute__ ((packed));
	unsigned char	day			__attribute__ ((packed));
	unsigned char	hours			__attribute__ ((packed));
	unsigned char	mins			__attribute__ ((packed));
	unsigned char	secs			__attribute__ ((packed));
	unsigned char	c_secs			__attribute__ ((packed));
};

struct login_info
{
	unsigned long	id			__attribute__ ((packed));
	unsigned short	un1			__attribute__ ((packed));
	char		name[LIMIT_OBJNAME+1]	__attribute__ ((packed));
	struct ncp_time time			__attribute__ ((packed));
};
#endif

