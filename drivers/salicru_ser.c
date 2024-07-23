/*
 * salicru_ser.c - driver for serial Salicru devices
 *
 * Copyright (C)
 *	2024	Bohdan <chbgdn@gmail.com>
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
 */

#include "main.h"
#include "serial.h"
#include "nut_stdint.h"

//#include "salicru.h"

#include <ctype.h>

#define ENDCHAR		'\r'
#define IGNCHAR		""
#define MAXTRIES	3
#define UPSDELAY	50000

#define SER_WAIT_SEC	0
#define SER_WAIT_USEC	250000

#define DRIVER_NAME	"Salicru serial protocol UPS driver"
#define DRIVER_VERSION	"0.01"

/* driver description structure */
upsdrv_info_t upsdrv_info = {
	DRIVER_NAME,
	DRIVER_VERSION,
	"Bohdan <chbgdn@gmail.com>\n",
	DRV_EXPERIMENTAL,
	{ NULL }
};

typedef struct {
	float          utility_voltage;
	float          output_voltage;
	int            percent_load;
	int            battery_capacity;
	float          battery_voltage;
	int            cabinet_temperature;
	float          utility_frequency;
	float          output_frequency;
	int            remaing_runtime;
//	int            remaining_chargetime;
	float          output_current;
	unsigned char  flags[6];
} status_t;

static long	ondelay = 60;	/* seconds */
static long	offdelay = 60;	/* seconds */

static char	salicru_answer[SMALLBUF];
/*
static struct {
	const char	*var;
	const char	*get;
	const char	*set;
} vartab[] = {
	{ "input.transfer.high", "P6\r", "C2:%03d\r" },
	{ "input.transfer.low", "P7\r", "C3:%03d\r" },
	{ "battery.charge.low", "P8\r", "C4:%02d\r" },
	{ NULL, NULL, NULL }
};
*/
static struct {
	const char	*cmd;
	const char	*command;
} cmdtab[] = {
	{ "test.battery.start.quick", "A\r" },
	{ "test.battery.start.deep", "AH\r" },
	{ "test.battery.stop", "KA\r" },
	{ "beeper.enable", "K60:1\r" },
	{ "beeper.disable", "K60:0\r" },
	{ "beeper.mute", "K60:2\r" },
	{ "shutdown.stop", "K\r" },
	{ NULL, NULL }
};

static ssize_t salicru_command(const char *command)
{
	ssize_t	ret;

	ser_flush_io(upsfd);

	ret = ser_send_pace(upsfd, UPSDELAY, "%s", command);

	if (ret < 0) {
		upsdebug_with_errno(3, "send");
		return -1;
	}

	if (ret == 0) {
		upsdebug_with_errno(3, "send: timeout");
		return -1;
	}

	upsdebug_hex(3, "send", command, strlen(command));

	usleep(100000);

	ret = ser_get_line(upsfd, salicru_answer, sizeof(salicru_answer),
					   ENDCHAR, IGNCHAR, SER_WAIT_SEC, SER_WAIT_USEC);

	if (ret < 0) {
		upsdebug_with_errno(3, "read");
		upsdebug_hex(4, "  \\_", salicru_answer, strlen(salicru_answer));
		return -1;
	}

	if (ret == 0) {
		upsdebugx(3, "read: timeout");
		upsdebug_hex(4, "  \\_", salicru_answer, strlen(salicru_answer));
		return -1;
	}

	upsdebug_hex(3, "read", salicru_answer, (size_t)ret);
	return ret;
}

