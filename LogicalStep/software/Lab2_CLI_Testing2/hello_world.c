/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Prototypes                                                     */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/
static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/***************************************************************************/
/*  TimerFunction                                                          */
/*                                                                         */
/*  This timer function will provide a 10ms timer and                      */
/*  call ffs_DiskIOTimerproc.                                              */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

/***************************************************************************/
/*  IoInit                                                                 */
/*                                                                         */
/*  Init the hardware like GPIO, UART, and more...                         */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;
#if _USE_LFN
char Lfname[512];
#endif

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */

// File storage information
// Stores up to 20 file objects
#define MAX_FILES 20
#define MAX_FNAME 20
static char fnames[MAX_FILES][MAX_FNAME];
static long flen[MAX_FILES];
static FIL curr_file;
static uint8_t findex;
static uint8_t num_files;
alt_up_audio_dev * audio_dev;
uint32_t s1, s2, cnt, blen = sizeof(Buff);

unsigned int l_buf;
unsigned int r_buf;
static void timer_isr(void *context, alt_u32 id);
static void btn_in_isr(void *context, alt_u32 id);


static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}

// Fsm declarations
#define NUM_STATES 5
typedef void (*fsm_func)(void);
typedef struct State {
	uint8_t id;
	fsm_func func;
} State;
//static State *table[NUM_STATES][NUM_STATES];

volatile State* curr_state;
typedef enum StateId {
	PAUSE = 0,
	PLAY,
	RESET,
	PREV,
	NEXT,
};

void pause_func(void) {
	// Do nothing while we wait to transition
	printf("Pause Func\n");
	while(curr_state->id == PAUSE) {
	}
}

static State pause = {
	.id = PAUSE,
	.func = pause_func,
};

void play_func(void) {
	printf("play func\n");
	printf("%s: \n", fnames[findex]);
	int res;
	int i;
	long p1 = flen[findex];
	printf("%d\n", p1);

	uint16_t sw = IORD(SWITCH_PIO_BASE, 0);
	uint8_t mode = sw & 0x03;
	while (p1 > 0  && curr_state->id == PLAY) // while the number of bytes to read are more than 0
	{
		cnt = sizeof(Buff);
		res = f_read(&File1, &Buff, cnt, &cnt);
		if (res != 0) {
			xprintf("Error reading\n");
			break;
		}
		p1 -= cnt;
		for (i = 0; i < cnt; i+=4) {
			printf("CNT: %d\n", cnt);

			int fifospace = alt_up_audio_read_fifo_avail (audio_dev, ALT_UP_AUDIO_RIGHT);
			if ( fifospace > 0 ) // check if data is available
			{
				printf("fifoSpce: %d\n", fifospace);
				switch(mode) {
				case 0:
					printf("Here\n");
					l_buf = (Buff[i + 1] << 8 | Buff[i]);
					r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
					printf("Here\n");

					while(!alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT));
					res = alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
					if (res != 1) {
						printf("res: %d\n");
					}
					printf("Here\n");

					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
					printf("Here\n");

					break;
				case 1:
					l_buf = (Buff[i + 1] << 8 | Buff[i]);
					r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
					while(alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT) == 0);
					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
					l_buf = (Buff[i + 1] << 8 | Buff[i]);
					r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
					while(alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT) == 0);
					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
					break;
				case 2:
					i += 4;
					l_buf = (Buff[i + 1] << 8 | Buff[i]);
					r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
					while(alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT) == 0);
					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
					break;
				case 3:
					r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_LEFT) == 0);
					while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
					break;
				default:
					xprintf("Bad switch mode\n");
				}

			}
		}
	}
	if (p1 != 0) {
		xprintf("Bytes left over: %d\n", p1);
	}

	xprintf("done\n");
}
static State play = {
	.id = PLAY,
	.func = play_func,
};

void reset_func(void) {
	printf("reset func\n");
	// Reset File Ptr
//    put_rc(f_close(&File1));
	f_open(&File1, fnames[findex], 1);

	// Transition to Pause
	curr_state = &pause;
	return;
}

static State reset = {
	.id = RESET,
	.func = reset_func,
};

void prev_func(void) {
	printf("prev func\n");
	if (findex == 0) {
		findex = num_files;
	} else {
		findex--;
	}
	f_open(&File1, fnames[findex], 1);
	printf("%s\n", fnames[findex]);
	curr_state = &pause;
	return;
}

static State prev = {
	.id = PREV,
	.func = prev_func,
};

void next_func(void) {
	printf("Next func\n");
	if (findex >= num_files) {
		findex = 0;
	} else {
		findex++;
	}
	f_open(&File1, fnames[findex], 1);
	printf("%s\n", fnames[findex]);
	curr_state = &pause;
	return;
}

static State next = {
	.id = NEXT,
	.func = next_func,
};


