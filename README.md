pacredir
========

**pacredir - redirect pacman requests, assisted by avahi service discovery**

By default every [Arch Linux](https://www.archlinux.org/) installation
downloads its package files from online mirrors, transferring all the
bits via WAN connection.

But often other Arch systems may be around that already have the files
available on local storage - just a fast LAN connection away. This is
where `pacredir` can help. It uses [Avahi](http://avahi.org/) to find
other instances and get the files there if available.

Requirements
------------

To compile and run `pacredir` you need:

* [systemd](https://www.github.com/systemd/systemd)
* [avahi](http://avahi.org/)
* [libmicrohttpd](http://www.gnu.org/software/libmicrohttpd/)
* [curl](http://curl.haxx.se/)
* [iniparser](http://ndevilla.free.fr/iniparser/)
* [darkhttpd](http://dmr.ath.cx/net/darkhttpd/)
* [markdown](http://daringfireball.net/projects/markdown/) (HTML documentation)

`Arch Linux` installs development files for the packages by default, so
no additional development packages are required.

Build and install
-----------------

Building and installing is very easy. Just run:

> make

followed by:

> make install

This will place an executable at `/usr/bin/pacredir`,
documentation can be found in `/usr/share/doc/pacredir/`.
Additionally systemd service files are installed to
`/usr/lib/systemd/system/` and avahi service files go to
`/etc/avahi/services/`.

Usage
-----

Enable systemd services `pacserve`, `pacdbserve` and `pacredir`, open TCP
ports `7078` and `7079` and add the following line to your repository
definitions in `pacman.conf`:

> Include = /etc/pacman.d/pacredir

Do not worry if `pacman` reports:

> error: failed retrieving file 'core.db' from localhost:7077 : The
> requested URL returned error: 404 Not Found

This is ok, it just tells `pacman` that `pacredir` could not find a file
and downloading it from an official server is required.

Please note that `pacredir` redirects to the most recent file found on
the local network. To make sure you really do have the latest files run
`pacman -Syu` *twice*.

Security
--------

There is no security within this project, information and file content
is transferred unencrypted and unverified. Anybody is free to serve
broken and/or malicious files to you, but this is by design. So make
sure `pacman` is configured to check signatures! It will then detect if
anything goes wrong.

### Upstream

URL: [GitHub.com](https://github.com/eworm-de/pacredir#pacredir)  
Mirror: [eworm.de](https://git.eworm.de/cgit.cgi/pacredir/about/)
