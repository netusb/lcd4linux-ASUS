/* $Id: drv_LPH7508.c,v 1.2 2005/12/13 14:07:28 reinelt Exp $
 *
 * driver for Pollin LPH7508
 *
 * Copyright (C) 2005 Michael Reinelt <reinelt@eunet.at>
 * Copyright (C) 2005 The LCD4Linux Team <lcd4linux-devel@users.sourceforge.net>
 *
 * This file is part of LCD4Linux.
 *
 * LCD4Linux is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * LCD4Linux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * $Log: drv_LPH7508.c,v $
 * Revision 1.2  2005/12/13 14:07:28  reinelt
 * LPH7508 driver finished
 *
 * Revision 1.1  2005/11/04 14:10:38  reinelt
 * drv_Sample and drv_LPH7508
 *
 */

/* 
 *
 * exported fuctions:
 *
 * struct DRIVER drv_LPH7508
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>

#include "debug.h"
#include "cfg.h"
#include "qprintf.h"
#include "udelay.h"
#include "plugin.h"
#include "widget.h"
#include "widget_text.h"
#include "widget_icon.h"
#include "widget_bar.h"
#include "drv.h"
#include "drv_generic_graphic.h"
#include "drv_generic_parport.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static char Name[] = "LPH7508";


static unsigned char SIGNAL_RES;
static unsigned char SIGNAL_CS1;
static unsigned char SIGNAL_RW;
static unsigned char SIGNAL_A0;

static int PAGES, SROWS, SCOLS;

unsigned char *Buffer1, *Buffer2;


/****************************************/
/***  hardware dependant functions    ***/
/****************************************/

static void drv_L7_write_ctrl(const unsigned char data)
{
    /* put data on DB1..DB8 */
    drv_generic_parport_data(data);

    /* CS1 = high, RW = low, A0 = low */
    drv_generic_parport_control(SIGNAL_CS1 | SIGNAL_RW | SIGNAL_A0, SIGNAL_CS1);

    /* Address Setup Time = 10 ns */
    /* Data Setup Time = 20 ns */
    ndelay(20);

    /* Control L Pulse Width = 22 ns */
    drv_generic_parport_toggle(SIGNAL_CS1, 0, 22);

    /* Address & Data Hold Time = 10 ns */
    ndelay(10);
}


static void drv_L7_write_data(const unsigned char data)
{
    /* put data on DB1..DB8 */
    drv_generic_parport_data(data);

    /* CS1 = high, RW = low, A0 = high */
    drv_generic_parport_control(SIGNAL_CS1 | SIGNAL_RW | SIGNAL_A0, SIGNAL_CS1 | SIGNAL_A0);

    /* Address Setup Time = 10 ns */
    /* Data Setup Time = 20 ns */
    ndelay(20);

    /* Control L Pulse Width = 22 ns */
    drv_generic_parport_toggle(SIGNAL_CS1, 0, 22);

    /* Address & Data Hold Time = 10 ns */
    ndelay(10);
}


static void drv_L7_clear (void)
{
    int p, c;

    for (p= 0; p < PAGES; p++) {
	/* select page */
	drv_L7_write_ctrl(0xb0 | p);
	/* select column address */
	drv_L7_write_ctrl(0x00);
	drv_L7_write_ctrl(0x10);
	for (c = 0; c < SCOLS; c++) {
	    drv_L7_write_data(0);
	}
    }
}


