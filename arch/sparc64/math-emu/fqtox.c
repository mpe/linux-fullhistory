#include "soft-fp.h"
#include "quad.h"

int FQTOX(unsigned long *rd, void *rs2)
{
	FP_DECL_Q(A);
	unsigned long r;

	__FP_UNPACK_Q(A, rs2);
	FP_TO_INT_Q(r, A, 64, 1);
	*rd = r;
	return 1;
}
