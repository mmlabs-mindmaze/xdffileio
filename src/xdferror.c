#include "xdffile.h"
#include "xdferror.h"
#include <errno.h>

#if HAVE_CONFIG_H
# include <config.h>
#endif

/*! \ingroup interface_XDF
 * \return 		Return the error code
 *
 * Return the error code of the last error that has occured in the library.
 */
int xdf_get_error(const struct xdf* xdf)
{
	if (xdf)
		return xdf->error;
	return errno;
}

int set_xdf_error(struct xdf* xdf, int error)
{
	if (xdf)
		xdf->error = error;
	errno = error;
	return (error) ? -1 : 0;
}

