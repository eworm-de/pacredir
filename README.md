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
* [avahi](https://avahi.org/)
* [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)
* [curl](https://curl.haxx.se/)
* [iniparser](https://github.com/ndevilla/iniparser)
* [darkhttpd](https://unix4lyfe.org/darkhttpd/)
* [markdown](https://daringfireball.net/projects/markdown/) (HTML documentation)

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

Enable systemd services `pacserve` and `pacredir`, open TCP
port `7078` and add the following line to your repository
definitions in `pacman.conf`:

> Include = /etc/pacman.d/pacredir

Do not worry if `pacman` reports:

> error: failed retrieving file 'core.db' from 127.0.0.1:7077 : The
> requested URL returned error: 404 Not Found

This is ok, it just tells `pacman` that `pacredir` could not find a file
and downloading it from an official server is required.

Please note that `pacredir` redirects to the most recent database file
found on the local network if it is not too old (currently 24 hours). To
make sure you really do have the latest files run `pacman -Syu` *twice*.

To get a better idea what happens in the background have a look at
[the request flow chart](FLOW.md).

Current caveat
--------------

With its latest release `pacman` now supports a *server error limit*: Three
download errors from a server results in the server being skipped for the
remainder of this transaction.
However `pacredir` sends a "*404 - not found*" response if the file is not
available in local network - and is skipped after just three misses.

This new feature is not configurable at runtime, so rebuilding `pacman` with
one of the following patches is the way to make things work with `pacredir`.

### Disable server error limit

This is the simplest workaround - just disable the server error limit.

    --- a/lib/libalpm/dload.c
    +++ b/lib/libalpm/dload.c
    @@ -60,7 +60,7 @@ static int curl_gethost(const char *url, char *buffer, size_t buf_len);
     
     /* number of "soft" errors required to blacklist a server, set to 0 to disable
      * server blacklisting */
    -const unsigned int server_error_limit = 3;
    +const unsigned int server_error_limit = 0;
     
     struct server_error_count {
     	char server[HOSTNAME_SIZE];

We can agree this is not to be desired - in general the feature is reasonable.

### Support http header to indicate a soft failure

This solution is simple, yet powerful:
[Support http header 'Cache-Control: no-cache' for soft failure](patches/0001-support-http-header-Cache-Control-no-cache-for-soft-failure.patch)

By setting the HTTP header `Cache-Control: no-cache` when returning with
the status code `404` (not found) the server can indicate that this is a
soft failure. No error message is shown, and server's error count is
not increased.

Sadly upstream denied, again.

Anyway, pushed this as merge request to Gitlab, which was closed without
merging. 😢

~~[support http header 'Cache-Control: no-cache' for soft failure](https://gitlab.archlinux.org/pacman/pacman/-/merge_requests/76)~~

### Implement CacheServer

A more complex solution that breaks current API is:
[Implement CacheServer](patches/0001-implement-CacheServer.patch)

This implements a new configuration option `CacheServer`. Adding a cache
server makes it ignore the server error limit.

Handling for soft failures is demanded in a long standing upstream bug, and
the given patch could solve it:
[FS#23407 - Allow soft failures on Server URLs](https://bugs.archlinux.org/task/23407)

Pushed this as merge request to Gitlab:
[implement CacheServer](https://gitlab.archlinux.org/pacman/pacman/-/merge_requests/74)

Security
--------

There is no security within this project, information and file content
is transferred unencrypted and unverified. Anybody is free to serve
broken and/or malicious files to you, but this is by design. So make
sure `pacman` is configured to check signatures! It will then detect if
anything goes wrong.

License and warranty
--------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
[GNU General Public License](COPYING.md) for more details.

### Upstream

URL:
[GitHub.com](https://github.com/eworm-de/pacredir#pacredir)

Mirror:
[eworm.de](https://git.eworm.de/cgit.cgi/pacredir/about/)
[GitLab.com](https://gitlab.com/eworm-de/pacredir#pacredir)
