# pacserve - serve pacman cache and redirect via avahi service

CC	:= gcc
MD	:= markdown
INSTALL	:= install
RM	:= rm
CFLAGS	+= -O2 -Wall -Werror
CFLAGS	+= $(shell pkg-config --libs --cflags libcurl)
CFLAGS	+= $(shell pkg-config --libs --cflags avahi-client)
CFLAGS	+= $(shell pkg-config --libs --cflags libmicrohttpd)
VERSION := $(shell git describe --tags --long 2>/dev/null)
# this is just a fallback in case you do not use git but downloaded
# a release tarball...
ifeq ($(VERSION),)
VERSION := 0.0.1
endif

all: pacredir README.html

pacredir: pacredir.c
	$(CC) $(CFLAGS) -o pacredir pacredir.c \
		-DVERSION="\"$(VERSION)\""

README.html: README.md
	$(MD) README.md > README.html

install: install-bin install-doc

install-bin: pacredir
	$(INSTALL) -D -m0755 pacredir $(DESTDIR)/usr/bin/pacredir
	$(INSTALL) -D -m0644 pacman/pacserve $(DESTDIR)/etc/pacman.d/pacserve
	$(INSTALL) -D -m0644 avahi/pacserve.service $(DESTDIR)/etc/avahi/services/pacserve.service
	$(INSTALL) -D -m0644 avahi/pacdbserve.service $(DESTDIR)/etc/avahi/services/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacserve.service $(DESTDIR)/usr/lib/systemd/system/pacserve.service
	$(INSTALL) -D -m0644 systemd/pacdbserve.service $(DESTDIR)/usr/lib/systemd/system/pacdbserve.service
	$(INSTALL) -D -m0644 systemd/pacredir.service $(DESTDIR)/usr/lib/systemd/system/pacredir.service

install-doc: README.html
	$(INSTALL) -D -m0644 README.md $(DESTDIR)/usr/share/doc/pacserve/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)/usr/share/doc/pacserve/README.html

clean:
	$(RM) -f *.o *~ README.html pacredir
