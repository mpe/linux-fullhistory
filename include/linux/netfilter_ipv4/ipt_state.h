#ifndef _IPT_STATE_H
#define _IPT_STATE_H

#define _IPT_STATE_BIT(ctinfo) (1 << ((ctinfo)+1))
#define IPT_STATE_BIT(ctinfo) ((ctinfo) >= IP_CT_IS_REPLY ? _IPT_STATE_BIT((ctinfo)-IP_CT_IS_REPLY) : _IPT_STATE_BIT(ctinfo))
#define IPT_STATE_INVALID (1 << 0)

struct ipt_state_info
{
	unsigned int statemask;
};
#endif /*_IPT_STATE_H*/
