#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#define BUFF_SIZE 120
MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

DECLARE_WAIT_QUEUE_HEAD(readQ);
DECLARE_WAIT_QUEUE_HEAD(writeQ);
struct semaphore sem;

int lifo[16];
int pos = 0;
int konf = 1;

int endRead = 0;

int lifo_open(struct inode *pinode, struct file *pfile);
int lifo_close(struct inode *pinode, struct file *pfile);
ssize_t lifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t lifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
int bintodec(int n)
{

	int dec = 0;

	int base = 1;

	int temp = n;

	while (temp)
	{
		int last_digit = temp % 10;
		temp = temp / 10;
		dec += last_digit * base;
		base = base * 2;
	}
	
	return dec;

}

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = lifo_open,
	.read = lifo_read,
	.write = lifo_write,
	.release = lifo_close,
};


int lifo_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened lifo\n");
		return 0;
}

int lifo_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed lifo\n");
		return 0;
}

ssize_t lifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	char buff[BUFF_SIZE];
	long int len = 0;
	int value1;
	int i = 0;
	if (endRead){
		endRead = 0;
		return 0;
	}

	if(down_interruptible(&sem))
		return -ERESTARTSYS;
	while(pos == 0)
	{
		up(&sem);
		if(wait_event_interruptible(readQ,(pos>0)))
			return -ERESTARTSYS;
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
	}
	
	
	if(pos > 0)
	{
		if(konf == 1)
		endRead = 1;	
		if(konf > 1)
		{
			endRead = 0;
			konf--;
		}
		pos --;
		len = scnprintf(buff, BUFF_SIZE, "%d ", lifo[0]);
		ret = copy_to_user(buffer, buff, len);
	
		if(ret)
			return -EFAULT;
		ret = sscanf(buff, "%d", &value1);
		printk(KERN_INFO "Succesfully read value %d", value1);
			for(; i<15; i++)
		{
			lifo[i] = lifo[i + 1];
		}
		
	}
	else
	{
			printk(KERN_WARNING "Lifo is empty\n"); 
	}
	

	up(&sem);
	wake_up_interruptible(&writeQ);
	return len;
}

ssize_t lifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	int value;
	int ret;
	int i;
	int br;	

	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length-1] = '\0';
	if(sscanf(buff,"num=%d",&konf))
		return length;	
	if(down_interruptible(&sem))
		return -ERESTARTSYS;
	while(pos == 16)
	{
		up(&sem);
		if(wait_event_interruptible(writeQ,(pos<16)))
			return -ERESTARTSYS;
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
	}

	while(pos < 16 && buff[0]!='\0')
	{	
	if(pos<16)
	{
					
		ret = sscanf(buff,"0b%d;",&value);
		value = bintodec(value);
		br = 0;
		for(;br<11;br++)
		{
			i = 0;
			for(;i<length-1;i++)
			{
					buff[i] = buff[i+1];
			}
			
			
		}	
		if(ret==1)//one parameter parsed in sscanf
		{
			printk(KERN_INFO "Succesfully wrote value %d", value); 
			lifo[pos] = value; 
			pos=pos+1;
				
			
		}
		else
		{
			printk(KERN_WARNING "Wrong command format\n");
			return length;
		}
			
				
		
	}
	else
	{
		printk(KERN_WARNING "Lifo is full\n"); 
	}
	}
	up(&sem);
	wake_up_interruptible(&readQ);

	return length;
}

static int __init lifo_init(void)
{
   int ret = 0;
	int i=0;
	sema_init(&sem,1);	

	//Initialize array
	for (i=0; i<16; i++)
		lifo[i] = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "lifo");
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "lifo_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, "lifo");
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   return 0;

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit lifo_exit(void)
{
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(lifo_init);
module_exit(lifo_exit);
