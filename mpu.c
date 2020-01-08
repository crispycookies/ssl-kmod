/*MPU Device Kernel Module
 *for registering and storing gyro data
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
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <asm/siginfo.h>

#define DEVICE_NAME "mpu"
#define DEVICE_COMP_STR "sch,mpu9250-1.0"
#define R_LEN 0x24
#define SIZE 18
#define THR_REG_SIZE 0x6
#define THR_OFFSET_BUFFER 0x2
#define THR_OFFSET_REGISTER 0x28
#define CFG_OFFSET_REGISTER 0x24
#define TGL_BITMASK 0x2
#define RES_2_LEN 3072
#define TOTAL_RES_LEN R_LEN/2+RES_2_LEN*2
#define IRQ_FLAG_APP 44
#define STREAM_SIZE R_LEN/4
#define CFG_BITMASK_TO_DELETE 0xFFFFFFFE

struct driver_struct{
	void * addr;
	void * addr_rbuffer;
	u8 buffer[SIZE];
	int irq_num;
	u32 pid;
	u16  *data_to_be_copied;
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
	u32 resetvalue = 0;
	ds = platform_get_drvdata(pdev);

	//Toggle Read Bit as Required by Hardware(DEL IRQ) and disable
	resetvalue = ioread32(ds->addr+CFG_OFFSET_REGISTER);
	resetvalue ^= TGL_BITMASK;
	resetvalue &= ~0x0001;
	iowrite32(resetvalue, ds->addr+CFG_OFFSET_REGISTER);

	pr_info("Unloading Driver");
	pr_info("Unregistering Misc-Device");

	misc_deregister(&ds->miscdev);
	platform_set_drvdata(pdev, NULL);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	//TODO
	struct driver_struct *ds = dev_id;
	struct siginfo info;
	struct task_struct *t_struct;
	u32 resetvalue = 0;


	//Toggle Read Bit as Required by Hardware
	resetvalue = ioread32(ds->addr+CFG_OFFSET_REGISTER);
	resetvalue ^= TGL_BITMASK;
	iowrite32(resetvalue, ds->addr+CFG_OFFSET_REGISTER);

	printk(KERN_INFO "IRQ Called");

	t_struct = pid_task(find_vpid(ds->pid), PIDTYPE_PID);
	if(t_struct == NULL)
		return IRQ_HANDLED;

	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = IRQ_FLAG_APP;
	info.si_code = SI_QUEUE;
	info.si_int = 1234;

	/* Tell userspace that IRQ occured */
	send_sig_info(IRQ_FLAG_APP, &info, t_struct);

	return IRQ_HANDLED;
}



