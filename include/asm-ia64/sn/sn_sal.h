#ifndef _ASM_IA64_SN_SN_SAL_H
#define _ASM_IA64_SN_SN_SAL_H

/*
 * System Abstraction Layer definitions for IA64
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All rights reserved.
 */


#include <linux/config.h>
#include <asm/sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/arch.h>
#include <asm/sn/geo.h>
#include <asm/sn/nodepda.h>

// SGI Specific Calls
#define  SN_SAL_POD_MODE                           0x02000001
#define  SN_SAL_SYSTEM_RESET                       0x02000002
#define  SN_SAL_PROBE                              0x02000003
#define  SN_SAL_GET_MASTER_NASID                   0x02000004
#define	 SN_SAL_GET_KLCONFIG_ADDR		   0x02000005
#define  SN_SAL_LOG_CE				   0x02000006
#define  SN_SAL_REGISTER_CE			   0x02000007
#define  SN_SAL_GET_PARTITION_ADDR		   0x02000009
#define  SN_SAL_XP_ADDR_REGION			   0x0200000f
#define  SN_SAL_NO_FAULT_ZONE_VIRTUAL		   0x02000010
#define  SN_SAL_NO_FAULT_ZONE_PHYSICAL		   0x02000011
#define  SN_SAL_PRINT_ERROR			   0x02000012
#define  SN_SAL_SET_ERROR_HANDLING_FEATURES	   0x0200001a	// reentrant
#define  SN_SAL_GET_FIT_COMPT			   0x0200001b	// reentrant
#define  SN_SAL_GET_HUB_INFO                       0x0200001c
#define  SN_SAL_GET_SAPIC_INFO                     0x0200001d
#define  SN_SAL_CONSOLE_PUTC                       0x02000021
#define  SN_SAL_CONSOLE_GETC                       0x02000022
#define  SN_SAL_CONSOLE_PUTS                       0x02000023
#define  SN_SAL_CONSOLE_GETS                       0x02000024
#define  SN_SAL_CONSOLE_GETS_TIMEOUT               0x02000025
#define  SN_SAL_CONSOLE_POLL                       0x02000026
#define  SN_SAL_CONSOLE_INTR                       0x02000027
#define  SN_SAL_CONSOLE_PUTB			   0x02000028
#define  SN_SAL_CONSOLE_XMIT_CHARS		   0x0200002a
#define  SN_SAL_CONSOLE_READC			   0x0200002b
#define  SN_SAL_SYSCTL_MODID_GET	           0x02000031
#define  SN_SAL_SYSCTL_GET                         0x02000032
#define  SN_SAL_SYSCTL_IOBRICK_MODULE_GET          0x02000033
#define  SN_SAL_SYSCTL_IO_PORTSPEED_GET            0x02000035
#define  SN_SAL_SYSCTL_SLAB_GET                    0x02000036
#define  SN_SAL_BUS_CONFIG		   	   0x02000037
#define  SN_SAL_SYS_SERIAL_GET			   0x02000038
#define  SN_SAL_PARTITION_SERIAL_GET		   0x02000039
#define  SN_SAL_SYSCTL_PARTITION_GET		   0x0200003a
#define  SN_SAL_SYSTEM_POWER_DOWN		   0x0200003b
#define  SN_SAL_GET_MASTER_BASEIO_NASID		   0x0200003c
#define  SN_SAL_COHERENCE                          0x0200003d
#define  SN_SAL_MEMPROTECT                         0x0200003e
#define  SN_SAL_SYSCTL_FRU_CAPTURE		   0x0200003f

#define  SN_SAL_SYSCTL_IOBRICK_PCI_OP		   0x02000042	// reentrant
#define	 SN_SAL_IROUTER_OP			   0x02000043
#define  SN_SAL_IOIF_INTERRUPT			   0x0200004a
#define  SN_SAL_HWPERF_OP			   0x02000050   // lock
#define  SN_SAL_IOIF_ERROR_INTERRUPT		   0x02000051

#define  SN_SAL_IOIF_SLOT_ENABLE		   0x02000053
#define  SN_SAL_IOIF_SLOT_DISABLE		   0x02000054
#define  SN_SAL_IOIF_GET_HUBDEV_INFO		   0x02000055
#define  SN_SAL_IOIF_GET_PCIBUS_INFO		   0x02000056
#define  SN_SAL_IOIF_GET_PCIDEV_INFO		   0x02000057
#define  SN_SAL_IOIF_GET_WIDGET_DMAFLUSH_LIST	   0x02000058

