/*
 *  acpi_thermal.c - ACPI Thermal Zone Driver ($Revision: 39 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This driver fully implements the ACPI thermal policy as described in the
 *  ACPI 2.0 Specification.
 *
 *  TBD: 1. Implement passive cooling hysteresis.
 *       2. Enhance passive cooling (CPU) states/limit interface to support
 *          concepts of 'multiple limiters', upper/lower limits, etc.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/compatmac.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_THERMAL_COMPONENT
ACPI_MODULE_NAME		("acpi_thermal")

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_THERMAL_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int tzp = 0;
MODULE_PARM(tzp, "i");
MODULE_PARM_DESC(tzp, "Thermal zone polling frequency, in 1/10 seconds.\n");

#define PREFIX			"ACPI: "


#define ACPI_THERMAL_MAX_ACTIVE	10

#define KELVIN_TO_CELSIUS(t)	((t-2732+5)/10)

static int acpi_thermal_add (struct acpi_device *device);
static int acpi_thermal_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_thermal_driver = {
	name:			ACPI_THERMAL_DRIVER_NAME,
	class:			ACPI_THERMAL_CLASS,
	ids:			ACPI_THERMAL_HID,
	ops:			{
					add:	acpi_thermal_add,
					remove:	acpi_thermal_remove,
				},
};

struct acpi_thermal_state {
	u8			critical:1;
	u8			hot:1;
	u8			passive:1;
	u8			active:1;
	u8			reserved:4;
	int			active_index;
};

struct acpi_thermal_state_flags {
	u8			valid:1;
	u8			enabled:1;
	u8			reserved:6;
};

struct acpi_thermal_critical {
	struct acpi_thermal_state_flags flags;
	unsigned long		temperature;
};

struct acpi_thermal_hot {
	struct acpi_thermal_state_flags flags;
	unsigned long		temperature;
};

struct acpi_thermal_passive {
	struct acpi_thermal_state_flags flags;
	unsigned long		temperature;
	unsigned long		tc1;
	unsigned long		tc2;
	unsigned long		tsp;
	struct acpi_handle_list	devices;
};

struct acpi_thermal_active {
	struct acpi_thermal_state_flags flags;
	unsigned long		temperature;
	struct acpi_handle_list	devices;
};

struct acpi_thermal_trips {
	struct acpi_thermal_critical critical;
	struct acpi_thermal_hot	hot;
	struct acpi_thermal_passive passive;
	struct acpi_thermal_active active[ACPI_THERMAL_MAX_ACTIVE];
};

struct acpi_thermal_flags {
	u8			cooling_mode:1;		/* _SCP */
	u8			devices:1;		/* _TZD */
	u8			reserved:6;
};

struct acpi_thermal {
	acpi_handle		handle;
	acpi_bus_id		name;
	unsigned long		temperature;
	unsigned long		last_temperature;
	unsigned long		polling_frequency;
	u8			cooling_mode;
	struct acpi_thermal_flags flags;
	struct acpi_thermal_state state;
	struct acpi_thermal_trips trips;
	struct acpi_handle_list	devices;
	struct timer_list	timer;
};


/* --------------------------------------------------------------------------
                             Thermal Zone Management
   -------------------------------------------------------------------------- */

static int
acpi_thermal_get_temperature (
	struct acpi_thermal *tz)
{
	acpi_status		status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_thermal_get_temperature");

	if (!tz)
		return_VALUE(-EINVAL);

	tz->last_temperature = tz->temperature;

	status = acpi_evaluate_integer(tz->handle, "_TMP", NULL, &tz->temperature);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Temperature is %lu dK\n", tz->temperature));

	return_VALUE(0);
}


static int
acpi_thermal_get_polling_frequency (
	struct acpi_thermal	*tz)
{
	acpi_status		status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_thermal_get_polling_frequency");

	if (!tz)
		return_VALUE(-EINVAL);

	status = acpi_evaluate_integer(tz->handle, "_TZP", NULL, &tz->polling_frequency);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Polling frequency is %lu dS\n", tz->polling_frequency));

	return_VALUE(0);
}


static int
acpi_thermal_set_polling (
	struct acpi_thermal	*tz,
	int			seconds)
{
	ACPI_FUNCTION_TRACE("acpi_thermal_set_polling");

	if (!tz)
		return_VALUE(-EINVAL);

	tz->polling_frequency = seconds * 10;	/* Convert value to deci-seconds */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Polling frequency set to %lu seconds\n", tz->polling_frequency));

	return_VALUE(0);
}


