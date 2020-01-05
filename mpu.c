/*MPU Device Kernel Module
 *for registering and storing button presses
 *and making them available to user
 *Copyright (C) 2019  Tobias Egger <s1910567016@students.fh-hagenberg.at>
 *This program is free software; you can redistribute it and/or
 *modify it under the terms of the GNU General Public License
 *as published by the Free Software Foundation; either version 2
 *of the License, or (at your option) any later version.
 *
 *This program is distributed in the hope that it will be useful,
 *but WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *GNU General Public License for more details.
 *
 *You should have received a copy of the GNU General Public License
 *along with this program; if not, write to the Free Software
 *Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DEVICE_NAME "mpu"
#define DEVICE_COMP_STR "sch,mpu9250-1.0"
#define R_LEN 0x24
#define SIZE 2
#define THRESHOLD_OFFSET 0x10

struct driver_struct{
	void * addr;
	u8 buffer[SIZE]
	struct miscdevice miscdev;
};

static ssize_t dev_write(struct file *filep, const char __user *mem,
					size_t count, loff_t *offp);
static ssize_t dev_read(struct file *filep, char __user *mem,
					size_t count, loff_t *offp);

static int dev_open(struct inode *inode, struct file *filep);
static int dev_release(struct inode *inode, struct file *filep);

static int dev_probe(struct platform_device *pdev);
static int dev_remove(struct platform_device *pdev);

struct file_operations fops = {
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
};
//TODO
//static struct miscdevice miscdev;
static struct of_device_id driver_match_table[] = {
	{
		.compatible = DEVICE_COMP_STR,
	},
	{},
};

MODULE_DEVICE_TABLE(of, driver_match_table);

static int dev_init(struct driver_struct * data){
	pr_info("Loading Driver");
	pr_info("Creating Misc-Device");
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = DEVICE_NAME;
	data->miscdev.fops = &fops;
	if(misc_register(&data->miscdev)){
		pr_err("Creating Failed");
		return -EIO;
	}
	pr_info("Created Misc-Device");
	return 0;
}

static void dev_exit(struct platform_device *pdev){
	struct driver_struct * ds;
	ds = platform_get_drvdata(pdev);

	pr_info("Unloading Driver");
	pr_info("Unregistering Misc-Device");

	misc_deregister(&ds->miscdev);
	platform_set_drvdata(pdev, NULL);
}


//TODO KZALLOC
static int dev_probe(struct platform_device *pdev){
	void * addr;
	struct resource *r;
	struct driver_struct * ds;

	pr_info("Attempting to probe Driver");

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(r==0){
		pr_err("Failed to fetch Resource");
		return -EINVAL;
	}

	pr_info("Trying to Allocate Region: Start -> %08lx",
		(long unsigned int)r->start);
	pr_info("Trying to Allocate Region: End -> %08lx",
		(long unsigned int)r->end);

	addr = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(addr)){
		pr_err("Failed to Allocate Memory");
		return -EIO;
	}
	pr_info("Succeeded to Register/Allocate/Map Resource");

	ds = devm_kzalloc(&pdev->dev, sizeof(struct driver_struct), GFP_KERNEL);
	if(ds == NULL){
		pr_err("Failed to Alloc Driver Data");
		return -ENOMEM;
	}
	ds->addr = addr;
	platform_set_drvdata(pdev, ds);
	//TODO set data ? Maybe finished
	//Request IRQ
	//...
	return dev_init(ds);
	//...
}
static int dev_remove(struct platform_device *pdev){
	dev_exit(pdev);
	return 0;
}

static struct platform_driver driver_platform_driver = {
	.probe = dev_probe,
	.remove = dev_remove,
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(driver_match_table),
  },
};
module_platform_driver(driver_platform_driver);

static ssize_t dev_read(struct file *filep, char __user *mem,
					size_t count, loff_t *offp){
	struct driver_struct * ds;
	u16 data_to_be_copied[R_LEN/4];
	unsigned long bytes_not_copied;
	u32 i = 0;
	u32 buffer = 0;

	ds = container_of(filep->private_data, struct driver_struct, miscdev);
	if(ds == NULL){
		pr_err("Failed to get Container Info");
		return -EINVAL;
	}

	for(i = 0; i < R_LEN/4; i++){
		// Only Debug
		buffer = ioread32(ds->addr+i*4);
		data_to_be_copied[i] = (u16)buffer;
		printk(KERN_ERR "Data Read is: %04x | Raw Data is %08x", data_to_be_copied[i], buffer);
	}

	if((*offp+count)>R_LEN/2)
		count = R_LEN/2-(*offp);
	if((*offp) < 0){
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if((*offp) > R_LEN/2){
		printk(KERN_ERR "%lld", *offp);
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if(count == 0){
		return count;
	}
	if(count > R_LEN/2){
		printk(KERN_ERR "%d", count);
		pr_err("Invalid Count");
		return -EINVAL;
	}
	bytes_not_copied = copy_to_user(mem, data_to_be_copied+(*offp), count);
	if(bytes_not_copied!=0){
		pr_err("Failed to copy all Bytes");
		return bytes_not_copied;
	}
	*offp += (count-bytes_not_copied);
  return count;
}
static ssize_t dev_write(struct file *filep, const char __user *mem,
					size_t count, loff_t *offp){
	struct driver_struct *ds;
	u16 threshold = 0;
	ds = container_of(filep->private_data, struct driver_struct, miscdev);

	if(ds == NULL){
		pr_err("Failed to get Container Info");
		return -EINVAL;
	}

	pr_info("Addr: Start -> %08lx", (long unsigned int)ds->addr);

	if((*offp+count)>SIZE)
		count = SIZE-(*offp);
	if((*offp) < 0){
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if((*offp) > SIZE){
		printk(KERN_ERR "%lld", *offp);
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if(count == 0){
		return count;
	}
	if(count > SIZE){
		printk(KERN_ERR "%d", count);
		pr_err("Invalid Count");
		return -EINVAL;
	}

	count = count - copy_from_user(ds->buffer + (*offp), mem, count);
	printk(KERN_INFO "Value Read : i -> 1: val - >  0x%02hhx", ds->buffer[0]);
	printk(KERN_INFO "Value Read : i -> 2: val - >  0x%02hhx", ds->buffer[1]);

	threshold = buffer[0] << 8;
	threshold |= buffer[1];

	iowrite16(threshold, ds->addr+THRESHOLD_OFFSET);
}

static int dev_open(struct inode *inode, struct file *filep){
	return 0;
}
static int dev_release(struct inode *inode, struct file *filep){
	return 0;
}

MODULE_AUTHOR("Tobias Egger <s1910567016@students.fh-hagenberg.at>");
MODULE_DESCRIPTION("Kernel Module to control the MPU via Character Device");
MODULE_LICENSE("GPL v2");
