  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Definitions of MAC state structures etc.
 */

struct mac_info {
    TimerTwosComplement	tmax;
    TimerTwosComplement tvx;
    TimerTwosComplement treq;
    ShortAddressType	s_address;
    LongAddressType	l_address;
    ShortAddressType	s_group_adrs;
    LongAddressType	l_group_adrs;
    int			rcv_own_frames;
    int			only_good_frames;
};


struct mac_buf {
    struct mac_buf *next;
    int ack;
    int length;
    void *ptr;
    int wraplen;
    void *wrapptr;
    int fr_start;
    int fr_end;
};

int mac_xmit_space(void);
void mac_xmit_alloc(struct mac_buf *, int);
void mac_queue_frame(struct mac_buf *);
int mac_recv_frame(struct mac_buf *);
void mac_discard_frame(struct mac_buf *);
int mac_init(struct mac_info *mip);
int mac_inited(struct mac_info *mip);
void mac_reset(LoopbackType loopback);
void mac_claim(void);
void mac_sleep(void);
void mac_poll(void);
void mac_disable(void);
void mac_make_spframes(void);
int mac_xalloc(int nwords);
int mac_xmit_dma(struct sk_buff *skb);
void mac_dma_complete(void);
void mac_process(void);
int mac_queue_append(struct sk_buff *skb);

struct dma_chan {
    int cmd;			/* cmd << 16 + size */
    int st;			/* status << 16 + current size */
    int hskip;			/* hskip << 16 + hcnt */
    int vskip;			/* vskip << 16 + vcnt */
    unsigned char *maddr;	/* memory address */
    unsigned char *cmaddr;	/* current memory address */
    int ccount;			/* h_count << 16 + v_count */
    int *tblp;			/* table pointer */
    int *ctblp;			/* current table pointer */
    unsigned char *hdptr;	/* header pointer */
};

#define ROUND4(x)	(((x) + 3) & -4)
#define ROUND8(x)	(((x) + 7) & -8)
#define ROUND16(x)	(((x) + 15) & -16)
#define ROUNDLINE(x)	ROUND16(x)

#define NWORDS(x)	(((x) + 3) >> 2)
#define NLINES(x)	(((x) + 15) >> 4)

/*
 * Queue element used to queue transmit requests on the FDDI.
 */
struct mac_queue {
    volatile struct mac_queue *next;
    struct sk_buff *skb;
};