static int
acpi_thermal_set_cooling_mode (
	struct acpi_thermal	*tz,
	int			mode)
{
	acpi_status		status = AE_OK;
	acpi_object		arg0 = {ACPI_TYPE_INTEGER};
	acpi_object_list	arg_list= {1, &arg0};
	acpi_handle		handle = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_set_cooling_mode");

	if (!tz)
		return_VALUE(-EINVAL);

	status = acpi_get_handle(tz->handle, "_SCP", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "_SCP not present\n"));
		return_VALUE(-ENODEV);
	}

	arg0.integer.value = mode;

	status = acpi_evaluate(handle, NULL, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	tz->cooling_mode = mode;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Cooling mode [%s]\n", 
		mode?"passive":"active"));

	return_VALUE(0);
}


static int
acpi_thermal_get_trip_points (
	struct acpi_thermal *tz)
{
	acpi_status		status = AE_OK;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_get_trip_points");

	if (!tz)
		return_VALUE(-EINVAL);

	/* Critical Shutdown (required) */

	status = acpi_evaluate_integer(tz->handle, "_CRT", NULL, 
		&tz->trips.critical.temperature);
	if (ACPI_FAILURE(status)) {
		tz->trips.critical.flags.valid = 0;
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "No critical threshold\n"));
		return -ENODEV;
	}
	else {
		tz->trips.critical.flags.valid = 1;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found critical threshold [%lu]\n", tz->trips.critical.temperature));
	}

	/* Critical Sleep (optional) */

	status = acpi_evaluate_integer(tz->handle, "_HOT", NULL, &tz->trips.hot.temperature);
	if (ACPI_FAILURE(status)) {
		tz->trips.hot.flags.valid = 0;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No hot threshold\n"));
	}
	else {
		tz->trips.hot.flags.valid = 1;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found hot threshold [%lu]\n", tz->trips.hot.temperature));
	}

	/* Passive: Processors (optional) */

	status = acpi_evaluate_integer(tz->handle, "_PSV", NULL, &tz->trips.passive.temperature);
	if (ACPI_FAILURE(status)) {
		tz->trips.passive.flags.valid = 0;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No passive threshold\n"));
	}
	else {
		tz->trips.passive.flags.valid = 1;

		status = acpi_evaluate_integer(tz->handle, "_TC1", NULL, &tz->trips.passive.tc1);
		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;

		status = acpi_evaluate_integer(tz->handle, "_TC2", NULL, &tz->trips.passive.tc2);
		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;

		status = acpi_evaluate_integer(tz->handle, "_TSP", NULL, &tz->trips.passive.tsp);
		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;

		status = acpi_evaluate_reference(tz->handle, "_PSL", NULL, &tz->trips.passive.devices);
		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;

		if (!tz->trips.passive.flags.valid)
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid passive threshold\n"));
		else
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found passive threshold [%lu]\n", tz->trips.passive.temperature));
	}

	/* Active: Fans, etc. (optional) */

	for (i=0; i<ACPI_THERMAL_MAX_ACTIVE; i++) {

		char name[5] = {'_','A','C',('0'+i),'\0'};

		status = acpi_evaluate_integer(tz->handle, name, NULL, &tz->trips.active[i].temperature);
		if (ACPI_FAILURE(status))
			break;

		name[2] = 'L';
		status = acpi_evaluate_reference(tz->handle, name, NULL, &tz->trips.active[i].devices);
		if (ACPI_SUCCESS(status)) {
			tz->trips.active[i].flags.valid = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found active threshold [%d]:[%lu]\n", i, tz->trips.active[i].temperature));
		}
		else
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid active threshold [%d]\n", i));
	}

	return_VALUE(0);
}


static int
acpi_thermal_get_devices (
	struct acpi_thermal	*tz)
{
	acpi_status		status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_thermal_get_devices");

	if (!tz)
		return_VALUE(-EINVAL);

