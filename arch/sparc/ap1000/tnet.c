  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* routines to control the AP1000 Tnet interface */

#include <asm/ap1000/apreg.h>
#include <asm/ap1000/aplib.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/pgtable.h>
#include <asm/pgtsrmmu.h>
#include <stdarg.h>
#include <linux/skbuff.h>


/* message types for system messages */
#define TNET_IP         0
#define TNET_IP_SMALL   1
#define TNET_RPC        2

static struct {
	int errors;
	int alloc_errors;
	int bytes_received;
	int bytes_sent;
	int packets_received;
	int packets_sent;
	int small_packets_received;
	int small_packets_sent;
} tnet_stats;

extern int cap_cid0;
extern int cap_ncel0;
static u_long xy_global_head;

extern unsigned _ncel, _ncelx, _ncely, _cid, _cidx, _cidy;

extern struct ringbuf_struct system_ringbuf;
extern u_long system_read_ptr;

u_long system_recv_flag = 0;
static u_long system_recv_count = 0;

int *tnet_rel_cid_table;

static int dummy=1;

#define TNET_IP_THRESHOLD 100

void tnet_check_completion(void);
static void reschedule(void);
static u_long tnet_add_completion(void (*fn)(int a1,...), 
				  int a1,int a2);
static void tnet_info(void);

static struct {
  int shift;
  void (*fn)(void);
} iports[4] = {
  {MC_INTP_0_SH,tnet_check_completion},
  {MC_INTP_1_SH,reschedule},
  {MC_INTP_2_SH,NULL},
  {MC_INTP_3_SH,NULL}};

static inline int rel_cid(unsigned dst)
{  
	unsigned dstx, dsty;
	unsigned dx,dy;

	if (dst == _cid) return 0;

	dstx = dst % _ncelx;
	dsty = dst / _ncelx;
	if (dstx >= _cidx)
		dx = dstx - _cidx;
	else
		dx = (_ncelx - _cidx) + dstx;
	
	if (dsty >= _cidy)
		dy = dsty - _cidy;
	else
		dy = (_ncely - _cidy) + dsty;
	
	return (dx<<8) | dy;
}

#define SAVE_PID() \
  unsigned long flags; \
  int saved_pid; \
  save_flags(flags); cli(); \
  saved_pid = MSC_IN(MSC_PID); \
  MSC_OUT(MSC_PID,SYSTEM_CONTEXT);

#define RESTORE_PID() \
  MSC_OUT(MSC_PID,saved_pid); \
  restore_flags(flags);


void ap_put(int dest_cell,u_long local_addr,int size,
	    u_long remote_addr,u_long dest_flag,u_long local_flag)
{
  volatile u_long *entry;
  SAVE_PID();
    
  entry = (volatile u_long *)MSC_PUT_QUEUE_S;

  *entry = tnet_rel_cid_table[dest_cell];
  *entry = ((size+3) >> 2); 
  *entry = (u_long)remote_addr;
  *entry = 0;
  *entry = (u_long)dest_flag;
  *entry = (u_long)local_flag;
  *entry = (u_long)local_addr;
  *entry = 0;
  RESTORE_PID();
}

/* remote_addr is physical 
   local address is virtual 
   both flags are virtual */
void ap_phys_put(int dest_cell,u_long local_addr,int size,
		 u_long remote_addr,u_long dest_flag,u_long local_flag)
{
  volatile u_long *entry;
  SAVE_PID();

  entry = (volatile u_long *)MSC_CPUT_QUEUE_S;

  *entry = tnet_rel_cid_table[dest_cell];
  *entry = ((size+3) >> 2); 
  *entry = (u_long)remote_addr;
  *entry = 0;
  *entry = (u_long)dest_flag;
  *entry = (u_long)local_flag;
  *entry = (u_long)local_addr;
  *entry = 0;
  RESTORE_PID();
}


/* broadcast put - yeah! */
void ap_bput(u_long local_addr,int size,
	     u_long remote_addr,u_long dest_flag,u_long local_flag)
{
  volatile u_long *entry = (volatile u_long *)MSC_XYG_QUEUE_S;
  SAVE_PID();

  *entry = xy_global_head;
  *entry = ((size+3) >> 2);
  *entry = (u_long)remote_addr;
  *entry = 0;
  *entry = (u_long)dest_flag;
  *entry = (u_long)local_flag;
  *entry = (u_long)local_addr;
  *entry = 0;
  RESTORE_PID();
}


