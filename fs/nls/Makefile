#
# Makefile for native language support
#

MOD_LIST_NAME := NLS_MODULES

NLS = nls_base.o

ifeq ($(CONFIG_NLS_CODEPAGE_437),y)
NLS += nls_cp437.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_437),m)
  M_OBJS += nls_cp437.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_737),y)
NLS += nls_cp737.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_737),m)
  M_OBJS += nls_cp737.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_775),y)
NLS += nls_cp775.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_775),m)
  M_OBJS += nls_cp775.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_850),y)
NLS += nls_cp850.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_850),m)
  M_OBJS += nls_cp850.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_852),y)
NLS += nls_cp852.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_852),m)
  M_OBJS += nls_cp852.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_855),y)
NLS += nls_cp855.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_855),m)
  M_OBJS += nls_cp855.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_857),y)
NLS += nls_cp857.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_857),m)
  M_OBJS += nls_cp857.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_860),y)
NLS += nls_cp860.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_860),m)
  M_OBJS += nls_cp860.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_861),y)
NLS += nls_cp861.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_861),m)
  M_OBJS += nls_cp861.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_862),y)
NLS += nls_cp862.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_862),m)
  M_OBJS += nls_cp862.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_863),y)
NLS += nls_cp863.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_863),m)
  M_OBJS += nls_cp863.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_864),y)
NLS += nls_cp864.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_864),m)
  M_OBJS += nls_cp864.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_865),y)
NLS += nls_cp865.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_865),m)
  M_OBJS += nls_cp865.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_866),y)
NLS += nls_cp866.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_866),m)
  M_OBJS += nls_cp866.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_869),y)
NLS += nls_cp869.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_869),m)
  M_OBJS += nls_cp869.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_874),y)
NLS += nls_cp874.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_874),m)
  M_OBJS += nls_cp874.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1250),y)
NLS += nls_cp1250.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1250),m)
  M_OBJS += nls_cp1250.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1251),y)
NLS += nls_cp1251.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1251),m)
  M_OBJS += nls_cp1251.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1252),y)
NLS += nls_cp1252.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1252),m)
  M_OBJS += nls_cp1252.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1253),y)
NLS += nls_cp1253.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1253),m)
  M_OBJS += nls_cp1253.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1254),y)
NLS += nls_cp1254.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1254),m)
  M_OBJS += nls_cp1254.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1255),y)
NLS += nls_cp1255.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1255),m)
  M_OBJS += nls_cp1255.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1256),y)
NLS += nls_cp1256.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1256),m)
  M_OBJS += nls_cp1256.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1257),y)
NLS += nls_cp1257.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1257),m)
  M_OBJS += nls_cp1257.o
  endif
endif

ifeq ($(CONFIG_NLS_CODEPAGE_1258),y)
NLS += nls_cp1258.o
else
  ifeq ($(CONFIG_NLS_CODEPAGE_1258),m)
  M_OBJS += nls_cp1258.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_1),y)
NLS += nls_iso8859-1.o
else
  ifeq ($(CONFIG_NLS_ISO8859_1),m)
  M_OBJS += nls_iso8859-1.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_2),y)
NLS += nls_iso8859-2.o
else
  ifeq ($(CONFIG_NLS_ISO8859_2),m)
  M_OBJS += nls_iso8859-2.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_3),y)
NLS += nls_iso8859-3.o
else
  ifeq ($(CONFIG_NLS_ISO8859_3),m)
  M_OBJS += nls_iso8859-3.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_4),y)
NLS += nls_iso8859-4.o
else
  ifeq ($(CONFIG_NLS_ISO8859_4),m)
  M_OBJS += nls_iso8859-4.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_5),y)
NLS += nls_iso8859-5.o
else
  ifeq ($(CONFIG_NLS_ISO8859_5),m)
  M_OBJS += nls_iso8859-5.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_6),y)
NLS += nls_iso8859-6.o
else
  ifeq ($(CONFIG_NLS_ISO8859_6),m)
  M_OBJS += nls_iso8859-6.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_7),y)
NLS += nls_iso8859-7.o
else
  ifeq ($(CONFIG_NLS_ISO8859_7),m)
  M_OBJS += nls_iso8859-7.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_8),y)
NLS += nls_iso8859-8.o
else
  ifeq ($(CONFIG_NLS_ISO8859_8),m)
  M_OBJS += nls_iso8859-8.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_9),y)
NLS += nls_iso8859-9.o
else
  ifeq ($(CONFIG_NLS_ISO8859_9),m)
  M_OBJS += nls_iso8859-9.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_10),y)
NLS += nls_iso8859-10.o
else
  ifeq ($(CONFIG_NLS_ISO8859_10),m)
  M_OBJS += nls_iso8859-10.o
  endif
endif

ifeq ($(CONFIG_NLS_ISO8859_15),y)
NLS += nls_iso8859-15.o
else
  ifeq ($(CONFIG_NLS_ISO8859_15),m)
  M_OBJS += nls_iso8859-15.o
  endif
endif

ifeq ($(CONFIG_NLS_KOI8_R),y)
NLS += nls_koi8-r.o
else
  ifeq ($(CONFIG_NLS_KOI8_R),m)
  M_OBJS += nls_koi8-r.o
  endif
endif

O_TARGET = nls.o
OX_OBJS  = $(NLS)

include $(TOPDIR)/Rules.make
