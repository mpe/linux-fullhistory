/*
 * This file contains the Itanium PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (C) 2002  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 */

#define RDEP(x)	(1UL<<(x))

#ifndef CONFIG_ITANIUM
#error "This file is only valid when CONFIG_ITANIUM is defined"
#endif

static int pfm_ita_pmc_check(struct task_struct *task, unsigned int cnum, unsigned long *val, struct pt_regs *regs);
static int pfm_write_ibr_dbr(int mode, struct task_struct *task, void *arg, int count, struct pt_regs *regs);

static pfm_reg_desc_t pmc_desc[256]={
/* pmc0  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc1  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc2  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc3  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc4  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(4),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc5  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(5),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc6  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(6),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc7  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(7),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc8  */ { PFM_REG_CONFIG, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc9  */ { PFM_REG_CONFIG, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc10 */ { PFM_REG_MONITOR, 6, NULL, NULL, {RDEP(0)|RDEP(1),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc11 */ { PFM_REG_MONITOR, 6, NULL, pfm_ita_pmc_check, {RDEP(2)|RDEP(3)|RDEP(17),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc12 */ { PFM_REG_MONITOR, 6, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc13 */ { PFM_REG_CONFIG, 0, NULL, pfm_ita_pmc_check, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
	    { PFM_REG_NONE, 0, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static pfm_reg_desc_t pmd_desc[256]={
/* pmd0  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(1),0UL, 0UL, 0UL}, {RDEP(10),0UL, 0UL, 0UL}},
/* pmd1  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(0),0UL, 0UL, 0UL}, {RDEP(10),0UL, 0UL, 0UL}},
/* pmd2  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(3)|RDEP(17),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
/* pmd3  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(2)|RDEP(17),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
/* pmd4  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(4),0UL, 0UL, 0UL}},
/* pmd5  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(5),0UL, 0UL, 0UL}},
/* pmd6  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(6),0UL, 0UL, 0UL}},
/* pmd7  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(7),0UL, 0UL, 0UL}},
/* pmd8  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd9  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd10 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd11 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd12 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd13 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd14 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd15 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd16 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd17 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(2)|RDEP(3),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
	    { PFM_REG_NONE, 0, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static int
pfm_ita_pmc_check(struct task_struct *task, unsigned int cnum, unsigned long *val, struct pt_regs *regs)
{
	pfm_context_t *ctx = task->thread.pfm_context;
	int ret;

	/*
	 * we must clear the (instruction) debug registers if pmc13.ta bit is cleared
	 * before they are written (fl_using_dbreg==0) to avoid picking up stale information. 
	 */
	if (cnum == 13 && ((*val & 0x1) == 0UL) && ctx->ctx_fl_using_dbreg == 0) {

		/* don't mix debug with perfmon */
		if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/* 
		 * a count of 0 will mark the debug registers as in use and also
		 * ensure that they are properly cleared.
		 */
		ret = pfm_write_ibr_dbr(1, task, NULL, 0, regs);
		if (ret) return ret;
	}

	/*
	 * we must clear the (data) debug registers if pmc11.pt bit is cleared
	 * before they are written (fl_using_dbreg==0) to avoid picking up stale information. 
	 */
	if (cnum == 11 && ((*val >> 28)& 0x1) == 0 && ctx->ctx_fl_using_dbreg == 0) {

		/* don't mix debug with perfmon */
		if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/* 
		 * a count of 0 will mark the debug registers as in use and also
		 * ensure that they are properly cleared.
		 */
		ret = pfm_write_ibr_dbr(0, task, NULL, 0, regs);
		if (ret) return ret;
	}
	return 0;
}

