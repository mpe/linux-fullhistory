#ifndef _PPC_INIT_H
#define _PPC_INIT_H

#include <linux/init.h>

#if __GNUC__ > 2 || __GNUC_MINOR__ >= 90 /* egcs */
#define __pmac __attribute__ ((__section__ (".text.pmac")))
#define __pmacdata __attribute__ ((__section__ (".data.pmac")))
#define __pmacfunc(__argpmac) \
	__argpmac __pmac; \
	__argpmac
	
#define __prep __attribute__ ((__section__ (".text.prep")))
#define __prepdata /* __attribute__ ((__section__ (".data.prep")))*/
#define __prepfunc(__argprep) \
	__argprep __prep; \
	__argprep

#define __apus __attribute__ ((__section__ (".text.apus")))
#define __apusdata __attribute__ ((__section__ (".data.apus")))
#define __apusfunc(__argapus) \
	__argapus __apus; \
	__argapus

/* this is actually just common chrp/pmac code, not OF code -- Cort */
#define __openfirmware __attribute__ ((__section__ (".text.openfirmware")))
#define __openfirmwaredata __attribute__ ((__section__ (".data.openfirmware")))
#define __openfirmwarefunc(__argopenfirmware) \
	__argopenfirmware __openfirmware; \
	__argopenfirmware
	
#else /* not egcs */

#define __pmac
#define __pmacdata
#define __pmacfunc(x) x
	
#define __prep
#define __prepdata
#define __prepfunc(x) x

#define __apus
#define __apusdata
#define __apusfunc(x) x

#define __openfirmware
#define __openfirmwaredata
#define __openfirmwarefunc(x) x

#endif /* egcs */

#endif /* _PPC_INIT_H */
