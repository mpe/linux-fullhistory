#include "soft-fp.h"
#include "double.h"

int FSQRTD(void *rd, void *rs2)
{
	FP_DECL_D(A); FP_DECL_D(R);
        
	__FP_UNPACK_D(A, rs2);
	FP_SQRT_D(R, A);
	__FP_PACK_D(rd, R);
	return 1;
}
