# Example makefile for CPE 465
#

CC = gcc
CFLAGS = -g -Wall -Werror
OS = $(shell uname -s)
PROC = $(shell uname -p)
EXEC_SUFFIX=$(OS)-$(PROC)

ifeq ("$(OS)", "SunOS")
	OSLIB=-L/opt/csw/lib -R/opt/csw/lib -lsocket -lnsl
	OSINC=-I/opt/csw/include
	OSDEF=-DSOLARIS
else
ifeq ("$(OS)", "Darwin")
	OSLIB=
	OSINC=
	OSDEF=-DDARWIN
else
	OSLIB=
	OSINC=
	OSDEF=-DLINUX
endif
endif

all:  controller-$(EXEC_SUFFIX)

controller-$(EXEC_SUFFIX): controller.c rw_packets.c
	$(CC) $(CFLAGS) $(OSINC) $(OSLIB) $(OSDEF) -o $@ controller.c checksum.c rw_packets.c smartalloc.c -lpcap

handin: README
	handin bellardo p2_m2 README smartalloc.c smartalloc.h  Makefile

clean:
	-rm -rf controller-* controller-*.dSYM
