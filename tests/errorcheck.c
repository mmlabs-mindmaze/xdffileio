//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#define MAXFSIZE	60000
#define SAMPLINGRATE	128
#define DURATION	13
#define NITERATION	((SAMPLINGRATE*DURATION)/NSAMPLE)
#define NSAMPLE	17
#define NEEG	11
#define NEXG	7
#define NTRI	1
#define scaled_t	float
static const enum xdftype arrtype = XDFFLOAT;
static const enum xdftype sttype = XDFINT24;
static const enum xdftype trigsttype = XDFUINT24;
static const enum xdftype trigarrtype = XDFUINT32;

int TestResultingFile(const char* testfilename, const char* reffilename)
{
	int retcode = 0;
	int n1, n2;
	unsigned int pointer = 0;
	unsigned char chunktest, chunkref;
	FILE *reffile, *testfile;


	// Open the files
	reffile = fopen(reffilename,"r");
	testfile = fopen(testfilename,"r");
	if (!reffile || !testfile) {
		fprintf(stderr, "\tOne of the files (ref or test) cannot be opened\n");
		retcode = 11;
	}

	while (!retcode) {
		if ((n1 = fread(&chunktest, sizeof(chunktest), 1, testfile))
		    != (n2 = fread(&chunkref, sizeof(chunkref), 1, reffile))) {
		    	fprintf(stderr, "\tThe files differ by their size at position 0x%08x\n", pointer);
			retcode = 12;
			break;
		}
		
		if ((n1 == 0) || (n2==0))
			break;

		// Check that the ref and test are the same
		// excepting for time and date field
		if ( (chunkref != chunktest) && !(((pointer >= 168)&&(pointer < 184)) || ((pointer >= 236)&&(pointer < 244))) ) {
		    	fprintf(stderr, "\tThe files differ by their content at position 0x%08x\n", pointer);
			retcode = 13;
			break;
		}
		pointer += sizeof(chunkref);
	}

	if (reffile)
		fclose(reffile);
	if (testfile)
		fclose(testfile);
	

	return (retcode) ? (int)pointer : -1;
}


void WriteSignalData(scaled_t* eegdata, scaled_t* exgdata, uint32_t* tridata, int seed)
{
	int i,j;
	static int isample = 0;

	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NEEG; j++)	
			eegdata[i*NEEG+j] = ((i+isample)%23)/*((j+1)*i)+seed*/;
	}
	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NEXG; j++)	
			exgdata[i*NEXG+j] = ((j+1)*i)+seed;
	}
	for(i=0; i<NSAMPLE; i++) {
		for (j=0; j<NTRI; j++)	
			tridata[i*NTRI+j] = (((i+isample)%10 == 0)?6:0);
	}
	isample += NSAMPLE;
}

int add_activeelec_channel(struct xdf* xdf, const char* label, int iarr, int ind)
{
	struct xdfch* ch;
	if (!(ch = xdf_add_channel(xdf)))
		return -1;

	xdf_set_chconf(ch, 
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_STOTYPE, sttype,
		XDF_CF_ARRINDEX, iarr,
		XDF_CF_ARROFFSET, (ind*sizeof(scaled_t)),
		XDF_CF_LABEL, label,
		XDF_CF_TRANSDUCTER, "Active Electrode",
		XDF_CF_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CF_PMIN, -262144.0,
		XDF_CF_PMAX, 262143.0,
		XDF_CF_DMIN, -8388608.0,
		XDF_CF_DMAX, 8388607.0,
		XDF_CF_UNIT, "uV",
		XDF_CF_RESERVED, "EEG",
		XDF_CF_NONE);

	return 0;
}

