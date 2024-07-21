/* nutdrv_qx_njoy.c - Subdriver for nJoy Qx protocol based UPSes
 *
 * Copyright (C)
 *   2013 Daniele Pezzini <hyouko@gmail.com>
 *   2024 Bohdan <chbgdn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "main.h"
#include "nutdrv_qx.h"
#include "nutdrv_qx_blazer-common.h"

#include "nutdrv_qx_njoy.h"

#define NJOY_VERSION "nJoy 0.01"

/* qx2nut lookup table */
static item_t	njoy_qx2nut[] = {

	/*
	 * > [Q1\r]
	 * < [(226.0 195.0 226.0 014 49.0 27.5 30.0 00001000\r]
	 *    01234567890123456789012345678901234567890123456
	 *    0         1         2         3         4
	 */

	{ "input.voltage",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	1,	5,	"%.1f",	0,	NULL,	NULL,	NULL },
	{ "input.voltage.fault",	0,	NULL,	"Q1\r",	"",	47,	'(',	"",	7,	11,	"%.1f",	0,	NULL,	NULL,	NULL },
	{ "output.voltage",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	13,	17,	"%.1f",	0,	NULL,	NULL,	NULL },
	{ "ups.load",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	19,	21,	"%.0f",	0,	NULL,	NULL,	NULL },
	{ "input.frequency",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	23,	26,	"%.1f",	0,	NULL,	NULL,	NULL },
	{ "battery.voltage",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	28,	31,	"%.2f",	0,	NULL,	NULL,	qx_multiply_battvolt },
	{ "ups.temperature",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	33,	36,	"%.1f",	0,	NULL,	NULL,	NULL },
	/* Status bits */
	{ "ups.status",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	38,	38,	NULL,	QX_FLAG_QUICK_POLL,	NULL,	NULL,	blazer_process_status_bits },	/* Utility Fail (Immediate) */
	{ "ups.status",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	39,	39,	NULL,	QX_FLAG_QUICK_POLL,	NULL,	NULL,	blazer_process_status_bits },	/* Battery Low */
	{ "ups.status",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	40,	40,	NULL,	QX_FLAG_QUICK_POLL,	NULL,	NULL,	blazer_process_status_bits },	/* Bypass/Boost or Buck Active */
	{ "ups.alarm",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	41,	41,	NULL,	0,			NULL,	NULL,	blazer_process_status_bits },	/* UPS Failed */
	{ "ups.type",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	42,	42,	"%s",	QX_FLAG_STATIC,		NULL,	NULL,	blazer_process_status_bits },	/* UPS Type */
	{ "ups.status",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	43,	43,	NULL,	QX_FLAG_QUICK_POLL,	NULL,	NULL,	blazer_process_status_bits },	/* Test in Progress */
	{ "ups.status",			0,	NULL,	"Q1\r",	"",	47,	'(',	"",	44,	44,	NULL,	QX_FLAG_QUICK_POLL,	NULL,	NULL,	blazer_process_status_bits },	/* Shutdown Active */
	{ "ups.beeper.status",		0,	NULL,	"Q1\r",	"",	47,	'(',	"",	45,	45,	"%s",	0,			NULL,	NULL,	blazer_process_status_bits },	/* Beeper status */

	/* Query UPS for ratings
	 * > [F\r]
	 * < [#230.0 2.6 12.0 50.0\r]
	 *    012345678901234567890
	 *    0         1         2
	 */

	{ "input.voltage.nominal",		0,	NULL,	"F\r",	"",	21,	'#',	"",	1,	5,	"%.1f",	QX_FLAG_STATIC,	NULL,	NULL,	NULL },
	{ "input.current.nominal",		0,	NULL,	"F\r",	"",	21,	'#',	"",	7,	9,	"%.1f",	QX_FLAG_STATIC,	NULL,	NULL,	NULL },
	{ "battery.voltage.nominal",	0,	NULL,	"F\r",	"",	21,	'#',	"",	11,	14,	"%.1f",	QX_FLAG_STATIC,	NULL,	NULL,	NULL },
	{ "input.frequency.nominal",	0,	NULL,	"F\r",	"",	21,	'#',	"",	16,	19,	"%.1f",	QX_FLAG_STATIC,	NULL,	NULL,	NULL },


	/*
	 * > [B\r]
	 * < [#I000.0O229.7L000B100V13.2F00.0H50.0R060S...]
	 *    01234567890123456789012345678901234567890123
	 *    0         1         2         3         4
	 */

	{ "battery.charge",		0,	NULL,	"B\r",	"",	40,	'#',	"",	18,	20,	"%.0f",	0,	NULL,	NULL,	NULL },

	/* Instant commands */
	{ "beeper.enable",			0,	NULL,	"K60:1\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "beeper.disable",			0,	NULL,	"K60:0\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "load.off",				0,	NULL,	"M00E65535\r",	"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },	/* Workaround to avoid Auto-Restart after mains power returns */
	{ "load.on",				0,	NULL,	"K\r",			"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "shutdown.return",		0,	NULL,	"M%s\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	blazer_process_command },
	{ "shutdown.stayoff",		0,	NULL,	"M%sE65535\r",	"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	blazer_process_command },	/* Workaround to avoid Auto-Restart after mains power returns */
	{ "shutdown.stop",				0,	NULL,	"K\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "test.battery.start.deep",	0,	NULL,	"AH\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "test.battery.start.quick",	0,	NULL,	"A\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },
	{ "test.battery.stop",			0,	NULL,	"KA\r",		"",	3,	0,	"",	0,	0,	NULL,	QX_FLAG_CMD,	NULL,	NULL,	NULL },

	/* Server-side settable vars */
	{ "ups.delay.start",		ST_FLAG_RW,	blazer_r_ondelay,	NULL,	"",	0,	0,	"",	0,	0,	DEFAULT_ONDELAY,	QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE,	NULL,	NULL,	blazer_process_setvar },
	{ "ups.delay.shutdown",		ST_FLAG_RW,	blazer_r_offdelay,	NULL,	"",	0,	0,	"",	0,	0,	DEFAULT_OFFDELAY,	QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE,	NULL,	NULL,	blazer_process_setvar },

	/* End of structure. */
	{ NULL,				0,	NULL,	NULL,		"",	0,	0,	"",	0,	0,	NULL,	0,	NULL,	NULL,	NULL }
};

/* Testing table */
#ifdef TESTING
static testing_t	njoy_testing[] = {
	{ "Q1\r",	"(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r",	-1 },
	{ "F\r",	"#230.0 2.6 12.0 50.0\r",	-1 },
	{ "M03\r",	"#0\r",	-1 },
	{ "K\r",	"#0\r",	-1 },
	{ "M02E0005\r",	"#0\r",	-1 },
	{ "M.5\r",	"#0\r",	-1 },
	{ "AH\r",	"#0\r",	-1 },
	{ "A\r",	"#0\r",	-1 },
	{ "KA\r",	"#0\r",	-1 },
	{ NULL }
};
#endif	/* TESTING */

/* This function allows the subdriver to "claim" a device: return 1 if the device is supported by this subdriver, else 0. */
static int	njoy_claim(void)
{
	/* We need at least B and Q1 to run this subdriver */

	/* UPS Protocol */
	item_t	*item = find_nut_info("battery.charge", 0, 0);

	/* Don't know what happened */
	if (!item)
		return 0;

	/* No reply/Unable to get value */
	if (qx_process(item, NULL))
		return 0;

	/* Unable to process value/Protocol not supported */
	if (ups_infoval_set(item) != 1)
		return 0;

	item = find_nut_info("input.voltage", 0, 0);

	/* Don't know what happened */
	if (!item) {
		dstate_delinfo("battery.charge");
		return 0;
	}

	/* No reply/Unable to get value */
	if (qx_process(item, NULL)) {
		dstate_delinfo("battery.charge");
		return 0;
	}

	/* Unable to process value */
	if (ups_infoval_set(item) != 1) {
		dstate_delinfo("battery.charge");
		return 0;
	}

	return 1;
}

/* Subdriver-specific initups */
static void	njoy_initups(void)
{
	blazer_initups_light(njoy_qx2nut);
}

/* Subdriver interface */
subdriver_t	njoy_subdriver = {
	NJOY_VERSION,
	njoy_claim,
	njoy_qx2nut,
	njoy_initups,
	NULL,
	blazer_makevartable_light,
	"#0\r",
	NULL,
#ifdef TESTING
	njoy_testing,
#endif	/* TESTING */
};
