#include <linux/types.h>
#include <linux/atmmpc.h>
#include <linux/time.h>

#include "mpoa_caches.h"
#include "mpc.h"

/*
 * mpoa_caches.c: Implementation of ingress and egress cache
 * handling functions
 */

#if 0
#define dprintk printk    /* debug */
#else
#define dprintk(format,args...)
#endif

#if 0
#define ddprintk printk  /* more debug */
#else
#define ddprintk(format,args...)
#endif

static in_cache_entry *in_cache_search(uint32_t dst_ip, 
				       struct mpoa_client *client)
{
        unsigned long flags;
        in_cache_entry *entry;

        read_lock_irqsave(&client->ingress_lock, flags);
        entry = client->in_cache;
        while(entry != NULL){
                if( entry->ctrl_info.in_dst_ip == dst_ip ){
                        read_unlock_irqrestore(&client->ingress_lock, flags);
                        return entry;
                }
                entry = entry->next;
        }
        read_unlock_irqrestore(&client->ingress_lock, flags);

        return NULL;
}

static in_cache_entry *in_cache_search_with_mask(uint32_t dst_ip,
						 struct mpoa_client *client,
						 uint32_t mask){
        unsigned long flags;
        in_cache_entry *entry;

        read_lock_irqsave(&client->ingress_lock, flags);
        entry = client->in_cache;
        while(entry != NULL){
                if((entry->ctrl_info.in_dst_ip & mask)  == (dst_ip & mask )){
                        read_unlock_irqrestore(&client->ingress_lock, flags);
                        return entry;
                }
                entry = entry->next;
        }
        read_unlock_irqrestore(&client->ingress_lock, flags);

        return NULL;
  
}

static in_cache_entry *in_cache_search_by_vcc(struct atm_vcc *vcc, 
					      struct mpoa_client *client )
{
        unsigned long flags;
        in_cache_entry *entry;

        read_lock_irqsave(&client->ingress_lock, flags);
        entry = client->in_cache;
        while(entry != NULL){
	        if(entry->shortcut == vcc) {
                        read_unlock_irqrestore(&client->ingress_lock, flags);
		        return entry;
                }
		entry = entry->next;
        }
        read_unlock_irqrestore(&client->ingress_lock, flags);

        return NULL;
}

static in_cache_entry *new_in_cache_entry(uint32_t dst_ip, 
					  struct mpoa_client *client)
{
        unsigned long flags;
        unsigned char *ip = (unsigned char *)&dst_ip;
        in_cache_entry* entry = kmalloc(sizeof(in_cache_entry), GFP_KERNEL);

        if (entry == NULL) {
                printk("mpoa: mpoa_caches.c: new_in_cache_entry: out of memory\n");
                return NULL;
        }

