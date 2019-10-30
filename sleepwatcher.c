/*
 *	Based on:
 *  "sleepwatcher.c - sleep mode watchdog program
 * 
 *	Copyright (c) 2002-2019 Bernhard Baehr"
 * 
 *  heavily altered by Juliano Viana to enable embedding in a Go program
 *
 *
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/wait.h>

#include <mach/mach_port.h>
#include <mach/mach_interface.h>
#include <mach/mach_init.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

#define TIMER_RESOLUTION 0.1 /* seconds - when changing, adjust the man page! */

#define DENY_SLEEP ((char *)1) /* for args.allowsleepcommand */


int idletimeout = 0;

int verbose = 0;



void message(int priority, const char *msg, ...)
{
	va_list ap;
	FILE *out;

	if (verbose || priority < LOG_INFO)
	{
		va_start(ap, msg);
		out = (priority == LOG_INFO) ? stdout : stderr;
		vfprintf(out, msg, ap);
		fflush(out);
	}

}





static CFRunLoopTimerRef setupTimer(long int timeout, CFRunLoopTimerRef timer, CFRunLoopTimerCallBack callback)
{
	
	if (timeout)
	{
		if (timer) 
			CFRunLoopTimerSetNextFireDate(timer, CFAbsoluteTimeGetCurrent() + timeout * TIMER_RESOLUTION);
		else
		{
			timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
										 CFAbsoluteTimeGetCurrent() + timeout * TIMER_RESOLUTION,
										 kCFAbsoluteTimeIntervalSince1904, 0, 0, callback, NULL);
			CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
		}
	}
	else
	{
		if (timer)
		{
			CFRunLoopTimerInvalidate(timer);
			CFRelease(timer);
			timer = NULL;
		}
	}
	return (timer);
}
static void setupIdleTimer(void);
static void idleCallback(CFRunLoopTimerRef timer, void *info)
{
	onSystemIdle();
	setupIdleTimer();
}

static void setupIdleTimer(void)
{
	static CFRunLoopTimerRef idleTimer = NULL;

	idleTimer = setupTimer(idletimeout, idleTimer, idleCallback);
}


// THIS CALLBACK IS NOT CALLED WHEN THE GUI IS NOT RUNNING
static void hidCallback(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
	
	static CFAbsoluteTime timeOfLastCall = 0;

	CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
	if (timeOfLastCall == 0)
		timeOfLastCall = now;
	timeOfLastCall = CFAbsoluteTimeGetCurrent(); // don't use "now" because the commands may consume some time
	setupIdleTimer();
}

static CFMutableDictionaryRef createDeviceMatchingDictionary(UInt32 usagePage, UInt32 usage) // see TN 2187
{
	CFMutableDictionaryRef result = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
															  &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (result)
	{
		CFNumberRef pageCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePage);
		if (pageCFNumberRef)
		{
			CFDictionarySetValue(result, CFSTR(kIOHIDDeviceUsagePageKey), pageCFNumberRef);
			CFRelease(pageCFNumberRef);
			CFNumberRef usageCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
			if (usageCFNumberRef)
			{
				CFDictionarySetValue(result, CFSTR(kIOHIDDeviceUsageKey), usageCFNumberRef);
				CFRelease(usageCFNumberRef);
			}
			else
			{
				message(LOG_ERR, "CFNumberCreate failed for usage\n");
				exit(1);
			}
		}
		else
		{
			message(LOG_ERR, "CFNumberCreate failed for usagePage\n");
			exit(1);
		}
	}
	else
	{
		message(LOG_ERR, "CFDictionaryCreateMutable failed\n");
		exit(1);
	}
	return result;
}

static CFArrayRef createGenericDesktopMatchingDictionaries(void) // see TN 2187
{
	CFMutableArrayRef matchingCFArrayRef = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
	if (matchingCFArrayRef)
	{
		CFDictionaryRef matchingCFDictRef = createDeviceMatchingDictionary(kHIDPage_GenericDesktop, kHIDUsage_GD_Mouse);
		if (matchingCFDictRef)
		{
			CFArrayAppendValue(matchingCFArrayRef, matchingCFDictRef);
			CFRelease(matchingCFDictRef);
		}
		else
		{
			message(LOG_ERR, "createDeviceMatchingDictionary failed for mouse\n");
			exit(1);
		}
		matchingCFDictRef = createDeviceMatchingDictionary(kHIDPage_GenericDesktop, kHIDUsage_GD_Keyboard);
		if (matchingCFDictRef)
		{
			CFArrayAppendValue(matchingCFArrayRef, matchingCFDictRef);
			CFRelease(matchingCFDictRef);
		}
		else
		{
			message(LOG_ERR, "createDeviceMatchingDictionary failed for keyboard\n");
			exit(1);
		}
	}
	else
	{
		message(LOG_ERR, "CFArrayCreateMutable failed\n");
		exit(1);
	}
	return matchingCFArrayRef;
}

static void initializeResumeNotifications(void) // see TN 2187
{
	IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if (!hidManager)
	{
		message(LOG_ERR, "IOHIDManagerCreate failed\n");
		exit(1);
	}
	if (IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone) != kIOReturnSuccess)
	{
		message(LOG_ERR, "IOHIDManagerOpen failed\n");
		exit(1);
	}
	IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	IOHIDManagerSetDeviceMatchingMultiple(hidManager, createGenericDesktopMatchingDictionaries());
	IOHIDManagerRegisterInputValueCallback(hidManager, hidCallback, (void *)-1);
}