/* remote_addr is physical */
void ap_phys_bput(u_long local_addr,int size,
		  u_long remote_addr,u_long dest_flag,u_long local_flag)
{
  volatile u_long *entry = (volatile u_long *)MSC_CXYG_QUEUE_S;
  SAVE_PID();

  *entry = xy_global_head;
  *entry = ((size+3) >> 2);
  *entry = (u_long)remote_addr;
  *entry = 0;
  *entry = (u_long)dest_flag;
  *entry = (u_long)local_flag;
  *entry = (u_long)local_addr;
  *entry = 0;
  RESTORE_PID();
}



void ap_get(int dest_cell,u_long local_addr,int size,
	    u_long remote_addr,u_long local_flag,u_long dest_flag)
{
  volatile u_long *entry;
  SAVE_PID();
    
  entry = (u_long *)MSC_GET_QUEUE_S;

  *entry = tnet_rel_cid_table[dest_cell];
  *entry = (size+3) >> 2;           /* byte --> word */
  *entry = (u_long)local_addr;
  *entry = 0;
  *entry = (u_long)local_flag;
  *entry = (u_long)dest_flag;
  *entry = (u_long)remote_addr;
  *entry = 0;    
  RESTORE_PID();
}


/* local_addr is physical 
   remote_addr is virtual
   both flags are virtual
*/
void ap_phys_get(int dest_cell,u_long local_addr,int size,
		 u_long remote_addr,u_long local_flag,u_long dest_flag)
{
  volatile u_long *entry;
  SAVE_PID();
    
  entry = (u_long *)MSC_CGET_QUEUE_S;

  *entry = tnet_rel_cid_table[dest_cell];
  *entry = (size+3) >> 2;           /* byte --> word */
  *entry = (u_long)local_addr;
  *entry = 0;
  *entry = (u_long)local_flag;
  *entry = (u_long)dest_flag;
  *entry = (u_long)remote_addr;
  *entry = 0;    
  RESTORE_PID();
}


/*
 * copy a message from the ringbuffer - being careful of wraparound
*/
static inline void tnet_copyin(unsigned *dest,unsigned *src,int size)
{
	unsigned *limit = (unsigned *)system_ringbuf.ringbuf + 
		(SYSTEM_RINGBUF_SIZE>>2);
	int size1 = limit - src;

	if (size < size1)
		size1 = size;

	size -= size1;
	while (size1--) {
		*dest++ = *src++;
	}
	src = system_ringbuf.ringbuf;
	while (size--) {
		*dest++ = *src++;
	}
}


/*
  put some data into a tasks ringbuffer. size is in words.
  */
static inline void memcpy_to_rbuf(unsigned tid,unsigned *msgp,unsigned size)
{
	struct aplib_struct *aplib;
	unsigned octx, ctx;
	struct task_struct *tsk;
	unsigned room;

	tsk = task[tid];
	if (!tsk || !tsk->aplib)
		return;

	octx = srmmu_get_context();
	ctx = MPP_TASK_TO_CTX(tid);
	if (octx != ctx)
		srmmu_set_context(ctx);
	aplib = tsk->aplib;

	if (aplib->write_pointer < aplib->read_pointer) 
		room = aplib->read_pointer - (aplib->write_pointer+1);
	else
		room = aplib->ringbuf_size - 
			((aplib->write_pointer+1)-aplib->read_pointer);

	if (room < size) {
		send_sig(SIGLOST,tsk,1);		
		goto finished;
	}

	tnet_copyin(&aplib->ringbuf[aplib->write_pointer], msgp, size);

	aplib->write_pointer += size;
	if (aplib->write_pointer >= aplib->ringbuf_size)
		aplib->write_pointer -= aplib->ringbuf_size;

	aplib->rbuf_flag1++;

finished:
	if (octx != ctx)
		srmmu_set_context(octx);
}



