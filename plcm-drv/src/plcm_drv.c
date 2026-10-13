/*
 * Lanner Parallel LCM driver
 */

/* Standard in kernel modules */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include "plcm_ioctl.h"

/*
 * Driver Version Control
 */
// Version 0.0.1 by Lanner SW as the Init. Version
static const char Driver_Version[] = "0.0.2";

/*
 * Is the device opened right now?
 * Prevent to access the device in the same time
 */
static int stop_thread = 0;
static DEFINE_MUTEX(plcm_lock);

/*
 * Device Depend Function Prototypes
 */
static int LCM_Init(void);
static void LCM_Command(unsigned char RS, unsigned char RWn, unsigned char CMD, unsigned int uDelay, unsigned char *Ret);
static void LCM_Backlight(void);

/*
 * Device Depend Definition
 */
#define LPT1 0x378
#define LPT2 0x278
#define LPT3 0x3BC

/*
 * Device Depend Variables
 */
static unsigned int  Port_Addr = 0; // LPTx Port Address
static unsigned int  DataPort = 0;
static unsigned int  StatusPort = 0;
static unsigned int  ControlPort = 0;
static unsigned char Backlight = 0; // Backlight ON
static unsigned char Cur_Line = 1; // Current Line#
static unsigned char Cur_EntryMode = 0x04; // Current Entry Mode Set CMD
static unsigned char Cur_Display = 0x0C; // Display on, cursor and blink off
static unsigned char Cur_Shift = 0x10; // Current Cursor/Dsiplay Shift Ctrl

static unsigned int row = 0; // count row

#define PLCM_IO_EXTENT 3

//  RS 	R\W     Function
//  0 	1 	    Status flag read
//  0 	0 	    Command write
//  1 	1 	    Display data and cursor address read
//  1 	0 	    Display data and parameter write


static int LCM_Init(void)
{
	unsigned int i = 0;
	static const unsigned int ports[] = { LPT1, LPT2, LPT3 };
	unsigned int port;

	for (i = 0; i < ARRAY_SIZE(ports); i++)
	{
		port = ports[i];
		if (!request_region(port, PLCM_IO_EXTENT, "plcm_drv"))
			continue;
		if (inb(port) != 0xFF)
		{
			Port_Addr = port;
			break;
		}
		release_region(port, PLCM_IO_EXTENT);
	}
	if(Port_Addr == 0)
	{
		pr_err("plcm_drv: cannot find an available LPT port\n");
		return -ENODEV;
	}
	else
	{
		pr_info("plcm_drv: LPT address = 0x%x\n", Port_Addr);
		DataPort = Port_Addr;
		StatusPort = Port_Addr + 1;
		ControlPort = Port_Addr + 2;
		LCM_Command(0, 0, 0x38, 8000, NULL); // Function Set
		LCM_Command(0, 0, 0x38,  300, NULL);
		LCM_Command(0, 0, 0x38,  300, NULL);
		LCM_Command(0, 0, 0x38,  300, NULL);
		LCM_Command(0, 0, Cur_Display, 300, NULL); // Display control
		LCM_Command(0, 0, 0x01, 3000, NULL); // Display Clear
		LCM_Command(0, 0, 0x06,  300, NULL); // Entry Mode Set
		
		/*
		 *  set seg and com
		 */
		LCM_Command(0, 0, 0x3c,  300, NULL);
		LCM_Command(0, 0, 0x49,  300, NULL);
		LCM_Command(0, 0, 0x38,  300, NULL);
		
		LCM_Command(0, 0, 0x80,  300, NULL); // Set DDRAM Address	
//		for(i = 0; i < 20; i++) // Range: 0x00~0x27
		for(i = 0; i < 27; i++) // Range: 0x00~0x27
		{
//			LCM_Command(1, 0, 'A', 300, NULL); // Write Data
			LCM_Command(1, 0, 0x41, 37, NULL); // Write Data
			//count++;
		}
		LCM_Command(0, 0, 0xC0, 300, NULL); // Set DDRAM Address
		for(i = 0; i < 27; i++) // Range: 0x40~0x67
		{
			LCM_Command(1, 0, 0x30+i, 300, NULL); // Write Data
			//count++;
		}
		
		

		// Add character in CGRAM as below.
		// 11111 
		// 10001
		// 10101
		// 10101
		// 10101
		// 10001
		// 11111
		// 00000
		for(i = 0; i < 8; i++)
		{
			LCM_Command(0, 0, 0x40+i*8+0, 300, NULL);
			LCM_Command(1, 0, 0x1F, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+1, 300, NULL);
			LCM_Command(1, 0, 0x11, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+2, 300, NULL);
			LCM_Command(1, 0, 0x15, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+3, 300, NULL);
			LCM_Command(1, 0, 0x15, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+4, 300, NULL);
			LCM_Command(1, 0, 0x15, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+5, 300, NULL);
			LCM_Command(1, 0, 0x11, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+6, 300, NULL);
			LCM_Command(1, 0, 0x1F, 300, NULL);
			LCM_Command(0, 0, 0x40+i*8+7, 300, NULL);
			LCM_Command(1, 0, 0x00, 300, NULL);
		}
	}
	return 0;
}

