#include "soft-fp.h"
#include "quad.h"
#include "double.h"

int FQTOD(void *rd, void *rs2)
{
	FP_DECL_Q(A); FP_DECL_D(R);

	__FP_UNPACK_Q(A, rs2);
	FP_CONV(D,Q,2,4,R,A);
	__FP_PACK_D(rd, R);
	return 1;
}
