.globl __getlock
__getlock:
	pushl %eax

        movl $-8192,%eax
        andl %esp,%eax
        movl 36(%eax),%eax
	incl spinlocks(,%eax,4)

	popl %eax
	ret

.globl __putlock
__putlock:
	pushl %eax

        movl $-8192,%eax
        andl %esp,%eax
        movl 36(%eax),%eax
	decl spinlocks(,%eax,4)
	js __bad_put

	popl %eax
	ret

__bad_put:
	pushl %edx
	pushl %ecx
	call __putlock_negative
	popl %ecx
	popl %edx
	popl %eax
	ret
