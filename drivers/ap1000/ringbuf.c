  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 *  linux/drivers/ap1000/ringbuf.c
 *
 * This provides the /proc/XX/ringbuf interface to the Tnet ring buffer
 */
#define _APLIB_
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include <asm/ap1000/pgtapmmu.h>
#include <asm/ap1000/apreg.h>
#include <asm/ap1000/apservice.h>


/* we have a small number of reserved ring buffers to ensure that at
   least one parallel program can always run */
#define RBUF_RESERVED 4
#define RBUF_RESERVED_ORDER 5
static struct {
	char *rb_ptr;
	char *shared_ptr;
	int used;
} reserved_ringbuf[RBUF_RESERVED];


void ap_ringbuf_init(void)
{
	int i,j;
	char *rb_ptr, *shared_ptr;
	int rb_size = PAGE_SIZE * (1<<RBUF_RESERVED_ORDER);

	/* preallocate some ringbuffers */
	for (i=0;i<RBUF_RESERVED;i++) {
		if (!(rb_ptr = (char *)__get_free_pages(GFP_ATOMIC,RBUF_RESERVED_ORDER))) {
			printk("failed to preallocate ringbuf %d\n",i);
			return;
		}
		for (j = MAP_NR(rb_ptr); j <= MAP_NR(rb_ptr+rb_size-1); j++) {
			set_bit(PG_reserved,&mem_map[j].flags);
		}

		if (!(shared_ptr = (char *)__get_free_page(GFP_ATOMIC))) {
			printk("failed to preallocate shared ptr %d\n",i);
                        return;
		}
		set_bit(PG_reserved,&mem_map[MAP_NR(shared_ptr)].flags);

		reserved_ringbuf[i].used = 0;
		reserved_ringbuf[i].rb_ptr = rb_ptr;
		reserved_ringbuf[i].shared_ptr = shared_ptr;
	}
}



void exit_ringbuf(struct task_struct *tsk)
{
	int i;
	
	if (!tsk->ringbuf) return;
	
	if (tsk->ringbuf->ringbuf) {
		char *rb_ptr = tsk->ringbuf->ringbuf;
		char *shared_ptr = tsk->ringbuf->shared;
		int order = tsk->ringbuf->order;
		int rb_size = PAGE_SIZE * (1<<order);

		for (i=0;i<RBUF_RESERVED;i++)
			if (rb_ptr == reserved_ringbuf[i].rb_ptr) break;
		
		if (i < RBUF_RESERVED) {
			reserved_ringbuf[i].used = 0;
		} else {
			for (i = MAP_NR(rb_ptr); i <= MAP_NR(rb_ptr+rb_size-1); i++) {
				clear_bit(PG_reserved, &mem_map[i].flags);
			}
			free_pages((unsigned)rb_ptr,order);

			i = MAP_NR(shared_ptr);
			clear_bit(PG_reserved,&mem_map[i]);
			free_page((unsigned)shared_ptr);
		}
	}

	kfree_s(tsk->ringbuf,sizeof(*(tsk->ringbuf)));
	tsk->ringbuf = NULL;
}


/*
 * map the ring buffer into users memory
 */