static ssize_t salicru_status(status_t *status)
{
	ssize_t	ret;
	ssize_t i;
	ssize_t valid = 0;
	int code = -1;
	char value[32];
	ssize_t ofs = 0;
	char seen_i_freq = 0;

//	memset(status, 0, sizeof(status_t));
	init_status(status);

	/*
	 * WRITE B\r
	 * READ #I119.0O119.0L000B100T027F060.0S..\r
	 *      #I118.0O118.0L029B100F060.0R0218S..\r
	 *      #I118.1O118.1L13B100V27.5F60.0HF60.0R65Q1.4S\x80\x84\xc0\x88\x80W\r		(CST135XLU)
	 *      01234567890123456789012345678901234567890123
	 *      0         1         2         3         4
	 */
	ret = salicru_command("B\r");
	if (ret <= 0)
		return -1;

	if (salicru_answer[0] != '#') {
		upsdebugx(2, "Expected start character '#', but got '%c'", salicru_answer[0]);
		return -1;
	}

	for (i = 1; i <= ret; i++) {
		if (i == ret || isalpha(salicru_answer[i])) {
			value[ofs++] = '\0';
			valid++;

			switch (code) {
				case 'I':
					status->utility_voltage = strtof(value, NULL);
					break;
				case 'O':
					status->output_voltage = strtof(value, NULL);
					break;
				case 'L':
					status->percent_load = strtol(value, NULL, 10);
					break;
				case 'B':
					status->battery_capacity = strtol(value, NULL, 10);
					break;
				case 'V':
					status->battery_voltage = strtof(value, NULL);
					break;
				case 'T':
					status->cabinet_temperature = strtol(value, NULL, 10);
					break;
				case 'F':
					status->utility_frequency = strtof(value, NULL);
					seen_i_freq = 1;
					break;
				case 'H':
					status->output_frequency = strtof(value, NULL);
					break;
				case 'R':
					status->remaing_runtime = strtol(value, NULL, 10);
					break;
//				case 'C':
//					status->remaining_chargetime = strtol(value, NULL, 10);
//					break;
				case 'Q':
					status->output_current = strtof(value, NULL);
					break;
				case 'S':
					memcpy(&status->flags, value, strlen(value));
					break;
				default:
					/* We didn't really find valid data */
					valid--;
					break;
			}

			code = salicru_answer[i];
			ofs = 0;

			/*
			 * Depending on device/firmware, the output frequency is coded as
			 * either an H, HF, or a second F (seen once transfered to battery)
			 */
			if (seen_i_freq && code == 'F')
				code = 'H';

			continue;
		}

		value[ofs++] = salicru_answer[i];
	}

	/* if we didn't get at least 3 values consider it a failure */
	if (valid < 3) {
		upsdebugx(4, "Parsing status string failed");
		return -1;
	}

	return 0;
}

