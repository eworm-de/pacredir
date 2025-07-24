# pacredir - redirect pacman requests, assisted by mDNS Service Discovery

PREFIX			:= /usr
SOURCE_DATE_EPOCH	?= $(shell date +%s)

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
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libsystemd)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libcurl)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags libmicrohttpd)
CFLAGS_EXTRA	+= $(shell pkg-config --libs --cflags iniparser)
LDFLAGS	+= -Wl,-z,now -Wl,-z,relro -pie

# the distribution ID
ARCH	:= $(shell shopt -u extglob && source /etc/makepkg.conf && echo $$CARCH)
ID	:= $(shell shopt -u extglob && source /etc/os-release && echo $$ID)

# this is just a fallback in case you do not use git but downloaded
# a release tarball...
DISTVER := 0.7.4
VERSION ?= $(shell git describe --long 2>/dev/null || echo $(DISTVER))

SERVICESIN	= $(wildcard */*.service.in)
SERVICES	= $(SERVICESIN:.in=)
MARKDOWN	= $(wildcard *.md)
HTML		= $(MARKDOWN:.md=.html)

all: pacredir $(SERVICES) $(HTML)

pacredir: pacredir.c pacredir.h config.h favicon.h html.h version.h
	$(CC) $< $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS) -o $@

config.h: config.def.h
	$(CP) $< $@

version.h: $(wildcard .git/HEAD .git/index .git/refs/tags/*) Makefile
	printf '#ifndef VERSION_H\n#define VERSION_H\n#define VERSION\t"%s"\n#define ARCH\t"%s"\n#define ID\t"%s"\n#endif\n' "${VERSION}" "$(ARCH)" "$(ID)" > $@

favicon.png: logo.svg Makefile
	rsvg-convert --width 32 --height 32 $< | oxipng - > $@

favicon.h: favicon.png Makefile
	printf '#ifndef FAVICON_H\n#define FAVICON_H\nstatic unsigned char favicon[] = {\n' > $@
	od -t x1 -A n -v < $< | sed 's/\([0-9a-f]\{2\}\)/0x\1,/g' >> $@
	printf '};\n#define FAVICON_SHA1 "%s"\n#define FAVICON_DATE "%s"\n#endif\n' "$(shell sha1sum $< | cut -d' ' -f1)" "$(shell date --utc --date=@$(SOURCE_DATE_EPOCH) '+%a, %d %b %Y %H:%M:%S GMT')" >> $@

%.service: %.service.in
	$(SED) 's/%ARCH%/$(ARCH)/; s/%ARCH_BYTES%/$(shell (printf $(ARCH) | wc -c; printf $(ARCH) | od -t d1 -A n) | tr -s " ")/; s/%ID%/$(ID)/; s/%ID_BYTES%/$(shell (printf $(ID) | wc -c; printf $(ID) | od -t d1 -A n) | tr -s " ")/' $< > $@

%.html: %.md Makefile
	markdown $< | sed 's/href="\([-[:alnum:]]*\)\.md"/href="\1.html"/g' > $@

install: install-bin install-doc

install-bin: pacredir systemd/pacserve.service
	$(INSTALL) -D -m0755 pacredir $(DESTDIR)$(PREFIX)/bin/pacredir
	$(LN) -s darkhttpd $(DESTDIR)$(PREFIX)/bin/pacserve
	$(INSTALL) -D -m0644 etc/pacredir.conf $(DESTDIR)/etc/pacredir.conf
	$(INSTALL) -D -m0644 etc/pacserve.conf $(DESTDIR)/etc/pacserve.conf
	$(INSTALL) -D -m0644 etc/01-pacredir-MulticastDNS-yes.conf $(DESTDIR)/etc/systemd/resolved.conf.d/01-pacredir-MulticastDNS-yes.conf
	$(INSTALL) -D -m0644 pacman/pacredir $(DESTDIR)/etc/pacman.d/pacredir
	$(INSTALL) -D -m0644 systemd/pacredir.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacredir.service
	$(INSTALL) -D -m0644 systemd/pacserve.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacserve.service
	$(INSTALL) -D -m0644 systemd/sysusers.conf $(DESTDIR)$(PREFIX)/lib/sysusers.d/pacredir.conf
	$(INSTALL) -D -m0644 systemd/tmpfiles.conf $(DESTDIR)$(PREFIX)/lib/tmpfiles.d/pacredir.conf
	$(INSTALL) -D -m0644 desktop/pacredir-status.desktop $(DESTDIR)$(PREFIX)/share/applications/pacredir-status.desktop
	$(INSTALL) -D -m0644 logo.png $(DESTDIR)$(PREFIX)/share/pixmaps/pacredir.png
	$(INSTALL) -D -m0644 dispatch/dhcpcd $(DESTDIR)$(PREFIX)/lib/dhcpcd/dhcpcd-hooks/80-pacredir
	$(INSTALL) -D -m0755 dispatch/networkd $(DESTDIR)$(PREFIX)/lib/networkd-dispatcher/routable.d/80-pacredir
	$(INSTALL) -D -m0755 dispatch/networkmanager $(DESTDIR)$(PREFIX)/lib/NetworkManager/dispatcher.d/80-pacredir

install-doc: $(HTML)
	$(INSTALL) -d -m0755 $(DESTDIR)$(PREFIX)/share/doc/pacredir/
	$(INSTALL) -D -m0644 $(MARKDOWN) $(HTML) logo.svg -t $(DESTDIR)$(PREFIX)/share/doc/pacredir/
	$(INSTALL) -d -m0755 $(DESTDIR)$(PREFIX)/share/doc/pacredir/README.d/
	$(INSTALL) -D -m0644 $(wildcard README.d/*) -t $(DESTDIR)$(PREFIX)/share/doc/pacredir/README.d/
	$(INSTALL) -d -m0755 $(DESTDIR)$(PREFIX)/share/doc/pacredir/FLOW.d/
	$(INSTALL) -D -m0644 $(wildcard FLOW.d/*) -t $(DESTDIR)$(PREFIX)/share/doc/pacredir/FLOW.d/

install-avahi: compat/pacserve-announce.service
	$(INSTALL) -D -m0644 compat/avahi.conf $(DESTDIR)$(PREFIX)/lib/systemd/system/pacserve.service.d/avahi.conf
	$(INSTALL) -D -m0644 compat/pacserve-announce.service $(DESTDIR)$(PREFIX)/lib/systemd/system/pacserve-announce.service
	$(INSTALL) -D -m0644 compat/02-pacredir-avahi-MulticastDNS-resolve.conf $(DESTDIR)/etc/systemd/resolved.conf.d/02-pacredir-avahi-MulticastDNS-resolve.conf

clean:
	$(RM) -f *.o *~ pacredir $(SERVICES) $(HTML) favicon.png favicon.h version.h

distclean:
	$(RM) -f *.o *~ pacredir $(SERVICES) $(HTML) version.h config.h

release:
	git archive --format=tar.xz --prefix=pacredir-$(DISTVER)/ $(DISTVER) > pacredir-$(DISTVER).tar.xz
	gpg --armor --detach-sign --comment pacredir-$(DISTVER).tar.xz pacredir-$(DISTVER).tar.xz
	git notes --ref=refs/notes/signatures/tar add -C $$(git archive --format=tar --prefix=pacredir-$(DISTVER)/ $(DISTVER) | gpg --armor --detach-sign --comment pacredir-$(DISTVER).tar | git hash-object -w --stdin) $(DISTVER)
