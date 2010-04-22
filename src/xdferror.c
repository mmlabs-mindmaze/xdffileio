#include "xdffile.h"
#include "xdferror.h"

#if HAVE_CONFIG_H
# include <config.h>
#endif

static int xdferrno = XDF_OK;

/*! \ingroup interface_XDF
 * \return 		Return the error code
 *
 * Return the error code of the last error that has occured in the library.
 */
int xdf_get_error(struct xdffile* xdf)
{
	if (xdf)
		return xdf->error;
	return xdferrno;
}

int set_xdf_error(struct xdffile* xdf, int error)
{
	if (xdf)
		xdf->error = error;
	xdferrno = error;
	return (error == XDF_OK) ? 0 : -1;
}

