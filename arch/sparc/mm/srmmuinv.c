/* $Id: srmmuinv.c,v 1.2 1995/11/25 00:59:36 davem Exp $
 * srmmuinv.c:  Invalidate routines for the various different
 *              SRMMU implementations.
 *
 * Copyright (C) 1995 David S. Miller
 */

/* HyperSparc */
hyper_invalidate(void)
{
	volatile unsigned int sfsr_clear;

	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	/* Flush ICACHE */
	flush_whole_icache();
	sfsr_clear = srmmu_get_fstatus();
	return;
}

hyper_invalidate_mp(void)
{
	volatile unsigned int sfsr_clear;

	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	/* Flush ICACHE */
	flush_whole_icache();

	sfsr_clear = srmmu_get_fstatus();

	/* Tell other CPUS to each call the Uniprocessor
	 * invalidate routine.
	 */

	return;
}

/* Cypress */
void
cypress_invalidate(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

void
cypress_invalidate_mp(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	/* Tell other CPUS to call the UP version */

	return;
}

void
cypress_invalidate_asibad(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache w/o using ASIs */

	return;
}

void
cypress_invalidate_asibad_mp(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache w/o using ASIs */

	/* Tell other CPUS to call the UP version */

	return;
}

/* Swift */
void
swift_invalidate(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

void
swift_invalidate_poke_kernel_pageperms(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

void
swift_invalidate_poke_kernel_pte_cbits(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

void
swift_invalidate_poke_everything(void)
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

/* Tsunami */
tsunami_invalidate()
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Flush Virtual Address Cache */

	return;
}

/* Viking */
viking_invalidate()
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	return;
}

viking_invalidate_mp()
{
	/* Flush TLB */
	srmmu_flush_whole_tlb();

	/* Make other CPUS call UP routine. */

	return;
}

/* That should be it */
