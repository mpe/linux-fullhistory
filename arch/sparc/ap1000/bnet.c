  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* routines to control the AP1000 bif interface. This is the interface
   used to talk to the front end processor */

#include <linux/sched.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/irq.h>
#include <linux/skbuff.h>

#define NET_DEBUG 0

#define DUMMY_MSG_LEN 100
#define DUMMY_MSG_WAIT 30

#define MAX_CELLS 128

#define HAVE_BIF() (BIF_IN(BIF_SDCSR) & BIF_SDCSR_BG)
#define BIF_BUSY() (BIF_IN(BIF_SDCSR) & BIF_SDCSR_BB)

#define SNET_ARBITRATION 0
#define TOKEN_ARBITRATION 1

#define DEBUG(x) 

#if TOKEN_ARBITRATION
static int have_token = 0;
#endif

extern struct cap_init cap_init;

static int interrupt_driven = 0;
static int use_dma = 0;
struct pt_regs *bif_pt_regs = NULL;
enum dma_state {DMA_IDLE,DMA_INCOMING,DMA_OUTGOING};
static enum dma_state dma_state = DMA_IDLE;

static int net_started = 0;
static int waiting_for_bif = 0;
static int queue_length = 0;

static int drop_ip_packets = 0;

#define DMA_THRESHOLD 64

static struct cap_request bread_req;

int tnet_ip_enabled = 1;

#define BIF_DATA_WAITING() (BIF_IN(BIF_SDCSR) & BIF_SDCSR_RB)

#define ROUND4(x)	(((x) + 3) & -4)

static void bif_intr_receive(struct cap_request *req1);


/* read some data from the bif */
void read_bif(char *buf,int size) 
{
	unsigned *ibuf = (unsigned *)buf;
	unsigned avail;

	DEBUG(("|read_bif %d\n",size));
	
	if (dma_state != DMA_IDLE) ap_dma_wait(DMA_CH2);  
	
	size = (size+3) >> 2;

	while (size > 4) {
		while (!(avail=(BIF_IN(BIF_SDCSR) >> BIF_SDCSR_RB_SHIFT) & 7))
			;
		if (avail & 4) {
			ibuf[0] = BIF_IN(BIF_DATA);
			ibuf[1] = BIF_IN(BIF_DATA);
			ibuf[2] = BIF_IN(BIF_DATA);
			ibuf[3] = BIF_IN(BIF_DATA);
			size -= 4; ibuf += 4;
			continue;
		}

		if (avail & 2) {
			ibuf[0] = BIF_IN(BIF_DATA);
			ibuf[1] = BIF_IN(BIF_DATA);
			size -= 2; ibuf += 2;
			continue;
		}
		*ibuf++ = BIF_IN(BIF_DATA);
		size--;
	}

	while (size--) {
		while (!(BIF_IN(BIF_SDCSR) & BIF_SDCSR_RB)) ;
		*ibuf++ = BIF_IN(BIF_DATA);
	}

	DEBUG(("|read bif done\n"));
}

/* throw out some data from the bif. This is usually called when we
 don't have the resources to handle it immediately */
void bif_toss(int size) 
{
	unsigned flags;
	save_flags(flags); cli();

	DEBUG(("|bif toss %d\n",size));

	while (size>0) {
		while (!BIF_DATA_WAITING());
		BIF_IN(BIF_DATA);
		size -= 4;
	}    

	DEBUG(("|bif toss done\n"));

	restore_flags(flags);
}


static void bif_reset_interrupts(void)
{
	BIF_OUT(BIF_INTR,AP_INTR_WENABLE << BIF_INTR_GET_SH);
	BIF_OUT(BIF_INTR,AP_INTR_WENABLE << BIF_INTR_HEADER_SH);
}

static void bif_mask_interrupts(void)
{
	BIF_OUT(BIF_INTR,(AP_INTR_MASK|AP_INTR_WENABLE) << BIF_INTR_GET_SH);
	BIF_OUT(BIF_INTR,(AP_INTR_MASK|AP_INTR_WENABLE) << BIF_INTR_HEADER_SH);
}

static void attn_enable(void)
{
	BIF_OUT(BIF_INTR,AP_INTR_WENABLE << BIF_INTR_ATTN_SH);
}

static void attn_mask(void)
{
	BIF_OUT(BIF_INTR,(AP_INTR_MASK|AP_INTR_WENABLE) << BIF_INTR_ATTN_SH);
}


