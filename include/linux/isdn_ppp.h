#ifndef _LINUX_ISDN_PPP_H
#define _LINUX_ISDN_PPP_H

extern int isdn_ppp_dial_slave(char *);
extern int isdn_ppp_hangup_slave(char *);

struct pppinfo
{
  int type; /* set by user */
  union {
    char clid[32]; /* calling ID */
    int  bundles;
    int  linknumber;
  } info;
};

 
#define PPPIOCLINKINFO _IOWR('t',128,struct pppinfo)
#define PPPIOCBUNDLE   _IOW('t',129,int)
#define PPPIOCGMPFLAGS _IOR('t',130,int)
#define PPPIOCSMPFLAGS _IOW('t',131,int)
#define PPPIOCSMPMTU   _IOW('t',132,int)
#define PPPIOCSMPMRU   _IOW('t',133,int)

#define PPP_MP         0x003d

#define SC_MP_PROT       0x00000200
#define SC_REJ_MP_PROT   0x00000400
#define SC_OUT_SHORT_SEQ 0x00000800
#define SC_IN_SHORT_SEQ  0x00004000

#define MP_END_FRAG    0x40
#define MP_BEGIN_FRAG  0x80

#endif
