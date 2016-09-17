#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>

#define DEV_DRIVER_NAME "afapci"
#include <linux/pci.h>

#define DRV_EXTRAVERSION ""
#define DRV_VERSION "0.1" DRV_EXTRAVERSION

char afad_driver_name[] = "afapci";
const char afad_driver_version[] = DRV_VERSION;

// =======================================================================================================
// prefix "AFAD" stands for "AFA Driver",
// which in turn stands for "ASPECT FPGA Accelerator Driver",
// which in turn stands for "A SPEctra Clustering Tool FPGA Accelerator Driver",
// which in turn stands for "A SPEctra Clustering Tool Field Programmable Gate Array Accelerator Driver"
// =======================================================================================================

typedef struct
{
	void __iomem *barHWAddress;
	unsigned long barFlags;
	char barValid;
} AFAD_BAR_t;

// here we collect all relevant data of the driver at one place
typedef struct
{
	char *bufferRead;
	char *bufferWrite;
	struct class *driverClass;
	struct device *driverDevice;
	AFAD_BAR_t bars[ 6 ];
	struct cdev charDev;
	int irq;
	int numDevices;
	int driverMajor;
	int driverMinor;
	int deviceOpen;
} AFAD_WorkData_t;

static AFAD_WorkData_t AFAD_AdapterBoard;
//static struct cdev AFAD_cdev;
//static struct class *AFAD_class;

// =======================================================================================================
//
// AFAD device functions
//
// =======================================================================================================

static ssize_t
AFAD_read(
	struct file *filp,
	char *buffer,
	size_t bufferSize,
	loff_t *offset )
{
	unsigned long unusedBytes;

	printk( "read: 0x%p [%ld, 0x%8.8lx]\n", buffer, bufferSize, bufferSize );
	unusedBytes = copy_to_user( ( void * ) buffer, AFAD_AdapterBoard.bars[ 0 ].barHWAddress, bufferSize );
	printk( "read: %ld [%ld]\n", bufferSize, unusedBytes );
	return 0;
}

static ssize_t
AFAD_write(
	struct file *filp,
	const char *buffer,
	size_t bufferSize,
	loff_t *offset )
{
	unsigned long unusedBytes;
	printk( "write: 0x%p [%ld, 0x%8.8lx]\n", buffer, bufferSize, bufferSize );
	unusedBytes = copy_from_user( AFAD_AdapterBoard.bars[ 0 ].barHWAddress, ( void * ) buffer, bufferSize );
	printk( "write: %ld [%ld]\n", bufferSize, unusedBytes );
	return 0;
}

static int
AFAD_open(
	struct inode *inode,
	struct file *file )
{
	printk( "open ...\n" );
	if ( AFAD_AdapterBoard.deviceOpen )
	{
		printk( "open: already open\n" );
		return -EBUSY;
	}
	AFAD_AdapterBoard.deviceOpen++;
	try_module_get( THIS_MODULE );
	printk( "open\n" );
	return 0;	// success
}

static int
AFAD_release(
	struct inode *inode,
	struct file *file )
{
	printk( "close ...\n" );
	if ( 0 == AFAD_AdapterBoard.deviceOpen )
	{
		printk( "release: not open\n" );
		return -EBUSY;
	}
	AFAD_AdapterBoard.deviceOpen--;

	module_put( THIS_MODULE );

	printk( "close\n" );
	return 0;	// success
}


struct file_operations AFAD_fops =
{
	.owner   = THIS_MODULE,
	.read    = AFAD_read,
	.write   = AFAD_write,
	.open    = AFAD_open,
	.release = AFAD_release,	//close
};

