/*
 * xfrm_input.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 * 	
 */

#include <net/ip.h>
#include <net/xfrm.h>

void __secpath_destroy(struct sec_path *sp)
{
	int i;
	for (i = 0; i < sp->len; i++)
		xfrm_state_put(sp->x[i].xvec);
	kmem_cache_free(sp->pool, sp);
}

/* Fetch spi and seq from ipsec header */

int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, u32 *spi, u32 *seq)
{
	int offset, offset_seq;

	switch (nexthdr) {
	case IPPROTO_AH:
		offset = offsetof(struct ip_auth_hdr, spi);
		offset_seq = offsetof(struct ip_auth_hdr, seq_no);
		break;
	case IPPROTO_ESP:
		offset = offsetof(struct ip_esp_hdr, spi);
		offset_seq = offsetof(struct ip_esp_hdr, seq_no);
		break;
	case IPPROTO_COMP:
		if (!pskb_may_pull(skb, sizeof(struct ip_comp_hdr)))
			return -EINVAL;
		*spi = ntohl(ntohs(*(u16*)(skb->h.raw + 2)));
		*seq = 0;
		return 0;
	default:
		return 1;
	}

	if (!pskb_may_pull(skb, 16))
		return -EINVAL;

	*spi = *(u32*)(skb->h.raw + offset);
	*seq = *(u32*)(skb->h.raw + offset_seq);
	return 0;
}
