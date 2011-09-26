AM_CPPFLAGS		= -I$(top_srcdir)/lib/include -I$(top_builddir)/lib/include

AM_CFLAGS		= -Wall -Wno-format

lib_LIBRARIES		= libmcast_group.a

libmcast_group_a_SOURCES	= election.c		\
				  format.c		\
				  main.c		\
				  rx.c			\
				  time.c		\
				  tx.c

LDADD			= $(top_builddir)/libivykis.la libmcast_group.a -lcrypto -luuid

noinst_PROGRAMS		= test

test_SOURCES		= test.c
