/*
 * misc.c: Miscellaneous ARCS PROM routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/config.h>
#include <linux/kernel.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>
#include <asm/system.h>

extern unsigned long mips_cputype;
extern int initialize_kbd(void);
extern void *sgiwd93_host;
extern void reset_wd33c93(void *instance);

static inline void shutoff_r4600_cache(void)
{
	unsigned long tmp1, tmp2, tmp3;

	if(mips_cputype != CPU_R4600 &&
	   mips_cputype != CPU_R4640 &&
	   mips_cputype != CPU_R4700)
		return;
	printk("Disabling R4600 SCACHE\n");
	__asm__ __volatile__("
	.set noreorder
	.set mips3
	li	%0, 0x1
	dsll	%0, 31
	lui	%1, 0x9000
	dsll32	%1, 0
	or	%0, %1, %0
	mfc0	%2, $12
	nop; nop; nop; nop;
	li	%1, 0x80
	mtc0	%1, $12
	nop; nop; nop; nop;
	sh	$0, 0(%0)
	mtc0	$0, $12
	nop; nop; nop; nop;
	mtc0	%2, $12
	nop; nop; nop; nop;
	.set mips2
	.set reorder
        " : "=r" (tmp1), "=r" (tmp2), "=r" (tmp3));
}

void prom_halt(void)
{
	shutoff_r4600_cache();
	initialize_kbd();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	cli();
	romvec->halt();
}

void prom_powerdown(void)
{
	shutoff_r4600_cache();
	initialize_kbd();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	cli();
	romvec->pdown();
}

/* XXX is this a soft reset basically? XXX */
void prom_restart(void)
{
	shutoff_r4600_cache();
	initialize_kbd();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	cli();
	romvec->restart();
}

void prom_reboot(void)
{
	shutoff_r4600_cache();
	initialize_kbd();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	cli();
	romvec->reboot();
}

void prom_imode(void)
{
	shutoff_r4600_cache();
	initialize_kbd();
#if CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	cli();
	romvec->imode();
}

long prom_cfgsave(void)
{
	return romvec->cfg_save();
}

struct linux_sysid *prom_getsysid(void)
{
	return romvec->get_sysid();
}

void prom_cacheflush(void)
{
	romvec->cache_flush();
}
