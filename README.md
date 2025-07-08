pacredir
========

[![GitHub stars](https://img.shields.io/github/stars/eworm-de/pacredir?logo=GitHub&style=flat&color=red)](https://github.com/eworm-de/pacredir/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/eworm-de/pacredir?logo=GitHub&style=flat&color=green)](https://github.com/eworm-de/pacredir/network)
[![GitHub watchers](https://img.shields.io/github/watchers/eworm-de/pacredir?logo=GitHub&style=flat&color=blue)](https://github.com/eworm-de/pacredir/watchers)

**pacredir - redirect pacman requests, assisted by... nothing**

(Work in progress - currently only static hosts are supported.)

By default every [Arch Linux ↗️](https://www.archlinux.org/) installation
downloads its package files from online mirrors, transferring all the
bits via WAN connection.

But often other Arch systems may be around that already have the files
available on local storage - just a fast LAN connection away. This is
where `pacredir` can help.

*Use at your own risk*, pay attention to
[license and warranty](#license-and-warranty), and
[disclaimer on external links](#disclaimer-on-external-links)!

Requirements
------------

To compile and run `pacredir` you need:

* [systemd ↗️](https://www.github.com/systemd/systemd)
* [libmicrohttpd ↗️](https://www.gnu.org/software/libmicrohttpd/)
* [curl ↗️](https://curl.haxx.se/)
* [iniparser ↗️](https://github.com/ndevilla/iniparser)
* [darkhttpd ↗️](https://unix4lyfe.org/darkhttpd/)
* [markdown ↗️](https://daringfireball.net/projects/markdown/) (HTML documentation)

`Arch Linux` installs development files for the packages by default, so
no additional development packages are required.

Build and install
-----------------

Building and installing is very easy. Just run:

    make

followed by:

    make install

This will place an executable at `/usr/bin/pacredir`,
documentation can be found in `/usr/share/doc/pacredir/`.
Additionally systemd service files are installed to
`/usr/lib/systemd/system/`.

Usage
-----

Enable systemd services `pacserve` and `pacredir`, open TCP
port `7078` and add the following line to your repository
definitions in `pacman.conf`:

    Include = /etc/pacman.d/pacredir

To get a better idea what happens in the background have a look at
[the request flow chart](FLOW.md).

### Databases from cache server

By default databases are not fetched from cache servers. To make that
happen just add the server twice, additionally as regular server. This
is prepared in `/etc/pacman.d/pacredir`, just uncomment the corresponding
line.

Do not worry if `pacman` reports the following after the change:

    error: failed retrieving file 'core.db' from 127.0.0.1:7077 : The requested URL returned error: 404 Not Found

This is ok, it just tells `pacman` that `pacredir` could not find a file
and downloading it from an official server is required.

Please note that `pacredir` redirects to the most recent database file
found on the local network if it is not too old (currently 24 hours). To
make sure you really do have the latest files run `pacman -Syu` *twice*.

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

Disclaimer on external links
----------------------------

Our website contains links to the websites of third parties ("external
links"). As the content of these websites is not under our control, we
cannot assume any liability for such external content. In all cases, the
provider of information of the linked websites is liable for the content
and accuracy of the information provided. At the point in time when the
links were placed, no infringements of the law were recognisable to us.
As soon as an infringement of the law becomes known to us, we will
immediately remove the link in question.

> 💡️ **Hint**: All external links are marked with an arrow pointing
> diagonally in an up-right (or north-east) direction (↗️).

### Upstream

URL:
[GitHub.com](https://github.com/eworm-de/pacredir#pacredir)

Mirror:
[eworm.de](https://git.eworm.de/cgit.cgi/pacredir/about/)
[GitLab.com](https://gitlab.com/eworm-de/pacredir#pacredir)

---
[⬆️ Go back to top](#top)
