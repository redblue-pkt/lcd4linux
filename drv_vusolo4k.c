/*
 * Copyright (C) 2018 redblue
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
 */

/*
 *
 * exported fuctions:
 *
 * struct DRIVER drv_vusolo4k
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <byteswap.h>

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

#define LCD_XRES "/proc/stb/lcd/xres"
#define LCD_YRES "/proc/stb/lcd/yres"
#define LCD_BPP "/proc/stb/lcd/bpp"

#ifndef LCD_IOCTL_ASC_MODE
#define LCDSET                                  0x1000
#define LCD_IOCTL_ASC_MODE              (21|LCDSET)
#define LCD_MODE_ASC                    0
#define LCD_MODE_BIN                    1
#endif

typedef enum { false = 0, true = !false } bool;

static char Name[] = "vusolo4k";

/* Display data */
static int fd = -1, bpp = 0, xres = 0, yres = 0, backlight = 0;
static unsigned char * newLCD = NULL, * oldLCD = NULL;

static int lcd_read_value(const char *filename)
{
	int value = 0;
	FILE *_fd = fopen(filename, "r");
	if (_fd) {
		int tmp;
		if (fscanf(fd, "%x", &tmp) == 1)
			value = tmp;
		fclose(_fd);
	}
	return value;
}

static int vusolo4k_open(const char *dev)
{
	bpp = 32;
	xres = 480;
	yres = 320;
	fd = open(dev, O_RDWR);
	if (fd == -1) {
		printf("cannot open lcd device\n");
		return -1;
	}

	int tmp = LCD_MODE_BIN;
	if (ioctl(fd, LCD_IOCTL_ASC_MODE, &tmp)) {
		printf("failed to set lcd bin mode\n");
	}

	return 0;
}

static int vusolo4k_close()
{
	if (newLCD)
	{
		free(newLCD);
		newLCD = 0;
	}
	if (oldLCD)
	{
		free(oldLCD);
		oldLCD = 0;
	}
	if (-1 != fd)
	{
		close(fd);
		fd=-1;
	}
	return 0;
}

static int drv_vusolo4k_open(const char *section)
{
	char *dev;

	dev = cfg_get(section, "Port", NULL);
	if (dev == NULL || *dev == '\0')
	{
		error("%s: no '%s.Port' entry from %s", Name, section, cfg_source());
		return -1;
	}

	int h = vusolo4k_open(dev);
	if (h == -1)
	{
		error("%s: cannot open vusolo4k device %s", Name, dev);
		return -1;
	}

	return 0;
}

static int drv_vusolo4k_close(void)
{
	vusolo4k_close();
	return 0;
}

static void drv_vusolo4k_set_pixel(int x, int y, RGBA pix)
{
	long int location;

	location = (x * xres + y) * 4;
	pix = drv_generic_graphic_rgb(x, y);
	*(newLCD + location + 0) = pix.B;
	*(newLCD + location + 1) = pix.G;
	*(newLCD + location + 2) = pix.R;
	*(newLCD + location + 3) = 0xff;
}

static void drv_vusolo4k_blit(const int row, const int col, const int height, const int width)
{
	bool refreshAll = false;
	int r, c;

	for (r = row; r < row + height; r++)
	{
		for (c = col; c < col + width; c++)
		{
			drv_vusolo4k_set_pixel(r, c, drv_generic_graphic_rgb(r, c));
		}
	}
	for (r = row; r < row + height; r++)
	{
		for (c = col; c < col + width; c++)
		{
			if (newLCD != oldLCD)
			{
				refreshAll = true;
				break;
			}
		}
	}
	if (refreshAll)
	{
		for (r = row; r < row + height; r++)
		{
			for (c = col; c < col + width; c++)
			{
				memcpy(oldLCD, newLCD, sizeof(newLCD)+1);
			}
		}
		int stride = xres * 4;
		write(fd, newLCD + stride, stride * yres);
	}
}

static int drv_vusolo4k_backlight(int number)
{
	int value = 0;
	switch(number) {
		case 0:
			value = 0;
			break;
		case 1:
			value = 25;
			break;
		case 2:
			value = 51;
			break;
		case 3:
			value = 76;
			break;
		case 4:
			value = 102;
			break;
		case 5:
			value = 127;
			break;
		case 6:
			value = 153;
			break;
		case 7:
			value = 178;
			break;
		case 8:
			value = 204;
			break;
		case 9:
			value = 229;
			break;
		case 10:
			value = 255;
			break;
	}

	FILE *f = fopen("/proc/stb/lcd/oled_brightness", "w");
	if (!f)
		f = fopen("/proc/stb/fp/oled_brightness", "w");
	if (f)
	{
		if (fprintf(f, "%d", value) == 0)
			printf("write /proc/stb/lcd/oled_brightness failed!! (%m)\n");
		fclose(f);
	}
	return 0;
}