#define SN_SAL_HUB_ERROR_INTERRUPT		   0x02000060


/*
 * Service-specific constants
 */

/* Console interrupt manipulation */
	/* action codes */
#define SAL_CONSOLE_INTR_OFF    0       /* turn the interrupt off */
#define SAL_CONSOLE_INTR_ON     1       /* turn the interrupt on */
#define SAL_CONSOLE_INTR_STATUS 2	/* retrieve the interrupt status */
	/* interrupt specification & status return codes */
#define SAL_CONSOLE_INTR_XMIT	1	/* output interrupt */
#define SAL_CONSOLE_INTR_RECV	2	/* input interrupt */

/* interrupt handling */
#define SAL_INTR_ALLOC		1
#define SAL_INTR_FREE		2

/*
 * IRouter (i.e. generalized system controller) operations
 */
#define SAL_IROUTER_OPEN	0	/* open a subchannel */
#define SAL_IROUTER_CLOSE	1	/* close a subchannel */
#define SAL_IROUTER_SEND	2	/* send part of an IRouter packet */
#define SAL_IROUTER_RECV	3	/* receive part of an IRouter packet */
#define SAL_IROUTER_INTR_STATUS	4	/* check the interrupt status for
					 * an open subchannel
					 */
#define SAL_IROUTER_INTR_ON	5	/* enable an interrupt */
#define SAL_IROUTER_INTR_OFF	6	/* disable an interrupt */
#define SAL_IROUTER_INIT	7	/* initialize IRouter driver */

/* IRouter interrupt mask bits */
#define SAL_IROUTER_INTR_XMIT	SAL_CONSOLE_INTR_XMIT
#define SAL_IROUTER_INTR_RECV	SAL_CONSOLE_INTR_RECV


/*
 * SAL Error Codes
 */
#define SALRET_MORE_PASSES	1
#define SALRET_OK		0
#define SALRET_NOT_IMPLEMENTED	(-1)
#define SALRET_INVALID_ARG	(-2)
#define SALRET_ERROR		(-3)


/**
 * sn_sal_rev_major - get the major SGI SAL revision number
 *
 * The SGI PROM stores its version in sal_[ab]_rev_(major|minor).
 * This routine simply extracts the major value from the
 * @ia64_sal_systab structure constructed by ia64_sal_init().
 */
static inline int
sn_sal_rev_major(void)
{
	struct ia64_sal_systab *systab = efi.sal_systab;

	return (int)systab->sal_b_rev_major;
}

/**
 * sn_sal_rev_minor - get the minor SGI SAL revision number
 *
 * The SGI PROM stores its version in sal_[ab]_rev_(major|minor).
 * This routine simply extracts the minor value from the
 * @ia64_sal_systab structure constructed by ia64_sal_init().
 */
static inline int
sn_sal_rev_minor(void)
{
	struct ia64_sal_systab *systab = efi.sal_systab;
	
	return (int)systab->sal_b_rev_minor;
}

/*
 * Specify the minimum PROM revsion required for this kernel.
 * Note that they're stored in hex format...
 */
#define SN_SAL_MIN_MAJOR	0x4  /* SN2 kernels need at least PROM 4.0 */
#define SN_SAL_MIN_MINOR	0x0

/*
 * Returns the master console nasid, if the call fails, return an illegal
 * value.
 */
static inline u64
ia64_sn_get_console_nasid(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL(ret_stuff, SN_SAL_GET_MASTER_NASID, 0, 0, 0, 0, 0, 0, 0);

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	/* Master console nasid is in 'v0' */
	return ret_stuff.v0;
}

/*
 * Returns the master baseio nasid, if the call fails, return an illegal
 * value.
 */
static inline u64
ia64_sn_get_master_baseio_nasid(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL(ret_stuff, SN_SAL_GET_MASTER_BASEIO_NASID, 0, 0, 0, 0, 0, 0, 0);

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	/* Master baseio nasid is in 'v0' */
	return ret_stuff.v0;
}