// =======================================================================================================
//
// AFAD management functions
//
// =======================================================================================================
static int
AFAD_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *pdev_id )
{
	int err = 0;
	unsigned long i;
	unsigned long mmioStart;
	unsigned long mmioLen;
	struct device *dev = &pdev->dev;

	printk(KERN_INFO "AFAD_probe: driver init: %s (enter) ==================\n", afad_driver_name );
	do
	{
		err = pci_enable_device( pdev );
		if ( err )
		{
			dev_err( dev, "pci_enable_device probe error %d for device %s\n",
				 err, pci_name(pdev));
			return err;
		}

		for ( i = 0; i < sizeof( AFAD_AdapterBoard.bars ) / sizeof( AFAD_BAR_t ); ++i )
		{
printk( "BAR#%ld:\n", i );
			AFAD_AdapterBoard.bars[ i ].barValid = 0;
			mmioStart = pci_resource_start( pdev, i );
			mmioLen   = pci_resource_len( pdev, i );
printk( "Start: [0x%8.8lx] %lu\n", mmioStart, mmioStart );
printk( "Len:   [0x%8.8lx] %lu\n", mmioLen, mmioLen );
			AFAD_AdapterBoard.bars[ i ].barHWAddress = pci_iomap( pdev, i, mmioLen );
//ioremap( mmioStart, mmioLen );
printk( "Addr:  [0x%p]\n", AFAD_AdapterBoard.bars[ i ].barHWAddress );
			AFAD_AdapterBoard.bars[ i ].barFlags = pci_resource_flags( pdev, i );
printk( "Flags: [0x%8.8lx] %lu\n", AFAD_AdapterBoard.bars[ i ].barFlags, AFAD_AdapterBoard.bars[ i ].barFlags );
			if ( NULL != AFAD_AdapterBoard.bars[ i ].barHWAddress )
			{
				AFAD_AdapterBoard.bars[ i ].barValid = 1;
			}
		}
		err = pci_request_regions( pdev, DEV_DRIVER_NAME );
	} while ( 0 );

	printk(KERN_INFO "AFAD_probe: driver init: %s (exit) ===================\n", afad_driver_name );
	return err;
}

static void AFAD_remove(
	struct pci_dev *pdev )
{
	unsigned long i;
	printk(KERN_INFO "AFAD_remove: driver remove: %s (enter) ===============\n", afad_driver_name );
//	free_irq( pdev->irq, xxx );
	for ( i = 0; i < sizeof( AFAD_AdapterBoard.bars ) / sizeof( AFAD_BAR_t ); ++i )
	{
		if ( AFAD_AdapterBoard.bars[ i ].barValid )
		{
			pci_iounmap( pdev, AFAD_AdapterBoard.bars[ i ].barHWAddress );
			AFAD_AdapterBoard.bars[ i ].barValid = 0;
		}
	}
	pci_release_regions( pdev );
	pci_disable_device( pdev );
	printk(KERN_INFO "AFAD_remove: driver remove: %s (exit) ================\n", afad_driver_name );
}

static void AFAD_shutdown(
	struct pci_dev * pdev )
{
	printk(KERN_INFO "AFAD_shutdown: driver shutdown: %s ===================\n", afad_driver_name );
}

// =======================================================================================================