/* a aplib message has arrived on the system message queue - process
   it immediately and return the number of bytes taken by the message in
   the system ringbuffer 

   Note that this function may be called from interrupt level
   */
static inline void aplib_system_recv(unsigned *msgp)
{
	unsigned tag = msgp[1]>>28;
	unsigned size, tid;

	if (tag == RBUF_BIGSEND) {
		aplib_bigrecv(msgp);
		return;
	}

	size = (msgp[0]&0xFFFFF);
	tid = (msgp[1]&0x3FF);

	memcpy_to_rbuf(tid,msgp,size+2);
}


void tnet_ip_complete(struct sk_buff *skb,int from)
{  
#if IP_DEBUG
	char *data = skb->data;
	int i;
	printk("CID(%d) tnet ip complete from %d\n",_cid,from);

	for (i=0;i<skb->len;i+=4)
		printk("(%08x)%c",*(int *)(data+i),i==32?'\n':' ');
	printk("\n");
#endif
	/* ap_phys_put(from,(u_long)&dummy,4,MC_INTP_0,0,0); */
	bif_rx(skb);
	tnet_stats.bytes_received += skb->len;
	tnet_stats.packets_received++;
}


static void tnet_ip_recv(int cid,u_long *info)
{
	u_long flag;
	u_long ipsize = info[1];
	u_long remote_addr = info[0];
	u_long remote_flag = info[2];
	struct sk_buff *skb = dev_alloc_skb(ipsize+8);
	char *p;

	if (!skb) {
		ap_put(cid,0,0,0,remote_flag,0);
		ap_phys_put(cid,(u_long)&dummy,4,MC_INTP_0,0,0);
		tnet_stats.alloc_errors++;
		return;
	}

	skb_reserve(skb,8); /* align on 16 byte boundary */

	flag = tnet_add_completion(tnet_ip_complete,(int)skb,(int)cid);

	p = (char *)skb_put(skb,ipsize);
#if 0
{
	static unsigned count=0;
	if (count%500 == 0)
		printk("CID(%d) fetching %d bytes from %x to %x\n",
		       _cid,ipsize,remote_addr,p);
	count++;
}
#endif
	ap_get(cid,p,ipsize,remote_addr,flag,remote_flag);
	ap_phys_get(cid,MC_INTP_0,4,(u_long)&dummy,0,0);
#if IP_DEBUG
	printk("CID(%d) ip packet of length %ld from %ld\n",_cid,ipsize,cid);
#endif
}


static void tnet_ip_recv_small(u_long *data,int size)
{
	struct sk_buff *skb = dev_alloc_skb(size+8);
	if (skb) {
		skb_reserve(skb,8);
		tnet_copyin((unsigned *)skb_put(skb,size),(unsigned *)data,(size+3)>>2);
		bif_rx(skb);
		tnet_stats.bytes_received += size;
		tnet_stats.packets_received++;
		tnet_stats.small_packets_received++;
	} else {
		tnet_stats.alloc_errors++;
	}
}


/* we've got an RPC from a remote cell */
static void tnet_rpc_recv(u_long *data,int size)
{
	struct fnp {
		void (*fn)();
	} fnp;
	fnp = *(struct fnp *)data;
	fnp.fn(data,size);
}

/*
 * receive messages from the system ringbuffer. We don't bother with
 * all the niceities that are done in user space, we just always
 * process the messages in order 
 */
