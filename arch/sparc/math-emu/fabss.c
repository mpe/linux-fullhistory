int FABSS(unsigned long *rd, unsigned long *rs2)
{
	/* Clear the sign bit (high bit of word 0) */
	rd[0] = rs2[0] & 0x7fffffffUL;
	return 1;
}
