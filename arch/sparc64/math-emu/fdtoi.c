#include "soft-fp.h"
#include "double.h"

int FDTOI(unsigned *rd, void *rs2)
{
	FP_DECL_D(A);
	unsigned r;

	__FP_UNPACK_D(A, rs2);
	FP_TO_INT_D(r, A, 32, 1);
	*rd = r;
	return 1;
}