void ap_bif_status(void)
{
	static int bif_sdcsr;
	static int bif_intr;
	static int bif_mhocr;
	static int bif_x0sk;
	static int bif_xsk;
	static int bif_xsz;
	static int bif_y0sk;
	static int bif_ysk;
	static int bif_ysz;
	static int bif_cx0sk;
	static int bif_cxsk;
	static int bif_cxsz;
	static int bif_cy0sk;
	static int bif_cysk;
	static int bif_cysz;
	static int bif_ttl;
	static int bif_cttl;
	static int bif_header;
	
	bif_sdcsr = BIF_IN(BIF_SDCSR);
	bif_intr  = BIF_IN(BIF_INTR);
	bif_mhocr = BIF_IN(BIF_MHOCR);
	
	bif_x0sk  = BIF_IN(BIF_X0SK);
	bif_xsk   = BIF_IN(BIF_XSK);
	bif_xsz   = BIF_IN(BIF_XSZ);
	bif_y0sk  = BIF_IN(BIF_Y0SK);
	bif_ysk   = BIF_IN(BIF_YSK);
	bif_ysz   = BIF_IN(BIF_YSZ);
	
	bif_cx0sk  = BIF_IN(BIF_CX0SK);
	bif_cxsk   = BIF_IN(BIF_CXSK);
	bif_cxsz   = BIF_IN(BIF_CXSZ);
	bif_cy0sk  = BIF_IN(BIF_CY0SK);
	bif_cysk   = BIF_IN(BIF_CYSK);
	bif_cysz   = BIF_IN(BIF_CYSZ);
	
	bif_ttl   = BIF_IN(BIF_TTL);
	bif_cttl  = BIF_IN(BIF_CTTL);
	bif_header = BIF_IN(BIF_HEADER);

	printk("|\t***** BIF REG. *****\n");
	printk("|\tBIF_SDCSR  = %08x  ", bif_sdcsr);
	if(bif_sdcsr & BIF_SDCSR_CN) printk("|<BUS DISCONNECT>");
	if(bif_sdcsr & BIF_SDCSR_FN) printk("|<SC/GA ENABLE>");
	if(bif_sdcsr & BIF_SDCSR_DE) printk("|<DMA ENABLE>");
	if(bif_sdcsr & BIF_SDCSR_DR) printk("|<GATHER>");
	if(bif_sdcsr & BIF_SDCSR_BB) printk("|<BUS BSY>");
	if(bif_sdcsr & BIF_SDCSR_BR) printk("|<BUS REQ>");
	if(bif_sdcsr & BIF_SDCSR_BG) printk("|<BUS GET>");
	if(bif_sdcsr & BIF_SDCSR_ER) printk("|<ERROR:");
	if(bif_sdcsr & BIF_SDCSR_SP) printk("|SYNC PARITY:");
	if(bif_sdcsr & BIF_SDCSR_LP) printk("|LBUS PARITY:");
	if(bif_sdcsr & BIF_SDCSR_LR) printk("|READ EMPTY FIFO:");
	if(bif_sdcsr & BIF_SDCSR_LW) printk("|WRITE FULL FIFO:");
	if(bif_sdcsr & BIF_SDCSR_AL) printk("|READ ENDBIT:");
	if(bif_sdcsr & BIF_SDCSR_SS) printk("|SET MASK SSTAT:");
	if(bif_sdcsr & BIF_SDCSR_SC) printk("|CLR SSYNC ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_SY) printk("|REQ SSYNC ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_FS) printk("|SET MASK FSTAT:");
	if(bif_sdcsr & BIF_SDCSR_FC) printk("|CLR FSYNC ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_FY) printk("|REQ FSYNC ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_CP) printk("|BNET PARITY:");
	if(bif_sdcsr & BIF_SDCSR_FP) printk("|FE NOT SET WHEN SC/GA:");
	if(bif_sdcsr & BIF_SDCSR_PS) printk("|RECV PACKET ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_RA) printk("|CHANGE FE ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_PA) printk("|SEND/RECV ILLEGALLY:");
	if(bif_sdcsr & BIF_SDCSR_DL) printk("|DATA LOST:");
	if(bif_sdcsr & BIF_SDCSR_ER) printk("|>");
	if(bif_sdcsr & BIF_SDCSR_PE) printk("|<SYNC PARITY ENABLE>");
	printk("|\n");
	printk("|\tBIF_INTR   = %08x\n", bif_intr);
	printk("|\tBIF_MHOCR  = %08x\n", bif_mhocr);

	printk("|\tBIF_X0SK   = %08x\n", bif_x0sk);
	printk("|\tBIF_XSK    = %08x\n", bif_xsk);
	printk("|\tBIF_XSZ    = %08x\n", bif_xsz);
	printk("|\tBIF_Y0SK   = %08x\n", bif_y0sk);
	printk("|\tBIF_YSK    = %08x\n", bif_ysk);
	printk("|\tBIF_YSZ    = %08x\n", bif_ysz);
	printk("|\tBIF_CX0SK  = %08x\n", bif_cx0sk);
	printk("|\tBIF_CXSK   = %08x\n", bif_cxsk);
	printk("|\tBIF_CXSZ   = %08x\n", bif_cxsz);
	printk("|\tBIF_CY0SK  = %08x\n", bif_cy0sk);
	printk("|\tBIF_CYSK   = %08x\n", bif_cysk);
	printk("|\tBIF_CYSZ   = %08x\n", bif_cysz);

	printk("|\tBIF_TTL    = %08x\n", bif_ttl);
	printk("|\tBIF_CTTL   = %08x\n", bif_cttl);
	printk("|\tBIF_HEADER = %08x\n", bif_header);
}


