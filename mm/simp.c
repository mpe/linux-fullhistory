#define NULL 0
/*
 * mm/simp.c  -- simple allocator for cached objects
 *
 * (C) 1997 Thomas Schoebel-Theuer
 */

#include <linux/simp.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <asm/spinlock.h>

/* The next two defines can be independently enabled for debugging */
/*#define DEBUG*/
/*#define DEAD_BEEF*/

#ifdef DEAD_BEEF
#define DEBUG_BEEF 1
#else
#define DEBUG_BEEF 0
#endif

#ifdef __SMP__
#define NR_PROCESSORS  NR_CPUS
#define GLOBAL_SIZE CHUNK_SIZE
#else
#define NR_PROCESSORS  1
#define GLOBAL_SIZE PAGE_SIZE
#endif

#define POSTBUFFER_SIZE 63
#define ORDER 2
#define CHUNK_SIZE (PAGE_SIZE*(1<<ORDER))
#define CHUNK_BASE(ptr) (struct header*)(((unsigned long)(ptr)) & ~(CHUNK_SIZE-1))
#define CHUNK_END(hdr) (void**)((char*)(hdr) + CHUNK_SIZE)

#define COLOR_INCREMENT (8*sizeof(void*)) /* should be 1 cache line */
#define ALIGN_CACHE(adr) ((((((unsigned long)adr) - 1) / COLOR_INCREMENT) + 1) * COLOR_INCREMENT)
#define HEADER_SIZE ALIGN_CACHE(sizeof(struct header))
#define ELEM_SIZE ALIGN_CACHE(sizeof(struct elem))
#define FILL_TYPE(name,wrongsize) char name[ALIGN_CACHE(wrongsize)-(wrongsize)]

#define MAX_SIMPS ((GLOBAL_SIZE / sizeof(struct simp)) - 1)

struct header { /* this is at the beginning of each memory region */
	/* 1st cache line */
	void ** index;
	void ** fresh;
	struct simp * father;
	void ** emptypos;
	struct header * next;
	structor again_ctor;
	structor first_ctor;
	void * fill[1];
#ifdef DEBUG
	/* 2nd cache line */
	char magic[32];
#endif
};

struct per_processor {
	void ** buffer_pos;
	void * postbuffer[POSTBUFFER_SIZE];
};

struct simp {
	/* 1st cache lines */
	struct per_processor private[NR_PROCESSORS];
	/* next cache line */
	struct header * usable_list;
	spinlock_t lock;
	char fill[sizeof(void*) - sizeof(spinlock_t)];
	long real_size;
	long max_elems;
	structor again_ctor;
	structor first_ctor;
	structor dtor;
	long fill2;
	/* next cache line */
	long create_offset;
	long color;
	long max_color;
	long size;
	long fill3[4];
	/* next cache line */
	char name[32];
};

struct global_data {
	/* 1st cache line */
	long changed_flag;
	long nr_simps;
	spinlock_t lock;
	char fill[(6+8)*sizeof(void*)+sizeof(void*)-sizeof(spinlock_t)];
	/* rest */
	struct simp simps[MAX_SIMPS];
};

static struct global_data * global = NULL;

#ifdef DEBUG
static char global_magic[32] = "SIMP header SdC581oi9rY20051962\n";
#endif

struct simp * simp_create(char * name, long size,
			  structor first_ctor, 
			  structor again_ctor, 
			  structor dtor)
{
	struct simp * simp;
	long fraction;
	long real_size;
	int cpu;

	if(!global) {
#ifdef __SMP__
		global = (struct global_data*)__get_free_pages(GFP_KERNEL, ORDER, 0);
		memset(global, 0, CHUNK_SIZE);
#else
		global = (struct global_data*)get_free_page(GFP_KERNEL);
#endif
		spin_lock_init(&global->lock);
	}

	spin_lock(&global->lock);
	simp = &global->simps[global->nr_simps++];
	spin_unlock(&global->lock);

	if(global->nr_simps >= MAX_SIMPS) {
		printk("SIMP: too many simps allocated\n");
		return NULL;
	}
	memset(simp, 0, sizeof(struct simp));
	spin_lock_init(&simp->lock);
	strncpy(simp->name, name, 15);
	simp->size = size;
	simp->real_size = real_size = ALIGN_CACHE(size);
	/* allow aggregation of very small objects in 2-power fractions of
	 * cachelines */
	fraction = COLOR_INCREMENT / 2;
	while(size <= fraction && fraction >= sizeof(void*)) {
		simp->real_size = fraction;
		fraction >>= 1;
	}
	simp->first_ctor = first_ctor;
	simp->again_ctor = again_ctor;
	simp->dtor = dtor;
	
	real_size += sizeof(void*);
	simp->max_elems = (CHUNK_SIZE - HEADER_SIZE) / real_size;
	simp->max_color = (CHUNK_SIZE - HEADER_SIZE) % real_size;
	for(cpu = 0; cpu < NR_PROCESSORS; cpu++) {
		struct per_processor * private = &simp->private[cpu];
		private->buffer_pos = private->postbuffer;
	}
	return simp;
}

