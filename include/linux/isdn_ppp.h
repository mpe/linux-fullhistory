#ifndef _LINUX_ISDN_PPP_H
#define _LINUX_ISDN_PPP_H

struct isdn_ppp_compressor
{
    struct isdn_ppp_compressor *next,*prev;
    int num;    /* proto num */
    void *priv; /* private data for compressor */
	int (*open)(struct isdn_ppp_compressor *);
	int (*close)(struct isdn_ppp_compressor *);
	int (*reset)(struct isdn_ppp_compressor *,int type);
	int (*config)(struct isdn_ppp_compressor *,void *data,int data_len);
	struct sk_buff *(*compress)(struct isdn_ppp_compressor *,struct sk_buff *);
	struct sk_buff *(*uncompress)(struct isdn_ppp_compressor *,struct sk_buff *);
};

extern int isdn_ppp_dial_slave(char *);
extern int isdn_ppp_hangup_slave(char *);
extern int isdn_ppp_register_compressor(struct isdn_ppp_compressor *);
extern int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *);

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

#define PPPIOCGCOMPRESSORS  _IOR('t',134,unsigned long)
#define PPPIOCSCOMPRESSOR   _IOW('t',135,int)

#define PPP_MP         0x003d

#define SC_MP_PROT       0x00000200
#define SC_REJ_MP_PROT   0x00000400
#define SC_OUT_SHORT_SEQ 0x00000800
#define SC_IN_SHORT_SEQ  0x00004000

#define MP_END_FRAG    0x40
#define MP_BEGIN_FRAG  0x80

#endif
