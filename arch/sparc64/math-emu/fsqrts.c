#include "soft-fp.h"
#include "single.h"

int FSQRTS(void *rd, void *rs2)
{
	FP_DECL_S(A); FP_DECL_S(R);
        
	__FP_UNPACK_S(A, rs2);
	FP_SQRT_S(R, A);
	__FP_PACK_S(rd, R);
	return 1;
}
