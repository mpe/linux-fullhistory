#include <linux/in.h>

unsigned int csum_tcpudp_magic(void);
void halt(void);
void _do_bottom_half(void);

void sys_ptrace(void) { _panic("sys_ptrace"); }
void sys_iopl(void) { _panic("sys_iopl"); }
void sys_vm86(void) { _panic("sys_vm86"); }
void sys_modify_ldt(void) { _panic("sys_modify_ldt"); }
void sys_quotactl(void) { _panic("sys_quotactl"); }

void sys_pipe(void) {_panic("sys_pipe"); }
void sys_ipc(void) {_panic("sys_ipc"); }
void sys_mmap(void) {_panic("sys_mmap"); }
/* unneeded 
void sys_readdir(void) {panic("sys_readdir"); }
*/

void halt(void)
{
	_printk("\n...Halt!\n");
	abort();
}

_panic(char *msg)
{
	_printk("Panic: %s\n", msg);
	printk("Panic: %s\n", msg);
	abort();
}

_warn(char *msg)
{
	_printk("*** Warning: %s UNIMPLEMENTED!\n", msg);
}

extern unsigned short _ip_fast_csum(unsigned char *buf, int len);

unsigned short
ip_fast_csum(unsigned char *buf, int len)
{
	unsigned short _val;
	_val = _ip_fast_csum(buf, len);
#if 0	
	printk("IP CKSUM(%x, %d) = %x\n", buf, len, _val);
#endif	
	return (_val);
}

extern unsigned short _ip_compute_csum(unsigned char *buf, int len);

unsigned short
ip_compute_csum(unsigned char *buf, int len)
{
	unsigned short _val;
	_val = _ip_compute_csum(buf, len);
#if 0	
	printk("Compute IP CKSUM(%x, %d) = %x\n", buf, len, _val);
#endif	
	return (_val);
}

unsigned short
_udp_check(unsigned char *buf, int len, int saddr, int daddr, int hdr);

unsigned short
udp_check(unsigned char *buf, int len, int saddr, int daddr)
{
	unsigned short _val;
	int hdr;
	hdr = (len << 16) + IPPROTO_UDP;
	_val = _udp_check(buf, len, saddr, daddr, hdr);
#if 0	
	printk("UDP CSUM(%x,%d,%x,%x) = %x\n", buf, len, saddr, daddr, _val);
	dump_buf(buf, len);
#endif	
	return (_val);
}
#if 0
unsigned short
_tcp_check(unsigned char *buf, int len, int saddr, int daddr, int hdr);

unsigned short
tcp_check(unsigned char *buf, int len, int saddr, int daddr)
{
	unsigned short _val;
	int hdr;
	hdr = (len << 16) + IPPROTO_TCP;
	if (saddr == 0) saddr = ip_my_addr();
	_val = _tcp_check(buf, len, saddr, daddr, hdr);
#if 0	
	printk("TCP CSUM(%x,%d,%x,%x) = %x\n", buf, len, saddr, daddr, _val);
	dump_buf(buf, len);
#endif	
	return (_val);
}
#endif

void _do_bottom_half(void)
{
	_enable_interrupts(1);
	do_bottom_half();
	_disable_interrupts();
}

unsigned int csum_partial(unsigned char * buff, int len, unsigned int sum)
{
  panic("csum_partial");
}


unsigned int csum_partial_copy(char *src, char *dst, int len, int sum)
{
  panic("csum_partial_copy");
}

unsigned int csum_tcpudp_magic()
{
  }