static struct pci_device_id
	AFAD_id_table[] =
	{
		{ 0x10ee, 0x7038, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
		{ 0 },
	};

static struct pci_driver
	AFAD_pci_driver_struct =
	{
		.name = afad_driver_name,
		.id_table = AFAD_id_table,
		.probe = AFAD_probe,
		.remove = AFAD_remove,
		.shutdown = AFAD_shutdown,
//.err_handler = &AFAD_err_handler
	};

// -------------------------------------------------------------------------------------------------------

static int __init
AFAD_init(void)
{
	int rv = 0;		// return value
	int err = 0;		// no error
	int stage = 0;
	char name[ 64 ];
	dev_t majorDev;
	dev_t devt;

	do
	{
		printk(KERN_INFO "AFAD_init: driver start: %s (enter) ==================\n", afad_driver_name );

		// clean memory "because you can never be too clean" ...
		stage++;
		// 1
		memset( &AFAD_AdapterBoard, 0x00, sizeof( AFAD_WorkData_t ));

		// set config data - maybe by parameters from user
		stage++;
		// 2
		AFAD_AdapterBoard.driverMinor = 0;	// user settable
		AFAD_AdapterBoard.numDevices = 1;	// user settable

		// let the system give us a major number
		stage++;
		// 3
		majorDev = MKDEV(
			0,
			0 );
		printk(KERN_INFO "AFAD_init: alloc_chrdev_region\n" );
		rv = alloc_chrdev_region(
			&majorDev,
			0,
			AFAD_AdapterBoard.numDevices,
			afad_driver_name );
		if ( 0 > rv )
		{
			pr_err( "alloc_chardev_region errror\n" );
			// here we end up having an error
			err = 1;
			break;
		}
		AFAD_AdapterBoard.driverMajor = MAJOR( majorDev );

		// initialize fops
		stage++;
		// 4
		printk(KERN_INFO "AFAD_init: cdev_init\n" );
		cdev_init( &AFAD_AdapterBoard.charDev, &AFAD_fops );
		AFAD_AdapterBoard.charDev.owner = THIS_MODULE;

		// apply major and minor number to character device
		stage++;
		// 5
		devt = MKDEV( AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor );
		printk(KERN_INFO "AFAD_init: cdev_add\n" );
		rv = cdev_add( &AFAD_AdapterBoard.charDev, devt, 1 );
		if ( rv )
		{
			pr_err( "cdev_add error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// create sysfs entries
		stage++;
		// 6
		printk(KERN_INFO "AFAD_init: class_create\n" );
		AFAD_AdapterBoard.driverClass = class_create( THIS_MODULE, afad_driver_name );
		if ( NULL == AFAD_AdapterBoard.driverClass )
		{
			pr_err( "class_create error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// create "/dev" node
		stage++;
		// 7
		sprintf( name, "%s%%d", afad_driver_name );	// construct "<name>i%d"
		printk(KERN_INFO "AFAD_init: device_create\n" );
		AFAD_AdapterBoard.driverDevice = device_create(
			AFAD_AdapterBoard.driverClass,
			NULL,
			devt,
			NULL,
			name,
			AFAD_AdapterBoard.driverMinor );
		if ( NULL == AFAD_AdapterBoard.driverDevice )
		{
			pr_err( "device_create error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// register pci device driver
		stage++;
		// 8
		printk(KERN_INFO "AFAD_init: register_driver\n" );
		rv = pci_register_driver(
			&AFAD_pci_driver_struct );
		if ( 0 > rv )
		{
			pr_err( "pci_register_driver error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// success - fall out of pseudo-loop
		printk(KERN_INFO "AFAD_init: driver Major, Minor = %d, %d\n", AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor );
	} while ( 0 );

	if ( err )
	{
		printk(KERN_ERR "Error: Stage %d\n", stage );
		switch ( stage )
		{
			case 8:
			{
				device_destroy(
					AFAD_AdapterBoard.driverClass,
					MKDEV( AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor ));
				// fall through
			}
			case 7:
			{
				class_destroy(
					AFAD_AdapterBoard.driverClass );
				// fall through
			}
			case 6:
			{
				cdev_del(
					&AFAD_AdapterBoard.charDev );
				// fall through
			}
			case 5:
			case 4:
			{
				// unregister device
				unregister_chrdev_region(
					MKDEV( AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor ),
					AFAD_AdapterBoard.numDevices );
				err = -stage;
				break;
			}
			case 3:
			case 2:
			case 1:
			case 0:
			default:
				err = 0;
				// no error possible here
				break;
		}
	}

	printk(KERN_INFO "AFAD_init: driver start: %s (exit) ===================\n", afad_driver_name );
	return err;
}
module_init( AFAD_init );

/*
 * AFAD_exit - Driver cleanup Routine
 *
 * This is called just before the driver is removed from memory
 **/
static void __exit
AFAD_exit(void)
{
	printk(KERN_INFO "AFAD_exit: driver shutdown: %s (enter) ===============\n", afad_driver_name );
	pci_unregister_driver(
		&AFAD_pci_driver_struct );
	device_destroy(
		AFAD_AdapterBoard.driverClass,
		MKDEV( AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor ));
	class_destroy(
		AFAD_AdapterBoard.driverClass );
	cdev_del(
		&AFAD_AdapterBoard.charDev );
	unregister_chrdev_region(
		MKDEV( AFAD_AdapterBoard.driverMajor, AFAD_AdapterBoard.driverMinor ),
		AFAD_AdapterBoard.numDevices );
	printk(KERN_INFO "AFAD_exit: driver shutdown: %s (exit) ================\n", afad_driver_name );
}
module_exit( AFAD_exit );

MODULE_AUTHOR( "Systemberatung Schwarzer, Jesko Schwarzer, <afa@schwarzers.de>" );
MODULE_LICENSE( "GPL" );
MODULE_DESCRIPTION( "Systemberatung Schwarzer (C) 2016/09 AFA Driver for VC709 board from Xilinx" );
MODULE_VERSION( DRV_VERSION );

