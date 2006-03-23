/******************************************************************************
 * check_apt.c: check for available updates in apt package management systems
 * original author: sean finney <seanius@seanius.net> 
 *                  (with some common bits stolen from check_nagios.c)
 ******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "check_apt";
const char *revision = "$Revision$";
const char *copyright = "2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "runcmd.h"
#include "utils.h"

#define APTGET_UPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq upgrade"
#define APTGET_DISTUPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq dist-upgrade"
#define APTGET_UPDATE "/usr/bin/apt-get -q update"

int process_arguments(int, char **);
void print_help(void);
void print_usage(void);

int run_update(void);
int run_upgrade(int *pkgcount);

static int verbose = 0;
static int do_update = 0;
static int dist_upgrade = 0;
static int stderr_warning = 0;
static int exec_warning = 0;

int main (int argc, char **argv) {
	int result=STATE_UNKNOWN, packages_available=0;

	if (process_arguments(argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* if they want to run apt-get update first... */
	if(do_update) result = run_update();

	/* apt-get upgrade */
	result = max_state(result, run_upgrade(&packages_available));

	if(stderr_warning){
		fprintf(stderr, "warning, output detected on stderr. ");
		fprintf(stderr, "re-run with -v for more information.\n");
	}

	if(packages_available > 0){
		result = max_state(result, STATE_WARNING);
	} else {
		result = max_state(result, STATE_OK);
	}

	printf("APT %s: %d packages available for %s.%s%s%s\n", 
	       state_text(result),
	       packages_available,
	       (dist_upgrade)?"dist-upgrade":"upgrade",
	       (stderr_warning)?" (warnings detected)":"",
	       (stderr_warning && exec_warning)?",":"",
	       (exec_warning)?" (errors detected)":""
	       );

	return result;
}

/* process command-line arguments */
int process_arguments (int argc, char **argv) {
	int c;

	static struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"update", no_argument, 0, 'u'},
		{"dist-upgrade", no_argument, 0, 'd'},
		{0, 0, 0, 0}
	};

	while(1) {
		c = getopt_long(argc, argv, "hVvt:ud", longopts, NULL);

		if(c == -1 || c == EOF || c == 1) break;

		switch(c) {
		case 'h':									/* help */
			print_help();
			exit(STATE_OK);
		case 'V':									/* version */
			print_revision(progname, revision);
			exit(STATE_OK);
		case 'v':
			verbose++;
			break;
		case 't':
			timeout_interval=atoi(optarg);
			break;
		case 'd':
			dist_upgrade=1;
			break;
		case 'u':
			do_update=1;
			break;
		default:
			/* print short usage statement if args not parsable */
			usage_va(_("Unknown argument - %s"), optarg);
		}
	}

	return OK;
}


/* informative help message */
void print_help(void){
	print_revision(progname, revision);
	printf(_(COPYRIGHT), copyright, email);
	printf(_("\
This plugin checks for software updates on systems that use\n\
package management systems based on the apt-get(8) command\n\
found in Debian GNU/Linux\n\
\n\n"));
	print_usage();
	printf(_(UT_HELP_VRSN));
	printf(_(UT_TIMEOUT), timeout_interval);
   	printf(_("\n\
 -d, --dist-upgrade\n\
   Perform a dist-upgrade instead of normal upgrade.\n\n\
The following options require root privileges and should be used with care: \
\n\n"));
   	printf(_("\
 -u, --update\n\
   First perform an 'apt-get update' (note: you may also need to use -t)\
\n\n"));
}

/* simple usage heading */
void print_usage(void){
	printf ("Usage: %s [-du] [-t timeout]\n", progname);
}

/* run an apt-get upgrade */
int run_upgrade(int *pkgcount){
	int i=0, result=STATE_UNKNOWN, pc=0;
	struct output chld_out, chld_err;

	/* run the upgrade */
	if(dist_upgrade==0){
		result = np_runcmd(APTGET_UPGRADE, &chld_out, &chld_err, 0);
	} else {
		result = np_runcmd(APTGET_DISTUPGRADE, &chld_out, &chld_err, 0);
	}
	/* apt-get only changes exit status if there is an internal error */
	if(result != 0){
		exec_warning=1;
		result = STATE_UNKNOWN;
		fprintf(stderr, "'%s' exited with non-zero status.\n%s\n",
		    APTGET_UPGRADE,
		    "Run again with -v for more info.");
	}

	/* parse the output, which should only consist of lines like
	 *
	 * Inst package ....
	 * Conf package ....
	 *
	 * so we'll filter based on "Inst".  If we ever want to do
	 */
	for(i = 0; i < chld_out.lines; i++) {
		if(strncmp(chld_out.line[i], "Inst", 4)==0){
			if(verbose){
				printf("%s\n", chld_out.line[i]);
			}
			pc++;
		}
	}
	*pkgcount=pc;

	/* If we get anything on stderr, at least set warning */
	if(chld_err.buflen){
		stderr_warning=1;
		result = max_state(result, STATE_WARNING);
		if(verbose){
			for(i = 0; i < chld_err.lines; i++) {
				printf("%s\n", chld_err.line[i]);
			}
		}
	}
	return result;
}

/* run an apt-get update (needs root) */
int run_update(void){
	int i=0, result=STATE_UNKNOWN;
	struct output chld_out, chld_err;

	/* run the upgrade */
	result = np_runcmd(APTGET_UPDATE, &chld_out, &chld_err, 0);
	/* apt-get only changes exit status if there is an internal error */
	if(result != 0){
		exec_warning=1;
		result = STATE_UNKNOWN;
		fprintf(stderr, "'%s' exited with non-zero status.\n",
		        APTGET_UPDATE);
	}

	if(verbose){
		for(i = 0; i < chld_out.lines; i++) {
			printf("%s\n", chld_out.line[i]);
		}
	}

	/* If we get anything on stderr, at least set warning */
	if(chld_err.buflen){
		stderr_warning=1;
		result = max_state(result, STATE_WARNING);
		if(verbose){
			for(i = 0; i < chld_err.lines; i++) {
				printf("%s\n", chld_err.line[i]);
			}
		}
	}
	return result;
}
