#include "soft-fp.h"
#include "quad.h"
#include "double.h"

int FDMULQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_D(IN); FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);

	__FP_UNPACK_D(IN, rs1);
	FP_CONV(Q,D,4,2,A,IN);
	__FP_UNPACK_D(IN, rs2);
	FP_CONV(Q,D,4,2,B,IN);
	FP_MUL_Q(R, A, B);
	__FP_PACK_Q(rd, R);
	return 1;
}