        dprintk("mpoa: mpoa_caches.c: adding an ingress entry, ip = %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
        memset(entry,0,sizeof(in_cache_entry));

        dprintk("mpoa: mpoa_caches.c: new_in_cache_entry: about to lock\n");
        write_lock_irqsave(&client->ingress_lock, flags);
        entry->next = client->in_cache;
        entry->prev = NULL;
        if (client->in_cache != NULL)
                client->in_cache->prev = entry;
        client->in_cache = entry;
        write_unlock_irqrestore(&client->ingress_lock, flags);
        dprintk("mpoa: mpoa_caches.c: new_in_cache_entry: unlocked\n");

        memcpy(entry->MPS_ctrl_ATM_addr, client->mps_ctrl_addr, ATM_ESA_LEN);
        entry->ctrl_info.in_dst_ip = dst_ip;
        do_gettimeofday(&(entry->tv));
        entry->retry_time = client->parameters.mpc_p4;
        entry->count = 1;
        entry->entry_state = INGRESS_INVALID; 
        entry->ctrl_info.holding_time = HOLDING_TIME_DEFAULT;

	return entry;
}

static int cache_hit( in_cache_entry * entry, struct mpoa_client *mpc)
{
        struct atm_mpoa_qos *qos;
        struct k_message msg;

        entry->count++;
	if(entry->entry_state == INGRESS_RESOLVED && entry->shortcut != NULL)
                return OPEN;

	if(entry->entry_state == INGRESS_REFRESHING){
	        if(entry->count > mpc->parameters.mpc_p1){
		        msg.type = SND_MPOA_RES_RQST;
			msg.content.in_info = entry->ctrl_info;
			memcpy(msg.MPS_ctrl, mpc->mps_ctrl_addr, ATM_ESA_LEN);
                        qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
                        if (qos != NULL) msg.qos = qos->qos;
			msg_to_mpoad(&msg, mpc);
			do_gettimeofday(&(entry->reply_wait));
			entry->entry_state = INGRESS_RESOLVING;
		}
		if(entry->shortcut != NULL)
                        return OPEN;
		return CLOSED;
	}

        if(entry->entry_state == INGRESS_RESOLVING && entry->shortcut != NULL)
	        return OPEN;

        if( entry->count > mpc->parameters.mpc_p1 &&
            entry->entry_state == INGRESS_INVALID){
                unsigned char *ip = (unsigned char *)&entry->ctrl_info.in_dst_ip;

		dprintk("mpoa: (%s) mpoa_caches.c: threshold exceeded for ip %u.%u.%u.%u, sending MPOA res req\n", mpc->dev->name, ip[0], ip[1], ip[2], ip[3]);
                entry->entry_state = INGRESS_RESOLVING;
                msg.type =  SND_MPOA_RES_RQST;
                memcpy(msg.MPS_ctrl, mpc->mps_ctrl_addr, ATM_ESA_LEN );
                msg.content.in_info = entry->ctrl_info;
                qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
                if (qos != NULL) msg.qos = qos->qos;
                msg_to_mpoad( &msg, mpc);
                do_gettimeofday(&(entry->reply_wait));
        }

        return CLOSED;
}

/*
 * If there are no more references to vcc in egress cache,
 * we are ready to close it.
 */
static void close_unused_egress_vcc(struct atm_vcc *vcc, struct mpoa_client *mpc)
{
	if (vcc == NULL)
		return;

	dprintk("mpoa: mpoa_caches.c: close_unused_egress_vcc:\n");
        if (mpc->eg_ops->search_by_vcc(vcc, mpc) != NULL)
                return;                             /* entry still in use */

	atm_async_release_vcc(vcc, -EPIPE); /* nobody uses this VCC anymore, close it */
	dprintk("mpoa: mpoa_caches.c: close_unused_egress_vcc, closed one:\n");

	return;
}

/*
 * This should be called with write lock on
 */
static int in_cache_remove( in_cache_entry *entry,
                            struct mpoa_client *client )
{
	struct atm_vcc *vcc;
        struct k_message msg;
        unsigned char *ip;

        if(entry == NULL)
                return 0;

	vcc = entry->shortcut;
        ip = (unsigned char *)&entry->ctrl_info.in_dst_ip;
        dprintk("mpoa: mpoa_caches.c: removing an ingress entry, ip = %u.%u.%u.%u\n",ip[0], ip[1], ip[2], ip[3]);

        if (entry->prev != NULL)
                entry->prev->next = entry->next;
        else
                client->in_cache = entry->next;
        if (entry->next != NULL)
                entry->next->prev = entry->prev;
        memset(entry, 0, sizeof(in_cache_entry));
        kfree(entry);
	if(client->in_cache == NULL && client->eg_cache == NULL){
	        msg.type = STOP_KEEP_ALIVE_SM;
		msg_to_mpoad(&msg,client);
	}