void bif_led_status(void)
{
#if 1
	static int i = 0;
	unsigned char res = 0;

	switch (i) {
	case 0: 
	case 2: 
		res = 0xff;
		break;
	case 1: 
	case 3: 
		res = 0;
		break;
	default:
		res = 0xFF & (BIF_IN(BIF_SDCSR) >> (((i-4)/4)*8));
	}
	i = (i+1) % 20;

	ap_led(res);
#endif
}

static void get_bif(void)
{
	if (HAVE_BIF())
		return;

	drop_ip_packets = 1;

	DEBUG(("|get_bif started\n"));

	if (dma_state != DMA_IDLE) 
		ap_dma_wait(DMA_CH2);  

#if SNET_ARBITRATION
	/* wait till the host doesn't want the BIF anymore, tossing
	   any data that arrives */
	while (BIF_IN(FSTT_CLR) & HOST_STATUS_BIT) 
		if (BIF_IN(BIF_SDCSR) & BIF_SDCSR_RB)
			bif_intr_receive(NULL);
	waiting_for_bif = 0;
#endif

#if TOKEN_ARBITRATION
	BIF_OUT(FSTT_CLR,HOST_STATUS_BIT);
#endif

	/* request the BIF */
	BIF_OUT(BIF_SDCSR,BIF_SDCSR_BR);

	/* loop waiting for us to get the BIF, tossing any data */
	while (!HAVE_BIF())
		if (BIF_IN(BIF_SDCSR) & BIF_SDCSR_RB)
			bif_intr_receive(NULL);

	bif_reset_interrupts();
	if (!interrupt_driven)
		bif_mask_interrupts();

	drop_ip_packets = 0;

#if TOKEN_ARBITRATION
	BIF_OUT(FSTT_SET,HOST_STATUS_BIT);
#endif

	DEBUG(("|get_bif done\n"));
}


/* write a message to the front end over the Bnet. This can be in
   multiple parts, as long as the first part sets "start" and the last
   part sets "end". The bus will be grabbed while this is going on 
   */
static void write_bif(char *buf,int size,int start,int end)
{
	unsigned *ibuf;
	unsigned avail;

	DEBUG(("|write_bif %d %d %d\n",size,start,end));

	if (start) {
		/* a dma op may be in progress */
		if (dma_state != DMA_IDLE) ap_dma_wait(DMA_CH2);
	}

	size = (size+3) >> 2;
	ibuf = (unsigned *)buf;
	if (end) size--;

	while (size > 4) {
		while (!(avail=(BIF_IN(BIF_SDCSR) >> BIF_SDCSR_TB_SHIFT) & 7))
			;
		if (avail & 4) {
			BIF_OUT(BIF_DATA,ibuf[0]);
			BIF_OUT(BIF_DATA,ibuf[1]);
			BIF_OUT(BIF_DATA,ibuf[2]);
			BIF_OUT(BIF_DATA,ibuf[3]);
			size -= 4; ibuf += 4;
			continue;
		}

		if (avail & 2) {
			BIF_OUT(BIF_DATA,ibuf[0]);
			BIF_OUT(BIF_DATA,ibuf[1]);
			size -= 2; ibuf += 2;
			continue;
		}
		BIF_OUT(BIF_DATA,ibuf[0]);
		ibuf++; size--;
	}

	while (size--) {
		while (!(BIF_IN(BIF_SDCSR) & BIF_SDCSR_TB)) ;
		BIF_OUT(BIF_DATA,ibuf[0]);
		ibuf++;
	}

	if (end) {
		while (!(BIF_IN(BIF_SDCSR) & BIF_SDCSR_TB)) ;
		BIF_OUT(BIF_EDATA,*ibuf);
	}

	DEBUG(("|write bif done\n"));
}

