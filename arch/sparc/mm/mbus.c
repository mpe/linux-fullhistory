/* $Id: mbus.c,v 1.9 1995/11/25 00:59:26 davem Exp $
 * mbus.c: MBUS probing routines, called from kernel/probe.c
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>

#include <asm/oplib.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/asi.h>
#include <asm/psr.h>
#include <asm/vac-ops.h>
#include <asm/mbus.h>

/* #define DEBUG_MBUS */

unsigned int viking_rev, swift_rev, cypress_rev;
enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;

void
probe_mbus(void)
{
	register unsigned int mreg, vaddr;
	register int impl, vers, syscntrl, pso, resv, nofault, enable;
	register int mod_typ, mod_rev;

	srmmu_modtype = SRMMU_INVAL_MOD;
	hwbug_bitmask = 0;
	vaddr = 0;

	mreg = srmmu_get_mmureg();
	impl = (mreg & SRMMU_CTREG_IMPL_MASK) >> SRMMU_CTREG_IMPL_SHIFT;
	vers = (mreg & SRMMU_CTREG_VERS_MASK) >> SRMMU_CTREG_VERS_SHIFT;
	syscntrl = (mreg & SRMMU_CTREG_SYSCNTRL_MASK) >> SRMMU_CTREG_SYSCNTRL_SHIFT;
	pso = (mreg & SRMMU_CTREG_PSO_MASK) >> SRMMU_CTREG_PSO_SHIFT;
	resv = (mreg & SRMMU_CTREG_RESV_MASK) >> SRMMU_CTREG_RESV_SHIFT;
	nofault = (mreg & SRMMU_CTREG_NOFAULT_MASK) >> SRMMU_CTREG_NOFAULT_SHIFT;
	enable = (mreg & SRMMU_CTREG_ENABLE_MASK) >> SRMMU_CTREG_ENABLE_SHIFT;

#ifdef DEBUG_MBUS
	printk("MMU REGISTER\n");
	printk("IMPL<%01x> VERS<%01x> SYSCNTRL<%04x> PSO<%d> RESV<%02x> NOFAULT<%d> ENABLE<%d>\n",
	       impl, vers, syscntrl, (int) pso, resv, (int) nofault, (int) enable);
#endif

	mod_typ = impl; mod_rev = vers;
	printk("MBUS: ");

	if(mod_typ == 0x1) /* Ross HyperSparc or Cypress */
		if (mod_rev == 0x7) {
			srmmu_modtype = HyperSparc;
			hwbug_bitmask |= HWBUG_VACFLUSH_BITROT;
			/* Turn off Cache Wrapping and copyback caching, I don't
			 * understand them completely yet... */
			printk("Enabling HyperSparc features...\n");
			/* FUCK IT, I wanna see this baby chug! */
			/* First, flush the cache */
#if 0
			for(vaddr = 0; vaddr != vac_size; vaddr+=vac_linesize)
				flush_ei_ctx(vaddr);
#endif
			mreg &= (~HYPERSPARC_CWENABLE);
			mreg &= (~HYPERSPARC_CMODE);
			mreg &= (~HYPERSPARC_WBENABLE);
			mreg |= (HYPERSPARC_CENABLE);
			srmmu_set_mmureg(mreg);
			/* Clear all the cache tags */
#if 0
			for(vaddr = 0; vaddr != vac_size; vaddr+=vac_linesize)
				__asm__ __volatile__("sta %%g0, [%0] %1" : :
						     "r" (vaddr), "i" (0xe));
#endif
			/* Flush the ICACHE */
			flush_whole_icache();

		} else {
			cypress_rev = mod_rev;
			if(mod_rev == 0xe) {
				srmmu_modtype = Cypress_vE;
				hwbug_bitmask |= HWBUG_COPYBACK_BROKEN;
			} else
				if(mod_rev == 0xd) {
					srmmu_modtype = Cypress_vD;
					hwbug_bitmask |= HWBUG_ASIFLUSH_BROKEN;
				} else
					srmmu_modtype = Cypress;

			/* It is a Cypress module */
			printk("ROSS Cypress Module %s\n",
			       (srmmu_modtype == Cypress_vE ? "Rev. E" :
				(srmmu_modtype == Cypress_vD ? "Rev. D" :
				 "")));
				/* Enable Cypress features */
				printk("Enabling Cypress features...\n");
				mreg &= (~CYPRESS_CMODE);
				mreg |= (CYPRESS_CENABLE);
				srmmu_set_mmureg(mreg);
				/* Maybe play with Cypress 604/605 cache stuff here? */
		}


	if(((get_psr()>>0x18)&0xff)==0x04) {
		__asm__ __volatile__("lda [%1] %2, %0\n\t" 
				     "srl %0, 0x18, %0\n\t" :
				     "=r" (swift_rev) :
				     "r" (0x10003000), "i" (0x20));
		printk("Fujitsu MB86904 or higher Swift module\n");  /* MB86905 etc. */
		switch(swift_rev) {
		case 0x11:
		case 0x20:
		case 0x23:
		case 0x30:
			srmmu_modtype = Swift_lots_o_bugs;
			hwbug_bitmask |= HWBUG_KERN_ACCBROKEN;
			hwbug_bitmask |= HWBUG_KERN_CBITBROKEN;
			printk("Detected Swift with Lots 'o' Bugs\n");
			break;
		case 0x25:
		case 0x31:
			srmmu_modtype = Swift_bad_c;
			hwbug_bitmask |= HWBUG_KERN_CBITBROKEN;
			printk("Detected Swift with kernel pte C bit bug\n");
			break;
		default:
			srmmu_modtype = Swift_ok;
			printk("Detected Swift with no bugs...\n");
			break;
		}
		/* Enable Fujitsu Swift specific features here... */
		printk("Enabling Swift features...\n");
		mreg |= 0;
		srmmu_set_mmureg(mreg);
	}

	if((((get_psr()>>0x18)&0xff)==0x40 ||
	    (((get_psr()>>0x18)&0xff)==0x41 && mod_typ==0 && mod_rev==0))) {
		if(((get_psr()>>0x18)&0xf)==0 && mod_rev==0) {
			srmmu_modtype = Viking_12;
			hwbug_bitmask |= HWBUG_MODIFIED_BITROT;
			hwbug_bitmask |= HWBUG_PC_BADFAULT_ADDR;
		} else {
			if(((get_psr()>>0x18)&0xf)!=0) {
				srmmu_modtype = Viking_2x;
				hwbug_bitmask |= HWBUG_PC_BADFAULT_ADDR;
			} else
				if(mod_rev==1) {
					srmmu_modtype = Viking_30;
					hwbug_bitmask |= HWBUG_PACINIT_BITROT;
				} else  {
					if (mod_rev<8)
						srmmu_modtype = Viking_35;
					else
						srmmu_modtype = Viking_new;
				}
		}

		/* SPARCclassic's STP1010 may be produced under other name */
		printk("VIKING Module\n");
		printk("Enabling Viking features...\n");
		mreg |= (VIKING_DCENABLE | VIKING_ICENABLE | VIKING_SBENABLE |
			 VIKING_TCENABLE | VIKING_DPENABLE);
		srmmu_set_mmureg(mreg);
	}
   
	if((((get_psr()>>0x18)&0xff)==0x41) && (mod_typ || mod_rev)) {
		srmmu_modtype = Tsunami;
		printk("Tsunami module\n");
		/* Enable Tsunami features */
	}

	if(srmmu_modtype == SRMMU_INVAL_MOD) {
		printk("Unknown SRMMU module type!\n");
		printk("MMU_CREG: impl=%x vers=%x\n", mod_typ, mod_rev);
		printk("PSR: impl=%x vers=%x\n", ((get_psr()>>28)&0xf),
		       ((get_psr()>>24)&0xf));
		panic("probe_mbus()");
	}

	/* AIEEE, should get this from the prom... */
	printk("Boot processor ID %d Module ID %d (%s MBUS)\n",
	       get_cpuid(), get_modid(), (get_modid() == 0x8 ? "Level 1" : "Level 2"));
}
