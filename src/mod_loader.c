#include <stdio.h>
#include <assert.h>

/* WARNING: This program modifies the supplied program in place. 
 * It changes the default loader of the program from /lib64/ld-linux-x86-64.so.2 to /lib64/ld-linuf-x86-64.so.2
 * Make sure to have the custom loader as /lib64/ld-linux-x86-64.so.2
 */

int main(int argc, char *argv[])
{
	FILE *fp;
	unsigned char c;
	
	if(argc != 2) goto usage_ret;

	fp = fopen(argv[1], "rb+");
	if(fp == NULL) {
		printf("Unable to open %s for writing.\n", argv[1]);
		goto usage_ret;
	}
	
	fseek(fp, 0x27a, SEEK_SET);
	//fseek(fp, 0x242, SEEK_SET);
	/* Verify that the contents are linu */
	c = fgetc(fp);
	assert(c == 'l');
	c = fgetc(fp);
	assert(c == 'i');
	c = fgetc(fp);
	assert(c == 'n');
	c = fgetc(fp);
	assert(c == 'u');
	fputc('f', fp);
	fclose(fp);
	printf("%s now uses /lib64/ld-linuf-x86-64.so.2 as the default loader.\n", argv[1]);
	
	return 0;

usage_ret:
	printf("%s <prog>\n", argv[1]);
	return -1;
}
	
