/*
 * (C) 2013-2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <stdio.h>
#include <stdlib.h>

#include "config.h"

/*** main ***/
int main(int argc, char ** argv) {
	/* just print the architecture */
	puts(ARCH);

	return EXIT_SUCCESS;
}

// vim: set syntax=c:
