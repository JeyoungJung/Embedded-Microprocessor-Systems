/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

volatile flag;
int mode;

static void stim_in_isr(void* context, alt_u32 id);

int background()
{
	int j;
	int x = 0;
	int grainsize = 4;
	int g_taskProcessed = 0;

	for(j = 0; j < grainsize; j++)
	{
	g_taskProcessed++;
	}
	return x;
}

static void setup_egm_test(alt_u16 period) {
	// Set period
	IOWR(EGM_BASE, 2, period);
	// Set duty cycle
	IOWR(EGM_BASE, 3, period/2);
	// Set enable
	IOWR(EGM_BASE, 0, 1);
}

static alt_u16 get_egm_status(void) {

}

int main()
{
  printf("Hello from Nios II!\n");
  mode = IORD(SWITCH_PIO_BASE, 0);
  // Initialize Interrupt for stim in
  alt_irq_register(STIMULUS_IN_IRQ, NULL, stim_in_isr);
  IOWR(STIMULUS_IN_BASE, 2, 0x1);

  while(IORD(EGM_BASE, 1))
  {

  }

  // query the missed pulses and average latency
  // and set egm enable to low
  return 0;
}


static void stim_in_isr(void* context, alt_u32 id)
{

	IOWR(STIMULUS_IN_BASE, 3,0x0);
}
