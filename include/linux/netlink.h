#ifndef __LINUX_NETLINK_H
#define __LINUX_NETLINK_H

struct nlmsghdr
{
	unsigned long	nlmsg_len;	/* Length of message including header */
	unsigned long	nlmsg_type;	/* Message type */
	unsigned long	nlmsg_seq;	/* Sequence number */
	unsigned long	nlmsg_pid;	/* Sending process PID */
	unsigned char	nlmsg_data[0];
};

#define NLMSG_ALIGN(len) ( ((len)+sizeof(long)-1) & ~(sizeof(long)-1) )

#define NLMSG_ACK		0x01	/* int - error code */
#define NLMSG_OVERRUN		0x02	/* unsigned long[2] - start and end
					 * of lost message sequence numbers.
					 */

#endif
