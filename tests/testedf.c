//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>
#include <errno.h>
#include "filecmp.h"
#include "copy_xdf.h"

#define RAMP_NS		50
#define SAMPLINGRATE	128
#define DURATION	13
#define NITERATION	((SAMPLINGRATE*DURATION)/NSAMPLE)
#define NSAMPLE	17
#define NEEG	11
#define NEXG	7
#define NTRI	1
#define scaled_t	float
static const enum xdftype arrtype = XDFFLOAT;
static const enum xdftype trigarrtype = XDFINT16;
off_t offskip[2] = {168, 184};


void set_signal_values(scaled_t* eeg, scaled_t* exg, int16_t* tri)
{
	int i,j;
	double dv;
	static int isample = 0;

	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NEEG; j++)	{
			dv = ((i+isample)%RAMP_NS) / (double)(RAMP_NS-1);
			dv = dv*UINT16_MAX + INT16_MIN;
			eeg[i*NEEG+j] = dv / (j+1);
		}
	}
	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NEXG; j++) {	
			dv = ((i+isample)%RAMP_NS) / (double)(RAMP_NS-1);
			dv = dv*UINT16_MAX + INT16_MIN;
			exg[i*NEXG+j] = dv + (j+1+NEEG);
		}
	}
	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NTRI; j++) {
			tri[i*NTRI+j] = 0;
			if ((i+isample) % RAMP_NS == 0)
				tri[i*NTRI+j] = (((i+isample)/RAMP_NS) % 2
				                 ? -256 : 256);
		}
	}
	isample += NSAMPLE;
}


static int set_default_analog(struct xdf* xdf, int arrindex)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Active Electrode",
		XDF_CF_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CF_PMIN, (double)INT16_MIN,
		XDF_CF_PMAX, (double)INT16_MAX,
		XDF_CF_UNIT, "uV",
		XDF_CF_RESERVED, "EEG",
		XDF_NOF);
	
	return 0;
}

static int set_default_trigger(struct xdf* xdf, int arrindex)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, trigarrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Triggers and Status",
		XDF_CF_PREFILTERING, "No filtering",
		XDF_CF_STOTYPE, XDFINT16,
		XDF_CF_DMIN, (double)INT16_MIN,
		XDF_CF_DMAX, (double)INT16_MAX,
		XDF_CF_PMIN, (double)INT16_MIN,
		XDF_CF_PMAX, (double)INT16_MAX,
		XDF_CF_UNIT, "Boolean",
		XDF_CF_RESERVED, "TRI",
		XDF_NOF);
	
	return 0;
}


int generate_xdffile(const char* filename)
{
	scaled_t* eegdata;
	scaled_t* exgdata;
	int16_t* tridata;
	int phase;
	struct xdf* xdf = NULL;
	int i,j;
	char tmpstr[16];
	unsigned int strides[3] = {
		NEEG*sizeof(*eegdata),
		NEXG*sizeof(*exgdata),
		NTRI*sizeof(*tridata)
	};


	phase = 5;

	// Allocate the temporary buffers for samples
	eegdata = malloc(NEEG*NSAMPLE*sizeof(*eegdata));
	exgdata = malloc(NEXG*NSAMPLE*sizeof(*exgdata));
	tridata = malloc(NTRI*NSAMPLE*sizeof(*tridata));
	if (!eegdata || !exgdata || !tridata)
		goto exit;
		

	phase--;
	xdf = xdf_open(filename, XDF_WRITE, XDF_EDF);
	if (!xdf) 
		goto exit;
	
	// Specify the structure (channels and sampling rate)
	phase--;
	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, (int)SAMPLINGRATE, XDF_NOF);
	set_default_analog(xdf, 0);
	for (j=0; j<NEEG; j++) {
		sprintf(tmpstr, "EEG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	xdf_set_conf(xdf, XDF_CF_ARRINDEX, 1, XDF_CF_ARROFFSET, 0, XDF_NOF);
	for (j=0; j<NEXG; j++) {
		sprintf(tmpstr, "EXG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	set_default_trigger(xdf, 2);
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}


	// Make the the file ready for accepting samples
	phase--;	
	xdf_define_arrays(xdf, 3, strides);
	if (xdf_prepare_transfer(xdf) < 0)
		goto exit;
	
	// Feed with samples
	phase--;
	for (i=0; i<NITERATION; i++) {
		// Set data signals and unscaled them
		set_signal_values(eegdata, exgdata, tridata);
		if (xdf_write(xdf, NSAMPLE, eegdata, exgdata, tridata) < 0)
			goto exit;
	}
	phase--;

exit:
	// if phase is non zero, a problem occured
	if (phase) 
		fprintf(stderr, "\terror: %s\n", strerror(errno));

	// Clean the structures and ressources
	xdf_close(xdf);
	free(eegdata);
	free(exgdata);
	free(tridata);

	return phase;
}



int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt, testcopy = 1;
	char genfilename[] = "essaiw.edf";
	char reffilename[128];

	while ((opt = getopt(argc, argv, "kc:")) != -1) {
		switch (opt) {
		case 'k':
			keep_file = 1;
			break;
		case 'c':
			testcopy = atoi(optarg);
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-k] [-c {0/1}]\n",
				argv[0]);
			exit(EXIT_FAILURE);
		}
	}


	fprintf(stderr, "\tVersion : %s\n", xdf_get_string());

	// Create the filename for the reference
	snprintf(reffilename, sizeof(reffilename),
		 "%s/ref%u-%u-%u-%u-%u-%u-%u.edf", getenv("srcdir"),
		 SAMPLINGRATE, DURATION, NITERATION, 
		 RAMP_NS, NEEG, NEXG, NTRI);

	
	// Test generation of a file
	unlink(genfilename);
	retcode = generate_xdffile(genfilename);
	if (!retcode)
		retcode = cmp_files(genfilename, reffilename, 1, offskip);

	// Test copy a file (implied reading)
	if (testcopy) {
		unlink(genfilename);
		retcode = copy_xdf(genfilename, reffilename, XDF_EDF);
		if (!retcode)
			retcode = cmp_files(genfilename, reffilename, 1, offskip);
	}

	if (!keep_file)
		unlink(genfilename);


	return retcode;
}


