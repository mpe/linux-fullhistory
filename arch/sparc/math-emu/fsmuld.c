#include "soft-fp.h"
#include "double.h"
#include "single.h"

int FSMULD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_S(IN); FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);

	__FP_UNPACK_S(IN, rs1);
	FP_CONV(D,S,2,1,A,IN);
	__FP_UNPACK_S(IN, rs2);
	FP_CONV(D,S,2,1,B,IN);
	FP_MUL_D(R, A, B);
	__FP_PACK_D(rd, R);
	return 1;
}