static inline void tnet_recv(void)
{
	unsigned flags;
	u_long from,*data,fix,align,size1,size,type;

	if (system_recv_flag == system_recv_count) 
		return;

	save_flags(flags); cli();
	while (system_recv_flag != system_recv_count) {
		u_long read_ptr = 
			(system_read_ptr + 1) % (SYSTEM_RINGBUF_SIZE>>5);  
		u_long *msgp = 
			((u_long *)system_ringbuf.ringbuf) + (read_ptr<<3);
		u_long tag = (msgp[1]>>28) & 0xF;
		size1 = (msgp[0]&0xFFFFF)<<2;

		/* move our read pointer past this message */
		system_read_ptr = (system_read_ptr + 
				   ((size1+8+31)>>5))%(SYSTEM_RINGBUF_SIZE>>5);
		system_recv_count++;		


		if (tag != RBUF_SYSTEM) {
			aplib_system_recv(msgp);
			continue;
		} 

		from = msgp[0] >> 22;
		data = msgp+2;
		fix = (msgp[0]>>20)&3;
		align = (msgp[1]>>26)&3;
		size = ((size1 - align) & ~3) | fix;
		type = (msgp[1]&0xFF);

		switch (type) {
		case TNET_IP:
			tnet_ip_recv(from,data);
			break;
			
		case TNET_IP_SMALL:
			tnet_ip_recv_small(data,size);
			break;
			
		case TNET_RPC:
			tnet_rpc_recv(data,size);
			break;
			
		default:
			tnet_stats.errors++;
			printk("unknown Tnet type %ld\n",type);
		}

#if DEBUG
		printk("CID(%d) recvd %d bytes of type %d read_ptr=%x\n",
		       _cid,size,type,system_read_ptr);
#endif
	}
	restore_flags(flags);
}


#define COMPLETION_LIST_LENGTH 256

static unsigned completion_list_rp = 0;
static unsigned completion_list_wp = 0;

static volatile struct completion_struct {
  u_long flag;
  void (*fn)(int a1,...);
  u_long args[2];
} completion_list[COMPLETION_LIST_LENGTH];


void tnet_check_completion(void)
{
	struct completion_struct *cs;
	unsigned flags;

	tnet_recv();  

	if (completion_list[completion_list_rp].flag != 2)
		return;

	save_flags(flags); cli();
  
	while (completion_list[completion_list_rp].flag == 2) {
		cs = &completion_list[completion_list_rp];
		cs->flag = 0;
		if (++completion_list_rp == COMPLETION_LIST_LENGTH)
			completion_list_rp = 0;

		restore_flags(flags);

		cs->fn(cs->args[0],cs->args[1]);

		if (completion_list[completion_list_rp].flag != 2)
			return;

		save_flags(flags); cli();
	}
	
	restore_flags(flags);
}


static u_long tnet_add_completion(void (*fn)(int a1,...),int a1,int a2)
{
        unsigned flags;
        struct completion_struct *cs;
	
	save_flags(flags); cli();

	while (completion_list[completion_list_wp].flag != 0)
		tnet_check_completion();

	cs = &completion_list[completion_list_wp];

	if (++completion_list_wp == COMPLETION_LIST_LENGTH)
		completion_list_wp = 0;

	restore_flags(flags);

        cs->flag = 1;
        cs->fn = fn;
	cs->args[0] = a1;
	cs->args[1] = a2;

        return (u_long)&cs->flag;
}


/* 
 * send a message to the tnet ringuffer on another cell. When the send has
 * completed call fn with the args supplied 
 */
static void tnet_send(long cid,long type,char *src_addr,long byteSize,
		      int immediate,u_long flag)
{
	int wordSize;
	int byteAlign, byteFix;
	u_long src;
	u_long info1, info2;
	volatile u_long *entry = (volatile u_long *)MSC_SEND_QUEUE_S;
	SAVE_PID();

	byteAlign = ((u_long)src_addr) & 0x3;
	byteFix = byteSize & 0x3;
	
	src = (u_long)src_addr & ~3;
	
	wordSize = (byteSize + byteAlign + 3) >> 2;
	
	info1 = (_cid << 22) | (byteFix << 20) | wordSize;
	info2 = (RBUF_SYSTEM<<28) | (byteAlign << 26) | type;
	
	*entry = tnet_rel_cid_table[cid];
	*entry = wordSize;
	*entry = (u_long)&system_recv_flag;
	*entry = flag;
	*entry = (u_long)src;
	*entry = 0;
	*entry = info1;
	*entry = info2;
	RESTORE_PID();
	
	ap_phys_put(cid,(u_long)&dummy,4,MC_INTP_0,0,0);
	if (immediate && flag) 
		ap_phys_put(_cid,(u_long)&dummy,4,MC_INTP_0,0,0);
}


static void free_skb(struct sk_buff *skb, int op)
{
	dev_kfree_skb(skb);
}