int isWav(char *filename) {
	if (!strcmp(strrchr(filename, '\0') - 4, ".wav") || !strcmp(strrchr(filename, '\0') - 4, ".WAV")){
		// if the file ends with .wav
	    return 1;
	}
	return 0;
}

/***************************************************************************/
/*  main                                                                   */
/***************************************************************************/
int main(void)
{
	int fifospace;
    char *ptr, *ptr2;
    long p1, p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;                  /* Pointer to file system object */

    alt_up_audio_dev * audio_dev;
    /* used for audio record/playback */
    unsigned int l_buf;
    unsigned int r_buf;
    // open the Audio port
    audio_dev = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev == NULL)
    alt_printf ("Error: could not open audio device \n");
    else
    alt_printf ("Opened audio device \n");

    IoInit();

    IOWR(SEVEN_SEG_PIO_BASE,1,0x0007);

    xputs(PSTR("FatFs module test monitor\n"));
    xputs(_USE_LFN ? "LFN Enabled" : "LFN Disabled");
    xprintf(", Code page: %u\n", _CODE_PAGE);

    // Initialize disk
    // di 0
    p1 = 0;
    xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) p1));

    // Force initiliaze file system
    // fi 0
    p1 = 0;
    put_rc(f_mount((uint8_t) p1, &Fatfs[p1]));

    // Get directory list and
    ptr = '\0';
    res = f_opendir(&Dir, ptr);
    if (res) // if res in non-zero there is an error; print the error.
    {
        put_rc(res);
    }
    for (num_files = 0; num_files < MAX_FILES; num_files++)
    {
        res = f_readdir(&Dir, &Finfo);
        if ((res != FR_OK) || !Finfo.fname[0])
            break;
        if (!isWav(Finfo.fname)) {
        	num_files--;
            continue;
        }
        if (strlen(Finfo.fname) < 20) {
        	strcpy(fnames[num_files], Finfo.fname);
        }

        else {
        	xprintf("Error: file name too long\n");
        	num_files--;
        	continue;
        }
        flen[num_files] = Finfo.fsize;
    }

    for (num_files = 0; num_files < MAX_FILES; num_files++)
    {
    	xprintf("%d: %s, %d\n", num_files, fnames[num_files], flen[num_files]);
    }

    // Open up first file
    f_open(&File1, fnames[0], 1);
    printf("%s\n", fnames[0]);
    // Initialize peripherals
	alt_irq_register(TIMER_0_IRQ, NULL, timer_isr);
	alt_irq_register(BUTTON_PIO_IRQ, NULL, btn_in_isr);
	IOWR(TIMER_0_BASE, 0x01, 0x0);
	IOWR(BUTTON_PIO_BASE, 2, 0xF);

	cnt = sizeof(Buff);
	res = f_read(&File1, &Buff, cnt, &cnt);
	printf("CNT: %d\n", cnt);
	int i = 0;
	for( i = 0; i < cnt; i += 4) {
		l_buf = (Buff[i + 1] << 8 | Buff[i]);
		r_buf = (Buff[i + 3] << 8 | Buff[i + 2]);
		printf( "Rbuf : %d, Lbuf: %d\n", l_buf, r_buf);

		while(alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT) == 0);

		while(alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT) == 0);
	}
// Setup fsm
    curr_state = &pause;
    while (1) {
    	curr_state->func();
    }

    /*
     * This return here make no sense.
     * But to prevent the compiler warning:
     * "return type of 'main' is not 'int'
     * we use an int as return :-)
     */
    return (0);
}

static void btn_in_isr(void *context, alt_u32 id)
{
	// Disable btn interrupt
	IOWR(BUTTON_PIO_BASE, 2, 0x0);

	// Set timer period -> 27.5 * 20ns = 0x14FB18
	IOWR(TIMER_0_BASE, 0x02, 0xFB18);
	IOWR(TIMER_0_BASE, 0x03, 0x0014);

	// Start timer
	IOWR(TIMER_0_BASE, 0x01, 0x05);
}

static void timer_isr(void *context, alt_u32 id)
{
	int btn_state = IORD(BUTTON_PIO_BASE, 0x0);
	if (btn_state == 0xe) {
		printf("Next pressed\n");
		curr_state = &next;
	} else if (btn_state == 0xD) {
		printf("Play/Pause 1 pressed\n");
		if (curr_state->id == PLAY) {
			curr_state = &pause;
		} else {
			curr_state = &play;
		}
	} else if (btn_state == 0x0B) {
		printf("Reset pressed\n");
		curr_state = &reset;
	} else if (btn_state == 0x07) {
		printf("Prev pressed\n");
		curr_state = &prev;
	} // Ignore other combinations

	// Reset timer
	IOWR(TIMER_0_BASE, 0x00, 0x0);
	IOWR(TIMER_0_BASE, 0x01, 0x0);

	// Enable btn interrupt
	IOWR(BUTTON_PIO_BASE, 0x03, 0x0);
	IOWR(BUTTON_PIO_BASE, 2, 0xF);
}
