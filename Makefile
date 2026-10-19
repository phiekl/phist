# phist - phi's simple terminal (st fork)
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = st.c x.c
OBJ = $(SRC:.c=.o)

all: phist

config.h:
	cp config.def.h config.h

.c.o:
	$(CC) $(STCFLAGS) -c $<

st.o: config.h st.h win.h
x.o: arg.h config.h st.h win.h

$(OBJ): config.h config.mk

phist: $(OBJ)
	$(CC) -o $@ $(OBJ) $(STLDFLAGS)

clean:
	rm -f phist $(OBJ) phist-$(VERSION).tar.gz

dist: clean
	mkdir -p phist-$(VERSION)
	cp -R LEGACY LICENSE Makefile README.md config.mk\
		config.def.h st.info st.1 arg.h st.h win.h $(SRC)\
		phist-$(VERSION)
	tar -cf - phist-$(VERSION) | gzip > phist-$(VERSION).tar.gz
	rm -rf phist-$(VERSION)

install: phist
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f phist $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/phist
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st.1 > $(DESTDIR)$(MANPREFIX)/man1/phist.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/phist.1
	tic -sx st.info
	@echo See README.md for the phist terminfo entries.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/phist
	rm -f $(DESTDIR)$(MANPREFIX)/man1/phist.1

.PHONY: all clean dist install uninstall
