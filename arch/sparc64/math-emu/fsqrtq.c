#include "soft-fp.h"
#include "quad.h"

int FSQRTQ(void *rd, void *rs2)
{
	FP_DECL_Q(A); FP_DECL_Q(R);
        
	__FP_UNPACK_Q(A, rs2);
	FP_SQRT_Q(R, A);
	__FP_PACK_Q(rd, R);
	return 1;
}
