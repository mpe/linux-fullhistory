#include "soft-fp.h"
#include "single.h"

int FCMPES(void *rd, void *rs2, void *rs1)
{
	FP_DECL_S(A); FP_DECL_S(B);
	long ret;
	unsigned long *fsr = rd;
	
	__FP_UNPACK_S(A, rs1);
	__FP_UNPACK_S(B, rs2);
	FP_CMP_S(ret, B, A, 1);
	if (ret == -1)
		ret = 2;

	*fsr = (*fsr & ~0xc00) | (ret << 10); 
	return 1;
}
