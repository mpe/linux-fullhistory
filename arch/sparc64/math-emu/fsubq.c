#include "soft-fp.h"
#include "quad.h"

int FSUBQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);

	__FP_UNPACK_Q(A, rs1);
	__FP_UNPACK_Q(B, rs2);
	if (B_c != FP_CLS_NAN)
		B_s ^= 1;
	FP_ADD_Q(R, A, B);
	__FP_PACK_Q(rd, R);
	return 1;
}