	status = acpi_evaluate_reference(tz->handle, "_TZD", NULL, &tz->devices);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


static int
acpi_thermal_call_usermode (
	char			*path)
{
	char			*argv[2] = {NULL, NULL};
	char			*envp[3] = {NULL, NULL, NULL};

	ACPI_FUNCTION_TRACE("acpi_thermal_call_usermode");

	if (!path)
		return_VALUE(-EINVAL);;

	argv[0] = path;

	/* minimal command environment */
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	
	call_usermodehelper(argv[0], argv, envp);

	return_VALUE(0);
}


static int
acpi_thermal_critical (
	struct acpi_thermal	*tz)
{
	int			result = 0;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_critical");

	if (!tz || !tz->trips.critical.flags.valid)
		return_VALUE(-EINVAL);

	if (tz->temperature >= tz->trips.critical.temperature) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Critical trip point\n"));
		tz->trips.critical.flags.enabled = 1;
	}
	else if (tz->trips.critical.flags.enabled)
		tz->trips.critical.flags.enabled = 0;

	result = acpi_bus_get_device(tz->handle, &device);
	if (result)
		return_VALUE(result);

	acpi_bus_generate_event(device, ACPI_THERMAL_NOTIFY_CRITICAL, tz->trips.critical.flags.enabled);

	acpi_thermal_call_usermode(ACPI_THERMAL_PATH_POWEROFF);

	return_VALUE(0);
}


static int
acpi_thermal_hot (
	struct acpi_thermal	*tz)
{
	int			result = 0;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_hot");

	if (!tz || !tz->trips.hot.flags.valid)
		return_VALUE(-EINVAL);

	if (tz->temperature >= tz->trips.hot.temperature) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Hot trip point\n"));
		tz->trips.hot.flags.enabled = 1;
	}
	else if (tz->trips.hot.flags.enabled)
		tz->trips.hot.flags.enabled = 0;

	result = acpi_bus_get_device(tz->handle, &device);
	if (result)
		return_VALUE(result);

	acpi_bus_generate_event(device, ACPI_THERMAL_NOTIFY_HOT, tz->trips.hot.flags.enabled);

	/* TBD: Call user-mode "sleep(S4)" function */

	return_VALUE(0);
}


static int
acpi_thermal_passive (
	struct acpi_thermal	*tz)
{
	int			result = 0;
	struct acpi_thermal_passive *passive = NULL;
	int			trend = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_passive");

	if (!tz || !tz->trips.passive.flags.valid)
		return_VALUE(-EINVAL);

	passive = &(tz->trips.passive);

	/*
	 * Above Trip?
	 * -----------
	 * Calculate the thermal trend (using the passive cooling equation)
	 * and modify the performance limit for all passive cooling devices
	 * accordingly.  Note that we assume symmetry.
	 */
	if (tz->temperature >= passive->temperature) {
		trend = (passive->tc1 * (tz->temperature - tz->last_temperature)) + (passive->tc2 * (tz->temperature - passive->temperature));
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"trend[%d]=(tc1[%lu]*(tmp[%lu]-last[%lu]))+(tc2[%lu]*(tmp[%lu]-psv[%lu]))\n", 
			trend, passive->tc1, tz->temperature, 
			tz->last_temperature, passive->tc2, 
			tz->temperature, passive->temperature));
		/* Heating up? */
		if (trend > 0)
			for (i=0; i<passive->devices.count; i++)
				acpi_processor_set_thermal_limit(
					passive->devices.handles[i], 
					ACPI_PROCESSOR_LIMIT_INCREMENT);
		/* Cooling off? */
		else if (trend < 0)
			for (i=0; i<passive->devices.count; i++)
				acpi_processor_set_thermal_limit(
					passive->devices.handles[i], 
					ACPI_PROCESSOR_LIMIT_DECREMENT);
	}

	/*
	 * Below Trip?
	 * -----------
	 * Implement passive cooling hysteresis to slowly increase performance
	 * and avoid thrashing around the passive trip point.  Note that we
	 * assume symmetry.
	 */
	else if (tz->trips.passive.flags.enabled) {
		for (i=0; i<passive->devices.count; i++)
			result = acpi_processor_set_thermal_limit(
				passive->devices.handles[i], 
				ACPI_PROCESSOR_LIMIT_DECREMENT);
		if (result == 1) {
			tz->trips.passive.flags.enabled = 0;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
				"Disabling passive cooling (zone is cool)\n"));
		}
	}

	return_VALUE(0);
}


static int
acpi_thermal_active (
	struct acpi_thermal	*tz)
{
	int			result = 0;
	struct acpi_thermal_active *active = NULL;
	int                     i = 0;
	int			j = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_active");

