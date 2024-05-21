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
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags iniparser)
LDFLAGS	+= -Wl,-z,now -Wl,-z,relro -pie

# the distribution ID
ARCH	:= $(shell shopt -u extglob && source /etc/makepkg.conf && echo $$CARCH)
ID	:= $(shell shopt -u extglob && source /etc/os-release && echo $$ID)

# this is just a fallback in case you do not use git but downloaded
# a release tarball...
VERSION := 0.4.7

MARKDOWN = $(wildcard *.md)
HTML = $(MARKDOWN:.md=.html)

all: pacredir avahi/pacserve.service $(HTML)

pacredir: pacredir.c pacredir.h config.h version.h
	$(CC) pacredir.c $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS) -DREPRODUCIBLE=$(REPRODUCIBLE) -DARCH=\"$(ARCH)\" -DID=\"$(ID)\" -o pacredir

config.h:
	$(CP) config.def.h config.h

version.h: $(wildcard .git/HEAD .git/index .git/refs/tags/*) Makefile
	printf "#ifndef VERSION\n#define VERSION \"%s\"\n#endif\n" $(shell git describe --long 2>/dev/null || echo ${VERSION}) > $@

avahi/pacserve.service: avahi/pacserve.service.in
	$(SED) 's/%ARCH%/$(ARCH)/;s/%ID%/$(ID)/' avahi/pacserve.service.in > avahi/pacserve.service

%.html: %.md Makefile
	markdown $< | sed 's/href="\([-[:alnum:]]*\)\.md"/href="\1.html"/g' > $@

install: install-bin install-doc

install-bin: pacredir avahi/pacserve.service
	$(INSTALL) -D -m0755 pacredir $(DESTDIR)$(PREFIX)/bin/pacredir
	$(LN) -s darkhttpd $(DESTDIR)$(PREFIX)/bin/pacserve
	$(INSTALL) -D -m0644 pacredir.conf $(DESTDIR)/etc/pacredir.conf
	$(INSTALL) -D -m0644 pacman/pacredir $(DESTDIR)/etc/pacman.d/pacredir
	$(INSTALL) -D -m0644 avahi/pacserve.service $(DESTDIR)/etc/avahi/services/pacserve.service
	$(INSTALL) -D -m0644 systemd/pacredir.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacredir.service
	$(INSTALL) -D -m0644 systemd/pacserve.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacserve.service
	$(INSTALL) -D -m0644 systemd/sysusers.conf $(DESTDIR)$(PREFIX)/lib/sysusers.d/pacredir.conf
	$(INSTALL) -D -m0644 systemd/tmpfiles.conf $(DESTDIR)$(PREFIX)/lib/tmpfiles.d/pacredir.conf
	$(INSTALL) -D -m0644 initcpio/hooks/pacredir $(DESTDIR)$(PREFIX)/lib/initcpio/hooks/pacredir
	$(INSTALL) -D -m0644 initcpio/install/pacredir $(DESTDIR)$(PREFIX)/lib/initcpio/install/pacredir
	$(INSTALL) -D -m0644 dhcpcd/80-pacredir $(DESTDIR)$(PREFIX)/lib/dhcpcd/dhcpcd-hooks/80-pacredir
	$(INSTALL) -D -m0755 networkmanager/80-pacredir $(DESTDIR)$(PREFIX)/lib/NetworkManager/dispatcher.d/80-pacredir

install-doc: $(HTML)
	$(INSTALL) -d -m0755 $(DESTDIR)$(PREFIX)/share/doc/pacredir/
	$(INSTALL) -D -m0644 $(MARKDOWN) $(HTML) -t $(DESTDIR)$(PREFIX)/share/doc/pacredir/
	$(INSTALL) -d -m0755 $(DESTDIR)$(PREFIX)/share/doc/pacredir/FLOW/
	$(INSTALL) -D -m0644 $(wildcard FLOW/*) -t $(DESTDIR)$(PREFIX)/share/doc/pacredir/FLOW/

clean:
	$(RM) -f *.o *~ pacredir avahi/pacserve.service $(HTML) version.h

distclean:
	$(RM) -f *.o *~ pacredir avahi/pacserve.service $(HTML) version.h config.h

release:
	git archive --format=tar.xz --prefix=pacredir-$(VERSION)/ $(VERSION) > pacredir-$(VERSION).tar.xz
	gpg --armor --detach-sign --comment pacredir-$(VERSION).tar.xz pacredir-$(VERSION).tar.xz
	git notes --ref=refs/notes/signatures/tar add -C $$(git archive --format=tar --prefix=pacredir-$(VERSION)/ $(VERSION) | gpg --armor --detach-sign --comment pacredir-$(VERSION).tar | git hash-object -w --stdin) $(VERSION)