void tnet_send_ip(int cid,struct sk_buff *skb)
{
	char *data = skb->data + sizeof(struct cap_request);
	int size = skb->len - sizeof(struct cap_request);
	u_long flag;
#if IP_DEBUG
	int i;
	for (i=0;i<size;i+=4)
		printk("[%08x]%c",*(int *)(data+i),i==32?'\n':' ');
	printk("\n");
#endif
	if (size > TNET_IP_THRESHOLD) {
		int *info = (int *)skb->data; /* re-use the header */
		info[0] = (int)data;
		info[1] = size;
		info[2] = tnet_add_completion(free_skb, (int)skb, 0);
		tnet_send(cid,TNET_IP,info,sizeof(int)*3,0,0);
	} else {
		flag = tnet_add_completion(free_skb, (int)skb, 0);
		tnet_send(cid,TNET_IP_SMALL,data,size,0,flag);
		tnet_stats.small_packets_sent++;
	}
	tnet_stats.packets_sent++;
	tnet_stats.bytes_sent += size;
#if IP_DEBUG
	printk("CID(%d) sent IP of size %d to %d\n",_cid,size,cid);
#endif
}

static void reschedule(void)
{
	current->need_resched = 1;
	mark_bh(TQUEUE_BH);
}


/* make a remote procedure call 
   If free is set then free the data after sending it 
   The first element of data is presumed to be a function pointer
*/
int tnet_rpc(int cell,char *data,int size,int free)
{
	unsigned flag=0;

	if (free) {
		flag = tnet_add_completion(kfree,data,0);
	}

	tnet_send(cell,TNET_RPC,data,size,0,flag);
	return 0;
}


static void iport_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;
	u_long intr = MC_IN(MC_INTR_PORT);

	for (i=0;i<4;i++) {
		if (intr & (AP_INTR_REQ << iports[i].shift)) {
			MC_OUT(MC_INTR_PORT,AP_CLR_INTR_REQ << iports[i].shift);
			if (iports[i].fn) iports[i].fn();
		}
	}
}


void ap_tnet_init(void)
{
	int i;

	bif_add_debug_key('T',tnet_info,"Tnet status");

	memset(completion_list,0,sizeof(completion_list));
	
	request_irq(APIPORT_IRQ, iport_irq, SA_INTERRUPT, "iport", 0);

	for (i=0;i<4;i++) {
		MC_OUT(MC_INTR_PORT,AP_CLR_INTR_REQ << iports[i].shift);
		MC_OUT(MC_INTR_PORT,AP_CLR_INTR_MASK << iports[i].shift);
	}    


	tnet_rel_cid_table = (int *)kmalloc(sizeof(int)*_ncel,GFP_ATOMIC);
	for (i=0;i<_ncel;i++)
		tnet_rel_cid_table[i] = rel_cid(i);

	if(_cid == 0) {
		xy_global_head  = (((_ncelx -1) << 8) & 0xff00) | 
			((_ncely - 1) & 0xff);
	}
	else {
		for(i = 1; i < _ncel; i *= 2){
			if(i & _cid) {	
				int	rcidx = (_cid-i)%_ncelx - _cid%_ncelx;
				int	rcidy = (_cid-i)/_ncelx - _cid/_ncelx;
				xy_global_head  = ((rcidx << 8) & 0xff00) | 
					(rcidy & 0xff);
				break;	
			}
		}
	}
}

static void tnet_info(void)
{
	struct completion_struct *cs;
	
	printk(
	       "errors=%d  alloc_errors=%d
bytes_received=%d  bytes_sent=%d
packets_received=%d  packets_sent=%d
small_received=%d  small_sent=%d
",
	       tnet_stats.errors, tnet_stats.alloc_errors,
	       tnet_stats.bytes_received,
	       tnet_stats.bytes_sent, tnet_stats.packets_received,
	       tnet_stats.packets_sent, tnet_stats.small_packets_received,
	       tnet_stats.small_packets_sent);

	printk("recv_flag=%d recv_count=%d read_ptr=%d\n",
	       system_recv_flag,system_recv_count,system_read_ptr);
	printk("completion_list_rp=%d  completion_list_wp=%d\n",
	       completion_list_rp,completion_list_wp);
}
