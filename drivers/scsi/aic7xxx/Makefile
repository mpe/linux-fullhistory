
.SUFFIXES: .c .y .o

PROG=	aic7xxx_asm

CSRCS=	aic7xxx_asm.c symbol.c
GENSRCS=	gram.c scan.c
GENHDRS=	y.tab.h token.h
OBJS=	gram.o scan.o symbol.o aic7xxx_asm.o

SRCS=	${GENSRCS} ${CSRCS}
CLEANFILES=	${GENSRCS} ${GENHDRS} y.output ${OBJS}
LIBS	=	-L/usr/local/lib -ldb
NOMAN=	noman

LEX	=	lex
LFLAGS	=	-t
YACC	=	yacc
YFLAGS	=	-d

${PROG}: $(OBJS)
	${CC} ${CFLAGS} -o ${PROG} ${OBJS} ${LIBS} 



y.tab.c: gram.y
	${YACC} ${YFLAGS} ${.IMPSRC}
	mv y.tab.c gram.c

gram.o: gram.c
	${CC} ${CFLAGS} -c gram.c


.y.o:
	${YACC} ${YFLAGS} ${.IMPSRC}
	${CC} ${CFLAGS} -c y.tab.c
	rm -f y.tab.c
	cp y.tab.o $@

.y.c:
	${YACC} ${YFLAGS} gram.y 
	mv y.tab.c gram.c 

.l.c:
	${LEX} ${LFLAGS} scan.l > scan.c
	mv lex.yy.c $@

.l.o:
	${LEX} ${LFLAGS} scan.l > scan.c
	${CC} ${CFLAGS} -c scan.c
#	rm -f lex.yy.c
#	mv lex.yy.o $@

clean_most:
	rm -f ${CLEANFILES}

clean:	clean_most
	rm -f ${PROG}
#
# This is how FreeBSD builds the assembler:
#
# bash$ make
# Warning: Object directory not changed from original /home/deischen/Linux/aic7xxx/Mar24/fbsd
# yacc -d gram.y
# mv y.tab.c gram.c
# cc -O2 -m486 -pipe   -c gram.c
# lex -t  scan.l > scan.c
# cc -O2 -m486 -pipe   -c scan.c
# cc -O2 -m486 -pipe   -c aic7xxx_asm.c
# cc -O2 -m486 -pipe   -c symbol.c
# cc -O2 -m486 -pipe    -o aic7xxx_asm gram.o scan.o aic7xxx_asm.o symbol.o  -ll
# bash$ cc -O2 -m486 -pipe    -o aic7xxx_asm gram.o scan.o aic7xxx_asm.o symbol.o
# bash$ aic7xxx_asm
# aic7xxx_asm: No input file specifiled
# usage: aic7xxx_asm      [-nostdinc] [-I-] [-I directory] [-o output_file]
#                         [-r register_output_file] [-l program_list_file]
#                         [-O option_name[|options_name2]] input_file
