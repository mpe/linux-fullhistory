#include "soft-fp.h"
#include "double.h"

int FCMPD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_D(A); FP_DECL_D(B);
	long ret;
	unsigned long *fsr = rd;
	
	__FP_UNPACK_D(A, rs1);
	__FP_UNPACK_D(B, rs2);
	FP_CMP_D(ret, B, A, 2);
	if (ret == -1)
		ret = 2;

	*fsr = (*fsr & ~0xc00) | (ret << 10); 
	return 1;
}
