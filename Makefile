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

st.o: combining.h compose.h config.h st.h win.h
x.o: arg.h config.h paste.c st.h win.h

$(OBJ): config.h config.mk

phist: $(OBJ)
	$(CC) -o $@ $(OBJ) $(STLDFLAGS)

clean:
	rm -f phist $(OBJ) phist-$(VERSION).tar.gz
	rm -rf .terminfo

dist: clean
	mkdir -p phist-$(VERSION)
	cp -R LEGACY LICENSE Makefile README.md config.mk doc tests\
		config.def.h st.info st.1 arg.h combining.h compose.h st.h win.h paste.c $(SRC)\
		phist-$(VERSION)
	tar -cf - phist-$(VERSION) | gzip > phist-$(VERSION).tar.gz
	rm -rf phist-$(VERSION)

terminfo: .terminfo

.terminfo: st.info
	tic -sx -o $@ st.info
	touch $@

install: phist terminfo
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f phist $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/phist
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st.1 > $(DESTDIR)$(MANPREFIX)/man1/phist.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/phist.1
	mkdir -p $(DESTDIR)/etc/terminfo
	cp -R .terminfo/. $(DESTDIR)/etc/terminfo
	@echo See README.md for the phist terminfo entries.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/phist
	rm -f $(DESTDIR)$(MANPREFIX)/man1/phist.1

.PHONY: all clean dist terminfo install uninstall
