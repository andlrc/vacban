CFLAGS	= -O2 -Wall -Werror -Wno-unused-function
DFLAGS	= -g -Wall -Werror -Wno-unused-function -DVACBAN_DEBUG=1
YFLAGS	= -d

vacban:	vacdb.o vacban.o
	$(CC) $(CFLAGS) -o vacban \
		vacdb.o vacban.o \
		-lcurl

vacdb.o:	vacdb.h vacdb.c
vacban.o:	vacban.h vacban.c

debug:	clean
	$(MAKE) CFLAGS="$(DFLAGS)"

README:	vacban.1
	LC_ALL=C MANWIDTH=80 man -l vacban.1 > README

clean:
	-rm *.o vacban
