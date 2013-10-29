# paccache - serve pacman cache and redirect via avahi service

CC	:= gcc
MD	:= markdown
INSTALL	:= install
CP	:= cp
RM	:= rm
SED	:= sed
CFLAGS	+= -O2 -Wall -Werror
CFLAGS	+= $(shell pkg-config --libs --cflags libcurl)
CFLAGS	+= $(shell pkg-config --libs --cflags avahi-client)
CFLAGS	+= $(shell pkg-config --libs --cflags libmicrohttpd)
VERSION := $(shell git describe --tags --long 2>/dev/null)
ARCH	:= $(shell uname -m)
# this is just a fallback in case you do not use git but downloaded
# a release tarball...
ifeq ($(VERSION),)
VERSION := 0.0.3
endif

all: pacredir pacdbserve README.html

pacredir: pacredir.c config.h
	$(CC) $(CFLAGS) -o pacredir pacredir.c \
		-DVERSION="\"$(VERSION)\""

config.h:
	$(CP) config.def.h config.h

pacdbserve: avahi/pacdbserve.service.in
	$(SED) 's/%ARCH%/$(ARCH)/' avahi/pacdbserve.service.in > avahi/pacdbserve.service

README.html: README.md
	$(MD) README.md > README.html

install: install-bin install-doc

install-bin: pacredir
	$(INSTALL) -D -m0755 pacredir $(DESTDIR)/usr/bin/pacredir
	$(INSTALL) -D -m0644 pacman/paccache $(DESTDIR)/etc/pacman.d/paccache
	$(INSTALL) -D -m0644 avahi/pacserve.service $(DESTDIR)/etc/avahi/services/pacserve.service
	$(INSTALL) -D -m0644 avahi/pacdbserve.service $(DESTDIR)/etc/avahi/services/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacserve.service $(DESTDIR)/usr/lib/systemd/system/pacserve.service
	$(INSTALL) -D -m0644 systemd/pacdbserve.service $(DESTDIR)/usr/lib/systemd/system/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacredir.service $(DESTDIR)/usr/lib/systemd/system/pacredir.service

install-doc: README.html
	$(INSTALL) -D -m0644 README.md $(DESTDIR)/usr/share/doc/paccache/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)/usr/share/doc/paccache/README.html

clean:
	$(RM) -f *.o *~ README.html pacredir
