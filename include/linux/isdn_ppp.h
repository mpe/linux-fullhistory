#ifndef _LINUX_ISDN_PPP_H
#define _LINUX_ISDN_PPP_H

extern int isdn_ppp_dial_slave(char *);
extern int isdn_ppp_hangup_slave(char *);

#define CALLTYPE_INCOMING 0x1
#define CALLTYPE_OUTGOING 0x2
#define CALLTYPE_CALLBACK 0x4

struct pppcallinfo
{
	int calltype;
	unsigned char local_num[64];
	unsigned char remote_num[64];
	int charge_units;
};

#define PPPIOCGCALLINFO _IOWR('t',128,struct pppcallinfo)
#define PPPIOCBUNDLE   _IOW('t',129,int)
#define PPPIOCGMPFLAGS _IOR('t',130,int)
#define PPPIOCSMPFLAGS _IOW('t',131,int)
#define PPPIOCSMPMTU   _IOW('t',132,int)
#define PPPIOCSMPMRU   _IOW('t',133,int)
#define PPPIOCGCOMPRESSORS _IOR('t',134,unsigned long)
#define PPPIOCSCOMPRESSOR _IOW('t',135,int)

#define PPP_MP          0x003d
#define PPP_LINK_COMP   0x00fb

#define SC_MP_PROT       0x00000200
#define SC_REJ_MP_PROT   0x00000400
#define SC_OUT_SHORT_SEQ 0x00000800
#define SC_IN_SHORT_SEQ  0x00004000

#define MP_END_FRAG    0x40
#define MP_BEGIN_FRAG  0x80

#ifdef __KERNEL__
/*
 * this is an 'old friend' from ppp-comp.h under a new name 
 * check the original include for more information
 */
struct isdn_ppp_compressor {
	struct isdn_ppp_compressor *next,*prev;
	int num; /* CCP compression protocol number */
	void *(*comp_alloc) (unsigned char *options, int opt_len);
	void (*comp_free) (void *state);
	int  (*comp_init) (void *state, unsigned char *options, int opt_len,
		 int unit, int opthdr, int debug);
	void (*comp_reset) (void *state);
	int  (*compress) (void *state,struct sk_buff *in, struct sk_buff *skb_out,
		 int proto);
	void (*comp_stat) (void *state, struct compstat *stats);
	void *(*decomp_alloc) (unsigned char *options, int opt_len);
	void (*decomp_free) (void *state);
	int  (*decomp_init) (void *state, unsigned char *options,
			int opt_len, int unit, int opthdr, int mru, int debug);
	void (*decomp_reset) (void *state);
	int  (*decompress) (void *state, unsigned char *ibuf, int isize, unsigned char *obuf, int osize);
	void (*incomp) (void *state, unsigned char *ibuf, int icnt);
	void (*decomp_stat) (void *state, struct compstat *stats);
};

extern int isdn_ppp_register_compressor(struct isdn_ppp_compressor *);
extern int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *);

#endif /* __KERNEL__ */

#endif /* _LINUX_ISDN_PPP_H */

