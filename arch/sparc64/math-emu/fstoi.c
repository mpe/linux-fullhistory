#include "soft-fp.h"
#include "single.h"

int FSTOI(unsigned *rd, void *rs2)
{
	FP_DECL_S(A);
	unsigned r;

	__FP_UNPACK_S(A, rs2);
	FP_TO_INT_S(r, A, 32, 1);
	*rd = r;
	return 1;
}
