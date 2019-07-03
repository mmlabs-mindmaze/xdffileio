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
#include <errno.h>
#include <unistd.h>
#include <xdfio.h>


#define FILENAME "dummy.bdf"

static struct xdf * xdf = NULL;


static
void setup(void)
{
	unlink(FILENAME);
	unlink(FILENAME".code");
	unlink(FILENAME".event");
}


static
void teardown(void)
{
	if (xdf != NULL) {
		xdf_close(xdf);
		xdf = NULL;
	}
}


START_TEST(trunc_flag)
{
	xdf = xdf_open(FILENAME, XDF_WRITE, XDF_BDF);
	ck_assert(xdf != NULL);
	xdf_close(xdf);

	xdf = xdf_open(FILENAME, XDF_WRITE, XDF_BDF);
	ck_assert(xdf == NULL);
	ck_assert_int_eq(errno, EEXIST);

	xdf = xdf_open(FILENAME, XDF_WRITE|XDF_TRUNC, XDF_BDF);
	ck_assert(xdf != NULL);
}
END_TEST


LOCAL_FN
TCase* create_open_tcase(void)
{
	TCase * tc = tcase_create("open");

	tcase_add_checked_fixture(tc, setup, teardown);

	tcase_add_test(tc, trunc_flag);

	return tc;
}