#if TOKEN_ARBITRATION
static void forward_token(void)
{
	struct cap_request req;
	req.cid = mpp_cid();
	req.type = REQ_BIF_TOKEN;
	req.size = sizeof(req);
	if (req.cid == cap_init.numcells - 1)
		req.header = MAKE_HEADER(HOST_CID);
	else
		req.header = MAKE_HEADER(req.cid + 1);
	
	write_bif((char *)&req,sizeof(req),1,1);
	have_token = 0;
}
#endif

static void release_bif(void)
{
	static int dummy[DUMMY_MSG_LEN];

	waiting_for_bif = 0;

#if SNET_ARBITRATION
	/* mask the attention interrupt */
	attn_mask();
#endif

	/* maybe we don't have it?? */
	if (!HAVE_BIF())
		return;

	DEBUG(("|release bif started\n"));

	if (dma_state != DMA_IDLE) ap_dma_wait(DMA_CH2);  

#if TOKEN_ARBITRATION
	if (have_token) 
		forward_token();	
#endif

#if 1
	/* send a dummy message to ensure FIFO flushing
	   (suggestion from woods to overcome bif release
	   hardware bug) */
	dummy[0] = 0xEEEE4000;
	write_bif((char *)dummy,DUMMY_MSG_LEN,1,1);
#endif
	/* wait till the send FIFO is completely empty */
	while (!((BIF_IN(BIF_SDCSR) & BIF_SDCSR_TB) == BIF_SDCSR_TB)) ;   

	/* wait another few us */
	udelay(DUMMY_MSG_WAIT);

	/* send release-data */
	BIF_OUT(BIF_DATA,BIF_HEADER_RS);
	
	/* wait until we don't have the bus */
	while (HAVE_BIF()) ;

	DEBUG(("|release bif done\n"));
}


/* wait for a particular request type - throwing away anything else! */
void ap_wait_request(struct cap_request *req,int type)
{
	drop_ip_packets = 1;
	do {
		while (!BIF_DATA_WAITING())
			if (HAVE_BIF()) release_bif();
		read_bif((char *)req,sizeof(*req));		
		if (req->type != type) {
			bif_intr_receive(req);
		}
	} while (req->type != type);
	drop_ip_packets = 0;
}


void write_bif_polled(char *buf1,int len1,char *buf2,int len2)
{
	unsigned flags;
	save_flags(flags); cli();

	get_bif();
	write_bif(buf1,len1,1,(buf2&&len2)?0:1);
	if (buf2 && len2)
		write_bif(buf2,len2,0,1);
	release_bif();
	restore_flags(flags);
}

static void want_bif(void)
{
	unsigned flags;

	save_flags(flags); cli();

	/* maybe we've already got it */
	if (HAVE_BIF()) {
		waiting_for_bif = 0;
		restore_flags(flags);
		return;
	}

#if SNET_ARBITRATION	
	if (interrupt_driven)
		attn_enable();

	/* check if the host wants it */
	if (BIF_IN(FSTT_CLR) & HOST_STATUS_BIT) {
		/* the host wants it - don't get it yet  */
		waiting_for_bif = 1;
	} else {
		/* the host doesn't want it - just set bus request */
		waiting_for_bif = 0;
		BIF_OUT(BIF_SDCSR,BIF_SDCSR_BR);
		while (!HAVE_BIF() && !BIF_BUSY()) ;
		DEBUG(("|set bif request\n"));
	}
	restore_flags(flags);
	return;
#endif

#if TOKEN_ARBITRATION
	if (net_started && !have_token) {
		BIF_OUT(FSTT_CLR,HOST_STATUS_BIT);
		restore_flags(flags);
		return;
	}
	BIF_OUT(FSTT_SET,HOST_STATUS_BIT);
#endif

	BIF_OUT(BIF_SDCSR,BIF_SDCSR_BR);
	restore_flags(flags);
}

#define BIF_NOCOPY (1<<0)

/* a queue of requests that need to be sent over the bif. Needs to be
modified sometime to allow the direct queueing of skb's */
struct bif_queue {
	volatile struct bif_queue *next;
	struct cap_request req;
	char *data;
	int data_size;
	int flags;
};

static volatile struct bif_queue *bif_queue_top = NULL;
static volatile struct bif_queue *bif_queue_end = NULL;

