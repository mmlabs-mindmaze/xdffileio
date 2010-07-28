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

const char sess_str[] = "This a test BDF file";
const char subj_str[] = "Nobody. This string is very long on purpose and test the truncation. It should be longer than the length of the field in the file";

typedef float	eeg_t;
#define EEG_TYPE 	XDFFLOAT
typedef double	sens_t;
#define SENS_TYPE 	XDFDOUBLE
typedef uint32_t	tri1_t;
#define TRI1_TYPE	XDFUINT32
#define TRI1_MIN	0.0
#define TRI1_MAX        16777216.0
typedef int32_t		tri2_t;
#define TRI2_TYPE	XDFINT32
#define TRI2_MIN	-8388608.0
#define TRI2_MAX        8388607.0 
off_t offskip[2] = {168, 184};

#define PMIN (-262144.0)
#define PMAX (262143.0)

void set_signal_values(eeg_t* eeg, sens_t* exg, tri1_t* tri1, tri2_t* tri2)
{
	int i,j, is, ir;
	double dv;
	static int isample = 0;

	for(i=0; i<NSAMPLE; i++) {
		is = i+isample;
		ir = is % RAMP_NS;

		for (j=0; j<NEEG; j++)	{
			dv = ir / (double)(RAMP_NS-1);
			dv = dv*(PMAX-PMIN) + PMIN;
			eeg[i*NEEG+j] = dv / (j+1);
		}

		for (j=0; j<NEXG; j++) {	
			dv = ir / (double)(RAMP_NS-1);
			dv = dv*(PMAX-PMIN) + PMIN;
			exg[i*NEXG+j] = dv / (j+1);
		}

		for (j=0; j<NTRI; j++) {
			tri1[i*NTRI+j] = 0;
			if (ir == 0)
				tri1[i*NTRI+j] = (is/RAMP_NS % 2) 
						? 131072 : 4096;
		}

		for (j=0; j<NTRI; j++) {
			tri2[i*NTRI+j] = 0;
			if (ir == 0)
				tri2[i*NTRI+j] = (is/RAMP_NS % 2) 
						? -256 : 256 ;
		}
	}
	isample += NSAMPLE;
}


static int set_default_analog(struct xdf* xdf, int arrindex,
						enum xdftype arrtype)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Active Electrode",
		XDF_CF_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CF_PMIN, -262144.0,
		XDF_CF_PMAX, 262143.0,
		XDF_CF_UNIT, "uV",
		XDF_CF_RESERVED, "EEG",
		XDF_NOF);
	
	return 0;
}

static int set_default_trigger(struct xdf* xdf, int arrindex,
						enum xdftype arrtype,
						double pmin, double pmax)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Triggers and Status",
		XDF_CF_PREFILTERING, "No filtering",
		XDF_CF_PMIN, pmin,
		XDF_CF_PMAX, pmax,
		XDF_CF_UNIT, "Boolean",
		XDF_CF_RESERVED, "TRI",
		XDF_NOF);
	
	return 0;
}


int generate_xdffile(const char* filename)
{
	eeg_t* eegdata;
	sens_t* exgdata;
	tri1_t* tri1data;
	tri2_t* tri2data;
	int retcode = -1;
	struct xdf* xdf = NULL;
	int i,j;
	char tmpstr[16];
	unsigned int strides[4] = {
		NEEG*sizeof(*eegdata),
		NEXG*sizeof(*exgdata),
		NTRI*sizeof(*tri1data),
		NTRI*sizeof(*tri2data),
	};


	// Allocate the temporary buffers for samples
	eegdata = malloc(NEEG*NSAMPLE*sizeof(*eegdata));
	exgdata = malloc(NEXG*NSAMPLE*sizeof(*exgdata));
	tri1data = malloc(NTRI*NSAMPLE*sizeof(*tri1data));
	tri2data = malloc(NTRI*NSAMPLE*sizeof(*tri2data));
	if (!eegdata || !exgdata || !tri1data || !tri2data)
		goto exit;
		

	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (!xdf) 
		goto exit;
	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, (int)SAMPLINGRATE,
	                  XDF_F_SESS_DESC, sess_str,
			  XDF_F_SUBJ_DESC, subj_str,
			  XDF_NOF);
	
	// Specify the structure (channels and sampling rate)
	set_default_analog(xdf, 0, EEG_TYPE);
	for (j=0; j<NEEG; j++) {
		sprintf(tmpstr, "EEG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	set_default_analog(xdf, 1, SENS_TYPE);
	for (j=0; j<NEXG; j++) {
		sprintf(tmpstr, "EXG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	set_default_trigger(xdf, 2, TRI1_TYPE, TRI1_MIN, TRI1_MAX);
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI1%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	set_default_trigger(xdf, 3, TRI2_TYPE, TRI2_MIN, TRI2_MAX);
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI2%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}


	// Make the the file ready for accepting samples
	xdf_define_arrays(xdf, 4, strides);
	if (xdf_prepare_transfer(xdf) < 0)
		goto exit;
	
	// Feed with samples
	for (i=0; i<NITERATION; i++) {
		// Set data signals and unscaled them
		set_signal_values(eegdata, exgdata, tri1data, tri2data);
		if (xdf_write(xdf, NSAMPLE, eegdata, exgdata, tri1data, tri2data) < 0)
			goto exit;
	}
	retcode = 0;

exit:
	// if phase is non zero, a problem occured
	if (retcode) 
		fprintf(stderr, "\terror: %s\n", strerror(errno));

	// Clean the structures and ressources
	xdf_close(xdf);
	free(eegdata);
	free(exgdata);
	free(tri1data);
	free(tri2data);

	return retcode;
}



int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt, testcopy = 1;
	char genfilename[] = "essaiw.bdf";
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
			fprintf(stderr, "Usage: %s [-k]\n",
				argv[0]);
			exit(EXIT_FAILURE);
		}
	}


	fprintf(stderr, "\tVersion : %s\n", xdf_get_string());

	// Create the filename for the reference
	snprintf(reffilename, sizeof(reffilename),
		 "%s/ref%u-%u-%u-%u-%u-%u-%u.bdf", getenv("srcdir"),SAMPLINGRATE, DURATION,
		 NITERATION, RAMP_NS, NEEG, NEXG, NTRI);

	
	// Test generation of a file
	unlink(genfilename);
	retcode = generate_xdffile(genfilename);
	if (!retcode)
		retcode = cmp_files(genfilename, reffilename, 1, offskip, NULL);

	// Test copy a file (implied reading)
	if (!retcode && testcopy) {
		unlink(genfilename);
		retcode = copy_xdf(genfilename, reffilename, XDF_BDF);
		if (!retcode)
			retcode = cmp_files(genfilename, reffilename, 1, offskip, NULL);
	}

	if (!keep_file)
		unlink(genfilename);


	return retcode ? EXIT_FAILURE : EXIT_SUCCESS;
}

