#include "soft-fp.h"
#include "quad.h"

int FDIVQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);
	int ret;

	__FP_UNPACK_Q(A, rs1);
	__FP_UNPACK_Q(B, rs2);
	if(B_c == FP_CLS_ZERO &&
	   A_c != FP_CLS_ZERO) {
		ret |= EFLAG_DIVZERO;
		if(__FPU_TRAP_P(EFLAG_DIVZERO))
			return ret;
	}
	FP_DIV_Q(R, A, B);
	return (ret | __FP_PACK_Q(rd, R));
}