/* Do *not* inline this, it clobbers too many registers... */
static void alloc_header(struct simp * simp)
{
	struct header * hdr;
	char * ptr;
	void ** index;
	long count;

	spin_unlock(&simp->lock);
	for(;;) {
		hdr = (struct header*)__get_free_pages(GFP_KERNEL, ORDER, 0);
		if(hdr)
			break;
		if(!simp_garbage())
			return;
	}
#ifdef DEBUG
	if(CHUNK_BASE(hdr) != hdr)
		panic("simp: bad kernel page alignment");
#endif

	memset(hdr, 0, HEADER_SIZE);
#ifdef DEBUG
	memcpy(hdr->magic, global_magic, sizeof(global_magic));
#endif
	hdr->father = simp;
	hdr->again_ctor = simp->again_ctor;
	hdr->first_ctor = simp->first_ctor;
	
	/* note: races on simp->color don't produce any error :-) */
	ptr = ((char*)hdr) + HEADER_SIZE + simp->color;
	index = CHUNK_END(hdr);
	for(count = 0; count < simp->max_elems; count++) {
		*--index = ptr;
		ptr += simp->real_size;
		/* note: constructors are not called here in bunch but
		 * instead at each single simp_alloc(), in order
		 * to maximize chances that the cache will be
		 * polluted after a simp_alloc() anyway,
		 * and not here. */
	}
	hdr->index = hdr->fresh = hdr->emptypos = index;

	spin_lock(&simp->lock);
	simp->color += COLOR_INCREMENT;
	if(simp->color >= simp->max_color)
		simp->color = 0;
	hdr->next = simp->usable_list;
	simp->usable_list = hdr;
}


/* current x86 memcpy() is horribly moving around registers for nothing,
 * is doing unnecessary work if the size is dividable by a power-of-two,
 * and it clobbers way too many registers.
 * This results in nearly any other register being transfered to stack.
 * Fixing this would be a major win for the whole kernel!
 */
static void ** bunch_alloc(struct simp * simp, void ** buffer)
{
	struct header * hdr;
	void ** index;
	void ** to;
	void ** end;
	structor todo;
	long length;

	spin_lock(&simp->lock);
	hdr = simp->usable_list;
	if(!hdr) {
		alloc_header(simp);
		hdr = simp->usable_list;
		if(!hdr) {
			spin_unlock(&simp->lock);
			*buffer = NULL;
			return buffer+1;
		}
	}
	
	index = hdr->index;
	end = hdr->fresh;
	todo = hdr->again_ctor;
	if(index == end) {
		end = CHUNK_END(hdr);
		todo = hdr->first_ctor;
	}
	to = index + POSTBUFFER_SIZE/2;
	if(to >= end) {
		to = end;
		if(to == CHUNK_END(hdr)) {
			simp->usable_list = hdr->next;
			hdr->next = NULL;
		}
	}
	if(to > hdr->fresh)
		hdr->fresh = to;
	hdr->index = to;
	length = ((unsigned long)to) - (unsigned long)index;
	to = buffer + (length/sizeof(void**));

	memcpy(buffer, index, length);

	spin_unlock(&simp->lock);

	if(todo) {
		do {
			todo(*buffer++);
		} while(buffer < to);
	}
	return to;
}

