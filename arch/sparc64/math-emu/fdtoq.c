#include "soft-fp.h"
#include "quad.h"
#include "double.h"

int FDTOQ(void *rd, void *rs2)
{
	FP_DECL_D(A); FP_DECL_Q(R);

	__FP_UNPACK_D(A, rs2);
	FP_CONV(Q,D,2,1,R,A);
	__FP_PACK_Q(rd, R);
	return 1;
}