//TODO KZALLOC
static int dev_probe(struct platform_device *pdev){
	void * addr;
	void * addr_rbuffer;
	struct resource *r;
	struct resource *r2;
	struct driver_struct * ds;
	int irq_num;

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


	r2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if(r2==0){
		pr_err("Failed to fetch Resource 2");
		return -EINVAL;
	}

	pr_info("Trying to Allocate Region 2: Start -> %08lx",
		(long unsigned int)r2->start);
	pr_info("Trying to Allocate Region 2: End -> %08lx",
		(long unsigned int)r2->end);

	addr_rbuffer = devm_ioremap_resource(&pdev->dev, r2);
	if (IS_ERR(addr_rbuffer)){
		pr_err("Failed to Allocate Memory 2");
		return -EIO;
	}
	pr_info("Succeeded to Register/Allocate/Map Resource 2");


	ds = devm_kzalloc(&pdev->dev, sizeof(struct driver_struct), GFP_KERNEL);
	if(ds == NULL){
		pr_err("Failed to Alloc Driver Data");
		return -ENOMEM;
	}

	ds->data_to_be_copied =
		devm_kzalloc(&pdev->dev, TOTAL_RES_LEN, GFP_KERNEL);
	if(ds->data_to_be_copied == NULL){
		pr_err("Failed to Alloc Driver Buffer-Array");
		return -ENOMEM;
	}

	//Get IRQ
	irq_num = platform_get_irq(pdev, 0);

	if(devm_request_irq(&pdev->dev, irq_num, irq_handler,
				  IRQF_SHARED,
				  DEVICE_NAME, ds)){
						pr_err("Failed to register Interrupt");
						return -EIO;
	}

	ds->addr = addr;
	ds->addr_rbuffer = addr_rbuffer;
	ds->irq_num = irq_num;
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
	unsigned long bytes_not_copied;
	u32 i = 0;
	u32 buffer = 0;
	u32 resetvalue = 0;
	u32 modecheck = 0;
	u32 size = TOTAL_RES_LEN;

	ds = container_of(filep->private_data, struct driver_struct, miscdev);
	if(ds == NULL){
		pr_err("Failed to get Container Info");
		return -EINVAL;
	}
	if(ds->data_to_be_copied == NULL){
		pr_err("Failed to get Buffer Array");
		return -ENOMEM;
	}

	printk(KERN_INFO "%08lx", (long unsigned int)ds->addr_rbuffer);
	printk(KERN_INFO "%08lx", (long unsigned int)ds->addr);

	for(i = 0; i < R_LEN/4; i++){
		// Only Debug
		buffer = ioread32(ds->addr+i*4);
		ds->data_to_be_copied[i] = (u16)buffer;
	}

	//Check Mode
	modecheck = ioread32(ds->addr+CFG_OFFSET_REGISTER);
	if(!(modecheck & 1)){
		size = STREAM_SIZE;
	}
	else{
		//3072 Vlaues
		for(i = 0; i < (RES_2_LEN/2); i++){
			// Only Debug
			buffer =ioread32(ds->addr_rbuffer+i*4);
			ds->data_to_be_copied[i+R_LEN/4] = (u16)buffer;
		}
		printk(KERN_INFO "Iteration: %d", i*4);
	}


	if((*offp+count)>size)
		count = size-(*offp);
	if((*offp) < 0){
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if((*offp) > size){
		printk(KERN_ERR "%lld", *offp);
		pr_err("Invalid Offest");
		return -EINVAL;
	}
	if(count == 0){
		return count;
	}
	if(count > size){
		printk(KERN_ERR "%d", count);
		pr_err("Invalid Count");
		return -EINVAL;
	}
	bytes_not_copied = copy_to_user(mem, ds->data_to_be_copied+(*offp), count);
	if(bytes_not_copied!=0){
		pr_err("Failed to copy all Bytes");
		return bytes_not_copied;
	}

	//Toggle Read Bit as Required by Hardware
	resetvalue = ioread32(ds->addr+CFG_OFFSET_REGISTER);
	resetvalue ^= TGL_BITMASK;
	iowrite32(resetvalue, ds->addr+CFG_OFFSET_REGISTER);




	*offp += (count-bytes_not_copied);
  return count;
}
static ssize_t dev_write(struct file *filep, const char __user *mem,
					size_t count, loff_t *offp){
	struct driver_struct *ds;
	u32 threshold = 0;
	u32 cfg_register = 0;
	int i = 0;
	u32 pid = 0;
	u32 cfg_register_old = 0;

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
	//Config Register
	printk(KERN_INFO "Value Read : i -> 1: val - >  0x%02hhx", ds->buffer[0]);
	printk(KERN_INFO "Value Read : i -> 2: val - >  0x%02hhx", ds->buffer[1]);

	//Threshold Registers
	printk(KERN_INFO "Value Read : i -> 3: val - >  0x%02hhx", ds->buffer[2]);
	printk(KERN_INFO "Value Read : i -> 4: val - >  0x%02hhx", ds->buffer[3]);

	printk(KERN_INFO "Value Read : i -> 5: val - >  0x%02hhx", ds->buffer[4]);
	printk(KERN_INFO "Value Read : i -> 6: val - >  0x%02hhx", ds->buffer[5]);

	printk(KERN_INFO "Value Read : i -> 7: val - >  0x%02hhx", ds->buffer[6]);
	printk(KERN_INFO "Value Read : i -> 8: val - >  0x%02hhx", ds->buffer[7]);

	printk(KERN_INFO "Value Read : i -> 9: val - >  0x%02hhx", ds->buffer[8]);
	printk(KERN_INFO "Value Read : i -> 10: val - >  0x%02hhx", ds->buffer[9]);

	printk(KERN_INFO "Value Read : i -> 11: val - >  0x%02hhx", ds->buffer[10]);
	printk(KERN_INFO "Value Read : i -> 12: val - >  0x%02hhx", ds->buffer[11]);

	printk(KERN_INFO "Value Read : i -> 13: val - >  0x%02hhx", ds->buffer[12]);
	printk(KERN_INFO "Value Read : i -> 14: val - >  0x%02hhx", ds->buffer[13]);

	printk(KERN_INFO "Value Read : i -> 11: val - >  0x%02hhx", ds->buffer[14]);
	printk(KERN_INFO "Value Read : i -> 12: val - >  0x%02hhx", ds->buffer[15]);

	printk(KERN_INFO "Value Read : i -> 13: val - >  0x%02hhx", ds->buffer[16]);
	printk(KERN_INFO "Value Read : i -> 14: val - >  0x%02hhx", ds->buffer[17]);

	cfg_register_old = ioread32(ds->addr+CFG_OFFSET_REGISTER);
	cfg_register = (ds->buffer[1] & ~CFG_BITMASK_TO_DELETE)|cfg_register_old;
	printk(KERN_INFO "CFG_REG no is set to be: %d", cfg_register);
	iowrite32(cfg_register, ds->addr+CFG_OFFSET_REGISTER);

	for(i = 0; i < (THR_REG_SIZE*2);i+=2){
		threshold = ds->buffer[THR_OFFSET_BUFFER+i] << 8;
		threshold |= ds->buffer[THR_OFFSET_BUFFER+i+1];

		printk(KERN_INFO "Built Threshold at i -> %d equals %08x", i ,threshold);

		iowrite32(threshold, ds->addr+THR_OFFSET_REGISTER+(i*2));
	}

	pid = ds->buffer[14] << 24;
	pid |= ds->buffer[15] << 16;
	pid |= ds->buffer[16] << 8;
	pid |= ds->buffer[17];

	printk(KERN_INFO "PID: %d", pid);
	printk(KERN_INFO "PIDS: %d", pid);

	ds->pid = pid;

	return count;
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