#define ENABLE 0x02
static void LCM_Command(unsigned char RS, unsigned char RWn, unsigned char CMD, unsigned int uDelay, unsigned char *Ret)
{
	unsigned char Ctrl = 0;

	Ctrl |= Backlight;
	if(RS == 0)
	{
		Ctrl |= 0x08; // RS: Real RS = ~RS
	}
	if(RWn == 1)
	{
		Ctrl |= 0x24; // RWn: Read = 1, Write = 0
	}
	outb(Ctrl | ENABLE, ControlPort); // Set RS and RWn, E = 0
//	udelay(40);
	udelay(2);
	outb(Ctrl & ~ENABLE, ControlPort); // E = 1 
	if(RWn == 0)
	{
		udelay(0);
		outb(CMD, DataPort); // LCM Data Write 
	}
	if((RWn == 1) && (Ret != NULL))
	{
		udelay(2);
		*Ret = inb(DataPort); // LCM Data Read	
	}
//	udelay(300);
	udelay (20) ;
	outb(Ctrl | ENABLE, ControlPort); // E = 0
//	udelay(uDelay + 1);
	udelay(uDelay + 20);
	return;
}

static void LCM_Backlight(void)
{
	unsigned char Ctrl = inb(ControlPort);

	if(Backlight == 1)
	{
		Ctrl |= 0x01;
	}
	else
	{
		Ctrl &= ~0x01;
	}
	outb(Ctrl, ControlPort);
	return;
}

#if 0
static int plcm_thread(void *s)
{
	unsigned int cur_val = 0;
	while(1)
	{
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(10);
		switch(cur_val)
		{
			default:
				break;
		}
		if(stop_thread == 1)
		{
			printk("plcm_drv thread stopped\n");
			return 0;
		}
	}
	return 0;
}
#endif 

static ssize_t plcm_read(struct file *file, char __user * buffer, size_t length, loff_t * offset)
{
	unsigned char LCM_Message[40];
	int i = 0;
	
	if(length < sizeof(LCM_Message))
	{
		return -EINVAL;
	}
	if(Cur_Line == 1)
	{
		LCM_Command(0, 0, 0x80, 300, NULL); // Set DDRAM Address
	}
	else if(Cur_Line == 2)
	{
		LCM_Command(0, 0, 0xC0, 300, NULL);
	}
	for(i = 0; i < 40; i++)
	{
		LCM_Command(1, 1, 0x00, 300, &LCM_Message[i]); // Read Data
		if((i > 0) && (LCM_Message[i - 1] == ' ') && (LCM_Message[i] == ' '))
		{
			LCM_Message[i] = 0;
		}
	}
	if (copy_to_user(buffer, LCM_Message, sizeof(LCM_Message)))
		return -EFAULT;

	return sizeof(LCM_Message);
}

static ssize_t plcm_write(struct file *file, const char __user * buffer, size_t length, loff_t * offset)
{
	unsigned char LCM_Message[40];
	int i = 0;

	if(length > sizeof(LCM_Message))
	{
		return -EINVAL;
	}
	memset(LCM_Message, ' ', sizeof(LCM_Message));
	if (copy_from_user(LCM_Message, buffer, length))
		return -EFAULT;
	if(Cur_Line == 1)
	{
		LCM_Command(0, 0, 0x80, 300, NULL);
	}
	else if(Cur_Line == 2)
	{
		LCM_Command(0, 0, 0xC0, 300, NULL);
	}
	for(i = 0; i < 40; i++)
	{
		if(i < length)
		{
			if(LCM_Message[i] == 0)
			{
				LCM_Message[i] = ' ';
			}
		}
		LCM_Command(1, 0, LCM_Message[i], 300, NULL);
	}
	return length;
}

