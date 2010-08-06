#include "fsync.h"

#if HAVE__COMMIT
#include <io.h>
#endif


XDF_LOCAL int fsync(int fd)
{
#if HAVE__COMMIT
	return _commit(fd);
#else
	(void)fd;
	return 0;
#endif
}