	close_unused_egress_vcc(vcc, client);
        return 1;
} 


/* Call this every MPC-p2 seconds... Not exactly correct solution, 
   but an easy one... */

static void clear_count_and_expired(struct mpoa_client *client)
{
        unsigned char *ip;
        unsigned long flags;
        in_cache_entry *entry, *next_entry;
        struct timeval now;

	do_gettimeofday(&now);
        
        write_lock_irqsave(&client->ingress_lock, flags);
        entry = client->in_cache;
        while(entry != NULL){
                entry->count=0;
                next_entry = entry->next;
                if((now.tv_sec - entry->tv.tv_sec) 
                   > entry->ctrl_info.holding_time){
		        ip = (unsigned char*)&entry->ctrl_info.in_dst_ip;
		        dprintk("mpoa: mpoa_caches.c: holding time expired, ip = %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
                        in_cache_remove(entry, client);
                }
                entry = next_entry;
        }
        write_unlock_irqrestore(&client->ingress_lock, flags);

        return;
}

/* Call this every MPC-p4 seconds. */ 

static void check_resolving_entries( struct mpoa_client * client )
{

        struct atm_mpoa_qos *qos;
        unsigned long flags;
        in_cache_entry *entry;
        struct timeval now;
        struct k_message msg;

	do_gettimeofday( &now );

        read_lock_irqsave(&client->ingress_lock, flags);
        entry = client->in_cache;
        while( entry != NULL ){
                if(entry->entry_state == INGRESS_RESOLVING){
                        if(now.tv_sec - entry->hold_down.tv_sec < client->parameters.mpc_p6){
                                entry = entry->next;                      /* Entry in hold down */
                                continue;
                        }
                        if( (now.tv_sec - entry->reply_wait.tv_sec) >
                            entry->retry_time ){
                                entry->retry_time = MPC_C1*( entry->retry_time );
                                if(entry->retry_time > client->parameters.mpc_p5){
				        /* Retry time maximum exceeded, put entry in hold down. */  
				        do_gettimeofday(&(entry->hold_down)); 
                                        entry->retry_time = client->parameters.mpc_p4; 
                                        entry = entry->next;
                                        continue;
                                }
				/* Ask daemon to send a resolution request. */
                                memset(&(entry->hold_down),0,sizeof(struct timeval));
                                msg.type = SND_MPOA_RES_RTRY;
                                memcpy(msg.MPS_ctrl, client->mps_ctrl_addr, ATM_ESA_LEN);
                                msg.content.in_info = entry->ctrl_info;
                                qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
                                if (qos != NULL) msg.qos = qos->qos;
                                msg_to_mpoad(&msg, client);
                                do_gettimeofday(&(entry->reply_wait));
                        }
                }
                entry = entry->next;
        }
        read_unlock_irqrestore(&client->ingress_lock, flags);
}

/* Call this every MPC-p5 seconds. */

static void refresh_entries( struct mpoa_client * client )
{
        unsigned long flags;
        struct timeval now;
        struct in_cache_entry *entry = client->in_cache;
        
        ddprintk("mpoa: mpoa_caches.c: refresh_entries\n");
	do_gettimeofday(&now);

        read_lock_irqsave(&client->ingress_lock, flags);
        while( entry != NULL ){
                if( entry->entry_state == INGRESS_RESOLVED ){
                        if(!(entry->refresh_time))
			        entry->refresh_time = (2*(entry->ctrl_info.holding_time))/3;
			if( (now.tv_sec - entry->reply_wait.tv_sec) > entry->refresh_time ){
                                dprintk("mpoa: mpoa_caches.c: refreshing an entry.\n");
				entry->entry_state = INGRESS_REFRESHING;
                                
			}
                }
                entry = entry->next;
        }
        read_unlock_irqrestore(&client->ingress_lock, flags);
}

static eg_cache_entry *eg_cache_search_by_cache_id(uint32_t cache_id,
						  struct mpoa_client *client)
{
        eg_cache_entry *entry;
        unsigned long flags;

        read_lock_irqsave(&client->egress_lock, flags);
        entry = client->eg_cache;
        while(entry != NULL){
                if( entry->ctrl_info.cache_id == cache_id){
                        read_unlock_irqrestore(&client->egress_lock, flags);
                        return entry;
                }
                entry = entry->next;
        }
	read_unlock_irqrestore(&client->egress_lock, flags);

        return NULL;
}

static eg_cache_entry *eg_cache_search_by_tag(uint32_t tag,
					      struct mpoa_client *client)
{
        unsigned long flags;
        eg_cache_entry *entry;

        read_lock_irqsave(&client->egress_lock, flags);
        entry = client->eg_cache;
        while(entry != NULL){
                if( entry->ctrl_info.tag == tag){
                        read_unlock_irqrestore(&client->egress_lock, flags);
                        return entry;
                }
                entry = entry->next;
        }
	read_unlock_irqrestore(&client->egress_lock, flags);

        return NULL;
}

static eg_cache_entry *eg_cache_search_by_vcc(struct atm_vcc *vcc,
                                               struct mpoa_client *client )
{
        unsigned long flags;
        eg_cache_entry *entry;

        read_lock_irqsave(&client->egress_lock, flags);
        entry = client->eg_cache;
        while( entry != NULL ){
                if( entry->shortcut == vcc ) {
               	        read_unlock_irqrestore(&client->egress_lock, flags);
                        return entry;
		}
                entry = entry->next;
        }
	read_unlock_irqrestore(&client->egress_lock, flags);

        return NULL;
}

static eg_cache_entry *eg_cache_search_by_src_ip(uint32_t ipaddr,
						 struct mpoa_client *client)
{
        unsigned long flags;
        eg_cache_entry *entry;

        read_lock_irqsave(&client->egress_lock, flags);
        entry = client->eg_cache;
        while( entry != NULL ){
                if(entry->latest_ip_addr == ipaddr) {
                        break;
		}
                entry = entry->next;
        }
	read_unlock_irqrestore(&client->egress_lock, flags);

        return entry;
}

/*
 * If there are no more references to vcc in ingress cache,
 * we are ready to close it.
 */
static void close_unused_ingress_vcc(struct atm_vcc *vcc, struct mpoa_client *mpc)
{
	if (vcc == NULL)
		return;

	dprintk("mpoa: mpoa_caches.c: close_unused_ingress_vcc:\n");
        if (mpc->in_ops->search_by_vcc(vcc, mpc) != NULL)
                return;                             /* entry still in use */

	atm_async_release_vcc(vcc, -EPIPE); /* nobody uses this VCC anymore, close it */
	dprintk("mpoa: mpoa_caches.c: close_unused_ingress_vcc:, closed one\n");

	return;
}
/*
 * This should be called with write lock on
 */
static int eg_cache_remove(eg_cache_entry *entry, 
                           struct mpoa_client *client)
{
	struct atm_vcc *vcc;
        struct k_message msg; 
        if(entry == NULL)
                return 0;

	vcc = entry->shortcut;
        dprintk("mpoa: mpoa_caches.c: removing an egress entry.\n");
        if (entry->prev != NULL)
                entry->prev->next = entry->next;
        else
                client->eg_cache = entry->next;
        if (entry->next != NULL)
                entry->next->prev = entry->prev;
        memset(entry, 0, sizeof(eg_cache_entry));
        kfree(entry);
	if(client->in_cache == NULL && client->eg_cache == NULL){
	        msg.type = STOP_KEEP_ALIVE_SM;
		msg_to_mpoad(&msg,client);
	}

	close_unused_ingress_vcc(vcc, client);

        return 1;
} 

static eg_cache_entry *new_eg_cache_entry(struct k_message *msg, struct mpoa_client *client)
{
        unsigned long flags;
        unsigned char *ip;
        eg_cache_entry *entry = kmalloc(sizeof(eg_cache_entry), GFP_KERNEL);

        if (entry == NULL) {
                printk("mpoa: mpoa_caches.c: new_eg_cache_entry: out of memory\n");
                return NULL;
	}

        ip = (unsigned char *)&msg->content.eg_info.eg_dst_ip;
        dprintk("mpoa: mpoa_caches.c: adding an egress entry, ip = %d.%d.%d.%d, this should be our IP\n", ip[0], ip[1], ip[2], ip[3]);
        memset(entry, 0, sizeof(eg_cache_entry));

        dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry: about to lock\n");
        write_lock_irqsave(&client->egress_lock, flags);
        entry->next = client->eg_cache;
        entry->prev = NULL;
        if (client->eg_cache != NULL)
                client->eg_cache->prev = entry;
        client->eg_cache = entry;
        write_unlock_irqrestore(&client->egress_lock, flags);
        dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry: unlocked\n");

        memcpy(entry->MPS_ctrl_ATM_addr, client->mps_ctrl_addr, ATM_ESA_LEN);
        entry->ctrl_info = msg->content.eg_info;
        do_gettimeofday(&(entry->tv));
        entry->entry_state = EGRESS_RESOLVED;
        dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry cache_id %lu\n", ntohl(entry->ctrl_info.cache_id));
        ip = (unsigned char *)&entry->ctrl_info.mps_ip;
        dprintk("mpoa: mpoa_caches.c: mps_ip = %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
        return entry;
}

static void update_eg_cache_entry(eg_cache_entry * entry, uint16_t holding_time)
{
        do_gettimeofday(&(entry->tv));
        entry->entry_state = EGRESS_RESOLVED;
        entry->ctrl_info.holding_time = holding_time;

        return;
}

static void clear_expired(struct mpoa_client *client){
        eg_cache_entry *entry, *next_entry;
        unsigned long flags;
        struct timeval now;
	struct k_message msg;

        do_gettimeofday(&now);

        write_lock_irqsave(&client->egress_lock, flags);
        entry = client->eg_cache;
        while(entry != NULL){
	        next_entry = entry->next;
                if((now.tv_sec - entry->tv.tv_sec) 
                   > entry->ctrl_info.holding_time){
		        msg.type = SND_EGRESS_PURGE;
			msg.content.eg_info = entry->ctrl_info;
		        dprintk("mpoa: mpoa_caches.c: egress_cache: holding time expired, cache_id = %lu.\n",ntohl(entry->ctrl_info.cache_id));
			msg_to_mpoad(&msg, client);
                        eg_cache_remove(entry, client);
                }
                entry = next_entry;
        }
        write_unlock_irqrestore(&client->egress_lock, flags);

        return;
}



static struct in_cache_ops ingress_ops = {
        new_in_cache_entry,               /* new_entry        */
        in_cache_search,                  /* search           */
        in_cache_search_with_mask,        /* search_with_mask */
	in_cache_search_by_vcc,           /* search_by_vcc    */
        cache_hit,                        /* cache_hit        */
        in_cache_remove,                  /* cache_remove     */
        clear_count_and_expired,          /* clear_count      */
        check_resolving_entries,          /* check_resolving  */
        refresh_entries,                  /* refresh          */
};

static struct eg_cache_ops egress_ops = {
        new_eg_cache_entry,               /* new_entry          */
        eg_cache_search_by_cache_id,      /* search_by_cache_id */
        eg_cache_search_by_tag,           /* search_by_tag      */ 
        eg_cache_search_by_vcc,           /* search_by_vcc      */
        eg_cache_search_by_src_ip,        /* search_by_src_ip   */
        eg_cache_remove,                  /* cache_remove       */
        update_eg_cache_entry,            /* update             */
	clear_expired                     /* clear_expired      */
};


void atm_mpoa_init_cache(struct mpoa_client *mpc)
{
        mpc->in_ops = &ingress_ops;
        mpc->eg_ops = &egress_ops;

        return;
}