static void drv_L7_blit(const int row, const int col, const int height, const int width)
{
    int r, p, p0;

    /* transfer layout to display framebuffer */
    for (r = row; r < row + height; r++) {
	if (r >= SROWS)
	    break;
	/* page */
	int p = r / 8;
	int c;
	for (c = col; c < col + width; c++) {
	    if (c >= SCOLS)
		break;
	    /* RAM address */
	    int a = p * SCOLS + c;
	    /* bit mask */
	    unsigned char m = 1 << (r % 8);
	    if (drv_generic_graphic_FB[r * LCOLS + c]) {
		/* set bit */
		Buffer1[a] |= m;
	    } else {
		/* clear bit */
		Buffer1[a] &= ~m;
	    }
	}
    }

    /* process display framebuffer */
    p0 = -1;
    for (p = row / 8; p <= (row + height) / 8; p++) {
	int i, j, a, e;
	if (p >= PAGES)
	    break;
	for (i = col; i < col+width; i++) {
	    a = p * SCOLS + i;
	    if (Buffer1[a] == Buffer2[a])
		continue;
	    for (j = i, e = 0; i < col+width; i++) {
		a = p * SCOLS + i;
		if (Buffer1[a] == Buffer2[a]) {
		    if (++e > 2)
			break;
		} else {
		    e = 0;
		}
	    }
	    /* change page if necessary */
	    if (p != p0) {
		p0 = p;
		drv_L7_write_ctrl(0xb0 | p);
	    }
	    /* column address */
	    /* first column address = 32 */
	    drv_L7_write_ctrl(0x00 | ((j + 32) & 0x0f));
	    drv_L7_write_ctrl(0x10 | ((j + 32) >> 4));
	    /* data */
	    for (j = j; j <= i - e; j++) {
		a = p * SCOLS + j;
		drv_L7_write_data(Buffer1[a]);
		Buffer2[a] = Buffer1[a];
	    }
	}
    }
}


static int drv_L7_contrast(int contrast)
{
    if (contrast < 0)
	contrast = 0;
    if (contrast > 31)
	contrast = 31;

    drv_L7_write_ctrl(0x80 | contrast);

    return contrast;
}


static int drv_L7_start(const char *section)
{
    char *s;
    int contrast;

    /* fixed size */
    DROWS = 64;
    DCOLS = 100;

    /* SED1560 display RAM layout */
    PAGES = 8;
    SROWS = PAGES * 8;
    SCOLS = 166;

    s = cfg_get(section, "Font", "6x8");
    if (s == NULL || *s == '\0') {
	error("%s: no '%s.Font' entry from %s", Name, section, cfg_source());
	return -1;
    }

    XRES = -1;
    YRES = -1;
    if (sscanf(s, "%dx%d", &XRES, &YRES) != 2 || XRES < 1 || YRES < 1) {
	error("%s: bad Font '%s' from %s", Name, s, cfg_source());
	return -1;
    }

    /* Fixme: provider other fonts someday... */
    if (XRES != 6 && YRES != 8) {
	error("%s: bad Font '%s' from %s (only 6x8 at the moment)", Name, s, cfg_source());
	return -1;
    }

    /* provide room for page 8 (symbols) */
    Buffer1 = malloc((PAGES + 1) * SCOLS);
    if (Buffer1 == NULL) {
	error("%s: framebuffer #1 could not be allocated: malloc() failed", Name);
	return -1;
    }

    Buffer2 = malloc((PAGES + 1) * SCOLS);
    if (Buffer2 == NULL) {
	error("%s: framebuffer #2 could not be allocated: malloc() failed", Name);
	return -1;
    }

    memset(Buffer1, 0, (PAGES + 1) * SCOLS * sizeof(*Buffer1));
    memset(Buffer2, 0, (PAGES + 1) * SCOLS * sizeof(*Buffer2));

    if (drv_generic_parport_open(section, Name) != 0) {
	error("%s: could not initialize parallel port!", Name);
	return -1;
    }

    if ((SIGNAL_RES = drv_generic_parport_hardwire_ctrl("RES", "INIT")) == 0xff)
	return -1;
    if ((SIGNAL_CS1 = drv_generic_parport_hardwire_ctrl("CS1", "STROBE")) == 0xff)
	return -1;
    if ((SIGNAL_RW = drv_generic_parport_hardwire_ctrl("RW", "SLCTIN")) == 0xff)
	return -1;
    if ((SIGNAL_A0 = drv_generic_parport_hardwire_ctrl("A0", "AUTOFD")) == 0xff)
	return -1;

    /* rise RES, CS1, RW and A0 */
    drv_generic_parport_control(SIGNAL_RES | SIGNAL_CS1 | SIGNAL_RW | SIGNAL_A0, SIGNAL_RES | SIGNAL_CS1 | SIGNAL_RW | SIGNAL_A0);

    /* set direction: write */
    drv_generic_parport_direction(0);

    /* reset display: lower RESET for 1 usec */
    drv_generic_parport_control(SIGNAL_RES, 0);
    udelay(1);
    drv_generic_parport_control(SIGNAL_RES, SIGNAL_RES);
    udelay(100);

    /* just to make sure: send a software reset */
    drv_L7_write_ctrl(0xe2);
    udelay(20000);

    drv_L7_write_ctrl(0xAE);	/* Display off */
    drv_L7_write_ctrl(0x40);	/* Start Display Line = 0 */
    drv_L7_write_ctrl(0x20);	/* reverse line driving off */
    drv_L7_write_ctrl(0xCC);	/* OutputStatus = $0C, 102x64 */
    drv_L7_write_ctrl(0xA0);	/* ADC = normal */
    drv_L7_write_ctrl(0xA9);	/* LCD-Duty = 1/64 */
    drv_L7_write_ctrl(0xAB);	/* LCD-Duty +1 (1/65, symbols) */
    drv_L7_write_ctrl(0x25);	/* power supply on */
    udelay(100 * 1000);		/* wait 100 msec */
    drv_L7_write_ctrl(0xED);	/* power supply on completion */
    drv_L7_write_ctrl(0x8F);	/* Contrast medium */
    drv_L7_write_ctrl(0xA4);	/* Display Test off */
    drv_L7_write_ctrl(0xAF);	/* Display on */
    drv_L7_write_ctrl(0xA6);	/* Display on */

    /* clear display */
    drv_L7_clear();

    if (cfg_number(section, "Contrast", 15, 0, 31, &contrast) > 0) {
	drv_L7_contrast(contrast);
    }

    return 0;
}


