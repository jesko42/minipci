#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>

#define MPD_EXTRAVERSION
#define MPD_VERSION 		"0.4" MPD_EXTRAVERSION

// 0.1 - only read and write
// 0.2 - mmap added
// 0.3 - IOCTL added

char MPD_driver_name[] = "minipci";
const char MPD_driver_version[] = MPD_VERSION;

static struct pci_device_id
	MPD_id_table[] =
	{
		{ 0x10ee, 0x7038, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
		{ 0 },
	};

// =======================================================================================================
// prefix "MPD" stands for "Mini PCI Driver",
// =======================================================================================================

typedef struct
{
	void __iomem *barHWAddress;
	unsigned long mmioStart;
	unsigned long barSizeInBytes;
	unsigned long barFlags;
	char barValid;
} MPD_BAR_t;

// here we collect all relevant data of the driver at one place
typedef struct
{
	char *bufferRead;
	char *bufferWrite;
	struct class *driverClass;
	struct device *driverDevice;
	MPD_BAR_t bars[ 6 ];
	struct cdev charDev;
	unsigned long BARIndex;
	unsigned long maxBARIndex;
	unsigned long barMask;
	unsigned long maxBARs;
	int irq;
	int numDevices;
	int driverMajor;
	int driverMinor;
	int deviceOpen;
} MPD_WorkData_t;

static MPD_WorkData_t MPD_AdapterBoard;

// =======================================================================================================
//
// MPD device functions
//
// =======================================================================================================

enum
{
	MPD_BAR_CHG = 1024,
	MPD_GET_BAR_MASK,
	MPD_GET_BAR_MAX_INDEX,
	MPD_GET_BAR_MAX_NUM,
} MPD_IOCTLS;

static long
MPD_ioctl(
	struct file *filp,
	unsigned int cmd,
	unsigned long arg )
{
	printk(KERN_INFO "MPD_ioctl: cmd = %d, arg = %lu [0x%16.16lx]\n", cmd, arg, arg );
	switch ( cmd )
	{
		// we have a maximum of 6 BARs (Basic Address Ranges).
		// During initialization we collected the maximum index
		// and created a mask indicating which BAR is usable
		// (length different from zero)
		case MPD_BAR_CHG:
		{
			// check index against max index of BARs
			if ( arg <= MPD_AdapterBoard.maxBARIndex )
			{
				// check index against usable index
				if ( MPD_AdapterBoard.barMask & ( 1 << arg ))
				{
					MPD_AdapterBoard.BARIndex = arg;
					printk( KERN_INFO "MPD_ioctl: MPD_BAR_CHG successful\n" );
					break;
				}
			}
			printk( KERN_INFO "MPD_ioctl: MPD_BAR_CHG error\n" );
			return -EINVAL;
		}

		// show with a mask what different BARs are available
		case MPD_GET_BAR_MASK:
		{
			return ( long ) MPD_AdapterBoard.barMask;
		}

		// the maximum index of a BAR ranges from 0 to 5 (maximum is 6 BARs)
		case MPD_GET_BAR_MAX_INDEX:
		{
			return ( long ) MPD_AdapterBoard.maxBARIndex;
		}

		// here we return the number of used BARs
		case MPD_GET_BAR_MAX_NUM:
		{
			return ( long ) MPD_AdapterBoard.maxBARs;
		}

		// nothing useful received
		default:
			return -EINVAL;
	}
	return 0;
}

static ssize_t
MPD_read(
	struct file *filp,
	char *buffer,
	size_t bufferSize,
	loff_t *offset )
{
	unsigned long unusedBytes;
	phys_addr_t pa;

	printk( "read: 0x%p [%ld, 0x%8.8lx]\n", buffer, bufferSize, bufferSize );
	unusedBytes = copy_to_user( ( void * ) buffer, MPD_AdapterBoard.bars[ MPD_AdapterBoard.BARIndex ].barHWAddress, bufferSize );
	printk( "read: %ld [%ld]\n", bufferSize, unusedBytes );

//phys_addr_t virt_to_phys( volatile void *address );
//void *      phys_to_virt( phys_addr_t    address );

	pa = virt_to_phys( buffer );
	printk( "read: pysical address of user buffer: 0x%16.16llx\n", pa );

	return 0;
}

static ssize_t
MPD_write(
	struct file *filp,
	const char *buffer,
	size_t bufferSize,
	loff_t *offset )
{
	unsigned long unusedBytes;
	printk( "write: 0x%p [%ld, 0x%8.8lx]\n", buffer, bufferSize, bufferSize );
	unusedBytes = copy_from_user( MPD_AdapterBoard.bars[ MPD_AdapterBoard.BARIndex ].barHWAddress, ( void * ) buffer, bufferSize );
	printk( "write: %ld [%ld]\n", bufferSize, unusedBytes );
	return 0;
}

static int
MPD_mmap(
	struct file *filp,
	struct vm_area_struct *vma )
{
        unsigned long offset;
	int rc;

	printk( "mmap: vm_start: 0x%8.8lx, vm_end: 0x%8.8lx, vm_pgoff: 0x%8.8lx\n", vma->vm_start, vma->vm_end, vma->vm_pgoff );

	offset = vma->vm_pgoff << PAGE_SHIFT;
        if (( offset + ( vma->vm_end - vma->vm_start )) > MPD_AdapterBoard.bars[ MPD_AdapterBoard.BARIndex ].barSizeInBytes )
	{
		printk( KERN_INFO "MPD_mmap: size error\n" );
                return -EINVAL;
	}

        offset += (unsigned long) MPD_AdapterBoard.bars[ MPD_AdapterBoard.BARIndex ].mmioStart;

        vma->vm_page_prot = pgprot_noncached( vma->vm_page_prot );

        rc = io_remap_pfn_range( vma, vma->vm_start, offset >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot );
	if ( rc )
	{
		printk( KERN_INFO "MPD_mmap: io_remap_pfn_range() error: rc = %d\n", rc );
                return -EAGAIN;
	}
        return 0;
}

static int
MPD_open(
	struct inode *inode,
	struct file *file )
{
	printk( "open ...\n" );
	if ( MPD_AdapterBoard.deviceOpen )
	{
		printk( "open: already open\n" );
		return -EBUSY;
	}
	MPD_AdapterBoard.deviceOpen++;
	try_module_get( THIS_MODULE );
	printk( "open\n" );
	return 0;	// success
}

static int
MPD_release(
	struct inode *inode,
	struct file *file )
{
	printk( "close ...\n" );
	if ( 0 == MPD_AdapterBoard.deviceOpen )
	{
		printk( "release: not open\n" );
		return -EBUSY;
	}
	MPD_AdapterBoard.deviceOpen--;

	module_put( THIS_MODULE );

	printk( "close\n" );
	return 0;	// success
}


struct file_operations MPD_fops =
{
	.owner            = THIS_MODULE,
	.read             = MPD_read,
	.write            = MPD_write,
	.unlocked_ioctl   = MPD_ioctl,
	.mmap             = MPD_mmap,
	.open             = MPD_open,
	.release          = MPD_release,	//close
};

// =======================================================================================================
//
// MPD management functions
//
// =======================================================================================================
static int
MPD_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *pdev_id )
{
	int err = 0;
	unsigned long i;
	unsigned long mmioStart;
	unsigned long mmioLen;
	unsigned long barMask = 0;	// all valid BARs get an one-bit here
	unsigned long maxBARs = 0;	// number of valid identified BARs
	unsigned long maxBARIndex = 0;	// index of highes valid BAR index
	struct device *dev = &pdev->dev;

	printk(KERN_INFO "MPD_probe: driver init: %s (enter) ==================\n", MPD_driver_name );

	err = pci_enable_device( pdev );
	if ( err )
	{
		dev_err( dev, "pci_enable_device probe error %d for device %s\n",
			 err, pci_name(pdev));
		return err;
	}

	for ( i = 0; i < sizeof( MPD_AdapterBoard.bars ) / sizeof( MPD_BAR_t ); ++i )
	{
		printk( "BAR#%ld:\n", i );
		MPD_AdapterBoard.bars[ i ].barValid = 0;
		mmioStart = pci_resource_start( pdev, i );
		mmioLen   = pci_resource_len( pdev, i );
		printk( "Start: [0x%8.8lx] %lu\n", mmioStart, mmioStart );
		printk( "Len:   [0x%8.8lx] %lu\n", mmioLen, mmioLen );
		MPD_AdapterBoard.bars[ i ].barHWAddress = pci_iomap( pdev, i, mmioLen );
		MPD_AdapterBoard.bars[ i ].mmioStart = mmioStart;
		MPD_AdapterBoard.bars[ i ].barSizeInBytes = mmioLen;
		printk( "Addr:  [0x%p]\n", MPD_AdapterBoard.bars[ i ].barHWAddress );
		MPD_AdapterBoard.bars[ i ].barFlags = pci_resource_flags( pdev, i );
		printk( "Flags: [0x%8.8lx] %lu\n", MPD_AdapterBoard.bars[ i ].barFlags, MPD_AdapterBoard.bars[ i ].barFlags );
		if (( NULL != MPD_AdapterBoard.bars[ i ].barHWAddress   ) &&
		    (    0 != MPD_AdapterBoard.bars[ i ].barSizeInBytes ))
		{
			MPD_AdapterBoard.bars[ i ].barValid = 1;
			barMask |= 1 << i;
			maxBARs++;
			maxBARIndex = i;
		}
	}
	MPD_AdapterBoard.barMask = barMask;
	MPD_AdapterBoard.maxBARs = maxBARs;
	MPD_AdapterBoard.maxBARIndex = maxBARIndex;
	err = pci_request_regions( pdev, MPD_driver_name );

	printk(KERN_INFO "MPD_probe: driver init: barMask: 0x%2.2lx\n", MPD_AdapterBoard.barMask );
	printk(KERN_INFO "MPD_probe: driver init: maxBARs: %ld\n", MPD_AdapterBoard.maxBARs );
	printk(KERN_INFO "MPD_probe: driver init: maxBARIndx: %ld\n", MPD_AdapterBoard.maxBARIndex );
	printk(KERN_INFO "MPD_probe: driver init: %s (exit) ===================\n", MPD_driver_name );
	return err;
}

