#define _PLUSB_INTPIPE		0x1
#define _PLUSB_BULKOUTPIPE	0x2
#define _PLUSB_BULKINPIPE	0x3

#define _SKB_NUM		1000
//  7  6  5  4  3  2  1  0
//  tx rx 1  0
// 1110 0000 rxdata
// 1010 0000 idle
// 0010 0000 tx over
// 0110      tx over + rxd

#define _PLUSB_RXD		0x40
#define _PLUSB_TXOK		0x80

#ifdef __KERNEL__
#define _BULK_DATA_LEN		16384

typedef struct
{
	struct list_head skb_list;
	struct sk_buff *skb;
	int state;
} skb_list_t,*pskb_list_t;

typedef struct
{
	struct usb_device *usbdev;

	int status;
	int connected;
	int in_bh;
	int opened;

	spinlock_t lock;

	urb_t *inturb;
	urb_t *bulkurb;	

	struct list_head tx_skb_list;
	struct list_head free_skb_list;
	struct tq_struct bh;

	struct net_device net_dev;
	struct net_device_stats net_stats;
} plusb_t,*pplusb_t;

#endif