/* start graphic display */
static int drv_vusolo4k_start(const char *section)
{
	int i;
	char *s;

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

	if (XRES < 6 || YRES < 8)
	{
		error("%s: bad Font '%s' from %s (must be at least 6x8)", Name, s, cfg_source());
		return -1;
	}
	free(s);

	// Get the backlight value (0 = off, 10 = max brightness)
	if (cfg_number(section, "Backlight", 0, 0, 10, &i) > 0)
		backlight = i;
	else
		backlight = 10;

	/* open communication with the display */
	if (drv_vusolo4k_open(section) < 0)
	{
		return -1;
	}

	/* you surely want to allocate a framebuffer or something... */
	newLCD = (unsigned char *)malloc(xres * yres * 4);
	if (newLCD)
		memset(newLCD, 0, xres * yres * 4);

	if (newLCD == NULL) {
		error("%s: newLCD buffer could not be allocated: malloc() failed", Name);
		return -1;
	}
	oldLCD = (unsigned char *)malloc(xres * yres * 4);
	if (oldLCD)
		memset(oldLCD, 0, xres * yres * 4);

	if (oldLCD == NULL) {
		error("%s: oldLCD buffer could not be allocated: malloc() failed", Name);
		return -1;
	}

	drv_vusolo4k_backlight(backlight);

	/* set width/height from vusolo4k firmware specs */
	DROWS = yres;
	DCOLS = xres;

	return 0;
}

/****************************************/
/***            plugins               ***/
/****************************************/

static void plugin_backlight(RESULT * result, RESULT * arg1)
{
	int bl_on;
	bl_on = (R2N(arg1) == 0 ? 0 : 1);
	drv_vusolo4k_backlight(bl_on);
	SetResult(&result, R_NUMBER, &bl_on);
}


/****************************************/
/***        widget callbacks          ***/
/****************************************/


/* using drv_generic_text_draw(W) */
/* using drv_generic_text_icon_draw(W) */
/* using drv_generic_text_bar_draw(W) */
/* using drv_generic_gpio_draw(W) */


/****************************************/
/***        exported functions        ***/
/****************************************/


/* list models */
int drv_vusolo4k_list(void)
{
	printf("vusolo4k OLED driver");
	return 0;
}

/* initialize driver & display */
int drv_vusolo4k_init(const char *section, const int quiet)
{
	int ret;

	/* real worker functions */
	drv_generic_graphic_real_blit = drv_vusolo4k_blit;

	/* start display */
	if ((ret = drv_vusolo4k_start(section)) != 0)
		return ret;

	/* initialize generic graphic driver */
	if ((ret = drv_generic_graphic_init(section, Name)) != 0)
		return ret;

	drv_generic_graphic_clear();

	if (!quiet)
	{
		char _buffer[40];
		qprintf(_buffer, sizeof(_buffer), "%s %dx%d", Name, DCOLS, DROWS);
		if (drv_generic_graphic_greet(_buffer, NULL))
		{
			sleep(3);
			drv_generic_graphic_clear();
		}
	}

	/* register plugins */
	AddFunction("LCD::backlight", 1, plugin_backlight);

	return 0;
}


/* close driver & display */
int drv_vusolo4k_quit(const int quiet)
{
	info("%s: shutting down.", Name);

	/* clear display */
	drv_generic_graphic_clear();

	//read goodby message from /tmp/lcd/goodbye
	char line1[80], value[80];
	FILE *fp;
	int i, size;

	fp = fopen("/tmp/lcd/goodbye", "r");
	if (!fp) {
		debug("couldn't open file '/tmp/lcd/goodbye'");
		line1[0] = '\0';
		if (!quiet) {
			drv_generic_graphic_greet("goodbye!", NULL);
		}
	} else {
		i = 0;
		while (!feof(fp) && i++ < 1) {
			fgets(value, sizeof(value), fp);
			size = strcspn(value, "\r\n");
			strncpy(line1, value, size);
			line1[size] = '\0';
			/* more than 80 chars, chew up rest of line */
			while (!feof(fp) && strchr(value, '\n') == NULL) {
				fgets(value, sizeof(value), fp);
			}
		}
		fclose(fp);
		if (i <= 1) {
			debug("'/tmp/lcd/goodbye' seems empty");
			line1[0] = '\0';
		}
		/* remove the file */
		debug("removing '/tmp/lcd/goodbye'");
		unlink("/tmp/lcd/goodbye");

		drv_generic_graphic_greet(NULL, line1);
	}

	drv_generic_graphic_quit();

	debug("closing connection");
	drv_vusolo4k_close();

	return (0);
}


DRIVER drv_vusolo4k = {
    .name = Name,
    .list = drv_vusolo4k_list,
    .init = drv_vusolo4k_init,
    .quit = drv_vusolo4k_quit,
};