static struct sk_buff *skb_out = NULL;
static struct sk_buff *skb_in = NULL;
static char *bif_dma_data = NULL;
static int bif_dma_out_size = 0;


/* send waiting elements. Called mainly when we get a bif "bus get"
   interrupt to say we now have the bus */
static void bif_intr_runqueue(void)
{
	unsigned flags;
	
	/* if I don't have the bus then return */
	if (!HAVE_BIF())
		return;
	
	if (dma_state != DMA_IDLE) return;
	
	save_flags(flags); cli();
	
	while (bif_queue_top) {
		volatile struct bif_queue *q = bif_queue_top;
		bif_queue_top = q->next;

		/* printk("|queue run (length=%d)\n",queue_length); */
		queue_length--;

		if (!q->data) {
			/* use programmed IO for small requests */
			write_bif((char *)&q->req,sizeof(q->req),1,1);
			kfree_s((char *)q,sizeof(*q));
			continue;
		}

		if (q->flags & BIF_NOCOPY) {
			write_bif((char *)&q->req,sizeof(q->req),1,0);
		}

		if (use_dma && q->data_size > DMA_THRESHOLD) {
			dma_state = DMA_OUTGOING;
			if (q->req.type == REQ_IP) {
				skb_out = (struct sk_buff *)q->data;
				ap_dma_go(DMA_CH2,(unsigned)skb_out->data,
					  q->data_size,DMA_DCMD_TD_MD);
			} else {
				if (!(q->flags & BIF_NOCOPY)) {
					bif_dma_data = q->data;
					bif_dma_out_size = q->data_size;
				}
				ap_dma_go(DMA_CH2,(unsigned)q->data,
					  q->data_size,DMA_DCMD_TD_MD);
			}
			kfree_s((char *)q,sizeof(*q));
			restore_flags(flags);
			return; /* wait for DMA to complete */
		} 

		if (q->req.type == REQ_IP) {
			struct sk_buff *skb = (struct sk_buff *)q->data;
			write_bif(skb->data,q->data_size,1,1);       
			dev_kfree_skb(skb);
		} else {
			write_bif(q->data,q->data_size,1,1);
			if (!(q->flags & BIF_NOCOPY))
				kfree_s(q->data,q->data_size);	
		}
		kfree_s((char *)q,sizeof(*q));
	}
  
	/* I don't want the bus now */
	release_bif(); 
	restore_flags(flags);
}


static void queue_attach(struct bif_queue *q)
{
	unsigned flags;
	save_flags(flags); cli();
	
	/* attach it to the end of the queue */
	if (!bif_queue_top) {
		bif_queue_top = q;
	} else {
		bif_queue_end->next = q;
	}
	bif_queue_end = q;

	queue_length++;

	/* printk("|queue add (length=%d)\n",queue_length); */

	/* tell the bus we want access */
	want_bif();

	restore_flags(flags);  
}


/* queue an element for sending over the bif. */
int bif_queue(struct cap_request *req,char *buf,int bufsize)
{
	struct bif_queue *q;

	if (req->header == 0)
		req->header = MAKE_HEADER(HOST_CID);
	
	/* if we aren't running interrupt driven then just send it 
	   immediately */
	if (!interrupt_driven) {
		write_bif_polled((char *)req,sizeof(*req),buf,bufsize);
		return(0);
	}

	/* allocate a queue element */
	q = (struct bif_queue *)kmalloc(sizeof(*q), GFP_ATOMIC);
	if (!q) {
		/* yikes! */
		return(-ENOMEM);
	}
	
	q->flags = 0;
	q->data = NULL;
	q->data_size = 0;
	
	if (buf && bufsize>0) {
		q->data_size = bufsize+sizeof(*req);
		q->data = (char *)kmalloc(q->data_size,GFP_ATOMIC);
		if (!q->data) {
			kfree_s(q,sizeof(*q));
			return(-ENOMEM);
		}
	}

	q->req = *req;
	if (buf&&bufsize) {
		memcpy(q->data,(char *)req,sizeof(*req));
		memcpy(q->data+sizeof(*req),buf,bufsize);
	}
	q->next = NULL;
	
	queue_attach(q);

	return(0);
}


/* queue an element for sending over the bif. */
int bif_queue_nocopy(struct cap_request *req,char *buf,int bufsize)
{
	struct bif_queue *q;

	if (req->header == 0)
		req->header = MAKE_HEADER(HOST_CID);
	
	/* allocate a queue element */
	q = (struct bif_queue *)kmalloc(sizeof(*q), GFP_ATOMIC);
	if (!q) {
		return(-ENOMEM);
	}
	
	q->data = buf;
	q->data_size = bufsize;
	q->flags = BIF_NOCOPY;
	q->req = *req;
	q->next = NULL;
	
	queue_attach(q);

	return(0);
}


