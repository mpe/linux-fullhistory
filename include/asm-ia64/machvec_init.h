#define MACHVEC_INHIBIT_RENAMING

#include <asm/machvec.h>

#define MACHVEC_HELPER(name)									\
 struct ia64_machine_vector machvec_##name __attribute__ ((unused, __section__ (".machvec")))	\
	= MACHVEC_INIT(name);

#define MACHVEC_DEFINE(name)	MACHVEC_HELPER(name)
