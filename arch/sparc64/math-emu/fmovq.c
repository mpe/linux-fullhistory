int FMOVQ(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0];
	rd[1] = rs2[1];
	return 0;
}
