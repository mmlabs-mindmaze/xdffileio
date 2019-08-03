/*
 * Copyright (C) 2019 MindMaze
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#if HAVE_CONFIG_H
# include <config.h>
#endif


#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xdfio.h>

#include "../src/xdftypes.h"

#define NELEM(arr)  ((int)(sizeof(arr)/sizeof(arr[0])))

#define FILENAME "ref_read.bdf"

#define RAMP_NS 17
#define SAMPLERATE 128
#define NUM_SAMPLES 2048

#define ANALOG_NUMCH    32
#define ANALOG_PMIN     -262144.0f
#define ANALOG_PMAX     262143.0f
#define ANALOG_SCALE    (0.5f * (ANALOG_PMIN - ANALOG_PMAX) / (ANALOG_NUMCH * RAMP_NS))
#define ANALOG_OFFSET   (0.25f * (ANALOG_PMIN - ANALOG_PMAX))

#define UNSCALED_NUMCH  32
#define UNSCALED_PMIN   ((double)(INT24_MIN))
#define UNSCALED_PMAX   ((double)(INT24_MAX))

#define END     -1


void set_ref_unscaled(int sample_index, int32_t* data)
{
	int i;

	for (i = 0; i < UNSCALED_NUMCH; i++)
		data[i] = (sample_index + (i * RAMP_NS));
}


void set_ref_analog(int sample_index, float* data)
{
	int i;

	for (i = 0; i < ANALOG_NUMCH; i++)
		data[i] = (sample_index + (i * RAMP_NS)) * ANALOG_NUMCH - ANALOG_OFFSET;
}


static
int setup_channels_in_ref(struct xdf* xdf, int index, int nch,
			  enum xdftype arrtype, const char* prefix,
			  double pmin, double pmax)
{
	int c;
	char label[32];

	// Configure channel that are going to be added
	xdf_set_conf(xdf,
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_ARRINDEX, index,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_PMIN, pmin,
		XDF_CF_PMAX, pmax,
		XDF_NOF);

	// Add channels
	for (c = 0; c < nch; c++) {
		sprintf(label, "%s-%i", prefix, c);
		if (!xdf_add_channel(xdf, label))
			return -1;
	}

	return 0;
}


static
int create_ref_file(void)
{
	struct xdf* xdf;
	int i, rv = -1;
	float analog[ANALOG_NUMCH];
	int32_t unscaled[UNSCALED_NUMCH];
	size_t strides[] = {sizeof(analog), sizeof(unscaled)};

	xdf = xdf_open(FILENAME, XDF_WRITE|XDF_TRUNC, XDF_BDF);
	if (!xdf)
		return -1;

	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, SAMPLERATE,
	                  XDF_F_SESS_DESC, "read test",
			  XDF_F_SUBJ_DESC, "Ema Nymton",
			  XDF_NOF);

	if (setup_channels_in_ref(xdf, 0, ANALOG_NUMCH, XDFFLOAT,
	                          "analog", ANALOG_PMIN, ANALOG_PMAX)
	 || setup_channels_in_ref(xdf, 1, UNSCALED_NUMCH, XDFINT32,
	                          "unscaled", UNSCALED_PMIN, UNSCALED_PMAX))
		goto exit;

	xdf_define_arrays(xdf, 2, strides);
	if (xdf_prepare_transfer(xdf) < 0)
		goto exit;

	for (i = 0; i < NUM_SAMPLES; i++) {
		set_ref_analog(i, analog);
		set_ref_unscaled(i, unscaled);
		if (xdf_write(xdf, 1, analog, unscaled) != 1)
			goto exit;
	}
	rv = 0;

exit:
	xdf_close(xdf);
	return rv;
}


static
void tcase_setup(void)
{
	if (create_ref_file()) {
		perror("Failed to create reference file");
		abort();
	}
}


static
void tcase_cleanup(void)
{
	remove(FILENAME);
	remove(FILENAME".code");
	remove(FILENAME".event");
}


static struct xdf * test_xdf = NULL;

static
void teardown(void)
{
	if (test_xdf != NULL) {
		xdf_close(test_xdf);
		test_xdf = NULL;
	}
}


static const
struct {
	int order[32 + 1];
	int nch_in_array;
} channel_sequences_cases[] = {
	{
		.order = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		          16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
		          29, 30, 31, END},
		.nch_in_array = 32,
	}, {
		.order = {9, 10, 11, 12, 13, 14, 15, END},
		.nch_in_array = 32,
	}, {
		.order = {6, 31, END},
		.nch_in_array = 16,
	}, {
		.order = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, END},
		.nch_in_array = 32,
	}, {
		.order = {0, 1, 2, 6, 7, 8, 9, 13, 14, 15, END},
		.nch_in_array = 16,
	}, {
		.order = {0, 1, 13, 14, 2, 9, 6, 7, 8, 15, END},
		.nch_in_array = 16,
	}, {
		.order = {0, END},
		.nch_in_array = 1,
	}, {
		.order = {1, END},
		.nch_in_array = 1,
	}, {
		.order = {END},
		.nch_in_array = 16,
	}, {
		.order = {END},
		.nch_in_array = 0,
	},
};
#define NUM_CHANNEL_SEQUENCES   NELEM(channel_sequences_cases)


static
int get_data_size(enum xdftype type)
{
	switch(type) {
	case XDFINT8: return sizeof(int8_t);
	case XDFUINT8: return sizeof(uint8_t);
	case XDFINT16: return sizeof(int16_t);
	case XDFUINT16: return sizeof(uint16_t);
	case XDFINT32: return sizeof(int32_t);
	case XDFUINT32: return sizeof(uint32_t);
	case XDFINT64: return sizeof(int64_t);
	case XDFUINT64: return sizeof(uint64_t);
	case XDFFLOAT: return sizeof(float);
	case XDFDOUBLE: return sizeof(double);
	default: return -1;
	}
}


/**
 * setup_read_channels() - setup a read of channel is a specific order
 * @xdf:        xdf file opened for reading
 * @order:      channel sequence terminated by EOL
 * @ch_idx_offset: first channel to setup
 * @type:       data type to use for reading the data in array
 *
 * Return: 0 in case of success, -1 otherwise
 */