static inline char *
ia64_sn_get_klconfig_addr(nasid_t nasid)
{
	struct ia64_sal_retval ret_stuff;
	int cnodeid;

	cnodeid = nasid_to_cnodeid(nasid);
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL(ret_stuff, SN_SAL_GET_KLCONFIG_ADDR, (u64)nasid, 0, 0, 0, 0, 0, 0);

	/*
	 * We should panic if a valid cnode nasid does not produce
	 * a klconfig address.
	 */
	if (ret_stuff.status != 0) {
		panic("ia64_sn_get_klconfig_addr: Returned error %lx\n", ret_stuff.status);
	}
	return ret_stuff.v0 ? __va(ret_stuff.v0) : NULL;
}

/*
 * Returns the next console character.
 */
static inline u64
ia64_sn_console_getc(int *ch)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_GETC, 0, 0, 0, 0, 0, 0, 0);

	/* character is in 'v0' */
	*ch = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/*
 * Read a character from the SAL console device, after a previous interrupt
 * or poll operation has given us to know that a character is available
 * to be read.
 */
static inline u64
ia64_sn_console_readc(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_READC, 0, 0, 0, 0, 0, 0, 0);

	/* character is in 'v0' */
	return ret_stuff.v0;
}

/*
 * Sends the given character to the console.
 */
static inline u64
ia64_sn_console_putc(char ch)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_PUTC, (uint64_t)ch, 0, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Sends the given buffer to the console.
 */
static inline u64
ia64_sn_console_putb(const char *buf, int len)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0; 
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_PUTB, (uint64_t)buf, (uint64_t)len, 0, 0, 0, 0, 0);

	if ( ret_stuff.status == 0 ) {
		return ret_stuff.v0;
	}
	return (u64)0;
}

/*
 * Print a platform error record
 */
static inline u64
ia64_sn_plat_specific_err_print(int (*hook)(const char*, ...), char *rec)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_REENTRANT(ret_stuff, SN_SAL_PRINT_ERROR, (uint64_t)hook, (uint64_t)rec, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Check for Platform errors
 */
static inline u64
ia64_sn_plat_cpei_handler(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_LOG_CE, 0, 0, 0, 0, 0, 0, 0);

	return ret_stuff.status;
}

/*
 * Checks for console input.
 */
static inline u64
ia64_sn_console_check(int *result)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_POLL, 0, 0, 0, 0, 0, 0, 0);

	/* result is in 'v0' */
	*result = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/*
 * Checks console interrupt status
 */
static inline u64
ia64_sn_console_intr_status(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_INTR, 
		 0, SAL_CONSOLE_INTR_STATUS,
		 0, 0, 0, 0, 0);

	if (ret_stuff.status == 0) {
	    return ret_stuff.v0;
	}
	
	return 0;
}

/*
 * Enable an interrupt on the SAL console device.
 */
static inline void
ia64_sn_console_intr_enable(uint64_t intr)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_INTR, 
		 intr, SAL_CONSOLE_INTR_ON,
		 0, 0, 0, 0, 0);
}

/*
 * Disable an interrupt on the SAL console device.
 */
static inline void
ia64_sn_console_intr_disable(uint64_t intr)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_INTR, 
		 intr, SAL_CONSOLE_INTR_OFF,
		 0, 0, 0, 0, 0);
}

/*
 * Sends a character buffer to the console asynchronously.
 */
static inline u64
ia64_sn_console_xmit_chars(char *buf, int len)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_CONSOLE_XMIT_CHARS,
		 (uint64_t)buf, (uint64_t)len,
		 0, 0, 0, 0, 0);

	if (ret_stuff.status == 0) {
	    return ret_stuff.v0;
	}

	return 0;
}

/*
 * Returns the iobrick module Id
 */
static inline u64
ia64_sn_sysctl_iobrick_module_get(nasid_t nasid, int *result)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_SYSCTL_IOBRICK_MODULE_GET, nasid, 0, 0, 0, 0, 0, 0);

	/* result is in 'v0' */
	*result = (int)ret_stuff.v0;

	return ret_stuff.status;
}

/**
 * ia64_sn_pod_mode - call the SN_SAL_POD_MODE function
 *
 * SN_SAL_POD_MODE actually takes an argument, but it's always
 * 0 when we call it from the kernel, so we don't have to expose
 * it to the caller.
 */