int add_trigger_channel(struct xdf* xdf, const char* label, int iarr, int ind)
{
	struct xdfch* ch;
	if (!(ch = xdf_add_channel(xdf)))
		return -1;

	xdf_set_chconf(ch, 
		XDF_CF_ARRTYPE, trigarrtype,
		XDF_CF_STOTYPE, trigsttype,
		XDF_CF_ARRINDEX, iarr,
		XDF_CF_ARROFFSET, (ind*sizeof(uint32_t)),
		XDF_CF_LABEL, label,
		XDF_CF_TRANSDUCTER, "Triggers and Status",
		XDF_CF_PREFILTERING, "No filtering",
		XDF_CF_PMIN, -8388608.0,
		XDF_CF_PMAX, 8388607.0,
		XDF_CF_DMIN, -8388608.0,
		XDF_CF_DMAX, 8388607.0,
		XDF_CF_UNIT, "Boolean",
		XDF_CF_RESERVED, "TRI",
		XDF_CF_NONE);

	return 0;
}

int generate_xdffile(const char* filename, unsigned int fsize)
{
	scaled_t* eegdata;
	scaled_t* exgdata;
	uint32_t* tridata;
	int phase, retval = 0;
	struct xdf* xdf = NULL;
	int i,j;
	char tmpstr[16];
	unsigned int strides[3] = {
		NEEG*sizeof(*eegdata),
		NEXG*sizeof(*exgdata),
		NTRI*sizeof(*tridata)
	};
	int samwarn, currns = 0;

	samwarn = (fsize-(256*(1+NEEG+NTRI+NEXG)))/((NEEG+NTRI+NEXG)*3);

	phase = 5;

	// Allocate the temporary buffers for samples
	eegdata = malloc(NEEG*NSAMPLE*sizeof(*eegdata));
	exgdata = malloc(NEXG*NSAMPLE*sizeof(*exgdata));
	tridata = malloc(NTRI*NSAMPLE*sizeof(*tridata));
	if (!eegdata || !exgdata || !tridata)
		goto exit;
		

	phase--;
	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (!xdf) 
		goto exit;
	
	// Specify the structure (channels and sampling rate)
	phase--;
	xdf_set_conf(xdf, XDF_F_REC_DURATION, (double)1.0,
			  XDF_F_NSAMPLE_PER_RECORD, (int)SAMPLINGRATE,
			  XDF_F_NONE);

	for (j=0; j<NEEG; j++) {
		sprintf(tmpstr, "EEG%i", j);
		if (add_activeelec_channel(xdf, tmpstr, 0, j) < 0)
			goto exit;
	}
	for (j=0; j<NEXG; j++) {
		sprintf(tmpstr, "EXG%i", j);
		if (add_activeelec_channel(xdf, tmpstr, 1, j) < 0)
			goto exit;
	}
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI%i", j);
		if (add_trigger_channel(xdf, tmpstr, 2, j) < 0)
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
		WriteSignalData(eegdata, exgdata, tridata, i);
		if (xdf_write(xdf, NSAMPLE, eegdata, exgdata, tridata) < 0)
			goto exit;
		currns += NSAMPLE;
	}

exit:
	if ( (currns <= samwarn) || (currns>=samwarn+2*SAMPLINGRATE) ) 
		retval = 1;
	if (errno != EFBIG)
		retval = 1;

	fprintf(stderr, "\tError caught: %s\n\t\twhen adding %i samples after %i samples\n\t\tsample lost: %i (record length: %i samples)\n", 
	                   strerror(errno), NSAMPLE, currns, currns+NSAMPLE - samwarn, SAMPLINGRATE);

	// Clean the structures and ressources
	xdf_close(xdf);
	free(eegdata);
	free(exgdata);
	free(tridata);

	return retval;
}

int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt, pos;
	char genfilename[] = "essaiw.bdf";
	char reffilename[128];
	struct rlimit lim;

	while ((opt = getopt(argc, argv, "k")) != -1) {
		switch (opt) {
		case 'k':
			keep_file = 1;
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
		 NITERATION, NSAMPLE, NEEG, NEXG, NTRI);



	getrlimit(RLIMIT_FSIZE, &lim);
	lim.rlim_cur = MAXFSIZE; 
	setrlimit(RLIMIT_FSIZE, &lim);

	unlink(genfilename);

	retcode = generate_xdffile(genfilename, MAXFSIZE);
	if (!retcode) {
		pos = TestResultingFile(genfilename, reffilename);
		if (pos != MAXFSIZE)
			retcode = 2;
	}

	if (!keep_file)
		unlink(genfilename);

	return retcode;
}

