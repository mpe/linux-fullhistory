/*
 * arch/alpha/lib/csum_ipv6_magic.S
 * Contributed by Richard Henderson <rth@tamu.edu>
 *
 * unsigned short csum_ipv6_magic(struct in6_addr *saddr,
 *                                struct in6_addr *daddr,
 *                                __u16 len,
 *                                unsigned short proto,
 *                                unsigned int csum);
 */

	.globl csum_ipv6_magic
	.align 4
	.ent csum_ipv6_magic
	.frame $30,0,$26,0
csum_ipv6_magic:
	.prologue 0

	ldq	$0,0($16)	# e0    : load src & dst addr words
	zapnot	$20,15,$20	# .. e1 : zero extend incoming csum
	extwh	$18,7,$4	# e0    : byte swap len & proto while we wait
	ldq	$1,8($16)	# .. e1 :
	extbl	$18,1,$18	# e0    :
	ldq	$2,0($17)	# .. e1 :
	extwh	$19,7,$5	# e0    :
	ldq	$3,8($17)	# .. e1 :
	extbl	$19,1,$19	# e0    :
	or	$18,$4,$18	# .. e1 :
	addq	$20,$0,$20	# e0    : begin summing the words
	or	$19,$5,$19	# .. e1 :
	sll	$18,48,$18	# e0    :
	cmpult	$20,$0,$0	# .. e1 :
	sll	$19,48,$19	# e0    :
	addq	$20,$1,$20	# .. e1 :
	sra	$18,32,$18	# e0    : len complete
	cmpult	$20,$1,$1	# .. e1 :
	sra	$19,32,$19	# e0    : proto complete
	addq	$20,$2,$20	# .. e1 :
	cmpult	$20,$2,$2	# e0    :
	addq	$20,$3,$20	# .. e1 :
	cmpult	$20,$3,$3	# e0    :
	addq	$20,$18,$20	# .. e1 :
	cmpult	$20,$18,$18	# e0    :
	addq	$20,$19,$20	# .. e1 :
	cmpult	$20,$19,$19	# e0    :
	addq	$0,$1,$0	# .. e1 : merge the carries back into the csum
	addq	$2,$3,$2	# e0    :
	addq	$18,$19,$18	# .. e1 :
	addq	$0,$2,$0	# e0    :
	addq	$20,$18,$20	# .. e1 :
	addq	$0,$20,$0	# e0    :
	unop			#       :
	extwl	$0,2,$2		# e0    : begin folding the 64-bit value
	zapnot	$0,3,$3		# .. e1 :
	extwl	$0,4,$1		# e0    :
	addq	$2,$3,$3	# .. e1 :
	extwl	$0,6,$0		# e0    :
	addq	$3,$1,$3	# .. e1 :
	addq	$0,$3,$0	# e0    :
	unop			#       :
	extwl	$0,2,$1		# e0    : fold 18-bit value
	zapnot	$0,3,$0		# .. e1 :
	addq	$0,$1,$0	# e0    :
	unop			#       :
	extwl	$0,2,$1		# e0    : fold 17-bit value
	zapnot	$0,3,$0		# .. e1 :
	addq	$0,$1,$0	# e0    :
	not	$0,$0		# e1    : and compliment.
	zapnot	$0,3,$0		# e0    :
	ret			# .. e1 :

	.end csum_ipv6_magic