static void
MPD_remove(
	struct pci_dev *pdev )
{
	unsigned long i;
	printk(KERN_INFO "MPD_remove: driver remove: %s (enter) ===============\n", MPD_driver_name );
//	free_irq( pdev->irq, xxx );
	for ( i = 0; i < sizeof( MPD_AdapterBoard.bars ) / sizeof( MPD_BAR_t ); ++i )
	{
		if ( MPD_AdapterBoard.bars[ i ].barValid )
		{
			pci_iounmap( pdev, MPD_AdapterBoard.bars[ i ].barHWAddress );
			MPD_AdapterBoard.bars[ i ].barValid = 0;
		}
	}
	pci_release_regions( pdev );
	pci_disable_device( pdev );
	printk(KERN_INFO "MPD_remove: driver remove: %s (exit) ================\n", MPD_driver_name );
}

static void
MPD_shutdown(
	struct pci_dev *pdev )
{
	printk(KERN_INFO "MPD_shutdown: driver shutdown: %s ===================\n", MPD_driver_name );
}

// =======================================================================================================

static struct pci_driver
	MPD_pci_driver_struct =
	{
		.name = MPD_driver_name,
		.id_table = MPD_id_table,
		.probe = MPD_probe,
		.remove = MPD_remove,
		.shutdown = MPD_shutdown,
//.err_handler = &MPD_err_handler
	};

