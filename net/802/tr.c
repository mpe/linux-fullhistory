#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/net.h>
#include <net/arp.h>

static void tr_source_route(struct trh_hdr *trh,struct device *dev);
static void tr_add_rif_info(struct trh_hdr *trh);
static void rif_check_expire(unsigned long dummy);

typedef struct rif_cache_s *rif_cache;

struct rif_cache_s {	
	 unsigned char addr[TR_ALEN];
	 unsigned short rcf;
	 unsigned short rseg[8];
	 rif_cache next;
	 unsigned long last_used;
};

#define RIF_TABLE_SIZE 16
rif_cache rif_table[RIF_TABLE_SIZE]={ NULL, };

#define RIF_TIMEOUT 60*10*HZ
#define RIF_CHECK_INTERVAL 60*HZ
static struct timer_list rif_timer={ NULL,NULL,RIF_CHECK_INTERVAL,0L,rif_check_expire };

int tr_header(struct sk_buff *skb, struct device *dev, unsigned short type,
              void *daddr, void *saddr, unsigned len) 
{

	struct trh_hdr *trh=(struct trh_hdr *)skb_push(skb,dev->hard_header_len);
	struct trllc *trllc=(struct trllc *)(trh+1);

	trh->ac=AC;
	trh->fc=LLC_FRAME;

	if(saddr)
		memcpy(trh->saddr,saddr,dev->addr_len);
	else
		memset(trh->saddr,0,dev->addr_len); /* Adapter fills in address */

	trllc->dsap=trllc->ssap=EXTENDED_SAP;
	trllc->llc=UI_CMD;
	
	trllc->protid[0]=trllc->protid[1]=trllc->protid[2]=0x00;
	trllc->ethertype=htons(type);

	if(daddr) {
		memcpy(trh->daddr,daddr,dev->addr_len);
		tr_source_route(trh,dev);
		return(dev->hard_header_len);
	}
	return -dev->hard_header_len;

}
	
int tr_rebuild_header(void *buff, struct device *dev, unsigned long dest,
							 struct sk_buff *skb) {

	struct trh_hdr *trh=(struct trh_hdr *)buff;
	struct trllc *trllc=(struct trllc *)(buff+sizeof(struct trh_hdr));

	if(trllc->ethertype != htons(ETH_P_IP)) {
		printk("tr_rebuild_header: Don't know how to resolve type %04X addresses ?\n",(unsigned int)htons(	trllc->ethertype));
		return 0;
	}

	if(arp_find(trh->daddr, dest, dev, dev->pa_addr, skb)) {
			return 1;
	}
	else {	
		tr_source_route(trh,dev); 
		return 0;
	}
}
	
unsigned short tr_type_trans(struct sk_buff *skb, struct device *dev) {

	struct trh_hdr *trh=(struct trh_hdr *)skb->data;
	struct trllc *trllc=(struct trllc *)(skb->data+sizeof(struct trh_hdr));
	
	skb->mac.raw = skb->data;
	
	skb_pull(skb,dev->hard_header_len);
	
	if(trh->saddr[0] & TR_RII)
		tr_add_rif_info(trh);

	if(*trh->daddr & 1) 
	{
		if(!memcmp(trh->daddr,dev->broadcast,TR_ALEN)) 	
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}

	else if(dev->flags & IFF_PROMISC) 
	{
		if(memcmp(trh->daddr, dev->dev_addr, TR_ALEN))
			skb->pkt_type=PACKET_OTHERHOST;
	}

 	return trllc->ethertype;
}

/* We try to do source routing... */

static void tr_source_route(struct trh_hdr *trh,struct device *dev) {

	int i;
	unsigned int hash;
	rif_cache entry;

	/* Broadcasts are single route as stated in RFC 1042 */
	if(!memcmp(&(trh->daddr[0]),&(dev->broadcast[0]),TR_ALEN)) {
		trh->rcf=htons((((sizeof(trh->rcf)) << 8) & TR_RCF_LEN_MASK)  
			       | TR_RCF_FRAME2K | TR_RCF_LIMITED_BROADCAST);
		trh->saddr[0]|=TR_RII;
	}
	else {
		for(i=0,hash=0;i<TR_ALEN;hash+=trh->daddr[i++]);
		hash&=RIF_TABLE_SIZE-1;
		for(entry=rif_table[hash];entry && memcmp(&(entry->addr[0]),&(trh->daddr[0]),TR_ALEN);entry=entry->next);

		if(entry) {
#if 0
printk("source routing for %02X %02X %02X %02X %02X %02X\n",trh->daddr[0],
		  trh->daddr[1],trh->daddr[2],trh->daddr[3],trh->daddr[4],trh->daddr[5]);
#endif
			if((ntohs(entry->rcf) & TR_RCF_LEN_MASK) >> 8) {
				trh->rcf=entry->rcf;
				memcpy(&trh->rseg[0],&entry->rseg[0],8*sizeof(unsigned short));
				trh->rcf^=htons(TR_RCF_DIR_BIT);	
				trh->rcf&=htons(0x1fff);	/* Issam Chehab <ichehab@madge1.demon.co.uk> */

				trh->saddr[0]|=TR_RII;
				entry->last_used=jiffies;
			}
		}
		else {
			trh->rcf=htons((((sizeof(trh->rcf)) << 8) & TR_RCF_LEN_MASK)  
				       | TR_RCF_FRAME2K | TR_RCF_LIMITED_BROADCAST);
			trh->saddr[0]|=TR_RII;
		}
	}
			
}

