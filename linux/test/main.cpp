#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <iostream>

// remember:
//
// in 64-bit linux systems a long is a 64-bit type (sizeof(long)=8) !


// #defines/config
// ===============

#define USE_MMAP
//#define USE_READ_WRITE
//#define PRINTOUT_64K

#define BAR_USAGE_AFACTRL	( 0 )	/* index of bar to use for AFA param block */
#define BAR_USAGE_MEMTEST	( 2 )	/* default index of bar to use for test */

// ----------------------------------------------------------
// JSC 2017-03-06
// ----------------------------------------------------------
#define	TEST_SIZE_IN_MB		( 1 )

// ----------------------------------------------------------
// JSC 2017-03-04
// I need this offset because I have modules running modifying constantly the first 128 bytes
// ----------------------------------------------------------
#define OFFSET			( 0 )

#ifdef DBG
#define DBG_WAIT()	\
do { printf( "<return>\n" ); getchar();} shile ( 0 )
#else
#define DBG_WAIT()	\
do { int a = 0; } while ( 0 )
#endif

// types
// =====

enum MPD_IOCTLS_tag
{
        MPD_BAR_CHG = 1024,
        MPD_GET_BAR_MASK,
        MPD_GET_BAR_MAX_INDEX,
        MPD_GET_BAR_MAX_NUM,
} MPD_IOCTLS;

// prototypes
// ==========
int getch();
int kbhit(void);

int getch()
{
	static int ch = -1, fd = 0;
	struct termios neu, alt;
	fd = fileno(stdin);
	tcgetattr(fd, &alt);
	neu = alt;
	neu.c_lflag &= ~(ICANON|ECHO);
	tcsetattr(fd, TCSANOW, &neu);
	ch = getchar();
	tcsetattr(fd, TCSANOW, &alt);
	return ch;
}

int kbhit(void)
{
	struct termios term, oterm;
	int fd = 0;
	int c = 0;

	tcgetattr(fd, &oterm);
	memcpy(&term, &oterm, sizeof(term));
	term.c_lflag = term.c_lflag & (!ICANON);
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 1;
	tcsetattr(fd, TCSANOW, &term);
	c = getchar();
	tcsetattr(fd, TCSANOW, &oterm);
	if (c != -1)
	ungetc(c, stdin);
	return ((c != -1) ? 1 : 0);
}

