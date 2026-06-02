#!/bin/sh

set -e

function bytes() {
	printf "%s %s" \
		"$(printf "${1}" | wc -c)" \
		"$(printf "${1}" | od -t d1 -A n)" | \
			tr -s ' '
}

sed \
	-e "s/%ARCH%/${ARCH}/" \
	-e "s/%ARCH_BYTES%/$(bytes ${ARCH})/" \
	-e "s/%ID%/${ID}/" \
	-e "s/%ID_BYTES%/$(bytes ${ID})/" \
	"${1}"