void upsdrv_initinfo(void)
{
	int	i;
	char	*s;

	dstate_setinfo("ups.delay.start", "%ld", ondelay);
	dstate_setinfo("ups.delay.shutdown", "%ld", offdelay);

	/*
	 * WRITE X41\r
	 * READ #BC1200     ,1.600,000000000000,CYBER POWER
	 *      #CST135XLU,BF01403AAH2,CR7EV2002320,CyberPower Systems Inc.,,,
	 *      01234567890123456789012345678901234567890123456
	 *      0         1         2         3         4
	 */
	if (salicru_command("X41\r") > 0) {
		if (salicru_answer[0] != '#') {
			upsdebugx(2, "Expected start character '#', but got '%c'", salicru_answer[0]);
			return -1;
		}
		if ((s = strtok(&salicru_answer[1], ",")) != NULL) {
			dstate_setinfo("ups.model", "%s", str_rtrim(s, ' '));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("ups.firmware", "%s", str_rtrim(s, ' '));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("ups.serial", "%s", str_rtrim(s, ' '));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("ups.mfr", "%s", str_rtrim(s, ' '));
		}
	}

	/*
	 * Allow to override the following parameters
	 */
	if ((s = getval("manufacturer")) != NULL) {
		dstate_setinfo("ups.mfr", "%s", s);
	}
	if ((s = getval("model")) != NULL) {
		dstate_setinfo("ups.model", "%s", s);
	}
	if ((s = getval("serial")) != NULL) {
		dstate_setinfo("ups.serial", "%s", s);
	}

	/*
	 * WRITE X5\r
	 * READ #12,1,5,0,5,480\r
	 */
	if (salicru_command("X5\r") > 0) {

		if ((s = strtok(&salicru_answer[1], ",")) != NULL) {
			dstate_setinfo("battery.voltage.nominal", "%g", strtod(s, NULL));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("battery.packs", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("battery.capacity.nominal", "%g", strtod(s, NULL));
		}
	}

	/*
	 * WRITE X72\r
	 * READ #600,360,230,40.0,70.0,2.6\r
	 */
	if (salicru_command("X72\r") > 0) {

		if ((s = strtok(&salicru_answer[1], ",")) != NULL) {
			dstate_setinfo("ups.power.nominal", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("ups.realpower.nominal", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.voltage.nominal", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.frequency.low", "%g", strtod(s, NULL));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.frequency.high", "%g", strtod(s, NULL));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.current.nominal", "%g", strtod(s, NULL));
		}
	}

	/*
	 * WRITE X27\r
	 * READ #230,280,145,10,300\r
	 */
	if (salicru_command("X27\r") > 0) {

		if ((s = strtok(&salicru_answer[1], ",")) != NULL) {
			dstate_setinfo("output.voltage.nominal", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.transfer.high", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("input.transfer.low", "%li", strtol(s, NULL, 10));
		}
		if ((s = strtok(NULL, ",")) != NULL) {
			dstate_setinfo("battery.charge.low", "%li", strtol(s, NULL, 10));
		}
	}

	for (i = 0; cmdtab[i].cmd != NULL; i++) {
		dstate_addcmd(cmdtab[i].cmd);
	}
/*
	for (i = 0; vartab[i].var != NULL; i++) {

		if (!dstate_getinfo(vartab[i].var)) {
			continue;
		}

		if (salicru_command(vartab[i].get) < 1) {
			continue;
		}

		if ((s = strtok(&salicru_answer[1], ",")) != NULL) {
			dstate_setflags(vartab[i].var, ST_FLAG_RW);
			dstate_addenum(vartab[i].var, "%li", strtol(s, NULL, 10));
		}

		while ((s = strtok(NULL, ",")) != NULL) {
			dstate_addenum(vartab[i].var, "%li", strtol(s, NULL, 10));
		}
	}
*/
	/*
	 * Cancel pending shutdown.
	 * WRITE K\r
	 * READ #0\r
	 */
	salicru_command("K\r");

	dstate_addcmd("shutdown.return");
	dstate_addcmd("shutdown.stayoff");
	dstate_addcmd("shutdown.reboot");
}

void init_status(status_t *status)
{
	status->utility_voltage = -1;
	status->output_voltage = -1;
	status->percent_load = -1;
	status->battery_capacity = -1;
	status->battery_voltage = -1;
	status->cabinet_temperature = -1;
	status->utility_frequency = -1;
	status->output_frequency = -1;
	status->remaing_runtime = -1;
//	status->remaining_chargetime = -1;
	status->output_current = -1;
	status->flags[0] = 0;
	status->flags[1] = 0;
	status->flags[2] = 0;
	status->flags[3] = 0;
	status->flags[4] = 0;
	status->flags[5] = 0;
}

void upsdrv_updateinfo(void)
{
	status_t	status;

	if (salicru_status(&status)) {
		return -1;
	}

	if(status.utility_voltage >= 0) dstate_setinfo("input.voltage", "%.1f", status.utility_voltage);
	if(status.output_voltage >= 0) dstate_setinfo("output.voltage", "%.1f", status.output_voltage);
	if(status.percent_load >= 0) dstate_setinfo("ups.load", "%d", status.percent_load);
	if(status.battery_capacity >= 0) dstate_setinfo("battery.charge", "%d", status.battery_capacity);
	if(status.battery_voltage >= 0) dstate_setinfo("battery.voltage", "%.1f", status.battery_voltage);
	if(status.cabinet_temperature >= 0) dstate_setinfo("ups.temperature", "%d", status.cabinet_temperature);
	if(status.utility_frequency >= 0) dstate_setinfo("input.frequency", "%.1f", status.utility_frequency);
	if(status.output_frequency >= 0) dstate_setinfo("output.frequency", "%.1f", status.output_frequency);
	if(status.remaing_runtime >= 0) dstate_setinfo("battery.runtime", "%d", status.remaing_runtime*60);
//	if(status.remaining_chargetime >= 0) dstate_setinfo("battery.chargetime", "%d", status.remaining_chargetime);
	if(status.output_current >= 0) dstate_setinfo("output.current", "%.1f", status.output_current);

	status_init();

	if (status.flags[0] >= 128) {
		if (status.flags[0] & 0x40) {
			status_set("OB");
		} else {
			status_set("OL");
		}

		if (status.flags[0] & 0x20) {
			status_set("LB");
		}

		if (status.flags[0] & 0x08) {
			status_set("CAL");
		}

		if (status.flags[0] == 0) {
			status_set("OFF");
		}
	}

	if (status.flags[1] >= 128) {
		if (status.flags[1] & 0x60) {
			status_set("TRIM");
		}

		if (status.flags[1] & 0x40 || status.flags[1] & 0x20) {
			status_set("BOOST");
		}
	}

	if (status.flags[2] >= 128) {
		if (status.flags[2] & 0x08) {
			dstate_setinfo("battery.charger.status", "discharging");
		} else if (status.flags[2] & 0x50) {
			dstate_setinfo("battery.charger.status", "floating");
		} else if (status.flags[2] & 0x40) {
			dstate_setinfo("battery.charger.status", "resting");
		} else if (status.flags[2] & 0x10) {
			dstate_setinfo("battery.charger.status", "charging");
		}
	}

	if (status.flags[3] >= 128) {
		if (status.flags[3] & 0x20) {
			status_set("BYPASS");
		}

		if (status.flags[3] & 0x04 || status.flags[3] & 0x40) {
			status_set("OVER");
		}
	}

	status_commit();

	dstate_dataok();
}

void upsdrv_shutdown(void)
{
	/* tell the UPS to shut down, then return - DO NOT SLEEP HERE */

	/* maybe try to detect the UPS here, but try a shutdown even if
	 *   it doesn't respond at first if possible */

	/* replace with a proper shutdown function */
	upslogx(LOG_ERR, "shutdown not supported");
	set_exit_flag(-1);

	/* you may have to check the line status since the commands
	 *   for toggling power are frequently different for OL vs. OB */

	/* OL: this must power cycle the load if possible */

	/* OB: the load must remain off until the power returns */
}
/*
static int instcmd(const char *cmdname, const char *extra)
{
	int	i;
	char	command[SMALLBUF];

	if (!strcasecmp(cmdname, "beeper.off")) {
		upslogx(LOG_WARNING,
				"The 'beeper.off' command has been renamed to 'beeper.disable'");
		return powpan_instcmd("beeper.disable", NULL);
	}

	if (!strcasecmp(cmdname, "beeper.on")) {
		upslogx(LOG_WARNING,
				"The 'beeper.on' command has been renamed to 'beeper.enable'");
		return powpan_instcmd("beeper.enable", NULL);
	}

	for (i = 0; cmdtab[i].cmd != NULL; i++) {

		if (strcasecmp(cmdname, cmdtab[i].cmd)) {
			continue;
		}

		if ((powpan_command(cmdtab[i].command) == 2) && (!strcasecmp(salicru_answer, "#0"))) {
			return STAT_INSTCMD_HANDLED;
		}

		upslogx(LOG_ERR, "%s: command [%s] [%s] failed", __func__, cmdname, extra);
		return STAT_INSTCMD_FAILED;
	}

	if (!strcasecmp(cmdname, "shutdown.return")) {
		if (offdelay < 60) {
			snprintf(command, sizeof(command), "Z.%ld\r", offdelay / 6);
		} else {
			snprintf(command, sizeof(command), "Z%02ld\r", offdelay / 60);
		}
	} else if (!strcasecmp(cmdname, "shutdown.stayoff")) {
		if (offdelay < 60) {
			snprintf(command, sizeof(command), "S.%ld\r", offdelay / 6);
		} else {
			snprintf(command, sizeof(command), "S%02ld\r", offdelay / 60);
		}
	} else if (!strcasecmp(cmdname, "shutdown.reboot")) {
		if (offdelay < 60) {
			snprintf(command, sizeof(command), "S.%ldR%04ld\r", offdelay / 6, ondelay);
		} else {
			snprintf(command, sizeof(command), "S%02ldR%04ld\r", offdelay / 60, ondelay);
		}
	} else {
		upslogx(LOG_NOTICE, "%s: command [%s] [%s] unknown", __func__, cmdname, extra);
		return STAT_INSTCMD_UNKNOWN;
	}

	if ((powpan_command(command) == 2) && (!strcasecmp(salicru_answer, "#0"))) {
		return STAT_INSTCMD_HANDLED;
	}

	upslogx(LOG_ERR, "%s: command [%s] [%s] failed", __func__, cmdname, extra);
	return STAT_INSTCMD_FAILED;
}

static int setvar(const char *varname, const char *val)
{
	char	command[SMALLBUF];
	int 	i;

	for (i = 0;  vartab[i].var != NULL; i++) {

		if (strcasecmp(varname, vartab[i].var)) {
			continue;
		}

		if (!strcasecmp(val, dstate_getinfo(varname))) {
			upslogx(LOG_INFO, "%s: [%s] no change for variable [%s]", __func__, val, varname);
			return STAT_SET_HANDLED;
		}

		#ifdef HAVE_PRAGMAS_FOR_GCC_DIAGNOSTIC_IGNORED_FORMAT_NONLITERAL
		#pragma GCC diagnostic push
		#endif
		#ifdef HAVE_PRAGMA_GCC_DIAGNOSTIC_IGNORED_FORMAT_NONLITERAL
		#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		#endif
		#ifdef HAVE_PRAGMA_GCC_DIAGNOSTIC_IGNORED_FORMAT_SECURITY
		#pragma GCC diagnostic ignored "-Wformat-security"
		#endif
		snprintf(command, sizeof(command), vartab[i].set, atoi(val));
		#ifdef HAVE_PRAGMAS_FOR_GCC_DIAGNOSTIC_IGNORED_FORMAT_NONLITERAL
		#pragma GCC diagnostic pop
		#endif

		if ((powpan_command(command) == 2) && (!strcasecmp(powpan_answer, "#0"))) {
			dstate_setinfo(varname, "%s", val);
			return STAT_SET_HANDLED;
		}

		upslogx(LOG_ERR, "%s: setting variable [%s] to [%s] failed", __func__, varname, val);
		return STAT_SET_UNKNOWN;
	}

	upslogx(LOG_ERR, "%s: variable [%s] not found", __func__, varname);
	return STAT_SET_UNKNOWN;
}
*/
void upsdrv_help(void)
{
}

void upsdrv_makevartable(void)
{
	addvar(VAR_VALUE, "ondelay", "Delay before UPS startup");
	addvar(VAR_VALUE, "offdelay", "Delay before UPS shutdown");

	addvar(VAR_VALUE, "manufacturer", "manufacturer");
	addvar(VAR_VALUE, "model", "modelname");
	addvar(VAR_VALUE, "serial", "serialnumber");
}

void upsdrv_initups(void)
{
	char	*val;

	upsfd = ser_open(device_path);
	ser_set_speed(upsfd, device_path, B2400);

	val = getval("ondelay");
	if (val) {
		ondelay = strtol(val, NULL, 10);
	}

	if ((ondelay < 0) || (ondelay >= 3932100)) {
		fatalx(EXIT_FAILURE, "Start delay '%ld' out of range [0..3932100]", ondelay);
	}

	/* Truncate to nearest setable value */
	if (ondelay < 60) {
		ondelay -= (ondelay % 6);
	} else {
		ondelay -= (ondelay % 60);
	}

	val = getval("offdelay");
	if (val) {
		offdelay = strtol(val, NULL, 10);
	}

	if ((offdelay < 0) || (offdelay >= 3932100)) {
		fatalx(EXIT_FAILURE, "Shutdown delay '%ld' out of range [0..3932100]", offdelay);
	}

	/* Truncate to nearest setable value */
	if (offdelay < 60) {
		offdelay -= (offdelay % 6);
	} else {
		offdelay -= (offdelay % 60);
	}
}

void upsdrv_cleanup(void)
{
	ser_close(upsfd, device_path);
}
