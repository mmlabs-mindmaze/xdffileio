#ifndef FSYNC_H
#define FSYNC_H

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_DECL_FSYNC
# include <unistd.h>
#else
XDF_LOCAL int fsync(int fd);
#endif

#endif /* FSYNC_H */