static long plcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case PLCM_IOCTL_STOP_THREAD:
			printk("sled_drv : PLCM_IOCTL_STOP_THREAD\n");
			stop_thread = 1;
			break;
		case PLCM_IOCTL_BACKLIGHT:
			if(arg == 0)
				Backlight = 1;
			else if (arg == 1)
				Backlight = 0;
			LCM_Backlight();
			break;
		case PLCM_IOCTL_SET_LINE:
			LCM_Command(0, 0, Cur_Display, 300, NULL);
			if(arg == 1)
			{
				Cur_Line = 1;
				LCM_Command(0, 0, 0x80+row, 300, NULL);
			}
			else if(arg == 2)
			{
				Cur_Line = 2;
				LCM_Command(0, 0, 0xC0+row, 300, NULL);
			}
			break;
		case PLCM_IOCTL_CLEARDISPLAY:
			LCM_Command(0, 0, 0x01, 1640, NULL);
			break;
		case PLCM_IOCTL_RETURNHOME:
			LCM_Command(0, 0, 0x02, 1640, NULL);
			break;
		case PLCM_IOCTL_ENTRYMODE_ID:
			if(arg == 0)
				Cur_EntryMode &= ~0x02;
			else if(arg == 1)
				Cur_EntryMode |= 0x02;
			LCM_Command(0, 0, Cur_EntryMode, 300, NULL);
			break;
		case PLCM_IOCTL_ENTRYMODE_SH:
			if(arg == 0)
				Cur_EntryMode &= ~0x01;
			else if(arg == 1)
				Cur_EntryMode |= 0x01;
			LCM_Command(0, 0, Cur_EntryMode, 300, NULL);
			break;
		case PLCM_IOCTL_DISPLAY_D:
			if(arg == 0)
				Cur_Display &= ~0x04;
			else if(arg == 1)
				Cur_Display |= 0x04;
			LCM_Command(0, 0, Cur_Display, 300, NULL);
			break;
		case PLCM_IOCTL_DISPLAY_C:
			if(arg == 0)
				Cur_Display &= ~0x02;
			else if(arg == 1)
				Cur_Display |= 0x02;
			LCM_Command(0, 0, Cur_Display, 300, NULL);
			break;
		case PLCM_IOCTL_DISPLAY_B:
			if(arg == 0)
				Cur_Display &= ~0x01;
			else if(arg == 1)
				Cur_Display |= 0x01;
			LCM_Command(0, 0, Cur_Display, 300, NULL);
			break; 
		case PLCM_IOCTL_SHIFT_SC:
			if(arg == 0)
				Cur_Shift &= ~0x08;
			else if(arg == 1)
				Cur_Shift |= 0x08;
			LCM_Command(0, 0, Cur_Shift, 300, NULL);
			break;
		case PLCM_IOCTL_SHIFT_RL:
			if(arg == 0)
				{
					Cur_Shift &= ~0x04;
					if(row > 0 && row < 20)
					{
						row--;
					}
				}
			else if(arg == 1)
				{
					Cur_Shift |= 0x04;
					if(row < 19)
					{
						row++;
					}
				}
			printk("shift left and right\n");
			LCM_Command(0, 0, Cur_Shift, 300, NULL);
			break;
		case PLCM_IOCTL_GET_KEYPAD:
			return inb(StatusPort);
		case PLCM_IOCTL_INPUT_CHAR:
			printk("input a char \n");
			LCM_Command(1, 0, (char)arg,  300, NULL);
			row ++;
			break;
		default:
			return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * This function is called whenever a process attempts to
 * open the device file
 */
static int plcm_open(struct inode * inode, struct file * file)
{
	/*
	 * Get the minor device number in case you have more than
	 * one physical device using the driver.
	 */
	pr_info("plcm_drv: device opened\n");
	/* we don't want to talk to two processes at the same time */
	if (!mutex_trylock(&plcm_lock))
		return -EBUSY;
	/* Make sure that the module isn't removed while the file
	 * is open by incrementing the usage count (the number of
	 * opened references to the module,if it's zero emmod will
	 * fail)
	 */
	printk("Lanner Parallel LCM Driver Opened\n");
	return 0;
}

/*
 * This function is called when a process closes the device file.
 */
static int plcm_release(struct inode * inode, struct file * file)
{
	/* ready for next caller */
	mutex_unlock(&plcm_lock);
	/* Decrement the usage count, otherwise once you opened the file
	 * you'll never get rid of the module.
	 */
	printk(KERN_NOTICE "Lanner Parallel LCM Driver Closed\n");
	return 0;
}

/*
 * This structure will hold the functions to be called
 * when a process does something to the device we created.
 * Since a pointer to this structure is kept in the
 * devices table, it can't be local to init_module.
 * NULL is for unimplemented functions.
 */
static const struct file_operations plcm_fops = {
	.owner		= THIS_MODULE,
	.read		= plcm_read,
	.write		= plcm_write,
	.unlocked_ioctl	= plcm_ioctl,
	.open		= plcm_open,
	.release	= plcm_release,
};

static struct miscdevice plcm_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "plcm_drv",
	.fops	= &plcm_fops,
};

static int __init plcm_init(void)
{
	int ret;

	ret = LCM_Init();
	if(ret)
		return ret;

	ret = misc_register(&plcm_device);
	if (ret)
	{
		release_region(Port_Addr, PLCM_IO_EXTENT);
		Port_Addr = 0;
		return ret;
	}
	pr_info("Parallel LCM Driver Version %s is loaded\n", Driver_Version);
#if 0
	kernel_thread(plcm_thread, (void *)"Parallel LCM Thread", 0);
#endif
	return 0;
}

/*
 * Cleanup - unregister the appropriate file from /proc
 */
static void __exit plcm_exit(void)
{
	misc_deregister(&plcm_device);
	if (Port_Addr)
		release_region(Port_Addr, PLCM_IO_EXTENT);
	/* If there's an error, report it */
	printk("Parallel LCM Driver Version %s is unloaded\n", Driver_Version);
}

module_init(plcm_init);
module_exit(plcm_exit);

MODULE_AUTHOR("TT Pan");
MODULE_DESCRIPTION("Lanner Parallel LCM Driver");
MODULE_LICENSE("Dual BSD/GPL");

