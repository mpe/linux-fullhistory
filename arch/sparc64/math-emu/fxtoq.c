#include "soft-fp.h"
#include "quad.h"

int FXTOQ(void *rd, void *rs2)
{
	FP_DECL_Q(R);
	long a = *(long *)rs2;

	FP_FROM_INT_Q(R, a, 64, long);
	__FP_PACK_Q(rd, R);
	return 1;
}