/* put an IP packet into the bif queue */
int bif_send_ip(int cid, struct sk_buff *skb)
{
	struct cap_request *req = (struct cap_request *)skb->data;
	struct bif_queue *q;
	u_long destip;

	destip = *(u_long *)(skb->data+sizeof(*req)+16);

	if (cid != -1) {
		req->header = MAKE_HEADER(cid);
	} else if (destip == (cap_init.baseIP | ~cap_init.netmask)) {
		req->header = BIF_HEADER_IN | BIF_HEADER_BR;    
	} else {
		req->header = MAKE_HEADER(HOST_CID);    
	}
	
	/* allocate a queue element */
	q = (struct bif_queue *)kmalloc(sizeof(*q), GFP_ATOMIC);
	if (!q) {
		/* yikes! */
		dev_kfree_skb(skb);
		return(-ENOMEM);
	}
	
	req->size = ROUND4(skb->len);
	req->cid = mpp_cid();
	req->type = REQ_IP;

	q->data = (char *)skb;
	q->data_size = req->size;
	q->next = NULL;
	q->req = *req;
	q->flags = 0;
	
	queue_attach(q);
	
	return(0);
}


/* send an OPENNET request to tell the front end to open the apnet
   network interface */
void start_apnet(void)
{
	struct cap_request req;
	req.cid = mpp_cid();
	req.type = REQ_OPENNET;
	req.size = sizeof(req);
	req.header = MAKE_HEADER(HOST_CID);
	
	bif_queue(&req,NULL,0);
	printk("sent start_apnet request\n");
}

/* we have received an IP packet - pass it to the bif network
   interface code */
static void reply_ip(struct cap_request *req)
{
	if (drop_ip_packets || 
	    !(skb_in = dev_alloc_skb(req->size - sizeof(*req)))) {
		bif_toss(req->size - sizeof(*req));
		return;
	}

	if (use_dma && req->size > DMA_THRESHOLD) {
		dma_state = DMA_INCOMING;
		ap_dma_go(DMA_CH2,
			  (unsigned)skb_put(skb_in,req->size - sizeof(*req)),
			  req->size - sizeof(*req),DMA_DCMD_TD_DM);
	} else {
		read_bif(skb_put(skb_in,req->size - sizeof(*req)),
			 req->size - sizeof(*req));
		bif_rx(skb_in);
		skb_in = NULL;
	}
}


/* we have received a bread block - DMA it in */
static void reply_bread(struct cap_request *req)
{
	extern char *ap_buffer(struct cap_request *creq);
	char *buffer;
	
	buffer = ap_buffer(req);
	bread_req = *req;
	
	if (use_dma) {
		dma_state = DMA_INCOMING;
		ap_dma_go(DMA_CH2,
			  (unsigned)buffer,req->size - sizeof(*req),
			  DMA_DCMD_TD_DM);
	} else {
		read_bif(buffer,req->size - sizeof(*req));
		ap_complete(&bread_req);
		bread_req.type = -1;	  
	}
}


static struct debug_key {
	struct debug_key *next;
	char key;
	void (*fn)(void);
	char *description;
} *debug_keys = NULL;


void show_debug_keys(void)
{
	struct debug_key *r;
	for (r=debug_keys;r;r=r->next)
		printk("%c: %s\n",r->key,r->description);
}


void bif_add_debug_key(char key,void (*fn)(void),char *description)
{
	struct debug_key *r,*r2;
	r = (struct debug_key *)kmalloc(sizeof(*r),GFP_ATOMIC);
	if (r) {
		r->next = NULL;
		r->key = key;
		r->fn = fn;
		r->description = description;
		if (!debug_keys) {
			debug_keys = r;
		} else {
			for (r2=debug_keys;
			     r2->next && r2->key != key;r2=r2->next) ;

			if (r2->key == key) {
				r2->fn = fn;
				r2->description = description;
				kfree_s(r,sizeof(*r));
			} else {
				r2->next = r;
			}
		}
	}
}

/* these are very useful for debugging ! */
static void reply_putchar(struct cap_request *req)
{  
	struct debug_key *r;

	char c = req->data[0];

	ap_set_user(req->data[1]);

	for (r=debug_keys;r;r=r->next)
		if (r->key == c) {
			r->fn();
			break;
		}      
	if (!r)
		printk("cell %d got character %d [%c]\n",mpp_cid(),(int)c,c);

	ap_set_user(-1);
}


