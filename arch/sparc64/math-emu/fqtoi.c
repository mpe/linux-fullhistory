#include "soft-fp.h"
#include "quad.h"

int FQTOI(unsigned *rd, void *rs2)
{
	FP_DECL_Q(A);
	unsigned r;

	__FP_UNPACK_Q(A, rs2);
	FP_TO_INT_Q(r, A, 32, 1);
	*rd = r;
	return 1;
}
