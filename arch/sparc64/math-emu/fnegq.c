int FNEGQ(unsigned long *rd, unsigned long *rs2)
{
	rd[0] = rs2[0] ^ 0x8000000000000000UL;
	rd[1] = rs2[1];
	return 1;
}

