//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>

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
		fprintf(stderr, "One of the files (ref or test) cannot be opened\n");
		retcode = 11;
	}

	while (!retcode) {
		if ((n1 = fread(&chunktest, sizeof(chunktest), 1, testfile))
		    != (n2 = fread(&chunkref, sizeof(chunkref), 1, reffile))) {
		    	fprintf(stderr, "The files differ by their size\n");
			retcode = 12;
			break;
		}
		
		if ((n1 == 0) || (n2==0))
			break;

		// Check that the ref and test are the same
		// excepting for time and date field
		if ( (chunkref != chunktest) && !((pointer >= 168)&&(pointer < 184)) ) {
		    	fprintf(stderr, "The files differ by their content at position 0x%08x\n", pointer);
			retcode = 13;
			break;
		}
		pointer += sizeof(chunkref);
	}

	if (reffile)
		fclose(reffile);
	if (testfile)
		fclose(testfile);
	

	return retcode;
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
		XDF_CHFIELD_ARRAY_TYPE, arrtype,
		XDF_CHFIELD_STORED_TYPE, sttype,
		XDF_CHFIELD_ARRAY_INDEX, iarr,
		XDF_CHFIELD_ARRAY_OFFSET, (ind*sizeof(scaled_t)),
		XDF_CHFIELD_LABEL, label,
		XDF_CHFIELD_TRANSDUCTER, "Active Electrode",
		XDF_CHFIELD_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CHFIELD_PHYSICAL_MIN, -262144.0,
		XDF_CHFIELD_PHYSICAL_MAX, 262143.0,
		XDF_CHFIELD_DIGITAL_MIN, -8388608.0,
		XDF_CHFIELD_DIGITAL_MAX, 8388607.0,
		XDF_CHFIELD_UNIT, "uV",
		XDF_CHFIELD_RESERVED, "EEG",
		XDF_CHFIELD_NONE);

	return 0;
}

int add_trigger_channel(struct xdf* xdf, const char* label, int iarr, int ind)
{
	struct xdfch* ch;
	if (!(ch = xdf_add_channel(xdf)))
		return -1;

	xdf_set_chconf(ch, 
		XDF_CHFIELD_ARRAY_TYPE, trigarrtype,
		XDF_CHFIELD_STORED_TYPE, trigsttype,
		XDF_CHFIELD_ARRAY_INDEX, iarr,
		XDF_CHFIELD_ARRAY_OFFSET, (ind*sizeof(uint32_t)),
		XDF_CHFIELD_LABEL, label,
		XDF_CHFIELD_TRANSDUCTER, "Triggers and Status",
		XDF_CHFIELD_PREFILTERING, "No filtering",
		XDF_CHFIELD_PHYSICAL_MIN, -8388608.0,
		XDF_CHFIELD_PHYSICAL_MAX, 8388607.0,
		XDF_CHFIELD_DIGITAL_MIN, -8388608.0,
		XDF_CHFIELD_DIGITAL_MAX, 8388607.0,
		XDF_CHFIELD_UNIT, "Boolean",
		XDF_CHFIELD_RESERVED, "TRI",
		XDF_CHFIELD_NONE);

	return 0;
}

int generate_xdffile(const char* filename)
{
	scaled_t* eegdata;
	scaled_t* exgdata;
	uint32_t* tridata;
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
	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (!xdf) 
		goto exit;
	
	// Specify the structure (channels and sampling rate)
	phase--;
	xdf_set_conf(xdf, XDF_FIELD_RECORD_DURATION, (double)1.0,
			  XDF_FIELD_NSAMPLE_PER_RECORD, (int)SAMPLINGRATE,
			  XDF_FIELD_NONE);

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
	}
	phase--;

exit:
	// if phase is non zero, a problem occured
	if (phase) 
		fprintf(stderr, "\terror: %s\n", strerror(xdf_get_error(xdf)));
	// Clean the structures and ressources
	if (xdf_close(xdf))
		fprintf(stderr, "\terror: %s\n", strerror(xdf_get_error(NULL)));
	free(eegdata);
	free(exgdata);
	free(tridata);

	return phase;
}


int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt;
	char genfilename[] = "essaiw.bdf";
	char reffilename[128];

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


	printf("Version : %s\n", xdf_get_string());

	// Create the filename for the reference
	snprintf(reffilename, sizeof(reffilename),
		 "%s/ref%u-%u-%u-%u-%u-%u-%u.bdf", getenv("srcdir"),SAMPLINGRATE, DURATION,
		 NITERATION, NSAMPLE, NEEG, NEXG, NTRI);


	retcode = generate_xdffile(genfilename);
	if (!retcode)
		retcode = TestResultingFile(genfilename, reffilename);

	if (!keep_file)
		unlink(genfilename);

	fprintf(stderr, "retcode: %i\n", retcode);
	return retcode;
}