/****************************************/
/***            plugins               ***/
/****************************************/


static void plugin_contrast(RESULT * result, RESULT * arg1)
{
    double contrast;

    contrast = drv_L7_contrast(R2N(arg1));
    SetResult(&result, R_NUMBER, &contrast);
}


/****************************************/
/***        widget callbacks          ***/
/****************************************/


/* using drv_generic_graphic_draw(W) */
/* using drv_generic_graphic_icon_draw(W) */
/* using drv_generic_graphic_bar_draw(W) */


/****************************************/
/***        exported functions        ***/
/****************************************/


/* list models */
int drv_L7_list(void)
{
    printf("LPH7508");
    return 0;
}


/* initialize driver & display */
int drv_L7_init(const char *section, const int quiet)
{
    WIDGET_CLASS wc;
    int ret;

    /* real worker functions */
    drv_generic_graphic_real_blit = drv_L7_blit;

    /* start display */
    if ((ret = drv_L7_start(section)) != 0)
	return ret;

    /* initialize generic graphic driver */
    if ((ret = drv_generic_graphic_init(section, Name)) != 0)
	return ret;

    if (!quiet) {
	char buffer[40];
	qprintf(buffer, sizeof(buffer), "%s %dx%d", Name, DCOLS, DROWS);
	if (drv_generic_graphic_greet(buffer, NULL)) {
	    sleep(3);
	    drv_generic_graphic_clear();
	}
    }

    /* register text widget */
    wc = Widget_Text;
    wc.draw = drv_generic_graphic_draw;
    widget_register(&wc);

    /* register icon widget */
    wc = Widget_Icon;
    wc.draw = drv_generic_graphic_icon_draw;
    widget_register(&wc);

    /* register bar widget */
    wc = Widget_Bar;
    wc.draw = drv_generic_graphic_bar_draw;
    widget_register(&wc);

    /* register plugins */
    AddFunction("LCD::contrast", 1, plugin_contrast);


    return 0;
}


/* close driver & display */
int drv_L7_quit(const int quiet)
{

    info("%s: shutting down.", Name);

    drv_generic_graphic_clear();

    if (!quiet) {
	drv_generic_graphic_greet("goodbye!", NULL);
    }

    drv_generic_graphic_quit();
    drv_generic_parport_close();

    if (Buffer1) {
	free(Buffer1);
	Buffer1 = NULL;
    }

    if (Buffer2) {
	free(Buffer2);
	Buffer2 = NULL;
    }

    return (0);
}


DRIVER drv_LPH7508 = {
  name:Name,
  list:drv_L7_list,
  init:drv_L7_init,
  quit:drv_L7_quit,
};