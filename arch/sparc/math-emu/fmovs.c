int FMOVS(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0];
	return 0;
}