/* send a signal to a task by name or pid */
static void reply_kill(struct cap_request *req)
{  
  int sig = req->data[0];
  struct task_struct *p;
  int len;
  char name[32];

  len = req->size - sizeof(*req);
  if (len == 0) {
    int pid = req->data[1];
    p = find_task_by_pid(pid);

    if(p)
	    send_sig(sig, p, 1);
    else
	    printk("cell %d: no task with pid %d\n",mpp_cid(),pid);
    return;
  }

  if (len > sizeof(name)-1) {
    bif_toss(len);
    return;
  }

  read_bif(name,len);
  name[len] = 0;

  read_lock(&tasklist_lock);
  for_each_task(p) 
    if (strcmp(name,p->comm) == 0)
      send_sig(sig,p,1);
  read_unlock(&tasklist_lock);
}


static struct req_list {
	struct req_list *next;
	int type;
	void (*fn)(struct cap_request *);
} *reg_req_list = NULL;


void bif_register_request(int type,void (*fn)(struct cap_request *))
{
	struct req_list *r,*r2;
	r = (struct req_list *)kmalloc(sizeof(*r),GFP_ATOMIC);
	if (r) {
		r->next = NULL;
		r->type = type;
		r->fn = fn;
		if (!reg_req_list) {
			reg_req_list = r;
		} else {
			for (r2=reg_req_list;
			     r2->next && r2->type != type;r2=r2->next) ;

			if (r2->type == type) {
				r2->fn = fn;
				kfree_s(r,sizeof(*r));
			} else {
				r2->next = r;
			}
		}
	}
}



/* a request has come in on the bif - process it */
static void bif_intr_receive(struct cap_request *req1)
{
	struct req_list *r;
	extern void ap_open_reply(struct cap_request *creq);  
	struct cap_request req;

	if (req1) {
		req = *req1;
	} else {
		/* read the main cap request header */
		read_bif((char *)&req,sizeof(req));
	}

	/* service it */
	switch (req.type)
	{
	case REQ_PUTCHAR:
		reply_putchar(&req);
		break;
	case REQ_KILL:
		reply_kill(&req);
		break;
	case REQ_BREAK:
		breakpoint();
		break;
	case REQ_IP:
		reply_ip(&req);
		break;
#if TOKEN_ARBITRATION
	case REQ_BIF_TOKEN:
		have_token = 1;
		want_bif();
		break;
#endif
	case REQ_OPENNET:
		net_started = 1;
		break;
	case REQ_BREAD:
		reply_bread(&req);
		break;
	case REQ_BOPEN:
		ap_open_reply(&req);
		break;
	case REQ_BWRITE:
		ap_complete(&req);
		break;
	case REQ_SCHEDULE:
		mpp_schedule(&req);
		break;

	default:
		for (r=reg_req_list;r;r=r->next)
			if (r->type == req.type) {
				r->fn(&req);
				return;
			}
		printk("Unknown request %d\n",req.type);
		break;
	}
}


static void bif_dma_complete(void)
{
	extern int bif_rx(struct sk_buff *skb);
	enum dma_state old_state = dma_state;
	unsigned a;
	
	a = DMA_IN(DMA2_DMST);
	
	if (a & DMA_DMST_AC) return;
	
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_REQ<<DMA_INTR_NORMAL_SH);
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_REQ<<DMA_INTR_ERROR_SH);
	
	if (old_state == DMA_INCOMING) {
		if (skb_in) bif_rx(skb_in);
		skb_in = NULL;
	}
	if (bread_req.type != -1) {
		ap_complete(&bread_req);
		bread_req.type = -1;
	}

	if (bif_dma_data) {
		kfree_s(bif_dma_data,bif_dma_out_size);
		bif_dma_data = NULL;
	}
	
	if (skb_out) {
		dev_kfree_skb(skb_out);
		skb_out = NULL;
	}
	
	dma_state = DMA_IDLE;
	
	if (a & (AP_INTR_REQ<<DMA_INTR_ERROR_SH)) {
		printk("BIF: got dma error\n");
	}
}


/* handle bif related interrupts. Currently handles 3 interrupts, the
   bif header interrupt, the bif get interrupt and the dma transfer
   complete interrupt */
