/*
 *      linux/arch/alpha/kernel/core_polaris.c
 *
 * POLARIS chip-specific code
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/pci.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_polaris.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "bios32.h"

/*
 * BIOS32-style PCI interface:
 */

#ifdef DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#define DEBUG_MCHECK
#ifdef DEBUG_MCHECK
# define DBG_MCK(args)	printk args
/* #define DEBUG_MCHECK_DUMP */
#else
# define DBG_MCK(args)
#endif

static volatile unsigned int POLARIS_mcheck_expected = 0;
static volatile unsigned int POLARIS_mcheck_taken = 0;
static volatile unsigned short POLARIS_jd = 0;

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address.  This is fairly straightforward
 * on POLARIS, since the chip itself generates Type 0 or Type 1
 * cycles automatically depending on the bus number (Bus 0 is
 * hardwired to Type 0, all others are Type 1.  Peer bridges
 * are not supported).
 *
 * All types:
 *
 *  3 3 3 3|3 3 3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |1|1|1|1|1|0|0|1|1|1|1|1|1|1|1|0|B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|x|x|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., scsi and ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static int
mk_conf_addr(u8 bus, u8 device_fn, u8 where, unsigned long *pci_addr, u8 *type1)
{
	*type1 = (bus == 0) ? 0 : 1;
	*pci_addr = (bus << 16) | (device_fn << 8) | (where) |
		    POLARIS_DENSE_CONFIG_BASE;

        DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
                 " returning address 0x%p\n"
                 bus, device_fn, where, *pci_addr));

	return 0;
}

int
polaris_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
                             struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldbu(*(vucp)pci_addr);
	return PCIBIOS_SUCCESSFUL;
}


int
polaris_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
                             struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldwu(*(vusp)pci_addr);
	return PCIBIOS_SUCCESSFUL;
}


int
polaris_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
                              struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *(vuip)pci_addr;
	return PCIBIOS_SUCCESSFUL;
}


int 
polaris_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
                              struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

        __kernel_stb(value, *(vucp)pci_addr);
	mb();
	__kernel_ldbu(*(vucp)pci_addr);
	return PCIBIOS_SUCCESSFUL;
}


int 
polaris_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
                              struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

        __kernel_stw(value, *(vusp)pci_addr);
	mb();
	__kernel_ldbu(*(vusp)pci_addr);
	return PCIBIOS_SUCCESSFUL;
}


int 
polaris_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
                               struct linux_hose_info *hose)
{
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
                return PCIBIOS_DEVICE_NOT_FOUND;

	*(vuip)pci_addr = value;
	mb();
	*(vuip)pci_addr;
	return PCIBIOS_SUCCESSFUL;
}

void __init
polaris_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	/* May need to initialize error reporting (see PCICTL0/1), but
	 * for now assume that the firmware has done the right thing
	 * already.
	 */
#if 0
	printk("polaris_init_arch(): trusting firmware for setup\n");
#endif
}

int polaris_pci_clr_err(void)
{
	POLARIS_jd = *((vusp)POLARIS_W_STATUS);
	DBG_MCK(("POLARIS_pci_clr_err: POLARIS_W_STATUS after read 0x%x\n",
		 POLARIS_jd));
	/* Write 1's to settable bits to clear errors */
	*((vusp)POLARIS_W_STATUS) = 0x7800; mb();
	POLARIS_jd = *((vusp)POLARIS_W_STATUS);
	return 0;
}

void polaris_machine_check(unsigned long vector, unsigned long la_ptr,
			 struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_POLARIS_sysdata_mcheck *mchk_sysdata;

	mchk_header = (struct el_common *)la_ptr;

	mchk_sysdata = 
	  (struct el_POLARIS_sysdata_mcheck *)(la_ptr+mchk_header->sys_offset);

#if 0
	DBG_MCK(("polaris_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	     vector, la_ptr));
	DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	     regs->pc, mchk_header->size, mchk_header->proc_offset,
	     mchk_header->sys_offset));
	DBG_MCK(("polaris_machine_check: expected %d status 0x%lx\n",
	     POLARIS_mcheck_expected, mchk_sysdata->psc_status));
#endif
#ifdef DEBUG_MCHECK_DUMP
	{
	    unsigned long *ptr;
	    int i;

	    ptr = (unsigned long *)la_ptr;
	    for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
	    }
	}
#endif /* DEBUG_MCHECK_DUMP */
	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	mb();
	if (POLARIS_mcheck_expected) {
		DBG_MCK(("POLARIS machine check expected\n"));
		POLARIS_mcheck_expected = 0;
		POLARIS_mcheck_taken = 1;
		mb();
		mb();
		draina();
		polaris_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#if 1
	else {
		printk("POLARIS machine check NOT expected\n") ;
	DBG_MCK(("polaris_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	     vector, la_ptr));
	DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	     regs->pc, mchk_header->size, mchk_header->proc_offset,
	     mchk_header->sys_offset));
		POLARIS_mcheck_expected = 0;
		POLARIS_mcheck_taken = 1;
		mb();
		mb();
		draina();
		polaris_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#endif
}
