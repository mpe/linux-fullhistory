#include "soft-fp.h"
#include "double.h"

int FDTOX(unsigned long *rd, void *rs2)
{
	FP_DECL_D(A);
	unsigned long r;

	__FP_UNPACK_D(A, rs2);
	FP_TO_INT_D(r, A, 64, 1);
	*rd = r;
	return 1;
}
