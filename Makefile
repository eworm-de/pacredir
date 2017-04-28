# paccache - serve pacman cache and redirect via avahi service

PREFIX	:= /usr

# commands
CC	:= gcc
CP	:= cp
INSTALL	:= install
LN	:= ln
MD	:= markdown
RM	:= rm
SED	:= sed

# flags
CFLAGS	+= -std=c11 -O2 -fPIC -Wall -Werror
CFLAGS_EXTRA	+= -lpthread
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libsystemd)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libcurl)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags avahi-client)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libmicrohttpd)
CFLAGS_EXTRA	+= -liniparser
LDFLAGS	+= -Wl,-z,now -Wl,-z,relro -pie

# this is just a fallback in case you do not use git but downloaded
# a release tarball...
VERSION := 0.1.25

all: pacredir avahi/pacdbserve.service README.html

arch: arch.c arch.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o arch arch.c

pacredir: pacredir.c arch.h pacredir.h config.h version.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS) -o pacredir pacredir.c

config.h:
	$(CP) config.def.h config.h

version.h: $(wildcard .git/HEAD .git/index .git/refs/tags/*) Makefile
	echo "#ifndef VERSION" > $@
	echo "#define VERSION \"$(shell git describe --tags --long 2>/dev/null || echo ${VERSION})\"" >> $@
	echo "#endif" >> $@

avahi/pacdbserve.service: arch avahi/pacdbserve.service.in
	$(SED) 's/%ARCH%/$(shell ./arch)/' avahi/pacdbserve.service.in > avahi/pacdbserve.service

README.html: README.md
	$(MD) README.md > README.html

install: install-bin install-doc

install-bin: pacredir
	$(INSTALL) -D -m0755 pacredir $(DESTDIR)$(PREFIX)/bin/pacredir
	$(LN) -s darkhttpd $(DESTDIR)$(PREFIX)/bin/pacserve
	$(LN) -s darkhttpd $(DESTDIR)$(PREFIX)/bin/pacdbserve
	$(INSTALL) -D -m0644 pacredir.conf $(DESTDIR)/etc/pacredir.conf
	$(INSTALL) -D -m0644 pacman/pacredir $(DESTDIR)/etc/pacman.d/pacredir
	$(INSTALL) -D -m0644 avahi/pacserve.service $(DESTDIR)/etc/avahi/services/pacserve.service
	$(INSTALL) -D -m0644 avahi/pacdbserve.service $(DESTDIR)/etc/avahi/services/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacdbserve.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacredir.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacredir.service
	$(INSTALL) -D -m0644 systemd/pacserve.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacserve.service
	$(INSTALL) -D -m0644 initcpio/hooks/pacredir $(DESTDIR)$(PREFIX)/lib/initcpio/hooks/pacredir
	$(INSTALL) -D -m0644 initcpio/install/pacredir $(DESTDIR)$(PREFIX)/lib/initcpio/install/pacredir
	$(INSTALL) -D -m0644 dhcpcd/80-pacredir $(DESTDIR)$(PREFIX)/lib/dhcpcd/dhcpcd-hooks/80-pacredir
	$(INSTALL) -D -m0755 networkmanager/80-pacredir $(DESTDIR)/etc/NetworkManager/dispatcher.d/80-pacredir

install-doc: README.html
	$(INSTALL) -D -m0644 README.md $(DESTDIR)$(PREFIX)/share/doc/paccache/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)$(PREFIX)/share/doc/paccache/README.html

clean:
	$(RM) -f *.o *~ arch pacredir avahi/pacdbserve.service README.html version.h

distclean:
	$(RM) -f *.o *~ arch pacredir avahi/pacdbserve.service README.html version.h config.h

release:
	git archive --format=tar.xz --prefix=paccache-$(VERSION)/ $(VERSION) > paccache-$(VERSION).tar.xz
	gpg -ab paccache-$(VERSION).tar.xz