static inline u64
ia64_sn_pod_mode(void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SN_SAL_POD_MODE, 0, 0, 0, 0, 0, 0, 0);
	if (isrv.status)
		return 0;
	return isrv.v0;
}

/**
 * ia64_sn_probe_mem - read from memory safely
 * @addr: address to probe
 * @size: number bytes to read (1,2,4,8)
 * @data_ptr: address to store value read by probe (-1 returned if probe fails)
 *
 * Call into the SAL to do a memory read.  If the read generates a machine
 * check, this routine will recover gracefully and return -1 to the caller.
 * @addr is usually a kernel virtual address in uncached space (i.e. the
 * address starts with 0xc), but if called in physical mode, @addr should
 * be a physical address.
 *
 * Return values:
 *  0 - probe successful
 *  1 - probe failed (generated MCA)
 *  2 - Bad arg
 * <0 - PAL error
 */
static inline u64
ia64_sn_probe_mem(long addr, long size, void *data_ptr)
{
	struct ia64_sal_retval isrv;

	SAL_CALL(isrv, SN_SAL_PROBE, addr, size, 0, 0, 0, 0, 0);

	if (data_ptr) {
		switch (size) {
		case 1:
			*((u8*)data_ptr) = (u8)isrv.v0;
			break;
		case 2:
			*((u16*)data_ptr) = (u16)isrv.v0;
			break;
		case 4:
			*((u32*)data_ptr) = (u32)isrv.v0;
			break;
		case 8:
			*((u64*)data_ptr) = (u64)isrv.v0;
			break;
		default:
			isrv.status = 2;
		}
	}
	return isrv.status;
}

/*
 * Retrieve the system serial number as an ASCII string.
 */
static inline u64
ia64_sn_sys_serial_get(char *buf)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_SYS_SERIAL_GET, buf, 0, 0, 0, 0, 0, 0);
	return ret_stuff.status;
}

extern char sn_system_serial_number_string[];
extern u64 sn_partition_serial_number;

static inline char *
sn_system_serial_number(void) {
	if (sn_system_serial_number_string[0]) {
		return(sn_system_serial_number_string);
	} else {
		ia64_sn_sys_serial_get(sn_system_serial_number_string);
		return(sn_system_serial_number_string);
	}
}
	

/*
 * Returns a unique id number for this system and partition (suitable for
 * use with license managers), based in part on the system serial number.
 */
static inline u64
ia64_sn_partition_serial_get(void)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_PARTITION_SERIAL_GET, 0, 0, 0, 0, 0, 0, 0);
	if (ret_stuff.status != 0)
	    return 0;
	return ret_stuff.v0;
}

static inline u64
sn_partition_serial_number_val(void) {
	if (sn_partition_serial_number) {
		return(sn_partition_serial_number);
	} else {
		return(sn_partition_serial_number = ia64_sn_partition_serial_get());
	}
}

/*
 * Returns the partition id of the nasid passed in as an argument,
 * or INVALID_PARTID if the partition id cannot be retrieved.
 */
static inline partid_t
ia64_sn_sysctl_partition_get(nasid_t nasid)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_SYSCTL_PARTITION_GET, nasid,
		 0, 0, 0, 0, 0, 0);
	if (ret_stuff.status != 0)
	    return INVALID_PARTID;
	return ((partid_t)ret_stuff.v0);
}

/*
 * Returns the partition id of the current processor.
 */

extern partid_t sn_partid;

static inline partid_t
sn_local_partid(void) {
	if (sn_partid < 0) {
		return (sn_partid = ia64_sn_sysctl_partition_get(cpuid_to_nasid(smp_processor_id())));
	} else {
		return sn_partid;
	}
}

/*
 * Register or unregister a physical address range being referenced across
 * a partition boundary for which certain SAL errors should be scanned for,
 * cleaned up and ignored.  This is of value for kernel partitioning code only.
 * Values for the operation argument:
 *	1 = register this address range with SAL
 *	0 = unregister this address range with SAL
 * 
 * SAL maintains a reference count on an address range in case it is registered
 * multiple times.
 * 
 * On success, returns the reference count of the address range after the SAL
 * call has performed the current registration/unregistration.  Returns a
 * negative value if an error occurred.
 */