int main()
{
	int rc;
	int fileHandle = -1;
	void *bufferSrc = NULL;
	void *bufferDst = NULL;
	unsigned char *bufferSrc2 = NULL;
	unsigned char *bufferDst2 = NULL;
	void *bufferDev = NULL;
	unsigned long sizeMemTest = TEST_SIZE_IN_MB * 1024 * 1024;
	unsigned long sizeAFACtrl = 384;
	unsigned long humanReadableSize = sizeMemTest;
	unsigned long ii, jj;
	unsigned long rv;
	unsigned long status = 0xfedcba9876543210;
	unsigned long *p;
	unsigned char *c;
	unsigned char u = ' ';	// Unit
	clock_t startTime, endTime;
	double finalTime;
	struct termios t;

	printf( "MiniPCI driver Test V1.00, (W) 2017/03 Jesko Schwarzer\n" );
//printf( "sizeof(int)=%ld, sizeof(long)=%ld\n", sizeof(int), sizeof(long));exit(0);

	printf( "* open file\n" );
	fileHandle = open( "/dev/minipci0", O_RDWR );
	if ( fileHandle < 0 )
	{
		printf( "Device cannot be opened\n" );
		exit( -1 );
	}
	DBG_WAIT();

	printf( "* get infos from board\n" );
	rv = ioctl( fileHandle, MPD_GET_BAR_MASK, status );
	printf( "  BARMask (%16.16lx): 0x%2.2lx\n", status, rv );
	rv = ioctl( fileHandle, MPD_GET_BAR_MAX_INDEX, status );
	printf( "  BARMaxIndex (%16.16lx): %ld\n", status, rv );
	rv = ioctl( fileHandle, MPD_GET_BAR_MAX_NUM, status );
	printf( "  BARMaxNum (%16.16lx): %ld\n", status, rv );
	DBG_WAIT();

	while ( humanReadableSize / 1024 > 0 )
	{
		u = ( ' ' == u ) ? 'k' : (( 'k' == u ) ? 'M' :  'G' );
		humanReadableSize /= 1024;
	}

	printf( "* allocate memory (%ld, %ld%cb )\n", sizeMemTest, humanReadableSize, u );
	bufferSrc = malloc( sizeMemTest );
	printf( "  - bufferSrc = %p\n", bufferSrc );
	bufferDst = malloc( sizeMemTest );
	printf( "  - bufferDst = %p\n", bufferDst );
	if (( NULL == bufferSrc ) || ( NULL == bufferDst ))
	{
		printf( "Out of memory\n" );
		exit( -1 );
	}

	printf( "* clear source and destination\n" );
	memset( bufferSrc, 0x00, sizeMemTest );
	memset( bufferDst, 0x00, sizeMemTest );
	DBG_WAIT();

	printf( "* create pattern in source\n" );
	c = ( unsigned char * )bufferSrc;
	for ( ii = 0; ii < sizeMemTest; ++ii )
	{
		c[ ii ] = ( unsigned char )ii;
	}
	DBG_WAIT();

	printf( "* use BAR#%d\n", BAR_USAGE_MEMTEST );
	rv = ioctl( fileHandle, MPD_BAR_CHG, BAR_USAGE_MEMTEST );
	printf( "  rv = %ld\n", rv );
	DBG_WAIT();

#ifdef USE_MMAP
	printf( "* map memory\n" );
	bufferDev = mmap( 0, sizeMemTest, PROT_WRITE, MAP_SHARED, fileHandle, 0 );
	if ( MAP_FAILED == bufferDev )
	{
		printf( "Mmap failed\n" );
		exit( -1 );
	}
	DBG_WAIT();

	printf( "* write hardware\n" );
	startTime = clock();
	memcpy( bufferDev, bufferSrc, sizeMemTest );
	endTime = clock();
	finalTime = ( double ) ( endTime - startTime ) / CLOCKS_PER_SEC;
	printf( "Time: %.2lfsec., %.lfMB/s\n", finalTime, ( double ) TEST_SIZE_IN_MB / finalTime );	// hmmm: ~16MB/s
	DBG_WAIT();

	startTime = clock();
	printf( "* read hardware\n" );
	memcpy( bufferDst, bufferDev, sizeMemTest );
	endTime = clock();
	finalTime = ( double ) ( endTime - startTime ) / CLOCKS_PER_SEC;
	printf( "Time: %.2lfsec., %.lfMB/s\n", finalTime, ( double ) TEST_SIZE_IN_MB / finalTime );
	DBG_WAIT();
#endif

#ifdef USE_READ_WRITE
	printf( "* write hardware\n" );
	startTime = clock();
        rc = write( fileHandle, bufferSrc, sizeMemTest );
	endTime = clock();
	finalTime = endTime - startTime;
	printf( "  - rc = %d (should be zero) ==> %.2lf\n", rc, ( double )( finalTime ) / CLOCKS_PER_SEC;		// hmmm: ~16MB/s

	printf( "* read hardware\n" );
        rc = read( fileHandle, bufferDst, sizeMemTest );		// hmmm: ~1MB/s
	printf( "  - rc = %d (should be zero)\n", rc );
#endif
	printf( "* compare data\n" );
	bufferSrc2 = ( unsigned char * )bufferSrc;
	bufferDst2 = ( unsigned char * )bufferDst;
	rc = memcmp( &bufferSrc2[ OFFSET ], &bufferDst2[ OFFSET ], sizeMemTest - OFFSET );
	
	if ( 0 != rc )
	{
		printf( "  - rc = %d (this is an error! should be zero)\n", rc );
	}
	else
	{
		printf( "  - success!!\n" );
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
	// ===================================================================================

	printf( "* use BAR#%d\n", BAR_USAGE_AFACTRL );
	rv = ioctl( fileHandle, MPD_BAR_CHG, BAR_USAGE_AFACTRL );
	printf( "  rv = %ld\n", rv );

#ifdef USE_MMAP
	printf( "* map memory\n" );
	bufferDev = mmap( 0, sizeAFACtrl, PROT_WRITE, MAP_SHARED, fileHandle, 0 );
	if ( MAP_FAILED == bufferDev )
	{
		printf( "Mmap failed\n" );
		exit( -1 );
	}
#if 1
	printf( "\033[2J" );	// clear screen
	for (int z=0;z<10000;z++)
	{
		printf( "\033[0;0H" );
		printf( "* read hardware (%d)\n", z );
		memcpy( bufferDst, bufferDev, sizeAFACtrl );
		c = ( unsigned char * )bufferDst;
		for ( ii = 0; ii < sizeAFACtrl; ii += 16 )
		{
			printf( "%8.8lx: ", ii );
			for ( jj = 0; jj < 16; ++jj )
			{
				printf( "%2.2x ", c[ ii + jj ]);
			}
			printf( "\n" );
		}
		if ( kbhit())
		{
			char c = getch();
			if ( 27 == c )
			{
				break;
			}
			switch ( c )
			{
				case 's':
				{
					unsigned int *s = ( unsigned int * )bufferSrc;
					unsigned int *d = ( unsigned int * )bufferDev;
					*s = 0x0fd00fd0;
					memcpy( &d[ 0 ], &s[ 0 ], 4 );
					break;
				}
				case 'S':
				{
					unsigned int *s = ( unsigned int * )bufferSrc;
					unsigned int *d = ( unsigned int * )bufferDev;
					*s = 0xd00fd00f;
					memcpy( &d[ 0 ], &s[ 0 ], 4 );
					break;
				}
				case ' ':
				{
					unsigned int *s = ( unsigned int * )bufferSrc;
					unsigned int *d = ( unsigned int * )bufferDev;
					static unsigned int v = 0;
					memcpy( &d[ 1 ], &v, 1 );
					v++;
					break;
				}
			}
		}
	}
	DBG_WAIT();
#endif
#endif

	// ===================================================================================
	printf( "* free memory\n" );
	free( bufferSrc );
	free( bufferDst );

	printf( "* close file\n" );
	close( fileHandle );
}
