
#ifndef _IEEE1394_TYPES_H
#define _IEEE1394_TYPES_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/list.h>
#include <asm/byteorder.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)

#include <linux/wait.h>
#define DECLARE_WAITQUEUE(name, task) struct wait_queue name = { task, NULL }

typedef struct wait_queue *wait_queue_head_t;
typedef struct wait_queue wait_queue_t;

inline static void init_waitqueue_head(wait_queue_head_t *wh)
{
        *wh = NULL;
}

inline static void init_waitqueue_entry(wait_queue_t *wq, struct task_struct *p)
{
        wq->task = p;
        wq->next = NULL;
}

static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
        __list_add(new, head->prev, head);
}

#define __constant_cpu_to_be32(x) __constant_htonl((x))

#define set_current_state(state_value) \
 do { current->state = (state_value); } while (0)


#include <asm/page.h>
/* Pure 2^n version of get_order */
extern __inline__ int get_order(unsigned long size)
{
        int order;
        size = (size-1) >> (PAGE_SHIFT-1);
        order = -1;
        do {
                size >>= 1;
                order++;
        } while (size);
        return order;
}

#include <linux/mm.h>
#include <linux/pci.h>
inline static int pci_enable_device(struct pci_dev *dev)
{
        u16 cmd;
        pci_read_config_word(dev, PCI_COMMAND, &cmd);
        pci_write_config_word(dev, PCI_COMMAND, cmd | PCI_COMMAND_MEMORY);
        return 0;
}

#define PCI_DMA_BIDIRECTIONAL	0
#define PCI_DMA_TODEVICE	1
#define PCI_DMA_FROMDEVICE	2
#define PCI_DMA_NONE            3
#define PCI_ROM_RESOURCE 6
#define pci_resource_start(dev, bar) ((bar) == PCI_ROM_RESOURCE     \
                                      ? (dev)->rom_address          \
                                      : (dev)->base_address[(bar)])
#define BUG() *(int *)0 = 0

#include <asm/io.h>
typedef u32 dma_addr_t;

extern inline int pci_dma_supported(struct pci_dev *hwdev, dma_addr_t mask)
{
	return 1;
}

extern inline void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
                                         dma_addr_t *dma_handle)
{
        void *ret;
        ret = (void *)__get_free_pages(GFP_ATOMIC, get_order(size));
        if (ret) {
                memset(ret, 0, size);
                *dma_handle = virt_to_bus(ret);
        }
        return ret;
}

extern inline void pci_free_consistent(struct pci_dev *hwdev, size_t size,
                                       void *vaddr, dma_addr_t dma_handle)
{
        free_pages((unsigned long)vaddr, get_order(size));
}

extern inline dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr,
					size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	return virt_to_bus(ptr);
}

extern inline void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
				    size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}

#include <asm/scatterlist.h>
extern inline int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
			     int nents, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	return nents;
}

extern inline void pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg,
				int nents, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}

extern inline void pci_dma_sync_single(struct pci_dev *hwdev,
				       dma_addr_t dma_handle,
				       size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}

extern inline void pci_dma_sync_sg(struct pci_dev *hwdev,
				   struct scatterlist *sg,
				   int nelems, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}


#ifndef _LINUX_DEVFS_FS_KERNEL_H
typedef struct devfs_entry * devfs_handle_t;
#define DEVFS_FL_NONE 0

static inline devfs_handle_t devfs_register (devfs_handle_t dir,
					     const char *name,
					     unsigned int flags,
					     unsigned int major,
					     unsigned int minor,
					     umode_t mode,
					     void *ops, void *info)
{
    return NULL;
}
static inline void devfs_unregister (devfs_handle_t de)
{
    return;
}
static inline int devfs_register_chrdev (unsigned int major, const char *name,
					 struct file_operations *fops)
{
    return register_chrdev (major, name, fops);
}
static inline int devfs_unregister_chrdev (unsigned int major,const char *name)
{
    return unregister_chrdev (major, name);
}
#endif /* _LINUX_DEVFS_FS_KERNEL_H */


#define V22_COMPAT_MOD_INC_USE_COUNT MOD_INC_USE_COUNT
#define V22_COMPAT_MOD_DEC_USE_COUNT MOD_DEC_USE_COUNT
#define OWNER_THIS_MODULE

#else /* Linux version < 2.3 */

#define V22_COMPAT_MOD_INC_USE_COUNT do {} while (0)
#define V22_COMPAT_MOD_DEC_USE_COUNT do {} while (0)
#define OWNER_THIS_MODULE owner: THIS_MODULE,

#endif /* Linux version < 2.3 */



#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,18)
#include <asm/spinlock.h>
#else
#include <linux/spinlock.h>
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

typedef u32 quadlet_t;
typedef u64 octlet_t;
typedef u16 nodeid_t;

#define BUS_MASK  0xffc0
#define NODE_MASK 0x003f
#define LOCAL_BUS 0xffc0
#define ALL_NODES 0x003f

#define HPSB_PRINT(level, fmt, args...) printk(level "ieee1394: " fmt "\n" , ## args)

#define HPSB_DEBUG(fmt, args...) HPSB_PRINT(KERN_DEBUG, fmt , ## args)
#define HPSB_INFO(fmt, args...) HPSB_PRINT(KERN_INFO, fmt , ## args)
#define HPSB_NOTICE(fmt, args...) HPSB_PRINT(KERN_NOTICE, fmt , ## args)
#define HPSB_WARN(fmt, args...) HPSB_PRINT(KERN_WARNING, fmt , ## args)
#define HPSB_ERR(fmt, args...) HPSB_PRINT(KERN_ERR, fmt , ## args)

#define HPSB_PANIC(fmt, args...) panic("ieee1394: " fmt "\n" , ## args)

#define HPSB_TRACE() HPSB_PRINT(KERN_INFO, "TRACE - %s, %s(), line %d", __FILE__, __FUNCTION__, __LINE__)


#ifdef __BIG_ENDIAN

static __inline__ void *memcpy_le32(u32 *dest, const u32 *src, size_t count)
{
        void *tmp = dest;

        count /= 4;

        while (count--) {
                *dest++ = swab32p(src++);
        }

        return tmp;
}

#else

static __inline__ void *memcpy_le32(u32 *dest, const u32 *src, size_t count)
{
        return memcpy(dest, src, count);
}

#endif /* __BIG_ENDIAN */

#endif /* _IEEE1394_TYPES_H */
