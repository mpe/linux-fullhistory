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

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif


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

static inline void
polaris_pci_clr_err(void)
{
	*(vusp)POLARIS_W_STATUS;
	/* Write 1's to settable bits to clear errors */
	*(vusp)POLARIS_W_STATUS = 0x7800;
	mb();
	*(vusp)POLARIS_W_STATUS;
}

void
polaris_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
	/* Clear the error before any reporting.  */
	mb();
	mb();
	draina();
	polaris_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, regs, "POLARIS",
			    mcheck_expected(0));
}
