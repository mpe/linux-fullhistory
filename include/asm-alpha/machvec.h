#ifndef __ALPHA_MACHVEC_H
#define __ALPHA_MACHVEC_H 1

#include <linux/config.h>
#include <linux/types.h>


/* The following structure vectors all of the I/O and IRQ manipulation
   from the generic kernel to the hardware specific backend.  */

struct task_struct;
struct mm_struct;
struct pt_regs;
struct vm_area_struct;
struct linux_hose_info;

struct alpha_machine_vector
{
	/* This "belongs" down below with the rest of the runtime
	   variables, but it is convenient for entry.S if these 
	   two slots are at the beginning of the struct.  */
	unsigned long hae_cache;
	unsigned long *hae_register;

	int nr_irqs;
	int rtc_port;
	int max_asn;
	unsigned long max_dma_address;
	unsigned long mmu_context_mask;
	unsigned long irq_probe_mask;
	unsigned long iack_sc;

	unsigned long (*mv_virt_to_bus)(void *);
	void * (*mv_bus_to_virt)(unsigned long);

	unsigned int (*mv_inb)(unsigned long);
	unsigned int (*mv_inw)(unsigned long);
	unsigned int (*mv_inl)(unsigned long);

	void (*mv_outb)(unsigned char, unsigned long);
	void (*mv_outw)(unsigned short, unsigned long);
	void (*mv_outl)(unsigned int, unsigned long);
	
	unsigned long (*mv_readb)(unsigned long);
	unsigned long (*mv_readw)(unsigned long);
	unsigned long (*mv_readl)(unsigned long);
	unsigned long (*mv_readq)(unsigned long);

	void (*mv_writeb)(unsigned char, unsigned long);
	void (*mv_writew)(unsigned short, unsigned long);
	void (*mv_writel)(unsigned int, unsigned long);
	void (*mv_writeq)(unsigned long, unsigned long);

	unsigned long (*mv_dense_mem)(unsigned long);

	int (*hose_read_config_byte)(u8, u8, u8, u8 *value,
				     struct linux_hose_info *);
	int (*hose_read_config_word)(u8, u8, u8, u16 *value,
				     struct linux_hose_info *);
	int (*hose_read_config_dword)(u8, u8, u8, u32 *value,
				      struct linux_hose_info *);

	int (*hose_write_config_byte)(u8, u8, u8, u8 value,
				      struct linux_hose_info *);
	int (*hose_write_config_word)(u8, u8, u8, u16 value,
				      struct linux_hose_info *);
	int (*hose_write_config_dword)(u8, u8, u8, u32 value,
				       struct linux_hose_info *);
	
	void (*mv_get_mmu_context)(struct task_struct *);
	void (*mv_flush_tlb_current)(struct mm_struct *);
	void (*mv_flush_tlb_other)(struct mm_struct *);
	void (*mv_flush_tlb_current_page)(struct mm_struct * mm,
					  struct vm_area_struct *vma,
					  unsigned long addr);

	void (*update_irq_hw)(unsigned long, unsigned long, int);
	void (*ack_irq)(unsigned long);
	void (*device_interrupt)(unsigned long vector, struct pt_regs *regs);
	void (*machine_check)(u64 vector, u64 la, struct pt_regs *regs);

	void (*init_arch)(unsigned long *, unsigned long *);
	void (*init_irq)(void);
	void (*init_pit)(void);
	void (*pci_fixup)(void);
	void (*kill_arch)(int, char *);

	const char *vector_name;

	/* System specific parameters.  */
	union {
	    struct {
		unsigned long gru_int_req_bits;
	    } cia;

	    struct {
		unsigned long gamma_bias;
	    } t2;
	} sys;

	/* Runtime variables it is handy to keep close.  */
	unsigned long dma_win_base;
	unsigned long dma_win_size;
	unsigned long sm_base_r1, sm_base_r2, sm_base_r3;
};

extern struct alpha_machine_vector alpha_mv;

#ifdef CONFIG_ALPHA_GENERIC
extern int alpha_using_srm;
extern int alpha_use_srm_setup;
#else
#ifdef CONFIG_ALPHA_SRM
#define alpha_using_srm 1
#else
#define alpha_using_srm 0
#endif
#if defined(CONFIG_ALPHA_SRM_SETUP)
#define alpha_use_srm_setup 1
#else
#define alpha_use_srm_setup 0
#endif
#endif /* GENERIC */

#endif /* __ALPHA_MACHVEC_H */
