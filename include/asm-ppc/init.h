#ifndef _PPC_INIT_H
#define _PPC_INIT_H

#if __GNUC__ > 2 || __GNUC_MINOR__ >= 90 /* egcs */
#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

#define __pmac __attribute__ ((__section__ (".text.pmac")))
#define __pmacdata __attribute__ ((__section__ (".data.pmac")))
#define __pmacfunc(__argpmac) \
	__argpmac __pmac; \
	__argpmac
	
#define __prep __attribute__ ((__section__ (".text.prep")))
#define __prepdata __attribute__ ((__section__ (".data.prep")))
#define __prepfunc(__argprep) \
	__argprep __prep; \
	__argprep

/* this is actually just common chrp/pmac code, not OF code -- Cort */
#define __openfirmware __attribute__ ((__section__ (".text.openfirmware")))
#define __openfirmwaredata __attribute__ ((__section__ (".data.openfirmware")))
#define __openfirmwarefunc(__argopenfirmware) \
	__argopenfirmware __openfirmware; \
	__argopenfirmware
	
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT	.previous
#define __INITDATA	.section	".data.init",#alloc,#write

#define __cacheline_aligned __attribute__ \
			 ((__section__ (".data.cacheline_aligned")))

#else /* not egcs */

#define __init
#define __initdata
#define __initfunc(x) x
	
#define __INIT
#define __FINIT
#define __INITDATA
	
#define __pmac
#define __pmacdata
#define __pmacfunc(x) x
	
#define __prep
#define __prepdata
#define __prepfunc(x) x

#define __openfirmware
#define __openfirmwaredata
#define __openfirmwarefunc(x) x

#define __cacheline_aligned
#endif /* egcs */
#endif