static void tr_add_rif_info(struct trh_hdr *trh) {

	int i;
	unsigned int hash;
	rif_cache entry;


	trh->saddr[0]&=0x7f;
	for(i=0,hash=0;i<TR_ALEN;hash+=trh->saddr[i++]);
	hash&=RIF_TABLE_SIZE-1;
#if 0
	printk("hash: %d\n",hash);
#endif
	for(entry=rif_table[hash];entry && memcmp(&(entry->addr[0]),&(trh->saddr[0]),TR_ALEN);entry=entry->next);

	if(entry==NULL) {
#if 0
printk("adding rif_entry: addr:%02X:%02X:%02X:%02X:%02X:%02X rcf:%04X\n",
		trh->saddr[0],trh->saddr[1],trh->saddr[2],
       		trh->saddr[3],trh->saddr[4],trh->saddr[5],
		trh->rcf);
#endif
		entry=kmalloc(sizeof(struct rif_cache_s),GFP_ATOMIC);
		if(!entry) {
			printk("tr.c: Couldn't malloc rif cache entry !\n");
			return;
		}
		entry->rcf=trh->rcf;
		memcpy(&(entry->rseg[0]),&(trh->rseg[0]),8*sizeof(unsigned short));
		memcpy(&(entry->addr[0]),&(trh->saddr[0]),TR_ALEN);
		entry->next=rif_table[hash];
		entry->last_used=jiffies;
		rif_table[hash]=entry;
	}
/* Y. Tahara added */
   else {                                       
		if ( entry->rcf != trh->rcf ) {               
				if (!(trh->rcf & htons(TR_RCF_BROADCAST_MASK))) {
#if 0
printk("updating rif_entry: addr:%02X:%02X:%02X:%02X:%02X:%02X rcf:%04X\n",
		trh->saddr[0],trh->saddr[1],trh->saddr[2],
		trh->saddr[3],trh->saddr[4],trh->saddr[5],
		trh->rcf);
#endif
     		       entry->rcf = trh->rcf;                  
        		    memcpy(&(entry->rseg[0]),&(trh->rseg[0]),8*sizeof(unsigned short));
           		 entry->last_used=jiffies;               
				}                                          
		}                                             
	}

}

static void rif_check_expire(unsigned long dummy) {

	int i;
	unsigned long now=jiffies,flags;

	save_flags(flags);
	cli();

	for(i=0; i < RIF_TABLE_SIZE;i++) {

	rif_cache entry, *pentry=rif_table+i;	

		while((entry=*pentry)) 
			if((now-entry->last_used) > RIF_TIMEOUT) {
				*pentry=entry->next;
				kfree_s(entry,sizeof(struct rif_cache_s));
			}
			else
				pentry=&entry->next;	
	}
	restore_flags(flags);

	del_timer(&rif_timer);
	rif_timer.expires=jiffies+RIF_CHECK_INTERVAL;
	add_timer(&rif_timer);

}

int rif_get_info(char *buffer,char **start, off_t offset, int length) {

   int len=0;
   off_t begin=0;
   off_t pos=0;
   int size,i;

   rif_cache entry;

	size=sprintf(buffer,
"   TR address     rcf             routing segments             TTL\n\n");
   pos+=size;
   len+=size;

	for(i=0;i < RIF_TABLE_SIZE;i++) {
		for(entry=rif_table[i];entry;entry=entry->next) {
			size=sprintf(buffer+len,"%02X:%02X:%02X:%02X:%02X:%02X %04X %04X %04X %04X %04X %04X %04X %04X %04X %lu\n",
								entry->addr[0],entry->addr[1],entry->addr[2],entry->addr[3],entry->addr[4],entry->addr[5],
								entry->rcf,entry->rseg[0],entry->rseg[1],entry->rseg[2],entry->rseg[3],
								entry->rseg[4],entry->rseg[5],entry->rseg[6],entry->rseg[7],jiffies-entry->last_used); 
			len+=size;
			pos=begin+len;

			if(pos<offset) {
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				break;
   	}
		if(pos>offset+length)
			break;
	}

   *start=buffer+(offset-begin); /* Start of wanted data */
   len-=(offset-begin);    /* Start slop */
   if(len>length)
      len=length;    /* Ending slop */
   return len;
}

void rif_init(struct net_proto *unused) {

	add_timer(&rif_timer);

}

