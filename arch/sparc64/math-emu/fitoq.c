#include "soft-fp.h"
#include "quad.h"

int FITOQ(void *rd, void *rs2)
{
	FP_DECL_Q(R);
	int a = *(int *)rs2;

	FP_FROM_INT_Q(R, a, 32, int);
	__FP_PACK_Q(rd, R);
	return 1;
}
