/* 
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)queue.h	8.3 (Berkeley) 12/13/93
 */

/*
 * Define the BSD singly linked queues which Linux doesn't
 * define (yet).
 */

#include <sys/queue.h>

#if !defined(SLIST_HEAD) && !defined(__BSD_Q__)

#define __BSD_Q__

#define SLIST_HEAD(name, type)						\
struct name {								\
	struct type *slh_first;	/* first element */			\
}
 
#define SLIST_ENTRY(type)						\
struct {								\
	struct type *sle_next;	/* next element */			\
}
 
/*
 * Singly-linked List functions.
 */
#define	SLIST_EMPTY(head)	((head)->slh_first == NULL)

#define	SLIST_FIRST(head)	((head)->slh_first)

#define SLIST_INIT(head) {						\
	(head)->slh_first = NULL;					\
}

#define SLIST_INSERT_AFTER(slistelm, elm, field) {			\
	(elm)->field.sle_next = (slistelm)->field.sle_next;		\
	(slistelm)->field.sle_next = (elm);				\
}

#define SLIST_INSERT_HEAD(head, elm, field) {				\
	(elm)->field.sle_next = (head)->slh_first;			\
	(head)->slh_first = (elm);					\
}

#define SLIST_NEXT(elm, field)	((elm)->field.sle_next)

#define SLIST_REMOVE_HEAD(head, field) {				\
	(head)->slh_first = (head)->slh_first->field.sle_next;		\
}

#define SLIST_REMOVE(head, elm, type, field) {				\
	if ((head)->slh_first == (elm)) {				\
		SLIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = (head)->slh_first;		\
		while( curelm->field.sle_next != (elm) )		\
			curelm = curelm->field.sle_next;		\
		curelm->field.sle_next =				\
		    curelm->field.sle_next->field.sle_next;		\
	}								\
}

/*
 * Singly-linked Tail queue definitions.
 */
#define STAILQ_HEAD(name, type)						\
struct name {								\
	struct type *stqh_first;/* first element */			\
	struct type **stqh_last;/* addr of last next element */		\
}

#define STAILQ_ENTRY(type)						\
struct {								\
	struct type *stqe_next;	/* next element */			\
}

/*
 * Singly-linked Tail queue functions.
 */
#define	STAILQ_INIT(head) {						\
	(head)->stqh_first = NULL;					\
	(head)->stqh_last = &(head)->stqh_first;			\
}

#define STAILQ_INSERT_HEAD(head, elm, field) {				\
	if (((elm)->field.stqe_next = (head)->stqh_first) == NULL)	\
		(head)->stqh_last = &(elm)->field.stqe_next;		\
	(head)->stqh_first = (elm);					\
}

#define STAILQ_INSERT_TAIL(head, elm, field) {				\
	(elm)->field.stqe_next = NULL;					\
	*(head)->stqh_last = (elm);					\
	(head)->stqh_last = &(elm)->field.stqe_next;			\
}

#define STAILQ_INSERT_AFTER(head, tqelm, elm, field) {			\
	if (((elm)->field.stqe_next = (tqelm)->field.stqe_next) == NULL)\
		(head)->stqh_last = &(elm)->field.stqe_next;		\
	(tqelm)->field.stqe_next = (elm);				\
}

#define STAILQ_REMOVE_HEAD(head, field) {				\
	if (((head)->stqh_first =					\
	     (head)->stqh_first->field.stqe_next) == NULL)		\
		(head)->stqh_last = &(head)->stqh_first;		\
}

#define STAILQ_REMOVE(head, elm, type, field) {				\
	if ((head)->stqh_first == (elm)) {				\
		STAILQ_REMOVE_HEAD(head, field);			\
	}								\
	else {								\
		struct type *curelm = (head)->stqh_first;		\
		while( curelm->field.stqe_next != (elm) )		\
			curelm = curelm->field.stqe_next;		\
		if((curelm->field.stqe_next =				\
		    curelm->field.stqe_next->field.stqe_next) == NULL)	\
			(head)->stqh_last = &(curelm)->field.stqe_next;	\
	}								\
}

#endif
