# Description file for ksymoops

#	Thu Nov 26 16:37:46 EST 1998
#	Version 0.6c
#	Add -c option.

#	Tue Nov  3 02:31:01 EST 1998
#	Version 0.6
#	Read lsmod (/proc/modules).
#	Add Makefile defaults for vmlinux, ksyms, objects, System.map, lsmod.
#	Upper case variables.
#	Convert from a.out to bfd, using same format as ksymoops.

DEFS = Makefile ksymoops.h

# Defaults for vmlinux, ksyms, objects, lsmod, System.map.  Externalised so
# distributions can tweak to suit their own file system layout.

# To default to not reading a source, set to any empty string.
# To default to reading a source, supply a quoted and escaped string.

# If the string contains *r (*m, *n, *s) then it is replaced at run time by
# the current value of `uname -r` (-m, -n, -s).  '*' was chosen as something
# that rarely appears in filenames and does not cause problems like '%' or '$'.

DEF_VMLINUX =				# default no vmlinux
DEF_OBJECTS = \"/lib/modules/*r/\"	# default current modules
DEF_KSYMS = \"/proc/ksyms\"		# default current ksyms
DEF_LSMOD = \"/proc/modules\"		# default current lsmod
DEF_MAP = \"/usr/src/linux/System.map\"	# default current map
DEF_CODE_BYTES = 1			# default bytes per code unit

# RedHat users might want defaults like these
# DEF_MAP = \"/boot/System.map-*r\"
# DEF_OBJECTS = \"/boot/module-info-*r\"

PROGS = ksymoops

CC=gcc
CFLAGS = -Dlinux \
	 -Wall \
	 -Wno-conversion \
	 -Waggregate-return \
	 -Wstrict-prototypes \
	 -Wmissing-prototypes \
	 $(DEBUG)

ifneq ($(strip $(DEF_VMLINUX)),)
	CFLAGS += -DDEF_VMLINUX=$(strip $(DEF_VMLINUX))
endif
ifneq ($(strip $(DEF_OBJECTS)),)
	CFLAGS += -DDEF_OBJECTS=$(strip $(DEF_OBJECTS))
endif
ifneq ($(strip $(DEF_KSYMS)),)
	CFLAGS += -DDEF_KSYMS=$(strip $(DEF_KSYMS))
endif
ifneq ($(strip $(DEF_LSMOD)),)
	CFLAGS += -DDEF_LSMOD=$(strip $(DEF_LSMOD))
endif
ifneq ($(strip $(DEF_MAP)),)
	CFLAGS += -DDEF_MAP=$(strip $(DEF_MAP))
endif

CFLAGS += -DDEF_CODE_BYTES=$(strip $(DEF_CODE_BYTES))

OBJECTS = io.o ksyms.o ksymoops.o map.o misc.o object.o oops.o re.o symbol.o

all: 	$(PROGS)

:	$(OBJECTS)

$(OBJECTS): $(DEFS)

$(PROGS): %: %.o $(DEFS) $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) -lbfd -liberty -o $@
	-@size $@

clean:
	rm -f core *.o $(PROGS)
