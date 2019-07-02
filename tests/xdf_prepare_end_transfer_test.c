#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "xdfio.h"


#define TESTFILE_GDF SRCDIR"/ref128-13-97-50-11-7-1.gdf2"
#define NCH 20
#define NSAMPLE 1


static struct xdf * xdf = NULL;

static
void xdf_cleanup(void)
{
	if (xdf != NULL) {
		xdf_close(xdf);
		xdf = NULL;
	}
}


START_TEST(test_xdf_prepare_transfer)
{
	int rv;
	size_t stride[1];
	void * buffer = malloc(NCH * sizeof(double) * NSAMPLE);
	ck_assert(buffer != NULL);

	xdf = xdf_open(TESTFILE_GDF, XDF_READ, XDF_GDF2);
	ck_assert(xdf != NULL);

	stride[0] = NCH * sizeof(double);
	rv = xdf_define_arrays(xdf, 1, stride);
	ck_assert(rv == 0);

	rv = xdf_prepare_transfer(xdf);
	ck_assert(rv == 0);

	/* We've prepared. Ensure that we can actually read */
	rv = xdf_read(xdf, NSAMPLE, buffer);
	ck_assert(rv == NSAMPLE);

	xdf_cleanup();
	free(buffer);
}
END_TEST


START_TEST(test_xdf_end_transfer)
{
	int rv;
	size_t stride[1];
	void * buffer = malloc(NCH * sizeof(double) * NSAMPLE);
	ck_assert(buffer != NULL);

	xdf = xdf_open(TESTFILE_GDF, XDF_READ, XDF_GDF2);
	ck_assert(xdf != NULL);

	stride[0] = NCH * sizeof(double);
	rv = xdf_define_arrays(xdf, 1, stride);
	ck_assert(rv == 0);

	rv = xdf_prepare_transfer(xdf);
	ck_assert(rv == 0);

	/* We've prepared. Ensure that we can actually read */
	rv = xdf_read(xdf, NSAMPLE, buffer);
	ck_assert(rv == NSAMPLE);

	rv = xdf_end_transfer(xdf);
	ck_assert(rv == 0);

	/* ensure we can prepare again */
	rv = xdf_prepare_transfer(xdf);
	ck_assert(rv == 0);

	/* We've prepared again. Ensure that we can read again */
	rv = xdf_read(xdf, NSAMPLE, buffer);
	ck_assert(rv == NSAMPLE);

	xdf_cleanup();
	free(buffer);
}
END_TEST


TCase* create_xdf_prepare_end_transfer_tcase(void)
{
	TCase * tc = tcase_create("xdf-prepare-end-transfer");

	tcase_add_unchecked_fixture(tc, NULL, xdf_cleanup);

	tcase_add_test(tc, test_xdf_prepare_transfer);
	tcase_add_test(tc, test_xdf_end_transfer);

	return tc;
}
