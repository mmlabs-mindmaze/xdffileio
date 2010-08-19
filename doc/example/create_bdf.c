/*  Example : create_bdf.c
 *
 * This program demonstrates how to write a BDF file. In particular it shows
 * how to initialize a xDF file, and setup the channels and feed the data
 *
 * It creates 3 blocks of channels:
 * - NEEG channels containing each a sine wave of 1 to NEEG Hz of float
 * - NSENS channels containing each a sine wave of 1 to NSENS Hz of double
 * - 1 channel containing a integer value of 0xF0 every 0.5 seconds
 */  
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <xdfio.h>
#include <math.h>

const char filename[] = "signal.bdf";
#define NS		8
#define NEEG 		64
#define NSENS 		8
#define FS		512
#define DURATION	10
#define TOTAL_NS	(FS*DURATION)

/* Buffers holding the data to be written on the file */
#define NARRAYS		3
float eeg[NS*NEEG];
double sens[NS*NSENS];
int32_t trigger[NS];

const char eeglabels[64][8] = {
	"Fp1", "AF7", "AF3", "F1", "F3", "F5", "F7", "FT7",
	"FC5", "FC3", "FC1", "C1", "C3", "C5", "T7", "TP7",
	"CP5", "CP3", "CP1", "P1", "P3", "P5", "P7", "P9",
	"PO7", "PO3", "O1", "Iz", "Oz", "POz", "Pz", "CPz",
	"Fpz", "Fp2", "AF8", "AF4", "AFz", "Fz", "F2", "F4",
	"F6", "F8", "FT8", "FC6", "FC4", "FC2", "FCz", "Cz",
	"C2", "C4", "C6", "T8", "TP8", "CP6", "CP4", "CP2",
	"P2", "P4", "P6", "P8", "P10", "PO8", "PO4", "O2"
};

const char senslabels[8][8] = {
	"EXG1", "EXG2", "EXG3", "EXG4", "EXG5", "EXG6", "EXG7", "EXG8"
};

const char stepmsg[][128] = {
	"creating the file",
	"configuring the channels",
	"preparing the transfer",
	"writing the data"
};


static int generate_signal(unsigned int neeg, float *eeg, 
                           unsigned int nsens, double* sens,
			   int32_t* triggers, 
                           float ffs, unsigned int ns)
{
	unsigned int j, i;
	static int k = 0;

	for (j=0; j<ns; j++) {
		for (i=0; i<neeg; i++)
			eeg[j*neeg+i] = sin(6.28*(i+1)*(j+k)/ffs);

		for (i=0; i<nsens; i++)
			sens[j*nsens+i] = sin((6.28*(i+1)*(j+k)/ffs));

		triggers[j] = (j+k % (int)(ffs/2.0)) ? 0 : 0xF0;
	}

	k += ns;
	return ns;
}


int configure_channels(struct xdf* xdf, unsigned int neeg, unsigned int nsens)
{
	unsigned int i;
	struct xdfch* ch = NULL;
	
	/* Set the default settings of the next channels.
	There is no need to setup the stored data type (XDF_CF_STOTYPE)
	neither the digital min or max since BDF supports only int24 data
	type and those settings are thus set by default */
	xdf_set_conf(xdf, XDF_CF_ARRTYPE, XDFFLOAT,
			  XDF_CF_ARRINDEX, 0,
			  XDF_CF_ARROFFSET, 0,/* starting offset (will be
			                       automatically incremented
					       after a channel is added) */
			  XDF_CF_TRANSDUCTER, "Active Electrode",
			  XDF_CF_PREFILTERING, "HP: DC; LP: 100 Hz",
			  XDF_CF_PMIN, -2.0,
			  XDF_CF_PMAX, 2.0,
			  XDF_CF_UNIT, "uV",
			  XDF_CF_RESERVED, "EEG",
			  XDF_NOF);

	/* Add EEG channels */
	for (i = 0; i < neeg; i++)
		if (xdf_add_channel(xdf, eeglabels[i]) == NULL)
			return -1;;


	/* Setup and add sensor channels */
	xdf_set_conf(xdf, XDF_CF_ARRTYPE, XDFDOUBLE,
			  XDF_CF_ARRINDEX, 1,
			  XDF_CF_ARROFFSET, 0,
			  XDF_NOF);
	for (i = 0; i < nsens; i++)
		if (xdf_add_channel(xdf, senslabels[i]) == NULL)
			return -1;


	/* Add and setup trigger channel
	(show another way to setup a channel) */
	ch = xdf_add_channel(xdf, "Status");
	if (ch == NULL)
		return -1;
	xdf_set_chconf(ch, XDF_CF_ARRTYPE, XDFINT32,
			   XDF_CF_ARRINDEX, 2,
	                   XDF_CF_ARROFFSET, 0,
	                   XDF_CF_TRANSDUCTER, "Triggers and Status",
	                   XDF_CF_PREFILTERING, "No filtering",
			   XDF_CF_PMIN, -8388608.0,
	                   XDF_CF_PMAX, 8388607.0,
	                   XDF_CF_UNIT, "Boolean",
	                   XDF_CF_RESERVED, "TRI", XDF_NOF);

	return 0;
}


int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	struct xdf *xdf = NULL;
	int step, retval = EXIT_FAILURE;
	unsigned int ns, i;
	size_t strides[NARRAYS] = {
		NEEG*sizeof(eeg[0]),
		NSENS*sizeof(sens[0]),
		sizeof(trigger[0])
	};

	/*************************************************
	 *            File preparation                   *
	 *************************************************/
	step = 0;
	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (xdf == NULL) 
		goto exit;

	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, FS, XDF_NOF);

	step++;
	if (configure_channels(xdf, NEEG, NSENS)) 
		goto exit;

	step++;
	if (xdf_define_arrays(xdf, NARRAYS, strides)
	     || xdf_prepare_transfer(xdf) )
		goto exit;

	/************************************************
	*            Writing loop                       *
	*************************************************/
	step++;
	for (i=0; i<TOTAL_NS; i+=NS) {
		ns = ((TOTAL_NS - i) >= NS) ? NS : (TOTAL_NS - i);
		generate_signal(NEEG, eeg, NSENS, sens, trigger, FS, ns);

		if (xdf_write(xdf, ns, eeg, sens, trigger) < 0) 
			goto exit;
	}

	retval = EXIT_SUCCESS;
exit:
	if (retval != EXIT_SUCCESS) {
		fprintf(stderr, "Error while %s : (%i) %s\n",
		        stepmsg[step], errno, strerror(errno));
	}
	
	xdf_close(xdf);
	return retval;
}

