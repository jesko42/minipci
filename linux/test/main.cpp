#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// remeber:
//
// in 64-bit linux systems a long is a 64-bit type (sizeof(long)=8) !

#define USE_MMAP
//#define USE_READ_WRITE
//#define PRINTOUT_64K

#define BAR_USAGE	( 0 )	/* default index of bar to use for test */

enum MPD_IOCTLS_tag
{
        MPD_BAR_CHG = 1024,
        MPD_GET_BAR_MASK,
        MPD_GET_BAR_MAX_INDEX,
        MPD_GET_BAR_MAX_NUM,
} MPD_IOCTLS;

int main()
{
	int rc;
	int fileHandle = -1;
	void *bufferSrc = NULL;
	void *bufferDst = NULL;
	void *bufferDev = NULL;
	unsigned long sizeTest = 1 * 1024 * 1024;
	unsigned long humanReadableSize = sizeTest;
	unsigned long ii, jj;
	unsigned long rv;
	unsigned long status = 0xfedcba9876543210;
	unsigned long *p;
	unsigned char *c;
	unsigned char u = ' ';	// Unit

	printf( "* open file\n" );
	fileHandle = open( "/dev/minipci0", O_RDWR );
	if ( fileHandle < 0 )
	{
		printf( "Device cannot be opened\n" );
		exit( -1 );
	}
printf("<return>\n");getchar();
	printf( "* get infos from board\n" );
	rv = ioctl( fileHandle, MPD_GET_BAR_MASK, status );
	printf( "  BARMask (%16.16lx): 0x%2.2lx\n", status, rv );
	rv = ioctl( fileHandle, MPD_GET_BAR_MAX_INDEX, status );
	printf( "  BARMaxIndex (%16.16lx): %ld\n", status, rv );
	rv = ioctl( fileHandle, MPD_GET_BAR_MAX_NUM, status );
	printf( "  BARMaxNum (%16.16lx): %ld\n", status, rv );
printf("<return>\n");getchar();
	while ( humanReadableSize / 1024 > 0 )
	{
		u = ( ' ' == u ) ? 'k' : (( 'k' == u ) ? 'M' :  'G' );
		humanReadableSize /= 1024;
	}

	printf( "* allocate memory (%ld, %ld%cb )\n", sizeTest, humanReadableSize, u );
	bufferSrc = malloc( sizeTest );
	printf( "  - bufferSrc = %p\n", bufferSrc );
	bufferDst = malloc( sizeTest );
	printf( "  - bufferDst = %p\n", bufferDst );
	if (( NULL == bufferSrc ) || ( NULL == bufferDst ))
	{
		printf( "Out of memory\n" );
		exit( -1 );
	}

	printf( "* fill memory1\n" );
	memset( bufferSrc, 0x00, sizeTest );
	memset( bufferDst, 0x00, sizeTest );

printf("<return>\n");getchar();
	printf( "* fill memory2\n" );
	c = ( unsigned char * )bufferSrc;
	for ( ii = 0; ii < sizeTest; ++ii )
	{
		c[ ii ] = ( unsigned char )ii;
	}


printf("<return>\n");getchar();
	printf( "* use BAR#%d\n", BAR_USAGE );
	rv = ioctl( fileHandle, MPD_BAR_CHG, BAR_USAGE );
	printf( "  rv = %ld\n", rv );


printf("<return>\n");getchar();
#ifdef USE_MMAP
	printf( "* map memory\n" );
	bufferDev = mmap( 0, sizeTest, PROT_WRITE, MAP_SHARED, fileHandle, 0 );
	if ( MAP_FAILED == bufferDev )
	{
		printf( "Mmap failed\n" );
		exit( -1 );
	}
printf("<return>\n");getchar();
	printf( "* write hardware\n" );
	memcpy( bufferDev, bufferSrc, sizeTest );

printf("<return>\n");getchar();
	printf( "* read hardware\n" );
	memcpy( bufferDst, bufferDev, sizeTest );

printf("<return>\n");getchar();
#endif

#ifdef USE_READ_WRITE
	printf( "* write hardware\n" );
        rc = write( fileHandle, bufferSrc, sizeTest );
	printf( "  - rc = %d (should be zero)\n", rc );		// hmmm: ~16MB/s

	printf( "* read hardware\n" );
        rc = read( fileHandle, bufferDst, sizeTest );		// hmmm: ~1MB/s
	printf( "  - rc = %d (should be zero)\n", rc );
#endif
	printf( "* compare data\n" );
	rc = memcmp( bufferSrc, bufferDst, sizeTest );
	if ( 0 != rc )
	{
		printf( "  - rc = %d (this is an error! should be zero)\n", rc );
	}
	else
	{
		printf( "  - success!!\n", rc );
	}


#ifdef PRINTOUT_64K
	c = ( unsigned char * )bufferDst;
	for ( ii = 1 * 1024 * 1024; ii < ( 1 * 1024 * 1024 + 64 * 1024 ); ii += 16 )
	{
		printf( "%8.8x: ", ii );
		for ( jj = 0; jj < 16; ++jj )
		{
			printf( "%2.2x ", c[ ii + jj ]);
		}
		printf( "\n" );
	}
#endif

	printf( "* free memory\n" );
	free( bufferSrc );
	free( bufferDst );

	printf( "* close file\n" );
	close( fileHandle );
}