void powerCallback(void *rootPort, io_service_t y, natural_t msgType, void *msgArgument)
{
	int result;
	char *s;

	/*
	fprintf (stderr, "powerCallback: message_type %08lx, arg %08lx\n",
		(long unsigned int) msgType, (long  unsigned int) msgArgument);
*/
	switch (msgType)
	{
	case kIOMessageCanSystemSleep:

		break;
	case kIOMessageSystemWillSleep:

		IOAllowPowerChange(*(io_connect_t *)rootPort, (long)msgArgument);
		break;
	case kIOMessageSystemWillNotSleep:

		break;
	case kIOMessageSystemHasPoweredOn:
		setupIdleTimer();

		break;
	}
}

static void initializePowerNotifications(void)
{
	static io_connect_t rootPort; /* used by powerCallback() via context pointer */

	IONotificationPortRef notificationPort;
	io_object_t notifier;

	rootPort = IORegisterForSystemPower(&rootPort, &notificationPort, powerCallback, &notifier);
	if (!rootPort)
	{
		message(LOG_ERR, "IORegisterForSystemPower failed\n");
		exit(1);
	}
	CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notificationPort), kCFRunLoopDefaultMode);
}

void displayCallback(void *context, io_service_t y, natural_t msgType, void *msgArgument)
{
	static enum { displayOn,
				  displayDimmed,
				  displayOff } state = displayOn;

	/*
	fprintf (stderr, "displayCallback: message_type %08lx, arg %08lx\n",
		(long unsigned int) msgType, (long  unsigned int) msgArgument);
*/
	switch (msgType)
	{
	case kIOMessageDeviceWillPowerOff:
	    onDisplaySleep();
		state++;

		break;
	case kIOMessageDeviceHasPoweredOn:

        onDisplayWakeup();
		state = displayOn;
		break;
	}
}

static void initializeDisplayNotifications(void)
{
	io_service_t displayWrangler;
	IONotificationPortRef notificationPort;
	io_object_t notifier;

	displayWrangler = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceNameMatching("IODisplayWrangler"));
	if (!displayWrangler)
	{
		message(LOG_ERR, "IOServiceGetMatchingService failed\n");
		exit(1);
	}
	notificationPort = IONotificationPortCreate(kIOMasterPortDefault);
	if (!notificationPort)
	{
		message(LOG_ERR, "IONotificationPortCreate failed\n");
		exit(1);
	}
	if (IOServiceAddInterestNotification(notificationPort, displayWrangler, kIOGeneralInterest,
										 displayCallback, NULL, &notifier) != kIOReturnSuccess)
	{
		message(LOG_ERR, "IOServiceAddInterestNotification failed\n");
		exit(1);
	}
	CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notificationPort), kCFRunLoopDefaultMode);
	IOObjectRelease(displayWrangler);
}

#define POWER_SOURCE_ERROR -1  // error, don't assume power source changed
#define POWER_SOURCE_BATTERY 0 // not plugged in, using battery power
#define POWER_SOURCE_AC 1	  // plugged in, using AC power

int getPowerSource(void) // returns one of the three #defines above
{
	int result = POWER_SOURCE_ERROR;

	CFTypeRef info = NULL;
	CFArrayRef powerSources = NULL;
	CFTypeRef source = NULL;
	CFDictionaryRef description = NULL;
	CFStringRef powerSourceState = NULL;

	info = IOPSCopyPowerSourcesInfo();
	if (!info)
		goto ret;
	powerSources = IOPSCopyPowerSourcesList(info);
	if (!powerSources)
		goto ret;
	if (CFArrayGetCount(powerSources) == 0)
		goto ret;
	source = CFArrayGetValueAtIndex(powerSources, 0);
	if (!source)
		goto ret;
	description = IOPSGetPowerSourceDescription(info, source);
	if (!description)
		goto ret;
	powerSourceState = CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
	if (!powerSourceState)
		goto ret;
	result = (CFStringCompare(powerSourceState, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) ? POWER_SOURCE_AC : POWER_SOURCE_BATTERY;
ret:
	if (info)
		CFRelease(info);
	if (powerSources)
		CFRelease(powerSources);
	
	return result;
}

void powerSourceCallback(void *context)
{
	static int oldPowerSource = POWER_SOURCE_ERROR;

	int powerSource = getPowerSource();
	if (powerSource != POWER_SOURCE_ERROR && powerSource != oldPowerSource)
	{

		oldPowerSource = powerSource;
	}
}

static void initializePowerSourceNotifications(void)
{
	CFRunLoopSourceRef source = IOPSNotificationCreateRunLoopSource(powerSourceCallback, NULL);
	if (!source)
	{
		message(LOG_ERR, "IOPSNotificationCreateRunLoopSource failed\n");
		exit(1);
	}
	CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
}

int setupSleepWatcher(int timeout)
{
	idletimeout = timeout;
	setupIdleTimer();
	initializeResumeNotifications();
	initializePowerNotifications();
	initializeDisplayNotifications();
	initializePowerSourceNotifications();
	CFRunLoopRun();
	return (0);
}