static inline int
sn_register_xp_addr_region(u64 paddr, u64 len, int operation)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_XP_ADDR_REGION, paddr, len, (u64)operation,
		 0, 0, 0, 0);
	return ret_stuff.status;
}

/*
 * Register or unregister an instruction range for which SAL errors should
 * be ignored.  If an error occurs while in the registered range, SAL jumps
 * to return_addr after ignoring the error.  Values for the operation argument:
 *	1 = register this instruction range with SAL
 *	0 = unregister this instruction range with SAL
 *
 * Returns 0 on success, or a negative value if an error occurred.
 */
static inline int
sn_register_nofault_code(u64 start_addr, u64 end_addr, u64 return_addr,
			 int virtual, int operation)
{
	struct ia64_sal_retval ret_stuff;
	u64 call;
	if (virtual) {
		call = SN_SAL_NO_FAULT_ZONE_VIRTUAL;
	} else {
		call = SN_SAL_NO_FAULT_ZONE_PHYSICAL;
	}
	SAL_CALL(ret_stuff, call, start_addr, end_addr, return_addr, (u64)1,
		 0, 0, 0);
	return ret_stuff.status;
}

/*
 * Change or query the coherence domain for this partition. Each cpu-based
 * nasid is represented by a bit in an array of 64-bit words:
 *      0 = not in this partition's coherency domain
 *      1 = in this partition's coherency domain
 *
 * It is not possible for the local system's nasids to be removed from
 * the coherency domain.  Purpose of the domain arguments:
 *      new_domain = set the coherence domain to the given nasids
 *      old_domain = return the current coherence domain
 *
 * Returns 0 on success, or a negative value if an error occurred.
 */
static inline int
sn_change_coherence(u64 *new_domain, u64 *old_domain)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_COHERENCE, new_domain, old_domain, 0, 0,
		 0, 0, 0);
	return ret_stuff.status;
}

/*
 * Change memory access protections for a physical address range.
 * nasid_array is not used on Altix, but may be in future architectures.
 * Available memory protection access classes are defined after the function.
 */
static inline int
sn_change_memprotect(u64 paddr, u64 len, u64 perms, u64 *nasid_array)
{
	struct ia64_sal_retval ret_stuff;
	int cnodeid;
	unsigned long irq_flags;

	cnodeid = nasid_to_cnodeid(get_node_number(paddr));
	// spin_lock(&NODEPDA(cnodeid)->bist_lock);
	local_irq_save(irq_flags);
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_MEMPROTECT, paddr, len, nasid_array,
		 perms, 0, 0, 0);
	local_irq_restore(irq_flags);
	// spin_unlock(&NODEPDA(cnodeid)->bist_lock);
	return ret_stuff.status;
}
#define SN_MEMPROT_ACCESS_CLASS_0		0x14a080
#define SN_MEMPROT_ACCESS_CLASS_1		0x2520c2
#define SN_MEMPROT_ACCESS_CLASS_2		0x14a1ca
#define SN_MEMPROT_ACCESS_CLASS_3		0x14a290
#define SN_MEMPROT_ACCESS_CLASS_6		0x084080
#define SN_MEMPROT_ACCESS_CLASS_7		0x021080

/*
 * Turns off system power.
 */
static inline void
ia64_sn_power_down(void)
{
	struct ia64_sal_retval ret_stuff;
	SAL_CALL(ret_stuff, SN_SAL_SYSTEM_POWER_DOWN, 0, 0, 0, 0, 0, 0, 0);
	while(1);
	/* never returns */
}

/**
 * ia64_sn_fru_capture - tell the system controller to capture hw state
 *
 * This routine will call the SAL which will tell the system controller(s)
 * to capture hw mmr information from each SHub in the system.
 */
static inline u64
ia64_sn_fru_capture(void)
{
        struct ia64_sal_retval isrv;
        SAL_CALL(isrv, SN_SAL_SYSCTL_FRU_CAPTURE, 0, 0, 0, 0, 0, 0, 0);
        if (isrv.status)
                return 0;
        return isrv.v0;
}

/*
 * Performs an operation on a PCI bus or slot -- power up, power down
 * or reset.
 */
