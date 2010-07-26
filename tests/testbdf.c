//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>
#include <errno.h>

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
		    	fprintf(stderr, "\tThe files differ by their size\n");
			retcode = 12;
			break;
		}
		
		if ((n1 == 0) || (n2==0))
			break;

		// Check that the ref and test are the same
		// excepting for time and date field
		if ( (chunkref != chunktest) && !((pointer >= 168)&&(pointer < 184)) ) {
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
	
	if (!retcode)
		fprintf(stderr, "\tThe files are identical\n");

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

int copy_xdffile(char genfilename[], char reffilename[])
{
	struct xdf *dst, *src;
	struct xdfch *dstch, *srcch;
	unsigned int ich = 0, samplesize, stride[1];
	int nch;
	void* buffer;
	ssize_t ns;
	int offset;

	src = xdf_open(reffilename, XDF_READ, XDF_BDF);
	dst = xdf_open(genfilename, XDF_WRITE, XDF_BDF);

	// Copy header and configuration
	xdf_copy_conf(dst, src);
	offset = 0;
	while ((srcch = xdf_get_channel(src, ich))) {
		dstch = xdf_add_channel(dst);
		if (ich != NEEG+NEXG) {
			xdf_set_chconf(srcch, 
			            XDF_CF_ARRDIGITAL, 0,
		                    XDF_CF_ARRTYPE, XDFFLOAT,
				    XDF_CF_ARROFFSET, offset,
				    XDF_CF_NONE);
			offset += sizeof(float);
		} else {
			xdf_set_chconf(srcch, 
			            XDF_CF_ARRDIGITAL, 0,
		                    XDF_CF_ARRTYPE, XDFINT32,
				    XDF_CF_ARROFFSET, offset,
				    XDF_CF_NONE);
			offset += sizeof(int32_t);
		}
		xdf_copy_chconf(dstch, srcch);
		ich++;
	}
	xdf_get_conf(src, XDF_F_NCHANNEL, &nch, XDF_F_NONE);
	if (nch != (int)ich) {
		fprintf(stderr, "\tich=%u, nch=%i\n", ich, nch);
		return 1;
	}

	samplesize = nch*4;
	buffer = malloc(samplesize*NSAMPLE);
	stride[0] = samplesize;
	xdf_define_arrays(src, 1, stride);
	xdf_define_arrays(dst, 1, stride);
	xdf_prepare_transfer(src);
	xdf_prepare_transfer(dst);

	if ( (xdf_seek(src, 1000, SEEK_CUR) < 0)
	    || (xdf_seek(src, 0, SEEK_SET) < 0) ) {
		fprintf(stderr, "\txdf_seek function failed: %s\n", strerror(errno));
		return 2;
	}

	while (1) {
		ns = xdf_read(src, NSAMPLE, buffer);
		if (ns <= 0)
			break;
		xdf_write(dst, ns, buffer);
	}

	free(buffer);
	xdf_close(src);
	xdf_close(dst);

	return 0;
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


	fprintf(stderr, "\tVersion : %s\n", xdf_get_string());

	// Create the filename for the reference
	snprintf(reffilename, sizeof(reffilename),
		 "%s/ref%u-%u-%u-%u-%u-%u-%u.bdf", getenv("srcdir"),SAMPLINGRATE, DURATION,
		 NITERATION, NSAMPLE, NEEG, NEXG, NTRI);

	
	// Test generation of a file
	unlink(genfilename);
	retcode = generate_xdffile(genfilename);
	if (!retcode)
		retcode = TestResultingFile(genfilename, reffilename);

	// Test copy a file (implied reading)
	unlink(genfilename);
	retcode = copy_xdffile(genfilename, reffilename);
	if (!retcode)
		retcode = TestResultingFile(genfilename, reffilename);


	if (!keep_file)
		unlink(genfilename);


	return retcode;
}

