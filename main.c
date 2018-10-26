/*
 * Adapted from monitor-sensor.c in iio-sensors-proxy
 *
 * Copyright (c) 2015 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <gio/gio.h>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a, b) (a) < (b) ? (a) : (b)
#endif
#ifndef MAX
#define MAX(a, b) (a) > (b) ? (a) : (b)
#endif

#define LOWER_BACKLIGHT 6
#define UPPER_BACKLIGHT 100

#define NORMAL_COORDS "1 0 0 0 1 0 0 0 1"
#define INVERT_COORDS "-1 0 1 0 -1 1 0 0 1"
#define LEFT_COORDS "0 -1 1 1 0 0 0 0 1"
#define RIGHT_COORDS "0 1 0 -1 0 1 0 0 1"

static GMainLoop *loop;
static guint watch_id;
static GDBusProxy *iio_proxy, *iio_proxy_compass;

static double calculate_brightness(double value, const char *unit) {
	if (strncmp(unit, "lux", 4) != 0) {
		printf("Unknown unit: %s\n", unit);
		return 0;
	}
	// use the microsoft lux conversion table
	// https://docs.microsoft.com/en-us/windows/desktop/sensorsapi/understanding-and-interpreting-lux-values
	return log10(value + 1) / 5.0;
}

static void
properties_changed (GDBusProxy *proxy,
		    GVariant   *changed_properties,
		    GStrv       invalidated_properties,
		    gpointer    user_data)
{
	GVariant *v;
	GVariantDict dict;
	const char *varstr;
	double dvalue;
	char buffer[256];
	char transdevices[3][64] = {
		"Atmel",
		"Wacom ISDv4 12C Pen stylus",
		"Wacom ISDv4 12C Pen eraser",
	};

	g_variant_dict_init (&dict, changed_properties);

	if (g_variant_dict_contains (&dict, "HasAccelerometer")) {
		v = g_dbus_proxy_get_cached_property (iio_proxy, "HasAccelerometer");
		if (g_variant_get_boolean (v))
			g_print ("+++ Accelerometer appeared\n");
		else
			g_print ("--- Accelerometer disappeared\n");
		g_variant_unref (v);
	}
	if (g_variant_dict_contains (&dict, "AccelerometerOrientation")) {
		v = g_dbus_proxy_get_cached_property (iio_proxy, "AccelerometerOrientation");
		varstr = g_variant_get_string(v, NULL);
		// g_print ("    Accelerometer orientation changed: %s\n", varstr);
		if (strcmp(varstr, "normal") == 0) {
			system("xrandr --output eDP1 --rotate normal");
			for (int i = 0; i < 3; i++) {
				sprintf(buffer, "xinput set-prop '%s' 'Coordinate Transformation Matrix' %s", transdevices[i], NORMAL_COORDS);
				system(buffer);
			}
		} else if (strcmp(varstr, "left-up") == 0) {
			system("xrandr --output eDP1 --rotate left");
			for (int i = 0; i < 3; i++) {
				sprintf(buffer, "xinput set-prop '%s' 'Coordinate Transformation Matrix' %s", transdevices[i], LEFT_COORDS);
				system(buffer);
			}
		} else if (strcmp(varstr, "right-up") == 0) {
			system("xrandr --output eDP1 --rotate right");
			for (int i = 0; i < 3; i++) {
				sprintf(buffer, "xinput set-prop '%s' 'Coordinate Transformation Matrix' %s", transdevices[i], RIGHT_COORDS);
				system(buffer);
			}
		} else if (strcmp(varstr, "bottom-up") == 0) {
			system("xrandr --output eDP1 --rotate inverted");
			for (int i = 0; i < 3; i++) {
				sprintf(buffer, "xinput set-prop '%s' 'Coordinate Transformation Matrix' %s", transdevices[i], INVERT_COORDS);
				system(buffer);
			}
		} else {
			printf("Unknown command\n");
		}
		g_variant_unref (v);
	}
	if (g_variant_dict_contains (&dict, "HasAmbientLight")) {
		v = g_dbus_proxy_get_cached_property (iio_proxy, "HasAmbientLight");
		if (g_variant_get_boolean (v))
			g_print ("+++ Light sensor appeared\n");
		else
			g_print ("--- Light sensor disappeared\n");
		g_variant_unref (v);
	}
	if (g_variant_dict_contains (&dict, "LightLevel")) {
		GVariant *unit;

		v = g_dbus_proxy_get_cached_property (iio_proxy, "LightLevel");
		unit = g_dbus_proxy_get_cached_property (iio_proxy, "LightLevelUnit");
		dvalue = g_variant_get_double(v);
		varstr = g_variant_get_string(unit, NULL);

		sprintf(buffer, "xbacklight -set %lf", calculate_brightness(dvalue, varstr) * 10 + LOWER_BACKLIGHT);
		printf("%s\n", buffer);
		system(buffer);

		g_variant_unref (v);
		g_variant_unref (unit);
	}
	g_variant_dict_clear (&dict);
}

static void
print_initial_values (void)
{
	GVariant *v;

	v = g_dbus_proxy_get_cached_property (iio_proxy, "HasAccelerometer");
	if (g_variant_get_boolean (v)) {
		g_variant_unref (v);
		v = g_dbus_proxy_get_cached_property (iio_proxy, "AccelerometerOrientation");
		g_print ("=== Has accelerometer (orientation: %s)\n",
			   g_variant_get_string (v, NULL));
	} else {
		g_print ("=== No accelerometer\n");
	}
	g_variant_unref (v);

	v = g_dbus_proxy_get_cached_property (iio_proxy, "HasAmbientLight");
	if (g_variant_get_boolean (v)) {
		GVariant *unit;

		g_variant_unref (v);
		v = g_dbus_proxy_get_cached_property (iio_proxy, "LightLevel");
		unit = g_dbus_proxy_get_cached_property (iio_proxy, "LightLevelUnit");
		g_print ("=== Has ambient light sensor (value: %lf, unit: %s)\n",
			   g_variant_get_double (v),
			   g_variant_get_string (unit, NULL));
		g_variant_unref (unit);
	} else {
		g_print ("=== No ambient light sensor\n");
	}
	// g_variant_unref (v);

	g_clear_pointer (&v, g_variant_unref);
}

static void
appeared_cb (GDBusConnection *connection,
	     const gchar     *name,
	     const gchar     *name_owner,
	     gpointer         user_data)
{
	GError *error = NULL;
	GVariant *ret = NULL;

	g_print ("+++ iio-sensor-proxy appeared\n");

	iio_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						   G_DBUS_PROXY_FLAGS_NONE,
						   NULL,
						   "net.hadess.SensorProxy",
						   "/net/hadess/SensorProxy",
						   "net.hadess.SensorProxy",
						   NULL, NULL);

	g_signal_connect (G_OBJECT (iio_proxy), "g-properties-changed",
			  G_CALLBACK (properties_changed), NULL);

	/* Accelerometer */
	ret = g_dbus_proxy_call_sync (iio_proxy,
				     "ClaimAccelerometer",
				     NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL, &error);
	if (!ret) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Failed to claim accelerometer: %s", error->message);
		g_main_loop_quit (loop);
		return;
	}
	g_clear_pointer (&ret, g_variant_unref);

	/* ALS */
	ret = g_dbus_proxy_call_sync (iio_proxy,
				     "ClaimLight",
				     NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL, &error);
	if (!ret) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Failed to claim light sensor: %s", error->message);
		g_main_loop_quit (loop);
		return;
	}
	g_clear_pointer (&ret, g_variant_unref);

	print_initial_values ();
}

static void
vanished_cb (GDBusConnection *connection,
	     const gchar *name,
	     gpointer user_data)
{
	if (iio_proxy) {
		g_clear_object (&iio_proxy);
		g_print ("--- iio-sensor-proxy vanished, waiting for it to appear\n");
	}
}

int main (int argc, char **argv)
{
	watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				     "net.hadess.SensorProxy",
				     G_BUS_NAME_WATCHER_FLAGS_NONE,
				     appeared_cb,
				     vanished_cb,
				     NULL, NULL);

	g_print ("    Waiting for iio-sensor-proxy to appear\n");
	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

	return 0;
}