static inline u64
ia64_sn_sysctl_iobrick_pci_op(nasid_t n, u64 connection_type, 
			      u64 bus, char slot, 
			      u64 action)
{
	struct ia64_sal_retval rv = {0, 0, 0, 0};

	SAL_CALL_NOLOCK(rv, SN_SAL_SYSCTL_IOBRICK_PCI_OP, connection_type, n, action,
		 bus, (u64) slot, 0, 0);
	if (rv.status)
	    	return rv.v0;
	return 0;
}


/*
 * Open a subchannel for sending arbitrary data to the system
 * controller network via the system controller device associated with
 * 'nasid'.  Return the subchannel number or a negative error code.
 */
static inline int
ia64_sn_irtr_open(nasid_t nasid)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_OPEN, nasid,
			   0, 0, 0, 0, 0);
	return (int) rv.v0;
}

/*
 * Close system controller subchannel 'subch' previously opened on 'nasid'.
 */
static inline int
ia64_sn_irtr_close(nasid_t nasid, int subch)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_CLOSE,
			   (u64) nasid, (u64) subch, 0, 0, 0, 0);
	return (int) rv.status;
}

/*
 * Read data from system controller associated with 'nasid' on
 * subchannel 'subch'.  The buffer to be filled is pointed to by
 * 'buf', and its capacity is in the integer pointed to by 'len'.  The
 * referent of 'len' is set to the number of bytes read by the SAL
 * call.  The return value is either SALRET_OK (for bytes read) or
 * SALRET_ERROR (for error or "no data available").
 */
static inline int
ia64_sn_irtr_recv(nasid_t nasid, int subch, char *buf, int *len)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_RECV,
			   (u64) nasid, (u64) subch, (u64) buf, (u64) len,
			   0, 0);
	return (int) rv.status;
}

/*
 * Write data to the system controller network via the system
 * controller associated with 'nasid' on suchannel 'subch'.  The
 * buffer to be written out is pointed to by 'buf', and 'len' is the
 * number of bytes to be written.  The return value is either the
 * number of bytes written (which could be zero) or a negative error
 * code.
 */
static inline int
ia64_sn_irtr_send(nasid_t nasid, int subch, char *buf, int len)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_SEND,
			   (u64) nasid, (u64) subch, (u64) buf, (u64) len,
			   0, 0);
	return (int) rv.v0;
}

/*
 * Check whether any interrupts are pending for the system controller
 * associated with 'nasid' and its subchannel 'subch'.  The return
 * value is a mask of pending interrupts (SAL_IROUTER_INTR_XMIT and/or
 * SAL_IROUTER_INTR_RECV).
 */
static inline int
ia64_sn_irtr_intr(nasid_t nasid, int subch)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_INTR_STATUS,
			   (u64) nasid, (u64) subch, 0, 0, 0, 0);
	return (int) rv.v0;
}

/*
 * Enable the interrupt indicated by the intr parameter (either
 * SAL_IROUTER_INTR_XMIT or SAL_IROUTER_INTR_RECV).
 */
static inline int
ia64_sn_irtr_intr_enable(nasid_t nasid, int subch, u64 intr)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_INTR_ON,
			   (u64) nasid, (u64) subch, intr, 0, 0, 0);
	return (int) rv.v0;
}

/*
 * Disable the interrupt indicated by the intr parameter (either
 * SAL_IROUTER_INTR_XMIT or SAL_IROUTER_INTR_RECV).
 */
static inline int
ia64_sn_irtr_intr_disable(nasid_t nasid, int subch, u64 intr)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_INTR_OFF,
			   (u64) nasid, (u64) subch, intr, 0, 0, 0);
	return (int) rv.v0;
}

/**
 * ia64_sn_get_fit_compt - read a FIT entry from the PROM header
 * @nasid: NASID of node to read
 * @index: FIT entry index to be retrieved (0..n)
 * @fitentry: 16 byte buffer where FIT entry will be stored.
 * @banbuf: optional buffer for retrieving banner
 * @banlen: length of banner buffer
 *
 * Access to the physical PROM chips needs to be serialized since reads and
 * writes can't occur at the same time, so we need to call into the SAL when
 * we want to look at the FIT entries on the chips.
 *
 * Returns:
 *	%SALRET_OK if ok
 *	%SALRET_INVALID_ARG if index too big
 *	%SALRET_NOT_IMPLEMENTED if running on older PROM
 *	??? if nasid invalid OR banner buffer not large enough
 */
