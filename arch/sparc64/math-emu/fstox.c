#include "soft-fp.h"
#include "single.h"

int FSTOX(unsigned long *rd, void *rs2)
{
	FP_DECL_S(A);
	unsigned long r;

	__FP_UNPACK_S(A, rs2);
	FP_TO_INT_S(r, A, 64, 1);
	*rd = r;
	return 1;
}
