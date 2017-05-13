# pacredir - redirect pacman requests, assisted by avahi service discovery

PREFIX		:= /usr
REPRODUCIBLE	:= 0

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

# the distribution ID
ARCH	:= $(shell shopt -u extglob && source /etc/makepkg.conf && echo $$CARCH)
ID	:= $(shell grep 'ID=' < /etc/os-release | cut -d= -f2)

# this is just a fallback in case you do not use git but downloaded
# a release tarball...
VERSION := 0.2.2

all: pacredir avahi/pacdbserve.service avahi/pacserve.service README.html

pacredir: pacredir.c pacredir.h config.h version.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS) -DREPRODUCIBLE=$(REPRODUCIBLE) -DARCH=\"$(ARCH)\" -DID=\"$(ID)\" -o pacredir pacredir.c

config.h:
	$(CP) config.def.h config.h

version.h: $(wildcard .git/HEAD .git/index .git/refs/tags/*) Makefile
	echo "#ifndef VERSION" > $@
	echo "#define VERSION \"$(shell git describe --tags --long 2>/dev/null || echo ${VERSION})\"" >> $@
	echo "#endif" >> $@

avahi/pacdbserve.service: avahi/pacdbserve.service.in
	$(SED) 's/%ARCH%/$(ARCH)/;s/%ID%/$(ID)/' avahi/pacdbserve.service.in > avahi/pacdbserve.service

avahi/pacserve.service: avahi/pacserve.service.in
	$(SED) 's/%ID%/$(ID)/' avahi/pacserve.service.in > avahi/pacserve.service

README.html: README.md
	$(MD) README.md > README.html

install: install-bin install-doc

install-bin: pacredir avahi/pacdbserve.service avahi/pacserve.service
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
	$(INSTALL) -D -m0644 README.md $(DESTDIR)$(PREFIX)/share/doc/pacredir/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)$(PREFIX)/share/doc/pacredir/README.html

clean:
	$(RM) -f *.o *~ pacredir avahi/pacdbserve.service avahi/pacserve.service README.html version.h

distclean:
	$(RM) -f *.o *~ pacredir avahi/pacdbserve.service avahi/pacserve.service README.html version.h config.h

release:
	git archive --format=tar.xz --prefix=pacredir-$(VERSION)/ $(VERSION) > pacredir-$(VERSION).tar.xz
	gpg -ab pacredir-$(VERSION).tar.xz