static inline int
ia64_sn_get_fit_compt(u64 nasid, u64 index, void *fitentry, void *banbuf,
		      u64 banlen)
{
	struct ia64_sal_retval rv;
	SAL_CALL_NOLOCK(rv, SN_SAL_GET_FIT_COMPT, nasid, index, fitentry,
			banbuf, banlen, 0, 0);
	return (int) rv.status;
}

/*
 * Initialize the SAL components of the system controller
 * communication driver; specifically pass in a sizable buffer that
 * can be used for allocation of subchannel queues as new subchannels
 * are opened.  "buf" points to the buffer, and "len" specifies its
 * length.
 */
static inline int
ia64_sn_irtr_init(nasid_t nasid, void *buf, int len)
{
	struct ia64_sal_retval rv;
	SAL_CALL_REENTRANT(rv, SN_SAL_IROUTER_OP, SAL_IROUTER_INIT,
			   (u64) nasid, (u64) buf, (u64) len, 0, 0, 0);
	return (int) rv.status;
}

/*
 * Returns the nasid, subnode & slice corresponding to a SAPIC ID
 *
 *  In:
 *	arg0 - SN_SAL_GET_SAPIC_INFO
 *	arg1 - sapicid (lid >> 16) 
 *  Out:
 *	v0 - nasid
 *	v1 - subnode
 *	v2 - slice
 */
static inline u64
ia64_sn_get_sapic_info(int sapicid, int *nasid, int *subnode, int *slice)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_GET_SAPIC_INFO, sapicid, 0, 0, 0, 0, 0, 0);

/***** BEGIN HACK - temp til old proms no longer supported ********/
	if (ret_stuff.status == SALRET_NOT_IMPLEMENTED) {
		if (nasid) *nasid = sapicid & 0xfff;
		if (subnode) *subnode = (sapicid >> 13) & 1;
		if (slice) *slice = (sapicid >> 12) & 3;
		return 0;
	}
/***** END HACK *******/

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	if (nasid) *nasid = (int) ret_stuff.v0;
	if (subnode) *subnode = (int) ret_stuff.v1;
	if (slice) *slice = (int) ret_stuff.v2;
	return 0;
}
 
/*
 * Returns information about the HUB/SHUB.
 *  In:
 *	arg0 - SN_SAL_GET_HUB_INFO
 * 	arg1 - 0 (other values reserved for future use)
 *  Out:
 *	v0 - shub type (0=shub1, 1=shub2)
 *	v1 - masid mask (ex., 0x7ff for 11 bit nasid)
 *	v2 - bit position of low nasid bit
 */
static inline u64
ia64_sn_get_hub_info(int fc, u64 *arg1, u64 *arg2, u64 *arg3)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;
	ret_stuff.v1 = 0;
	ret_stuff.v2 = 0;
	SAL_CALL_NOLOCK(ret_stuff, SN_SAL_GET_HUB_INFO, fc, 0, 0, 0, 0, 0, 0);

/***** BEGIN HACK - temp til old proms no longer supported ********/
	if (ret_stuff.status == SALRET_NOT_IMPLEMENTED) {
		if (arg1) *arg1 = 0;
		if (arg2) *arg2 = 0x7ff;
		if (arg3) *arg3 = 38;
		return 0;
	}
/***** END HACK *******/

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	if (arg1) *arg1 = ret_stuff.v0;
	if (arg2) *arg2 = ret_stuff.v1;
	if (arg3) *arg3 = ret_stuff.v2;
	return 0;
}
 
/*
 * This is the access point to the Altix PROM hardware performance
 * and status monitoring interface. For info on using this, see
 * include/asm-ia64/sn/sn2/sn_hwperf.h
 */
static inline int
ia64_sn_hwperf_op(nasid_t nasid, u64 opcode, u64 a0, u64 a1, u64 a2,
                  u64 a3, u64 a4, int *v0)
{
	struct ia64_sal_retval rv;
	SAL_CALL_NOLOCK(rv, SN_SAL_HWPERF_OP, (u64)nasid,
		opcode, a0, a1, a2, a3, a4);
	if (v0)
		*v0 = (int) rv.v0;
	return (int) rv.status;
}

#endif /* _ASM_IA64_SN_SN_SAL_H */