static
int setup_read_channels(struct xdf* xdf, const int* order,
                        int ch_idx_offset, enum xdftype dtype)
{
	struct xdfch* xdfch;
	int i, max_nch, dsize;

	// Mark all channel as unread
	xdf_get_conf(xdf, XDF_F_NCHANNEL, &max_nch, XDF_NOF);
	for (i = 0; i < max_nch; i++) {
		xdfch = xdf_get_channel(xdf, i);
		xdf_set_chconf(xdfch, XDF_CF_ARRINDEX, -1, XDF_NOF);
	}

	// For all channel in order array, setup send data in array indexed 0
	dsize = get_data_size(dtype);
	for (i = 0; order[i] != END; i++) {
		xdfch = xdf_get_channel(xdf, order[i] + ch_idx_offset);
		if (!xdfch)
			return -1;

		xdf_set_chconf(xdfch,
		               XDF_CF_ARRTYPE, dtype,
		               XDF_CF_ARRDIGITAL, 0,
		               XDF_CF_ARRINDEX, 0,
		               XDF_CF_ARROFFSET, i * dsize,
		               XDF_NOF);
	}

	return 0;
}


START_TEST(unscaled_read)
{
	struct xdf* xdf;
	const int* order = channel_sequences_cases[_i].order;
	int nch_in_array = channel_sequences_cases[_i].nch_in_array;
	size_t strides[1] = {nch_in_array * get_data_size(XDFINT32)};
	int i, c;
	int32_t data[UNSCALED_NUMCH], ref[UNSCALED_NUMCH];

	// Configure a xdf handle for reading the channel in specific order
	xdf = test_xdf = xdf_open(FILENAME, XDF_READ, XDF_ANY);
	if (xdf == NULL
	 || setup_read_channels(xdf, order, ANALOG_NUMCH, XDFINT32)
	 || xdf_define_arrays(xdf, 1, strides)
	 || xdf_prepare_transfer(xdf) )
		ck_abort_msg("configuration failed");

	for (i = 0; i < NUM_SAMPLES; i++) {
		ck_assert(xdf_read(xdf, 1, data) == 1);

		// check the data read match the reordered reference data
		set_ref_unscaled(i, ref);
		for (c = 0; order[c] != END; c++)
			ck_assert(ref[order[c]] == data[c]);
	}
}
END_TEST


LOCAL_FN
TCase* create_read_tcase(void)
{
	TCase * tc = tcase_create("read");

	tcase_add_unchecked_fixture(tc, tcase_setup, tcase_cleanup);
	tcase_add_checked_fixture(tc, NULL, teardown);

	tcase_add_loop_test(tc, unscaled_read, 0, NUM_CHANNEL_SEQUENCES);

	return tc;
}
