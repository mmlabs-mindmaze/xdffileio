#ifndef FILECMP_H
#define FILECMP_H

#include <sys/types.h>

int cmp_files(const char* testfilename, const char* reffilename,
              int nskip, const off_t* skip);

#endif