	if (!tz)
		return_VALUE(-EINVAL);

	for (i=0; i<ACPI_THERMAL_MAX_ACTIVE; i++) {

		active = &(tz->trips.active[i]);
		if (!active || !active->flags.valid)
			break;

		/*
		 * Above Threshold?
		 * ----------------
		 * If not already enabled, turn ON all cooling devices
		 * associated with this active threshold.
		 */
		if (tz->temperature >= active->temperature) {
			tz->state.active_index = i;
			if (!active->flags.enabled) {
				for (j = 0; j < active->devices.count; j++) {
					result = acpi_bus_set_power(active->devices.handles[j], ACPI_STATE_D0);
					if (result) {
						ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Unable to turn cooling device [%p] 'on'\n", active->devices.handles[j]));
						continue;
					}
					active->flags.enabled = 1;
					ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Cooling device [%p] now 'on'\n", active->devices.handles[j]));
				}
			}
		}
		/*
		 * Below Threshold?
		 * ----------------
		 * Turn OFF all cooling devices associated with this
		 * threshold.
		 */
		else if (active->flags.enabled) {
			for (j = 0; j < active->devices.count; j++) {
				result = acpi_bus_set_power(active->devices.handles[j], ACPI_STATE_D3);
				if (result) {
					ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Unable to turn cooling device [%p] 'off'\n", active->devices.handles[j]));
					continue;
				}
				active->flags.enabled = 0;
				ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Cooling device [%p] now 'off'\n", active->devices.handles[j]));
			}
		}
	}

	return_VALUE(0);
}


static void acpi_thermal_check (void *context);

static void
acpi_thermal_run (
	unsigned long		data)
{
	acpi_os_queue_for_execution(OSD_PRIORITY_GPE,  acpi_thermal_check, (void *) data);
}


static void
acpi_thermal_check (
	void                    *data)
{
	int			result = 0;
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	unsigned long		sleep_time = 0;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_check");

	if (!tz) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	result = acpi_thermal_get_temperature(tz);
	if (result)
		return_VOID;
	
	memset(&tz->state, 0, sizeof(tz->state));
	
	/*
	 * Check Trip Points
	 * -----------------
	 * Compare the current temperature to the trip point values to see
	 * if we've entered one of the thermal policy states.  Note that
	 * this function determines when a state is entered, but the 
	 * individual policy decides when it is exited (e.g. hysteresis).
	 */
	if ((tz->trips.critical.flags.valid) && (tz->temperature >= tz->trips.critical.temperature))
		tz->trips.critical.flags.enabled = 1;
	if ((tz->trips.hot.flags.valid) && (tz->temperature >= tz->trips.hot.temperature))
		tz->trips.hot.flags.enabled = 1;
	if ((tz->trips.passive.flags.valid) && (tz->temperature >= tz->trips.passive.temperature))
		tz->trips.passive.flags.enabled = 1;
	for (i=0; i<ACPI_THERMAL_MAX_ACTIVE; i++)
		if ((tz->trips.active[i].flags.valid) && (tz->temperature >= tz->trips.active[i].temperature))
			tz->trips.active[i].flags.enabled = 1;

	/*
	 * Invoke Policy
	 * -------------
	 * Separated from the above check to allow individual policy to 
	 * determine when to exit a given state.
	 */
	if (tz->trips.critical.flags.enabled)
		acpi_thermal_critical(tz);
	if (tz->trips.hot.flags.enabled)
		acpi_thermal_hot(tz);
	if (tz->trips.passive.flags.enabled)
		acpi_thermal_passive(tz);
	if (tz->trips.active[0].flags.enabled)
		acpi_thermal_active(tz);

	/*
	 * Calculate State
	 * ---------------
	 * Again, separated from the above two to allow independent policy
	 * decisions.
	 */
	if (tz->trips.critical.flags.enabled)
		tz->state.critical = 1;
	if (tz->trips.hot.flags.enabled)
		tz->state.hot = 1;
	if (tz->trips.passive.flags.enabled)
		tz->state.passive = 1;
	for (i=0; i<ACPI_THERMAL_MAX_ACTIVE; i++)
		if (tz->trips.active[i].flags.enabled)
			tz->state.active = 1;

	/*
	 * Calculate Sleep Time
	 * --------------------
	 * If we're in the passive state, use _TSP's value.  Otherwise
	 * use the default polling frequency (e.g. _TZP).  If no polling
	 * frequency is specified then we'll wait forever (at least until
	 * a thermal event occurs).  Note that _TSP and _TZD values are
	 * given in 1/10th seconds (we must covert to milliseconds).
	 */
	if (tz->state.passive)
		sleep_time = tz->trips.passive.tsp * 100;
	else if (tz->polling_frequency > 0)
		sleep_time = tz->polling_frequency * 100;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "%s: temperature[%lu] sleep[%lu]\n", 
		tz->name, tz->temperature, sleep_time));

	/*
	 * Schedule Next Poll
	 * ------------------
	 */
	if (!sleep_time) {
		if (timer_pending(&(tz->timer)))
			del_timer(&(tz->timer));
	}
	else {
		if (timer_pending(&(tz->timer)))
			mod_timer(&(tz->timer), (HZ * sleep_time) / 1000);
		else {
			tz->timer.data = (unsigned long) tz;
			tz->timer.function = acpi_thermal_run;
			tz->timer.expires = jiffies + (HZ * sleep_time) / 1000;
			add_timer(&(tz->timer));
		}
	}

	return_VOID;
}