void * simp_alloc(struct simp * simp)
{
#ifdef __SMP__
	const long cpu = smp_processor_id();
	struct per_processor * priv = &simp->private[cpu];
#else
#define priv (&simp->private[0]) /*fool gcc to use no extra register*/
#endif
	void ** buffer_pos = priv->buffer_pos;
	void * res;

	if(buffer_pos == priv->postbuffer) {
		buffer_pos = bunch_alloc(simp, buffer_pos);
	}
	buffer_pos--;
	res = *buffer_pos;
	priv->buffer_pos = buffer_pos;
	return res;
}

#ifdef DEBUG
long check_header(struct header * hdr, void * ptr)
{
	void ** test;

	if(!hdr) {
		printk("SIMP: simp_free() with NULL pointer\n");
		return 1;
	}
	if(strncmp(hdr->magic, global_magic, 32)) {
		printk("SIMP: simpe_free() with bad ptr %p, or header corruption\n", ptr);
		return 1;
	}
	/* This is brute force, but I don't want to pay for any
	 * overhead if debugging is not enabled, in particular
	 * no space overhead for keeping hashtables etc. */
	test = hdr->index;
	while(test < CHUNK_END(hdr)) {
		if(*test++ == ptr) {
			printk("SIMP: trying to simp_free(%p) again\n", ptr);
			return 1;
		}
	}
	return 0;
}
#endif

static void ** bunch_free(struct simp * simp, void ** buffer)
{
	void ** stop;

	stop = buffer - POSTBUFFER_SIZE/3;

	spin_lock(&simp->lock);
	while(buffer > stop) {
		void * elem = buffer[-1];
		struct header * hdr = CHUNK_BASE(elem);
		void ** index = hdr->index;
		index--;
		hdr->index = index;
		*index = elem;
		if(!hdr->next) {
			hdr->next = simp->usable_list;
			simp->usable_list = hdr;
		}

		buffer -= 2;
		elem = *buffer;
		hdr = CHUNK_BASE(elem);
		index = hdr->index;
		index--;
		hdr->index = index;
		*index = elem;
		if(!hdr->next) {
			hdr->next = simp->usable_list;
			simp->usable_list = hdr;
		}
	}
	spin_unlock(&simp->lock);
	global->changed_flag = 1;
	return buffer;
}

void simp_free(void * objp)
{
	struct header * hdr;
	void ** buffer_pos;
	struct per_processor * private;
#ifdef __SMP__
	const long cpu = smp_processor_id();
#else
	const long cpu = 0;
#endif

	hdr = CHUNK_BASE(objp);
#ifdef DEBUG
	if(check_header(hdr, objp))
		return;
#endif

	private = &hdr->father->private[cpu];
	buffer_pos = private->buffer_pos;
	if(buffer_pos >= private->postbuffer+POSTBUFFER_SIZE) {
		buffer_pos = bunch_free(hdr->father, buffer_pos);
	}

	*buffer_pos++ = objp;
	private->buffer_pos = buffer_pos;

#ifdef DEAD_BEEF
	{
		unsigned int * ptr = (unsigned int*)objp;
		int count = (hdr->father->real_size - ELEM_SIZE) / sizeof(unsigned int);
		while(count--)
			*ptr++ = 0xdeadbeef;
	}
#endif
}

long simp_garbage(void)
{
	int i;
	int res;

	if(!global->changed_flag)
		return 0; /* shortcut */
	/* Note: costs do not matter here. Any heavy thrashing of
	 * simp chunks that could be caused by pools stealing each
	 * other's memory has to be considered a BUG :-)
	 * Simply avoid memory shortages by conservative allocating
	 * policies.
	 */
	global->changed_flag = 0;
	res = 0;
	for(i = 0; i < global->nr_simps; i++) {
		struct simp * simp = &global->simps[i];
		struct header ** base = &simp->usable_list;
		struct header * del;

		spin_lock(&simp->lock);
		del = *base;
		while(del) {
			if(del->index == del->emptypos) {
				if(simp->dtor) {
					void ** ptr = del->index;
					while(ptr < CHUNK_END(del)) {
						simp->dtor(*ptr++);
					}
				}
				*base = del->next;
#ifdef DEBUG
				memset(del, 0, CHUNK_SIZE);
#endif
				free_pages((unsigned long)del, ORDER);
				res++;
			} else
				base = &del->next;
			del = *base;
		}
		spin_unlock(&simp->lock);
	}
	return res;
}