static int cap_map(int rb_size)
{
	struct task_struct *tsk=current;
	int i;
	char *rb_ptr=NULL;
	char *shared_ptr=NULL;
	int order = 0;
	int error,old_uid;

	error = verify_area(VERIFY_WRITE,(char *)RBUF_VBASE,rb_size);
	if (error) return error;
	
	if (!MPP_IS_PAR_TASK(tsk->taskid)) {
		printk("ringbuf_mmap called from non-parallel task\n");
		return -EINVAL;
	}

	
	if (tsk->ringbuf) return -EINVAL;

	rb_size -= RBUF_RING_BUFFER_OFFSET;
	rb_size >>= 1;

	switch (rb_size/1024) {
	case 128:
		order = 5;
		break;
	case 512:
		order = 7;
		break;
	case 2048:
		order = 9;
		break;
	case 8192:
		order = 11;
		break;
	default:
		printk("ringbuf_mmap with invalid size %d\n",rb_size);
		return -EINVAL;
	}
	  
	if (order == RBUF_RESERVED_ORDER) {
		for (i=0;i<RBUF_RESERVED;i++) 
			if (!reserved_ringbuf[i].used) {
				rb_ptr = reserved_ringbuf[i].rb_ptr;
				shared_ptr = reserved_ringbuf[i].shared_ptr;
				reserved_ringbuf[i].used = 1;
				break;
			}
	}
	  
	if (!rb_ptr) {
		rb_ptr = (char *)__get_free_pages(GFP_USER,order);
		if (!rb_ptr) return -ENOMEM;

		for (i = MAP_NR(rb_ptr); i <= MAP_NR(rb_ptr+rb_size-1); i++) {
			set_bit(PG_reserved,&mem_map[i].flags);
		}
		  
		shared_ptr = (char *)__get_free_page(GFP_USER);
		if (!shared_ptr)
			return -ENOMEM;
		set_bit(PG_reserved,&mem_map[MAP_NR(shared_ptr)].flags);
	}

	if (!rb_ptr)
		return -ENOMEM;
  
	memset(rb_ptr,0,rb_size);
	memset(shared_ptr,0,PAGE_SIZE);

	if (remap_page_range(RBUF_VBASE + RBUF_RING_BUFFER_OFFSET, 
			     mmu_v2p((unsigned)rb_ptr),
			     rb_size,APMMU_PAGE_SHARED))
		return -EAGAIN;

	if (remap_page_range(RBUF_VBASE + RBUF_RING_BUFFER_OFFSET + rb_size, 
			     mmu_v2p((unsigned)rb_ptr),
			     rb_size,APMMU_PAGE_SHARED))
		return -EAGAIN;
  
	/* the shared area */
	if (remap_page_range(RBUF_VBASE + RBUF_SHARED_PAGE_OFF,
			     mmu_v2p((unsigned)shared_ptr),
			     PAGE_SIZE,APMMU_PAGE_SHARED))
		return -EAGAIN;

#if 0
	/* lock the ringbuffer in memory */
	old_uid = current->euid;
	current->euid = 0;
	error = sys_mlock(RBUF_VBASE,2*rb_size+RBUF_RING_BUFFER_OFFSET);
	current->euid = old_uid;
	if (error) {
		printk("ringbuffer mlock failed\n");
		return error;
	}
#endif

	/* the queue pages */
#define MAP_QUEUE(offset,phys) \
	io_remap_page_range(RBUF_VBASE + offset, \
			    phys<<PAGE_SHIFT,PAGE_SIZE,APMMU_PAGE_SHARED,0xa)
	
	MAP_QUEUE(RBUF_PUT_QUEUE,  0x00000);
	MAP_QUEUE(RBUF_GET_QUEUE,  0x00001);
	MAP_QUEUE(RBUF_SEND_QUEUE, 0x00040);
	
	MAP_QUEUE(RBUF_XY_QUEUE,   0x00640);
	MAP_QUEUE(RBUF_X_QUEUE,    0x00240);
	MAP_QUEUE(RBUF_Y_QUEUE,    0x00440);
	MAP_QUEUE(RBUF_XYG_QUEUE,  0x00600);
	MAP_QUEUE(RBUF_XG_QUEUE,   0x00200);
	MAP_QUEUE(RBUF_YG_QUEUE,   0x00400);  
	MAP_QUEUE(RBUF_CSI_QUEUE,  0x02004);  
	MAP_QUEUE(RBUF_FOP_QUEUE,  0x02005);  

#undef MAP_QUEUE

	if (!tsk->ringbuf) {
		tsk->ringbuf = (void *)kmalloc(sizeof(*(tsk->ringbuf)),GFP_ATOMIC);
		if (!tsk->ringbuf)
			return -ENOMEM;    
	}
  
	memset(tsk->ringbuf,0,sizeof(*tsk->ringbuf));
	tsk->ringbuf->ringbuf = rb_ptr;
	tsk->ringbuf->shared = shared_ptr;
	tsk->ringbuf->order = order;
	tsk->ringbuf->write_ptr = mmu_v2p((unsigned)rb_ptr)<<1;
	tsk->ringbuf->vaddr = RBUF_VBASE;
  
	memset(tsk->ringbuf->vaddr+RBUF_SHARED_PAGE_OFF,0,PAGE_SIZE);
	{
		struct _kernel_cap_shared *_kernel = 
			(struct _kernel_cap_shared *)tsk->ringbuf->vaddr;
		_kernel->rbuf_read_ptr = (rb_size>>5) - 1;
	}
  
	return 0;
}


static int 
ringbuf_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int numcells, *phys_cells;      
	extern struct cap_init cap_init;

	switch (cmd) {
	case CAP_GETINIT:
		if (copy_to_user((char *)arg,(char *)&cap_init,sizeof(cap_init)))
			return -EFAULT;
		break;

	case CAP_SYNC:
		if (verify_area(VERIFY_READ, (void *) arg, sizeof(int)*2))
			return -EFAULT;
		if (get_user(numcells,(int *)arg)) return -EFAULT;
		if (get_user((unsigned)phys_cells,
			     ((int *)arg)+1)) return -EFAULT;
		if (verify_area(VERIFY_READ,phys_cells,sizeof(int)*numcells))
			return -EFAULT;
		return ap_sync(numcells,phys_cells);
		break;

	case CAP_SETGANG:
		{
			int v;
			if (get_user(v,(int *)arg)) return -EFAULT;
			mpp_set_gang_factor(v);
			break;
		}

	case CAP_MAP:
		return cap_map(arg);
		
	default:
		printk("unknown ringbuf ioctl %d\n",cmd);
		return -EINVAL;
	}
	return 0;
}


static struct file_operations proc_ringbuf_operations = {
	NULL,
	NULL,
	NULL,
	NULL,		/* readdir */
	NULL,		/* poll */
	ringbuf_ioctl,	/* ioctl */
	NULL,           /* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_ringbuf_inode_operations = {
	&proc_ringbuf_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
