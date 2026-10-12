/*
 * Lanner Paralle LCM Driver Test Program
 */
#include <sys/file.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "plcm_ioctl.h"

int main(int argc, char *argv[])
{
	int devfd;
	unsigned char LCM_Message1[40] = "Lanner@Taiwan";
	unsigned char Ret_Message1[40] = "";
	unsigned char LCM_Message2[40] = "2010-09-07";
	unsigned char Ret_Message2[40] = "";
	unsigned char LCM_Message3[40] = "";
	int i = 0;
	unsigned char Keypad_Value = 0, Pre_Value = 0, Counter = 0;
	unsigned char Keypad_Message[40] = "";
	unsigned char Up[]    = "Up   ";
	unsigned char Down[]  = "Down ";
	unsigned char Left[]  = "Left ";
	unsigned char Right[] = "Right"; 
	int MaxTestTime = 15 * 10; // 30 Sec

	for(i = 0; i < 5; i++)
	{
		LCM_Message3[i*8 + 0] = 0;
                LCM_Message3[i*8 + 1] = 1;
                LCM_Message3[i*8 + 2] = 2;
                LCM_Message3[i*8 + 3] = 3;
                LCM_Message3[i*8 + 4] = 4;
                LCM_Message3[i*8 + 5] = 5;
                LCM_Message3[i*8 + 6] = 6;
                LCM_Message3[i*8 + 7] = 7;
	}
	iopl(3);
	printf("Lanner Parallel LCM Test Program\n");
	devfd = open("/dev/plcm_drv", O_RDWR);
	if(devfd == -1)
	{
		printf("Can't open /dev/plcm_drv\n");
		return -1;
	}
	printf("IOCTRL Testing...\n");
	if(argc == 2)
	{
		if(strcmp("-stop", argv[1]) == 0)
		{
			ioctl(devfd, PLCM_IOCTL_STOP_THREAD, 0);
			printf("plcm_drv thread has been stopped.\n");
			close(devfd);
			return 0;
		}
		if(strcmp("-On", argv[1]) == 0)
		{
			ioctl(devfd, PLCM_IOCTL_BACKLIGHT, 1);
			sleep(3);
			close(devfd);
			return 0;
		}
		if(strcmp("-Off", argv[1]) == 0)
		{
			ioctl(devfd, PLCM_IOCTL_BACKLIGHT, 0);
			sleep(3);
			close(devfd);
			return 0;
		}
		if(strcmp("-LCM1", argv[1]) == 0)
		{
			ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
			write(devfd, LCM_Message1, sizeof(LCM_Message1));
			close(devfd);
			return 0;
		}
		if(strcmp("-LCM2", argv[1]) == 0)
		{
			ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
			write(devfd, LCM_Message2, sizeof(LCM_Message2));
			close(devfd);
			return 0;
		}
		if(strcmp("-Keypad", argv[1]) == 0)
		{
			printf("Keypad Testing....\n");
			printf("  You only have 15 second to test it.\n");
			printf("  Or press Ctrl+C to exit.\n");
			ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
			do{
				Keypad_Value = ioctl(devfd, PLCM_IOCTL_GET_KEYPAD, 0);
				printf("receive value is %x\n",Keypad_Value);
				/*if(Pre_Value != Keypad_Value)
				{
					ioctl(devfd, PLCM_IOCTL_CLEARDISPLAY, 0);
					ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);
					Counter = 0;
				}
				else
				{
					Counter++;
				}
				switch(Keypad_Value)
				{
					case 0xC7:
						strcpy(Keypad_Message, Up);
						break;
					case 0xE7:
						strcpy(Keypad_Message, Left);
						break;
					case 0xEF:
						strcpy(Keypad_Message, Right);
						break;
					case 0xCF:
						strcpy(Keypad_Message, Down);
						break;
				}
				if(Counter > 5)
				{
					write(devfd, Keypad_Message, 40);
				}
				Pre_Value = Keypad_Value;*/
				usleep(100000); // 100 msec
				MaxTestTime--;
				if(MaxTestTime == 0)
				{
					printf("Keypad Testing Finished.\n");
					break;
				}
			}while(1);
			close(devfd);
			return 0;
		}
		printf("Invalid command.\n");
		printf("plcm_test [-stop|-On|-Off|-LCM1|-LCM2|-Keypad]\n");
		close(devfd);
		return 0;	
	}
	printf("  Backlight Control - Off\n");
	ioctl(devfd, PLCM_IOCTL_BACKLIGHT, 0);
	sleep(2);
	printf("  Backlight Control - On\n");
	ioctl(devfd, PLCM_IOCTL_BACKLIGHT, 1);
	sleep(2);
	printf("  Display Control - Display Off\n"); 
	ioctl(devfd, PLCM_IOCTL_DISPLAY_D, 0);
	sleep(2);
	printf("  Display Control - Display On\n"); 
	ioctl(devfd, PLCM_IOCTL_DISPLAY_D, 1);
	sleep(2);
	printf("  Display Control - Cursor Off\n"); 
	ioctl(devfd, PLCM_IOCTL_DISPLAY_C, 0);
	sleep(2);
	printf("  Display Control - Cursor On\n");
	ioctl(devfd, PLCM_IOCTL_DISPLAY_C, 1);
	sleep(2);
	printf("  Display Control - Blinking Off\n"); 
	ioctl(devfd, PLCM_IOCTL_DISPLAY_B, 0);
	sleep(2);
	printf("  Display Control - Blinking On\n"); 
	ioctl(devfd, PLCM_IOCTL_DISPLAY_B, 1);
	sleep(2);
	printf("Write and Read Testing...\n");
	ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	write(devfd, LCM_Message1, sizeof(LCM_Message1));
	printf("  Write: %s\n", LCM_Message1);
	sleep(2);
#if 0
 	read(devfd, Ret_Message1, sizeof(Ret_Message1));
	printf("  Read: %s\n", Ret_Message1);
	sleep(2);
#endif
	ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
	write(devfd, LCM_Message2, sizeof(LCM_Message2));
	printf("  Write: %s\n", LCM_Message2);
	sleep(2);
#if 0
	read(devfd, Ret_Message2, sizeof(Ret_Message2));
	printf("  Read: %s\n", Ret_Message2);
	sleep(2);
    ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	printf("CGRAM Test...\n");
	write(devfd, LCM_Message3, sizeof(LCM_Message3));
	sleep(5);
#endif
	printf("Keypad Testing....\n");
	printf("  You only have 15 second to test it.\n");
	printf("  Or press Ctrl+C to exit.\n");
	ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	do{
		Keypad_Value = ioctl(devfd, PLCM_IOCTL_GET_KEYPAD, 0);
		printf("receive value is %x\n",Keypad_Value);
		/*if(Pre_Value != Keypad_Value)
		{
			ioctl(devfd, PLCM_IOCTL_CLEARDISPLAY, 0);
			ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);
			Counter = 0;
		}
		else
		{
			Counter++;
		}
		switch(Keypad_Value)
		{
			case 0xC7:
				strcpy(Keypad_Message, Up);
				break;
			case 0xE7:
				strcpy(Keypad_Message, Left);
				break;
			case 0xEF:
				strcpy(Keypad_Message, Right);
				break;
			case 0xCF:
				strcpy(Keypad_Message, Down);
				break;
		}
		if(Counter > 5)
		{
			write(devfd, Keypad_Message, 40);
		}
		Pre_Value = Keypad_Value;*/
		usleep(100000); // 100 msec
		MaxTestTime--;
		if(MaxTestTime == 0)
		{
			printf("Keypad Testing Finished.\n");
			break;
		}
	}while(1);
	close(devfd);
	return 0;
}

