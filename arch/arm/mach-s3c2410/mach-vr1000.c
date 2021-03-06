/* linux/arch/arm/mach-s3c2410/mach-vr1000.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Machine support for Thorcom VR1000 board. Designed for Thorcom by
 * Simtec Electronics, http://www.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     14-Sep-2004 BJD  USB Power control
 *     04-Sep-2004 BJD  Added new uart init, and io init
 *     21-Aug-2004 BJD  Added struct s3c2410_board
 *     06-Aug-2004 BJD  Fixed call to time initialisation
 *     05-Apr-2004 BJD  Copied to make mach-vr1000.c
 *     18-Oct-2004 BJD  Updated board struct
 *     04-Nov-2004 BJD  Clock and serial configuration update
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/bast-map.h>
#include <asm/arch/vr1000-map.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "usb-simtec.h"

/* macros for virtual address mods for the io space entries */
#define VA_C5(item) ((item) + BAST_VAM_CS5)
#define VA_C4(item) ((item) + BAST_VAM_CS4)
#define VA_C3(item) ((item) + BAST_VAM_CS3)
#define VA_C2(item) ((item) + BAST_VAM_CS2)

/* macros to modify the physical addresses for io space */

#define PA_CS2(item) ((item) + S3C2410_CS2)
#define PA_CS3(item) ((item) + S3C2410_CS3)
#define PA_CS4(item) ((item) + S3C2410_CS4)
#define PA_CS5(item) ((item) + S3C2410_CS5)

static struct map_desc vr1000_iodesc[] __initdata = {
  /* ISA IO areas */

  { S3C2410_VA_ISA_BYTE, PA_CS2(BAST_PA_ISAIO),	   SZ_16M, MT_DEVICE },
  { S3C2410_VA_ISA_WORD, PA_CS3(BAST_PA_ISAIO),	   SZ_16M, MT_DEVICE },

  /* we could possibly compress the next set down into a set of smaller tables
   * pagetables, but that would mean using an L2 section, and it still means
   * we cannot actually feed the same register to an LDR due to 16K spacing
   */

  /* bast CPLD control registers, and external interrupt controls */
  { VR1000_VA_CTRL1, VR1000_PA_CTRL1,		       SZ_1M, MT_DEVICE },
  { VR1000_VA_CTRL2, VR1000_PA_CTRL2,		       SZ_1M, MT_DEVICE },
  { VR1000_VA_CTRL3, VR1000_PA_CTRL3,		       SZ_1M, MT_DEVICE },
  { VR1000_VA_CTRL4, VR1000_PA_CTRL4,		       SZ_1M, MT_DEVICE },

  /* peripheral space... one for each of fast/slow/byte/16bit */
  /* note, ide is only decoded in word space, even though some registers
   * are only 8bit */

  /* slow, byte */
  { VA_C2(VR1000_VA_DM9000),  PA_CS2(VR1000_PA_DM9000),	  SZ_1M,  MT_DEVICE },
  { VA_C2(VR1000_VA_IDEPRI),  PA_CS3(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C2(VR1000_VA_IDESEC),  PA_CS3(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C2(VR1000_VA_IDEPRIAUX), PA_CS3(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C2(VR1000_VA_IDESECAUX), PA_CS3(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* slow, word */
  { VA_C3(VR1000_VA_DM9000),  PA_CS3(VR1000_PA_DM9000),	  SZ_1M,  MT_DEVICE },
  { VA_C3(VR1000_VA_IDEPRI),  PA_CS3(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C3(VR1000_VA_IDESEC),  PA_CS3(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C3(VR1000_VA_IDEPRIAUX), PA_CS3(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C3(VR1000_VA_IDESECAUX), PA_CS3(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, byte */
  { VA_C4(VR1000_VA_DM9000),  PA_CS4(VR1000_PA_DM9000),	  SZ_1M,  MT_DEVICE },
  { VA_C4(VR1000_VA_IDEPRI),  PA_CS5(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C4(VR1000_VA_IDESEC),  PA_CS5(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C4(VR1000_VA_IDEPRIAUX), PA_CS5(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C4(VR1000_VA_IDESECAUX), PA_CS5(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, word */
  { VA_C5(VR1000_VA_DM9000),  PA_CS5(VR1000_PA_DM9000),	  SZ_1M,  MT_DEVICE },
  { VA_C5(VR1000_VA_IDEPRI),  PA_CS5(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C5(VR1000_VA_IDESEC),  PA_CS5(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C5(VR1000_VA_IDEPRIAUX), PA_CS5(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C5(VR1000_VA_IDESECAUX), PA_CS5(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

/* uart clock source(s) */

static struct s3c24xx_uart_clksrc vr1000_serial_clocks[] = {
	[0] = {
		.name		= "uclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	},
	[1] = {
		.name		= "pclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0.
	}
};

static struct s3c2410_uartcfg vr1000_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),

	}
};

static struct platform_device *vr1000_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct clk *vr1000_clocks[] = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c24xx_board vr1000_board __initdata = {
	.devices       = vr1000_devices,
	.devices_count = ARRAY_SIZE(vr1000_devices),
	.clocks	       = vr1000_clocks,
	.clocks_count  = ARRAY_SIZE(vr1000_clocks),
};


void __init vr1000_map_io(void)
{
	/* initialise clock sources */

	s3c24xx_dclk0.parent = NULL;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = NULL;
	s3c24xx_dclk1.rate   = 3692307;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c24xx_init_io(vr1000_iodesc, ARRAY_SIZE(vr1000_iodesc));
	s3c2410_init_uarts(vr1000_uartcfgs, ARRAY_SIZE(vr1000_uartcfgs));
	s3c24xx_set_board(&vr1000_board);
	usb_simtec_init();
}

void __init vr1000_init_irq(void)
{
	s3c2410_init_irq();
}

MACHINE_START(VR1000, "Thorcom-VR1000")
     MAINTAINER("Ben Dooks <ben@simtec.co.uk>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, S3C2410_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
     MAPIO(vr1000_map_io)
     INITIRQ(vr1000_init_irq)
     .timer		= &s3c2410_timer,
MACHINE_END
