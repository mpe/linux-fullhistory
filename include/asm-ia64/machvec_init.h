#define __MACHVEC_HDR(n)		<asm/machvec_##n##.h>
#define __MACHVEC_EXPAND(n)		__MACHVEC_HDR(n)
#define MACHVEC_PLATFORM_HEADER		__MACHVEC_EXPAND(MACHVEC_PLATFORM_NAME)

#include <asm/machvec.h>

#define MACHVEC_HELPER(name)									\
 struct ia64_machine_vector machvec_##name __attribute__ ((unused, __section__ (".machvec")))	\
	= MACHVEC_INIT(name);

#define MACHVEC_DEFINE(name)	MACHVEC_HELPER(name)

MACHVEC_DEFINE(MACHVEC_PLATFORM_NAME)
