#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>

static void
p8023_datalink_header(struct datalink_proto *dl, 
		struct sk_buff *skb, unsigned char *dest_node)
{
	struct device	*dev = skb->dev;
	
	dev->hard_header(skb, dev, ETH_P_802_3, dest_node, NULL, skb->len);
}

struct datalink_proto *
make_8023_client(void)
{
	struct datalink_proto	*proto;

	proto = (struct datalink_proto *) kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto != NULL) {
		proto->type_len = 0;
		proto->header_length = 0;
		proto->datalink_header = p8023_datalink_header;
		proto->string_name = "802.3";
	}

	return proto;
}

void destroy_8023_client(struct datalink_proto *dl)
{
	if (dl)
		kfree_s(dl,sizeof(struct datalink_proto));
}

