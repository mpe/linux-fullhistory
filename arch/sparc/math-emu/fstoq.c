#include "soft-fp.h"
#include "quad.h"
#include "single.h"

int FSTOQ(void *rd, void *rs2)
{
	FP_DECL_S(A); FP_DECL_Q(R);

	__FP_UNPACK_S(A, rs2);
	FP_CONV(Q,S,4,1,R,A);
	__FP_PACK_Q(rd, R);
	return 1;
}