// -------------------------------------------------------------------------------------------------------

static int __init
MPD_init(void)
{
	int rv = 0;		// return value
	int err = 0;		// no error
	int stage = 0;
	char name[ 64 ];
	dev_t majorDev;
	dev_t devt;

	do
	{
		printk(KERN_INFO "MPD_init: driver start: %s (enter) ==================\n", MPD_driver_name );
		printk(KERN_INFO "MPD_init: driver version: %s\n", MPD_driver_version );
 		//printk(KERN_INFO "MPD_INIT: " __DATE__ ", " __TIME__ ")" );
 
		// clean memory "because you can never be too clean" ...
		stage++;
		// 1
		memset( &MPD_AdapterBoard, 0x00, sizeof( MPD_WorkData_t ));

		// set config data - maybe by parameters from user
		stage++;
		// 2
		MPD_AdapterBoard.driverMinor = 0;	// user settable
		MPD_AdapterBoard.numDevices = 1;	// user settable

		// let the system give us a major number
		stage++;
		// 3
		majorDev = MKDEV(
			0,
			0 );
		printk(KERN_INFO "MPD_init: alloc_chrdev_region\n" );
		rv = alloc_chrdev_region(
			&majorDev,
			0,
			MPD_AdapterBoard.numDevices,
			MPD_driver_name );
		if ( 0 > rv )
		{
			pr_err( "alloc_chardev_region errror\n" );
			// here we end up having an error
			err = 1;
			break;
		}
		MPD_AdapterBoard.driverMajor = MAJOR( majorDev );

		// initialize fops
		stage++;
		// 4
		printk(KERN_INFO "MPD_init: cdev_init\n" );
		cdev_init( &MPD_AdapterBoard.charDev, &MPD_fops );
		MPD_AdapterBoard.charDev.owner = THIS_MODULE;

		// apply major and minor number to character device
		stage++;
		// 5
		devt = MKDEV( MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor );
		printk(KERN_INFO "MPD_init: cdev_add\n" );
		rv = cdev_add( &MPD_AdapterBoard.charDev, devt, 1 );
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
		printk(KERN_INFO "MPD_init: class_create\n" );
		MPD_AdapterBoard.driverClass = class_create( THIS_MODULE, MPD_driver_name );
		if ( NULL == MPD_AdapterBoard.driverClass )
		{
			pr_err( "class_create error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// create "/dev" node
		stage++;
		// 7
		sprintf( name, "%s%%d", MPD_driver_name );	// construct "<name>i%d"
		printk(KERN_INFO "MPD_init: device_create\n" );
		MPD_AdapterBoard.driverDevice = device_create(
			MPD_AdapterBoard.driverClass,
			NULL,
			devt,
			NULL,
			name,
			MPD_AdapterBoard.driverMinor );
		if ( NULL == MPD_AdapterBoard.driverDevice )
		{
			pr_err( "device_create error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// register pci device driver
		stage++;
		// 8
		printk(KERN_INFO "MPD_init: register_driver\n" );
		rv = pci_register_driver(
			&MPD_pci_driver_struct );
		if ( 0 > rv )
		{
			pr_err( "pci_register_driver error\n" );
			// here we end up having an error
			err = 1;
			break;
		}

		// success - fall out of pseudo-loop
		printk(KERN_INFO "MPD_init: driver Major, Minor = %d, %d\n", MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor );
	} while ( 0 );

	if ( err )
	{
		printk(KERN_ERR "Error: Stage %d\n", stage );
		switch ( stage )
		{
			case 8:
			{
				device_destroy(
					MPD_AdapterBoard.driverClass,
					MKDEV( MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor ));
				// fall through
			}
			case 7:
			{
				class_destroy(
					MPD_AdapterBoard.driverClass );
				// fall through
			}
			case 6:
			{
				cdev_del(
					&MPD_AdapterBoard.charDev );
				// fall through
			}
			case 5:
			case 4:
			{
				// unregister device
				unregister_chrdev_region(
					MKDEV( MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor ),
					MPD_AdapterBoard.numDevices );
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

	printk(KERN_INFO "MPD_init: driver start: %s (exit) ===================\n", MPD_driver_name );
	return err;
}
module_init( MPD_init );

/*
 * MPD_exit - Driver cleanup Routine
 *
 * This is called just before the driver is removed from memory
 **/
static void __exit
MPD_exit(void)
{
	printk(KERN_INFO "MPD_exit: driver shutdown: %s (enter) ===============\n", MPD_driver_name );
	pci_unregister_driver(
		&MPD_pci_driver_struct );
	device_destroy(
		MPD_AdapterBoard.driverClass,
		MKDEV( MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor ));
	class_destroy(
		MPD_AdapterBoard.driverClass );
	cdev_del(
		&MPD_AdapterBoard.charDev );
	unregister_chrdev_region(
		MKDEV( MPD_AdapterBoard.driverMajor, MPD_AdapterBoard.driverMinor ),
		MPD_AdapterBoard.numDevices );
	printk(KERN_INFO "MPD_exit: driver shutdown: %s (exit) ================\n", MPD_driver_name );
}
module_exit( MPD_exit );

MODULE_AUTHOR( "Systemberatung Schwarzer, Jesko Schwarzer, <minipci@schwarzers.de>" );
MODULE_LICENSE( "GPL" );
MODULE_DESCRIPTION( "Systemberatung Schwarzer (C) 2016/09 Mini PCI Driver" );
MODULE_VERSION( MPD_VERSION );

