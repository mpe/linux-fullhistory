/*	Sorry!	*/
#include "sound_config.h"

void
sb16_dsp_interrupt (int unused)
{
}

long sb16_dsp_init(long mem_start, struct address_info *hw_config)
{
	return mem_start;
}

int sb16_dsp_detect(struct address_info *hw_config)
{
	return 0;
}
