#include "soft-fp.h"
#include "single.h"

int FDIVS(void *rd, void *rs2, void *rs1)
{
	FP_DECL_S(A); FP_DECL_S(B); FP_DECL_S(R);
	int ret = 0;

	__FP_UNPACK_S(A, rs1);
	__FP_UNPACK_S(B, rs2);
	if(B_c == FP_CLS_ZERO &&
	   A_c != FP_CLS_ZERO) {
		ret |= EFLAG_DIVZERO;
		if(__FPU_TRAP_P(EFLAG_DIVZERO))
			return ret;
	}
	FP_DIV_S(R, A, B);
	return (ret | __FP_PACK_S(rd, R));
}

