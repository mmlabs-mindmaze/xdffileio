#include <stdio.h>
#include <sys/types.h>
#include "filecmp.h"

int cmp_files(const char* testfilename, const char* reffilename,
              int nskip, const off_t* skip)
{
	int retcode = 0;
	int n1, n2;
	off_t pointer = 0;
	unsigned char chunktest, chunkref;
	FILE *reffile, *testfile;


	// Open the files
	reffile = fopen(reffilename,"r");
	testfile = fopen(testfilename,"r");
	if (!reffile || !testfile) {
		fprintf(stderr,"\tOne of the files cannot be opened\n");
		retcode = 11;
	}

	int iskip = 0; 
	while (!retcode) {
		if ((iskip < nskip) && (pointer == skip[iskip*2])) {
			fseek(testfile, skip[iskip*2+1], SEEK_SET);
			fseek(reffile, skip[iskip*2+1], SEEK_SET);
			iskip++;
		}

		n1 = fread(&chunktest, sizeof(chunktest), 1, testfile);
		n2 = fread(&chunkref, sizeof(chunkref), 1, reffile);
		if (n1 != n2) {
		    	fprintf(stderr,"\tThe files differ by the size\n");
			retcode = 12;
			break;
		}
		
		if ((n1 == 0) || (n2==0))
			break;

		// Check that the ref and test are the same
		if (chunkref != chunktest) {
		    	fprintf(stderr, 
			        "\tThe files differs at 0x%08x\n", 
				(unsigned int)pointer);
			retcode = 13;
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

	return retcode;
}


