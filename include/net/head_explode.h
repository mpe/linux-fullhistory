/*
 *	Header exploders. We inline those only appearing once.
 *
 *	We assume 8 bit bytes.
 *
 *	This is oriented to getting good code out of GCC. It may need
 *	tuning for other processors.
 *
 *	Note only IGMP uses this so far. Just as an experiment.
 */
 
 
extern __inline__ unsigned char *exp_getu16(unsigned char *bp, unsigned short *u)
{
	*u=(*bp<<8)|bp[1];
	return bp+2;
}

extern __inline__ unsigned char *exp_getn16(unsigned char *bp, unsigned short *u)
{
	unsigned char *tp=(unsigned char *)u;
	*tp++=*bp++;
	*tp++=*bp++;
	return bp;
}

extern __inline__ unsigned char *imp_putu16(unsigned char *bp, unsigned short n)
{
	*bp=(n>>8);
	bp[1]=n&0xFF;
	return bp+2;
}

extern __inline__ unsigned char *imp_putn16(unsigned char *bp, unsigned short n)
{
	unsigned char *sp=(unsigned char *)&n;
	*bp++=*sp++;
	*bp++=*sp++;
	return bp;
}

extern __inline__ unsigned char *exp_getu32(unsigned char *bp, unsigned long *u)
{
	*u=(bp[0]<<24)|(bp[1]<<16)|(bp[2]<<8)|bp[3];
	return bp+4;
}

extern __inline__ unsigned char *exp_getn32(unsigned char *bp, unsigned long *u)
{
	unsigned char *tp=(unsigned char *)u;
	*tp++=*bp++;
	*tp++=*bp++;
	*tp++=*bp++;
	*tp++=*bp++;
	return bp;
}

extern __inline__ unsigned char *imp_putu32(unsigned char *bp, unsigned long n)
{
	bp[0]=n>>24;
	bp[1]=(n>>16)&0xFF;
	bp[2]=(n>>8)&0xFF;
	bp[3]=n&0xFF;
	return bp+4;
}

extern __inline__ unsigned char *imp_putn32(unsigned char *bp, unsigned long n)
{
	unsigned char *sp=(unsigned char *)&n;
	*bp++=*sp++;
	*bp++=*sp++;
	*bp++=*sp++;
	*bp++=*sp++;
	return bp;
}

#if 0

extern __inline__ unsigned char *ip_explode(unsigned char *iph, struct ip_header *ip)
{
	ip->version=*iph>>4;		/* Avoid the shift. We do our equality checks shifted too */
	ip->ihl=(*iph++)&0xF;		/* Length in long words */
	ip->tos=*iph++;			/* Service type */
	iph=exp_getu16(iph,&ip->tot_len);	/* Length of packet */
	iph=exp_getu16(iph,&ip->id);		/* Packet identity */
	iph=exp_getu16(iph,&ip->frag_off);	/* Fragment offset */
	ip->ttl=*iph++;
	ip->protocol=*iph++;
	iph=exp_getn16(iph,&ip->check);
	iph=exp_getn32(iph,&ip->saddr);
	iph=exp_getn32(iph,&ip->daddr);
	return iph;
}

extern __inline__ unsigned char *icmp_explode(unsigned char *icmph, struct icmp_header *icmp)
{
	icmp->type=*icmp++;
	icmp->code=*icmp++;
	icmph=exp_getn16(icmph,&icmp->checksum);
	/* These two pairs are a union... expand both */
	exp_getu32(icmph,&icmp->gateway);	
	icmph=exp_getu16(icmph,&icmp->id);
	icmph=exp_getu16(icmph,&icmp->sequence);
	return icmph;
}

#endif

extern __inline__ unsigned char *igmp_explode(unsigned char *igmph, struct igmp_header *igmp)
{
	igmp->type=*igmph++;
	igmph++;	/* unused */
	igmph=exp_getn16(igmph,&igmp->csum);
	igmph=exp_getn32(igmph,&igmp->group);
	return igmph;
}

#if 0
extern __inline__ unsigned char *tcp_explode(unsigned char *tcph, struct tcp_header *tcp)
{
	tcph=exp_getu16(tcph,&tcp->source);
	tcph=exp_getu16(tcph,&tcp->dest);
	tcph=exp_getu32(tcph,&tcp->seq);
	tcph=exp_getu32(tcph,&tcp->ack_seq);
	tcph=exp_getu16(tcph,&tcp->u.bitmask);
	tcph=exp_getu16(tcph,&tcp->window);
	tcph=exp_getn16(tcph,&tcp->check);
	tcph=exp_getu16(tcph,&tcp->urg_ptr);
	return tcph;
}

extern __inline__ unsigned char *udp_explode(unsigned char *udph, struct udp_header *udp)
{
	udph=exp_getu16(tcph,&udp->source);
	udph=exp_getu16(udph,&udp->dest);
	udph=exp_getu16(udph,&udp->len);
	udph=exp_getn16(udph,&udp->check);
	return udph;
}
#endif
