/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include "imprimer.h"
#include "conversions.h"

/*
 * "Imprimer" printer spooler.
 */
int main(int argc, char *argv[])
{
    char optval;
    FILE *in = NULL;
    FILE *out = stdout;
    int quit = 0;
    //extern int sf_suppress_chatter;
    //sf_suppress_chatter = 1;
    sf_init();
    conversions_init();
    while(optind < argc) {
	if((optval = getopt(argc, argv, "i:o:")) != -1) {
	    switch(optval) {
	    case 'i':
		in = fopen(optarg, "r");
		if(in == NULL) {
		    perror("input file");
		    exit(EXIT_FAILURE);
		}
		break;
	    case 'o':
		if((out = fopen(optarg, "w")) == NULL) {
		    perror("write_file");
		    exit(EXIT_FAILURE);
		}
		break;
	    case '?':
		fprintf(stderr, "Usage: %s [-i <cmd_file>] [-o <out_file>]\n", argv[0]);
		exit(EXIT_FAILURE);
		break;
	    default:
		break;
	    }
	}
    }
    if(in != NULL) {
	quit = run_cli(in, out);
	fclose(in);
	fflush(out);
    }
    if(!quit) {
	run_cli(stdin, out);
	fflush(out);
    }
    conversions_fini();
    sf_fini();
    exit(EXIT_SUCCESS);
}
