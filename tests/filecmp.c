/*
    Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <sys/types.h>
#include "filecmp.h"

int cmp_files(const char* testfilename, const char* reffilename,
              int nskip, const off_t* skip, off_t* where)
{
	int retcode = 0;
	int n1, n2;
	off_t pointer = 0;
	unsigned char chunktest, chunkref;
	FILE *reffile, *testfile;


	// Open the files
	reffile = fopen(reffilename,"rb");
	testfile = fopen(testfilename,"rb");
	if (!reffile || !testfile) {
		fprintf(stderr,"\tOne of the files cannot be opened\n");
		retcode = -1;
	}

	int iskip = 0; 
	while (!retcode) {
		if ((iskip < nskip) && (pointer == skip[iskip*2])) {
			fseek(testfile, skip[iskip*2+1], SEEK_SET);
			fseek(reffile, skip[iskip*2+1], SEEK_SET);
			pointer = skip[iskip*2+1];
			iskip++;
		}

		n1 = fread(&chunktest, sizeof(chunktest), 1, testfile);
		n2 = fread(&chunkref, sizeof(chunkref), 1, reffile);
		if (n1 != n2) {
		    	fprintf(stderr,"\tThe files differ by the size\n");
			retcode = -1;
			break;
		}
		
		if ((n1 == 0) || (n2==0))
			break;

		// Check that the ref and test are the same
		if (chunkref != chunktest) {
		    	fprintf(stderr, 
			        "\tThe files differs at 0x%08x\n", 
				(unsigned int)pointer);
			retcode = -1;
			break;
		}
		pointer++;
	}

	if (reffile)
		fclose(reffile);
	if (testfile)
		fclose(testfile);
	
	if (!retcode)
		fprintf(stderr, "\tThe files are identical\n");
	else if (where)
		*where = pointer;
		

	return retcode;
}


