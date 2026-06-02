#!/bin/sh

set -e

NAME="${1%.*}"
EXT="${1#*.}"
SHA1="$(sha1sum "${1}" | cut -d' ' -f1)"
SIZE="$(stat --format='%s' "${1}")"

cat <<-EOF
	#ifndef ${NAME^^}_H
	#define ${NAME^^}_H
	#include "static_file.h"
	
	static_file ${NAME} = {
	  .date = "${DATE}",
	  .mime = MIME_${EXT^^},
	  .sha1 = "${SHA1}",
	  .size = ${SIZE},
	  .content = {
	EOF
od -t x1 -A n -v < "${1}" | \
	sed -e 's/^/    /' \
	    -e 's/\([0-9a-f]\{2\}\)/0x\1,/g'
cat <<-EOF
	  }
	};
	#endif
	EOF