static void bif_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned flags;
	
	bif_pt_regs = regs;                       
	
	save_flags(flags); cli();

	mac_dma_complete();

	if (dma_state != DMA_IDLE) {
		bif_dma_complete();
	}
	
	bif_reset_interrupts();
	
	while (dma_state == DMA_IDLE && BIF_DATA_WAITING()) {
		bif_intr_receive(NULL);
	}

	if (dma_state != DMA_IDLE) {
		bif_dma_complete();
	}
	
	if (dma_state == DMA_IDLE && bif_queue_top && !HAVE_BIF()) {
		want_bif();
	}
	
	if (dma_state == DMA_IDLE && HAVE_BIF()) { 
		waiting_for_bif = 0;
		bif_intr_runqueue(); 
	}

	if (dma_state == DMA_IDLE && HAVE_BIF()) {
		release_bif();
	}

	restore_flags(flags);
	bif_pt_regs = NULL;
}


/* handle the attention interrupt - used for BIF arbitration */
static void attn_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned flags;

	save_flags(flags); cli();

	attn_enable();

#if SNET_ARBITRATION
	attn_mask();
	DEBUG(("|bif attn irq %d\n",irq));

	if (waiting_for_bif)
		want_bif();

	DEBUG(("|bif attn irq %d done\n",irq));
#endif

	bif_interrupt(irq,dev_id,regs);
	restore_flags(flags);
}


void bif_timer(void)
{
#if 1
	if (interrupt_driven)
		bif_interrupt(0,NULL,NULL);
#endif
}

static void tnet_ip_enable(void)
{ 
	tnet_ip_enabled = 1; 
	printk("tnet_ip_enabled=%d\n",tnet_ip_enabled);
}

static void tnet_ip_disable(void)
{ 
	tnet_ip_enabled = 0; 
	printk("tnet_ip_enabled=%d\n",tnet_ip_enabled);
}

/* initialise the bif code */
void ap_bif_init(void)
{
	int res;
	unsigned long flags;
	printk("doing ap_bif_init()\n");
	
	bif_add_debug_key('+',tnet_ip_enable,"enable Tnet based IP");
	bif_add_debug_key('-',tnet_ip_disable,"disable Tnet based IP");
	
	save_flags(flags); cli();
	
	/* register the BIF interrupt */
	if ((res=request_irq(APBIF_IRQ, bif_interrupt, 
			     SA_INTERRUPT, "apbif", NULL))) {
		printk("Failed to install bif interrupt handler\n");
		restore_flags(flags);
		return;
	}

	/* and the bus get interrupt */
	if ((res=request_irq(APBIFGET_IRQ, bif_interrupt, 
			     SA_INTERRUPT, "apbifget", NULL))) {
		printk("Failed to install bifget interrupt handler\n");
		restore_flags(flags);
		return;
	}

	/* dma complete interrupt */
	if ((res=request_irq(APDMA_IRQ, bif_interrupt, SA_INTERRUPT, 
			     "apdma", NULL))) {
		printk("Failed to install bifdma interrupt handler\n");
		restore_flags(flags);
		return;
	}

	/* attention interrupt */
	if ((res=request_irq(APATTN_IRQ, attn_interrupt, SA_INTERRUPT, 
			     "apattn", NULL))) {
		printk("Failed to install apattn interrupt handler\n");
		restore_flags(flags);
		return;
	}

	printk("Installed bif handlers\n");

	/* enable dma-request */
	BIF_OUT(BIF_SDCSR,BIF_SDCSR_DE);
	
	DMA_OUT(DMA2_DCMD,DMA_DCMD_SA);	
	DMA_OUT(DMA2_DMST,DMA_DMST_RST);
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_REQ<<DMA_INTR_NORMAL_SH);
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_MASK<<DMA_INTR_NORMAL_SH);
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_REQ<<DMA_INTR_ERROR_SH);
	DMA_OUT(DMA2_DMST,AP_CLR_INTR_MASK<<DMA_INTR_ERROR_SH);

	/* enable the attention interrupt */
	attn_enable();
	
	DMA_OUT(DMA_BIF_BCMD,DMA_BCMD_SA);
	DMA_OUT(DMA_BIF_BRST,DMA_DMST_RST);
	
	/* from now on we are interrupt driven */
	bread_req.type = -1;
	dma_state = DMA_IDLE;
	interrupt_driven = 1;
	use_dma = 1;
	bif_reset_interrupts();

	/* if theres something in the queue then we also want the bus */
	if (bif_queue_top) 
		want_bif();
	
	/* tell the host that networking is now OK */
	start_apnet();
	
	printk("bif initialised\n");
	
	restore_flags(flags);
}


