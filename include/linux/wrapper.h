#ifndef _WRAPPER_H_
#define _WRAPPER_H_

#define vma_set_inode(v,i)	((v)->vm_inode = (i))
#define vma_get_flags(v)	((v)->vm_flags)
#define vma_get_pgoff(v)	((v)->vm_pgoff)
#define vma_get_start(v)	((v)->vm_start)
#define vma_get_end(v)		((v)->vm_end)
#define vma_get_page_prot(v)	((v)->vm_page_prot)

#define mem_map_reserve(p)	set_bit(PG_reserved, &((p)->flags))
#define mem_map_unreserve(p)	clear_bit(PG_reserved, &((p)->flags))

#endif /* _WRAPPER_H_ */