/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

struct proc_dir_entry		*acpi_thermal_dir = NULL;


static int
acpi_thermal_read_state (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_read_state");

	if (!tz || (off != 0))
		goto end;

	p += sprintf(p, "state:                   ");

	if (!tz->state.critical && !tz->state.hot && !tz->state.passive && !tz->state.active)
		p += sprintf(p, "ok\n");
	else {
		if (tz->state.critical)
			p += sprintf(p, "critical ");
		if (tz->state.hot)
			p += sprintf(p, "hot ");
		if (tz->state.passive)
			p += sprintf(p, "passive ");
		if (tz->state.active)
			p += sprintf(p, "active[%d]", tz->state.active_index);
		p += sprintf(p, "\n");
	}

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_thermal_read_temperature (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	int			result = 0;
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_read_temperature");

	if (!tz || (off != 0))
		goto end;

	result = acpi_thermal_get_temperature(tz);
	if (result)
		goto end;

	p += sprintf(p, "temperature:             %lu C\n", 
		KELVIN_TO_CELSIUS(tz->temperature));
	
end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_thermal_read_trip_points (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			*p = page;
	int			len = 0;
	int			i = 0;
	int			j = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_read_trip_points");

	if (!tz || (off != 0))
		goto end;

	if (tz->trips.critical.flags.valid)
		p += sprintf(p, "critical (S5):           %lu C\n",
			KELVIN_TO_CELSIUS(tz->trips.critical.temperature));

	if (tz->trips.hot.flags.valid)
		p += sprintf(p, "hot (S4):                %lu C\n",
			KELVIN_TO_CELSIUS(tz->trips.hot.temperature));

	if (tz->trips.passive.flags.valid) {
		p += sprintf(p, "passive:                 %lu C: tc1=%lu tc2=%lu tsp=%lu devices=",
			KELVIN_TO_CELSIUS(tz->trips.passive.temperature),
			tz->trips.passive.tc1,
			tz->trips.passive.tc2, 
			tz->trips.passive.tsp);
		for (j=0; j<tz->trips.passive.devices.count; j++) {

			p += sprintf(p, "0x%p ", tz->trips.passive.devices.handles[j]);
		}
		p += sprintf(p, "\n");
	}

	for (i=0; i<ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (!(tz->trips.active[i].flags.valid))
			break;
		p += sprintf(p, "active[%d]:               %lu C: devices=",
			i, KELVIN_TO_CELSIUS(tz->trips.active[i].temperature));
		for (j=0; j<tz->trips.active[i].devices.count; j++) 
			p += sprintf(p, "0x%p ",
				tz->trips.active[i].devices.handles[j]);
		p += sprintf(p, "\n");
	}

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_thermal_read_cooling_mode (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_read_cooling_mode");

	if (!tz || (off != 0))
		goto end;

	if (!tz->flags.cooling_mode) {
		p += sprintf(p, "<not supported>\n");
		goto end;
	}

	p += sprintf(p, "cooling mode:            %s\n",
		tz->cooling_mode?"passive":"active");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_thermal_write_cooling_mode (
	struct file		*file,
	const char		*buffer,
	unsigned long		count,
	void			*data)
{
	int			result = 0;
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			mode_string[12] = {'\0'};

	ACPI_FUNCTION_TRACE("acpi_thermal_write_cooling_mode");

	if (!tz || (count > sizeof(mode_string) - 1))
		return_VALUE(-EINVAL);

	if (!tz->flags.cooling_mode)
		return_VALUE(-ENODEV);

	if (copy_from_user(mode_string, buffer, count))
		return_VALUE(-EFAULT);
	
	mode_string[count] = '\0';
	
	result = acpi_thermal_set_cooling_mode(tz, 
		simple_strtoul(mode_string, NULL, 0));
	if (result)
		return_VALUE(result);

	return_VALUE(count);
}


static int
acpi_thermal_read_polling (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_read_polling");

	if (!tz || (off != 0))
		goto end;

	if (!tz->polling_frequency) {
		p += sprintf(p, "<polling disabled>\n");
		goto end;
	}

	p += sprintf(p, "polling frequency:       %lu seconds\n",
		(tz->polling_frequency / 10));

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_thermal_write_polling (
	struct file		*file,
	const char		*buffer,
	unsigned long		count,
	void			*data)
{
	int			result = 0;
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	char			polling_string[12] = {'\0'};
	int			seconds = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_write_polling");

	if (!tz || (count > sizeof(polling_string) - 1))
		return_VALUE(-EINVAL);
	
	if (copy_from_user(polling_string, buffer, count))
		return_VALUE(-EFAULT);
	
	polling_string[count] = '\0';

	seconds = simple_strtoul(polling_string, NULL, 0);
	
	result = acpi_thermal_set_polling(tz, seconds);
	if (result)
		return_VALUE(result);

	acpi_thermal_check(tz);

	return_VALUE(count);
}


static int
acpi_thermal_add_fs (
	struct acpi_device	*device)
{
	struct proc_dir_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_add_fs");

	if (!acpi_thermal_dir) {
		acpi_thermal_dir = proc_mkdir(ACPI_THERMAL_CLASS, 
			acpi_root_dir);
		if (!acpi_thermal_dir)
			return_VALUE(-ENODEV);
	}

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
			acpi_thermal_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
	}

	/* 'state' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_STATE,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_THERMAL_FILE_STATE));
	else {
		entry->read_proc = acpi_thermal_read_state;
		entry->data = acpi_driver_data(device);
	}

	/* 'temperature' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_TEMPERATURE,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_THERMAL_FILE_TEMPERATURE));
	else {
		entry->read_proc = acpi_thermal_read_temperature;
		entry->data = acpi_driver_data(device);
	}

	/* 'trip_points' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_TRIP_POINTS,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_THERMAL_FILE_POLLING_FREQ));
	else {
		entry->read_proc = acpi_thermal_read_trip_points;
		entry->data = acpi_driver_data(device);
	}

	/* 'cooling_mode' [R/W] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_COOLING_MODE,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_THERMAL_FILE_COOLING_MODE));
	else {
		entry->read_proc = acpi_thermal_read_cooling_mode;
		entry->write_proc = acpi_thermal_write_cooling_mode;
		entry->data = acpi_driver_data(device);
	}

	/* 'polling_frequency' [R/W] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_POLLING_FREQ,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_THERMAL_FILE_POLLING_FREQ));
	else {
		entry->read_proc = acpi_thermal_read_polling;
		entry->write_proc = acpi_thermal_write_polling;
		entry->data = acpi_driver_data(device);
	}

	return_VALUE(0);
}


static int
acpi_thermal_remove_fs (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_thermal_remove_fs");

	if (!acpi_thermal_dir)
		return_VALUE(-ENODEV);

	if (acpi_device_dir(device))
		remove_proc_entry(acpi_device_bid(device), acpi_thermal_dir);

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void
acpi_thermal_notify (
	acpi_handle 		handle,
	u32 			event,
	void 			*data)
{
	struct acpi_thermal	*tz = (struct acpi_thermal *) data;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_notify");

	if (!tz)
		return_VOID;

	if (acpi_bus_get_device(tz->handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_THERMAL_NOTIFY_TEMPERATURE:
		acpi_thermal_check(tz);
		break;
	case ACPI_THERMAL_NOTIFY_THRESHOLDS:
		acpi_thermal_get_trip_points(tz);
		acpi_thermal_check(tz);
		acpi_bus_generate_event(device, event, 0);
		break;
	case ACPI_THERMAL_NOTIFY_DEVICES:
		if (tz->flags.devices)
			acpi_thermal_get_devices(tz);
		acpi_bus_generate_event(device, event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}


static int
acpi_thermal_get_info (
	struct acpi_thermal	*tz)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_get_info");

	if (!tz)
		return_VALUE(-EINVAL);

	/* Get temperature [_TMP] (required) */
	result = acpi_thermal_get_temperature(tz);
	if (result)
		return_VALUE(result);

	/* Set the cooling mode [_SCP] to active cooling (default) */
	result = acpi_thermal_set_cooling_mode(tz, ACPI_THERMAL_MODE_ACTIVE);
	if (!result)
		tz->flags.cooling_mode = 1;

	/* Get trip points [_CRT, _PSV, etc.] (required) */
	result = acpi_thermal_get_trip_points(tz);
	if (result)
		return_VALUE(result);

	/* Get default polling frequency [_TZP] (optional) */
	if (tzp)
		tz->polling_frequency = tzp;
	else
		acpi_thermal_get_polling_frequency(tz);

	/* Get devices in this thermal zone [_TZD] (optional) */
	result = acpi_thermal_get_devices(tz);
	if (!result)
		tz->flags.devices = 1;

	return_VALUE(0);
}


static int
acpi_thermal_add (
	struct acpi_device 		*device)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_thermal	*tz = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_add");

	if (!device)
		return_VALUE(-EINVAL);

	tz = kmalloc(sizeof(struct acpi_thermal), GFP_KERNEL);
	if (!tz)
		return_VALUE(-ENOMEM);
	memset(tz, 0, sizeof(struct acpi_thermal));

	tz->handle = device->handle;
	sprintf(tz->name, "%s", device->pnp.bus_id);
	sprintf(acpi_device_name(device), "%s", ACPI_THERMAL_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_THERMAL_CLASS);
	acpi_driver_data(device) = tz;

	result = acpi_thermal_get_info(tz);
	if (result)
		goto end;

	result = acpi_thermal_add_fs(device);
	if (result)
		return_VALUE(result);

	acpi_thermal_check(tz);

	status = acpi_install_notify_handler(tz->handle,
		ACPI_DEVICE_NOTIFY, acpi_thermal_notify, tz);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error installing notify handler\n"));
		result = -ENODEV;
		goto end;
	}

	init_timer(&tz->timer);

	printk(KERN_INFO PREFIX "%s [%s] (%lu C)\n",
		acpi_device_name(device), acpi_device_bid(device),
		KELVIN_TO_CELSIUS(tz->temperature));

end:
	if (result) {
		acpi_thermal_remove_fs(device);
		kfree(tz);
	}

	return_VALUE(result);
}


static int
acpi_thermal_remove (
	struct acpi_device	*device,
	int			type)
{
	acpi_status		status = AE_OK;
	struct acpi_thermal	*tz = NULL;

	ACPI_FUNCTION_TRACE("acpi_thermal_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	tz = (struct acpi_thermal *) acpi_driver_data(device);

	if (timer_pending(&(tz->timer)))
		del_timer(&(tz->timer));

	status = acpi_remove_notify_handler(tz->handle,
		ACPI_DEVICE_NOTIFY, acpi_thermal_notify);
	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error removing notify handler\n"));

	/* Terminate policy */
	if (tz->trips.passive.flags.valid
		&& tz->trips.passive.flags.enabled) {
		tz->trips.passive.flags.enabled = 0;
		acpi_thermal_passive(tz);
	}
	if (tz->trips.active[0].flags.valid
		&& tz->trips.active[0].flags.enabled) {
		tz->trips.active[0].flags.enabled = 0;
		acpi_thermal_active(tz);
	}

	acpi_thermal_remove_fs(device);

	return_VALUE(0);
}


static int __init
acpi_thermal_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_init");

	result = acpi_bus_register_driver(&acpi_thermal_driver);
	if (result < 0)
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


static void __exit
acpi_thermal_exit (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_thermal_exit");

	result = acpi_bus_unregister_driver(&acpi_thermal_driver);
	if (!result)
		remove_proc_entry(ACPI_THERMAL_CLASS, acpi_root_dir);

	return_VOID;
}


module_init(acpi_thermal_init);
module_exit(acpi_thermal_exit);
