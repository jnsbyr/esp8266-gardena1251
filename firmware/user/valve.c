/*****************************************************************************
 *
 * Copyright (c) 2015-2016 jnsbyr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************
 *
 * project: WLAN control unit for Gardena solenoid irrigation valve no. 1251
 *
 * file:    valve.c
 *
 * created: 24.04.2015
 *
 *****************************************************************************/

#include "valve.h"

#include <eagle_soc.h>
#include <gpio.h>
#include <osapi.h>

#include "esp_time.h"

// time tolerance for scheduling next activity
#define SCHEDULE_TIME_TOLERANCE (SLEEPER_MIN_DOWNTIME + SLEEPER_COMMANDTIME)  // milliseconds

// use manual open duration if activity duration is 0
#define effectiveDuration(d) (d > 0? d : sleeperState->rtcMem.defaultDuration)

typedef struct
{
  uint64 start;
  uint64 end;
  uint32 duration; // milliseconds
} OperationT;

LOCAL uint64 now;
LOCAL struct ets_tm tms;
LOCAL OperationT valveTiming;


#if VALVE_DRIVER_TYPE == 1

// open Gardena latching valve using a 9 V step up converter and a capacitor
//
// GPIO  4 close valve         (push/pull, active high, floating in deep sleep)
// GPIO  5 open valve          (open collector, active low, floating in deep sleep)
// GPIO 13 charge capacitor    (open collector, active low)
// GPIO 15 power supply enable (push/pull, active high, pulldown)

// close valve output
#define CLOSE_VALVE_GPIO_MUX PERIPHS_IO_MUX_GPIO4_U
#define CLOSE_VALVE_GPIO_FUNC FUNC_GPIO4
#define CLOSE_VALVE_GPIO 4

// open valve output works inverted!
#define OPEN_VALVE_GPIO_MUX PERIPHS_IO_MUX_GPIO5_U
#define OPEN_VALVE_GPIO_FUNC FUNC_GPIO5
#define OPEN_VALVE_GPIO 5

// recharging capacitor output works inverted!
#define CAPACITOR_GPIO_MUX PERIPHS_IO_MUX_MTCK_U
#define CAPACITOR_GPIO_FUNC FUNC_GPIO13
#define CAPACITOR_GPIO 13

// enable generator output
#define GENERATOR_GPIO_MUX PERIPHS_IO_MUX_MTDO_U
#define GENERATOR_GPIO_FUNC FUNC_GPIO15
#define GENERATOR_GPIO 15

/**
 * called first at OS init
 */
void ICACHE_FLASH_ATTR valveDriverInit()
{
  // configure GPIO outputs
  PIN_FUNC_SELECT(GENERATOR_GPIO_MUX,   GENERATOR_GPIO_FUNC);
  PIN_FUNC_SELECT(OPEN_VALVE_GPIO_MUX,  OPEN_VALVE_GPIO_FUNC);
  PIN_FUNC_SELECT(CLOSE_VALVE_GPIO_MUX, CLOSE_VALVE_GPIO_FUNC);
  PIN_FUNC_SELECT(CAPACITOR_GPIO_MUX,   CAPACITOR_GPIO_FUNC);

  // configure passive output state
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 0);
  GPIO_DIS_OUTPUT(OPEN_VALVE_GPIO);
  GPIO_OUTPUT_SET(CLOSE_VALVE_GPIO, 0);
  GPIO_DIS_OUTPUT(CAPACITOR_GPIO);
}

/**
 * open valve
 */
LOCAL void ICACHE_FLASH_ATTR valveOpen(SleeperStateT* sleeperState)
{
  // discharge capacitor as much as possible while powering up generator (this may also close valve if still open)
  GPIO_OUTPUT_SET(CLOSE_VALVE_GPIO, 1);

  // start generator
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 1);
  os_delay_us(50000); // 50 ms -> us

  // stop discharging capacitor
  GPIO_OUTPUT_SET(CLOSE_VALVE_GPIO, 0);
  os_delay_us(20); // 20 us

  // open latching valve by charing capacitor
  GPIO_OUTPUT_SET(OPEN_VALVE_GPIO, 0);
  os_delay_us(VALVE_OPEN_PULSE_DURATION); // (250 ms) -> us

  // disable power to valve and disable generator
  GPIO_DIS_OUTPUT(OPEN_VALVE_GPIO);
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 0);

  // update state
  if (!sleeperState->rtcMem.valveOpen)
  {
    sleeperState->rtcMem.valveOpen = true;
    sleeperState->rtcMem.totalOpenCount++;
  }
  sleeperState->rtcMem.valveOpenTime = now;
  ets_uart_printf("valveOpen\r\n");
}

/**
 * close valve
 */
LOCAL void ICACHE_FLASH_ATTR valveClose(SleeperStateT* sleeperState)
{
  // start generator
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 1);
  os_delay_us(1000); // 1 ms -> us

  // recharge capacitor while bypassing valve
  GPIO_OUTPUT_SET(CAPACITOR_GPIO, 0);
  os_delay_us(50000); // 50 ms -> us

  // stop capacitor charging and disable generator
  GPIO_DIS_OUTPUT(CAPACITOR_GPIO);
  os_delay_us(20); // 20 us
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 0);

  // close latching valve by discharging capacitor
  GPIO_OUTPUT_SET(CLOSE_VALVE_GPIO, 1);
  os_delay_us(62500); // 62.5 ms -> us

  // continue discharging capacitor until os shutdown

  // update state
  sleeperState->rtcMem.valveOpen = false;
  if (now > sleeperState->rtcMem.valveOpenTime)
  {
    sleeperState->rtcMem.totalOpenDuration += (now - sleeperState->rtcMem.valveOpenTime)/1000;
  }
  ets_uart_printf("valveClose\r\n");
}

/**
 * called last before OS shutdown
 */
void ICACHE_FLASH_ATTR valveDriverShutdown()
{
  // stop discharging capacitor (and possibly closing valve)
  GPIO_OUTPUT_SET(CLOSE_VALVE_GPIO, 0);
}

#elif VALVE_DRIVER_TYPE == 2

// GPIO  4 valve direction select (push/pull, active high, floating in deep sleep)
// GPIO  5 operate valve          (push/pull, active high, floating in deep sleep)
// GPIO 15 power supply enable    (push/pull, active high, pulldown)

// close valve output
#define OPEN_VALVE_GPIO_MUX PERIPHS_IO_MUX_GPIO4_U
#define OPEN_VALVE_GPIO_FUNC FUNC_GPIO4
#define OPEN_VALVE_GPIO 4

// open valve output works inverted!
#define OPERATE_VALVE_GPIO_MUX PERIPHS_IO_MUX_GPIO5_U
#define OPERATE_VALVE_GPIO_FUNC FUNC_GPIO5
#define OPERATE_VALVE_GPIO 5

// enable generator output
#define GENERATOR_GPIO_MUX PERIPHS_IO_MUX_MTDO_U
#define GENERATOR_GPIO_FUNC FUNC_GPIO15
#define GENERATOR_GPIO 15

/**
 * called first at OS init
 */
void ICACHE_FLASH_ATTR valveDriverInit()
{
  // configure GPIO outputs and passive output state
  PIN_FUNC_SELECT(GENERATOR_GPIO_MUX,     GENERATOR_GPIO_FUNC);
  GPIO_OUTPUT_SET(GENERATOR_GPIO, 0);

  PIN_FUNC_SELECT(OPERATE_VALVE_GPIO_MUX, OPERATE_VALVE_GPIO_FUNC);
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 0);

  PIN_FUNC_SELECT(OPEN_VALVE_GPIO_MUX,    OPEN_VALVE_GPIO_FUNC);
  GPIO_OUTPUT_SET(OPEN_VALVE_GPIO, 0);
}

/**
 * open valve
 */
LOCAL void ICACHE_FLASH_ATTR valveOpen(SleeperStateT* sleeperState)
{
  // start generator, preset valve direction to open and wait for generator voltage to stablelize
  GPIO_OUTPUT_SET(GENERATOR_GPIO,  1);
  GPIO_OUTPUT_SET(OPEN_VALVE_GPIO, 0);  // 0 -> VOUT1 = H
  os_delay_us(5000); // 5 ms -> us

  // open valve by enabling H-bridge
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 1);
  os_delay_us(VALVE_OPEN_PULSE_DURATION); // (250 ms) -> us

  // short circuit valve current
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 0);
  os_delay_us(1000); // 1 ms -> us

  // done, go to passive state
  valveDriverShutdown();

  // update state
  if (!sleeperState->rtcMem.valveOpen)
  {
    sleeperState->rtcMem.valveOpen = true;
    sleeperState->rtcMem.totalOpenCount++;
  }
  sleeperState->rtcMem.valveOpenTime = now;
  ets_uart_printf("valveOpen\r\n");
}

/**
 * close valve
 */
LOCAL void ICACHE_FLASH_ATTR valveClose(SleeperStateT* sleeperState)
{
  // start generator, preset valve direction to close and wait for generator voltage to stablelize
  GPIO_OUTPUT_SET(GENERATOR_GPIO,  1);
  GPIO_OUTPUT_SET(OPEN_VALVE_GPIO, 1); // 1 -> VOUT2 = H
  os_delay_us(5000); // 5 ms -> us

  // close valve by enabling H-bridge
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 1);
  os_delay_us(62500); // 62.5 ms -> us

  // short circuit valve current
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 0);
  os_delay_us(1000); // 1 ms -> us

  // done, go to passive state
  valveDriverShutdown();

  // update state
  sleeperState->rtcMem.valveOpen = false;
  if (now > sleeperState->rtcMem.valveOpenTime)
  {
    sleeperState->rtcMem.totalOpenDuration += (now - sleeperState->rtcMem.valveOpenTime)/1000;
  }
  ets_uart_printf("valveClose\r\n");
}

/**
 * called last before OS shutdown
 */
void ICACHE_FLASH_ATTR valveDriverShutdown()
{
  // passive GPIO output state
  GPIO_OUTPUT_SET(OPERATE_VALVE_GPIO, 0);
  GPIO_OUTPUT_SET(GENERATOR_GPIO,     0);
  GPIO_OUTPUT_SET(OPEN_VALVE_GPIO,    0);
}

#else

#error "selected VALVE_DRIVER_TYPE is not supported, choose 1 (capacitor) or 2 (H-bridge)"

#endif // VALVE_DRIVER_TYPE


/**
 * find index of 1st scheduled activity that matches current time (tms)
 *
 * @todo will not find current activity that runs over midnight GMT after day has changed
 *
 * @return -1 if not found
 */
LOCAL int ICACHE_FLASH_ATTR getCurrentActivity(SleeperStateT* sleeperState)
{
  uint32 minuteOfDay = 60*tms.tm_hour + tms.tm_min;
  uint32 secondOfDay = 60*minuteOfDay + tms.tm_sec;
  for (int i=0; i<MAX_ACTIVITIES; i++)
  {
    ActivityT* activity = &sleeperState->rtcMem.activities[i];
    if (activity->day == DAY_INVALID)
    {
      // found 1st invalid activity, done
      break;
    }
    else if (activity->day == DAY_EVERY || (activity->day == DAY_SECOND && tms.tm_yday%2 == 0) ||
             (activity->day == DAY_THIRD && tms.tm_yday%3 == 0) || (activity->day - DAY_SUNDAY) == tms.tm_wday)
    {
      // found activity for today
      if (minuteOfDay >= activity->startTime && secondOfDay <= (60*activity->startTime + effectiveDuration(activity->duration)))
      {
        // now is between start and end time: match
        return i;
      }
    }
  }

  return -1;
}

/**
 * calculate start, end and duration for current valve operation
 *
 * @param sleeperState
 * @param setMode request mode (MANUAL or AUTO)
 * @param startTime start time for MANUAL, ignored for AUTO
 * @param duration duration for MANUAL, ignored for AUTO
 */
LOCAL uint8 ICACHE_FLASH_ATTR calculateValveTiming(SleeperStateT* sleeperState, uint8 setMode, uint64 startTime, uint16 duration)
{
  valveTiming.start = 0;
  valveTiming.end   = 0;
  valveTiming.duration  = 0;

  switch (setMode)
  {
    case MODE_AUTO:
    {
      // find current activity
      int index = getCurrentActivity(sleeperState);
      if (index >= 0)
      {
        // create start and end times
        ActivityT* activity = &sleeperState->rtcMem.activities[index];
        uint16 hour   = activity->startTime/60;
        uint16 minute = activity->startTime%60;
        tms.tm_hour = hour;
        tms.tm_min  = minute;
        tms.tm_sec  = 0;
        tms.tm_msec = 0;
        valveTiming.duration = 1000UL*effectiveDuration(activity->duration);
        valveTiming.start    = esp_mktime(&tms);
        valveTiming.end      = valveTiming.start + valveTiming.duration;
      }
      break;
    }

    case MODE_MANUAL:
    {
      valveTiming.duration = 1000UL*duration;
      valveTiming.start    = startTime;
      valveTiming.end      = valveTiming.start + valveTiming.duration;
      break;
    }
  }

  return valveTiming.duration > 0;
}

/**
 * calculate end of override
 */
LOCAL uint64 ICACHE_FLASH_ATTR getOverrideEndTime(SleeperStateT* sleeperState, uint8 setMode, uint64 startTime)
{
  uint64 overrideEndTime = 0;

  // calculate end of override
  if (setMode != MODE_OFF)
  {
    // AUTO or MANUAL mode, check planned end time
    if (calculateValveTiming(sleeperState, setMode, startTime, sleeperState->rtcMem.defaultDuration))
    {
      if (valveTiming.start <= now && valveTiming.end >= now)
      {
        // regular activity would be in progress, block until end of activity
        overrideEndTime = valveTiming.end  + SCHEDULE_TIME_TOLERANCE;
      }
    }
  }

  return overrideEndTime;
}

/**
 * get start time of next scheduled activity
 * @return 0 if not found
 */
LOCAL uint64 ICACHE_FLASH_ATTR getNextActivityStart(SleeperStateT* sleeperState)
{
  uint32 minuteOfDay = 60*tms.tm_hour + tms.tm_min;
  uint32 minutesTillStart = MINUTES_PER_DAY;
  for (int i=0; i<MAX_ACTIVITIES; i++)
  {
    ActivityT* activity = &sleeperState->rtcMem.activities[i];
    //ets_uart_printf("valve: A checking next day %u minute %u: day %u minute %u\r\n", tms.tm_wday, minuteOfDay, activity->day, activity->startTime);
    if (activity->day == DAY_INVALID)
    {
      // found 1st invalid activity, done
      break;
    }
    else if (activity->day == DAY_EVERY || (activity->day == DAY_SECOND && tms.tm_yday%2 == 0) ||
             (activity->day == DAY_THIRD && tms.tm_yday%3 == 0) || (activity->day - DAY_SUNDAY) == tms.tm_wday)
    {
      // found activity for today
      if (minuteOfDay < activity->startTime)
      {
        // found activity not jet started
        uint32 delta = activity->startTime - minuteOfDay;
        if (delta < minutesTillStart)
        {
          // found activity that starts earlier
          //ets_uart_printf("valve: A checking next: %u minutes\r\n", delta);
          minutesTillStart = delta;
        }
      }
    }
  }
  if (minutesTillStart < MINUTES_PER_DAY)
  {
    // found activity for today
    return getTime() + 60000UL*minutesTillStart; // milliseconds
  }

  // nothing found for today, check tomorrow because tomorrow may be only a few seconds away
  uint32 nextWday = (tms.tm_wday + 1) % 7;
  for (int i=0; i<MAX_ACTIVITIES; i++)
  {
    ActivityT* activity = &sleeperState->rtcMem.activities[i];
    //ets_uart_printf("valve: B checking next day %u: day %u minute %u\r\n", nextWday, activity->day, activity->startTime);
    if (activity->day == DAY_INVALID)
    {
      // found 1st invalid activity, done
      break;
    }
    else if (activity->day == DAY_EVERY || (activity->day == DAY_SECOND && tms.tm_yday%2 == 0) ||
             (activity->day == DAY_THIRD && tms.tm_yday%3 == 0) || (activity->day - DAY_SUNDAY) == nextWday)
    {
      // found activity for tomorrow
      if (activity->startTime < minutesTillStart)
      {
        // found activity that starts earlier
        //ets_uart_printf("valve: B checking next: OK\r\n");
        minutesTillStart = activity->startTime;
      }
    }
  }
  if (minutesTillStart < MINUTES_PER_DAY)
  {
    // found activity for tomorrow
    return getTime() + 60000UL*(MINUTES_PER_DAY - minuteOfDay + minutesTillStart); // milliseconds
  }

  // found nothing
  return 0;
}

/**
 * operate valve in AUTO or MANUAL mode
 *
 * @parm fallback true if valve was closed (or kept close) to fall back to previous mode (MANUAL -> AUTO/OFF)
 * @return next event time
 */
LOCAL uint64 ICACHE_FLASH_ATTR operateValve(SleeperStateT* sleeperState, uint8* fallback)
{
  ets_uart_printf("operateValve\r\n");

  uint64 nextEventTime = 0;

  // operate valve
  if (!sleeperState->rtcMem.valveOpen)
  {
    if (now < valveTiming.start)
    {
      // waiting for start time (never start early)
      ets_uart_printf("valve: waiting for start time\r\n");
      nextEventTime = valveTiming.start;
    }
    else if (now < (valveTiming.end + SCHEDULE_TIME_TOLERANCE))
    {
      // start time reached but not end time: open valve and calculate actual end time
      ets_uart_printf("valve: start time reached\r\n");
      valveOpen(sleeperState);
      sleeperState->rtcMem.valveCloseTime = now + valveTiming.duration;
      sleeperState->rtcMem.valveCloseTimeEstimated = !sleeperState->timeSynchronized;
      nextEventTime = sleeperState->rtcMem.valveCloseTime;
    }
    else
    {
      // too late: keep valve closed
      ets_uart_printf("valve: too late\r\n");
      *fallback = true;
    }
  }
  else
  {
    if (now < valveTiming.start && sleeperState->rtcMem.mode == MODE_MANUAL)
    {
      // next start time not reached: abort manual, close valve
      ets_uart_printf("valve: start time not reached\r\n");
      valveClose(sleeperState);
      nextEventTime = valveTiming.start;
    }
    else if (now >= sleeperState->rtcMem.valveCloseTime)
    {
      // end time reached: close valve (never stop early)
      ets_uart_printf("valve: end time reached\r\n");
      valveClose(sleeperState);
      sleeperState->rtcMem.valveCloseTime = 0;
      *fallback = true;
    }
    else
    {
      // start time reached: keep valve open
      nextEventTime = sleeperState->rtcMem.valveCloseTime;
    }
  }

  return nextEventTime;
}

/**
 * start manual:     mode OFF/AUTO   -> command MANUAL -> mode MANUAL -> mode OFF/AUTO
 * stop manual:      mode MANUAL     -> command OFF
 *
 * start auto:       mode OFF/MANUAL -> command AUTO
 * suspend auto:     mode AUTO       -> command OFF
 *
 * @return next event time or 0 if no next event is pending
 */
uint64 ICACHE_FLASH_ATTR valveControl(SleeperStateT* sleeperState, uint8 override, uint8 setMode, uint64 startTime)
{
  uint64 nextEventTime = 0;

  now = getTime();
  esp_gmtime(&now, &tms);

  if (sleeperState->batteryVoltage < MIN_BATTERY_VOLTAGE)
  {
    // priority 1: low battery
    sleeperState->rtcMem.lowBattery = true;
    if (sleeperState->rtcMem.valveOpen)
    {
      // valve still open, close valve immediately
      valveClose(sleeperState);
      sleeperState->rtcMem.valveCloseTime = 0;
    }
  }
  else
  {
    // battery voltage above threshold
    sleeperState->rtcMem.lowBattery = false;
    if (override)
    {
      ets_uart_printf("valveControl: manual override\r\n");

      // priority 2: manual override
      if (!sleeperState->rtcMem.override)
      {
        // override initiated, backup current mode
        sleeperState->rtcMem.overriddenMode = sleeperState->rtcMem.mode;
      }

      // toggle valve state
      if (sleeperState->rtcMem.valveOpen)
      {
        // close valve immediately
        valveClose(sleeperState);
        sleeperState->rtcMem.valveCloseTime = 0;
        sleeperState->rtcMem.overrideEndTime = getOverrideEndTime(sleeperState, sleeperState->rtcMem.overriddenMode, startTime);
        sleeperState->rtcMem.overrideEndTimeEstimated = !sleeperState->timeSynchronized;
        if (now <= sleeperState->rtcMem.overrideEndTime)
        {
          // valve is closed but override has not jet ended, keep waiting
          nextEventTime = sleeperState->rtcMem.overrideEndTime;
          sleeperState->rtcMem.override = true;
        }
        else
        {
          // valve is closed and override has ended, unlock
          sleeperState->rtcMem.override = false;

          // restore previous mode but do not operate on restored mode because it will be done anyway in same cycle
          sleeperState->rtcMem.mode = sleeperState->rtcMem.overriddenMode;
        }
      }
      else
      {
        // open valve immediately using manual mode
        sleeperState->rtcMem.override = false;
        nextEventTime = valveControl(sleeperState, false, MODE_MANUAL, startTime);
        sleeperState->rtcMem.override = true;
        sleeperState->rtcMem.overrideEndTime = 0; // must be set when closing valve
      }
    }
    else if (sleeperState->rtcMem.override)
    {
      ets_uart_printf("valveControl: override mode\r\n");

      // override operation in progress
      if (sleeperState->rtcMem.valveOpen)
      {
        // maintain manual mode until valve is closed
        sleeperState->rtcMem.override = false;
        nextEventTime = valveControl(sleeperState, false, MODE_MANUAL, 0);
        sleeperState->rtcMem.override = true;
      }
      if (!sleeperState->rtcMem.valveOpen)
      {
        // valve is closed, calculate end of override time
        if (sleeperState->rtcMem.overrideEndTime == 0)
        {
          // define end of override depending on set mode and current activity
          sleeperState->rtcMem.overrideEndTime = getOverrideEndTime(sleeperState, sleeperState->rtcMem.overriddenMode, startTime);
          sleeperState->rtcMem.overrideEndTimeEstimated = !sleeperState->timeSynchronized;
        }
        if (now <= sleeperState->rtcMem.overrideEndTime && setMode != MODE_OFF)
        {
          // valve is closed but override has not jet ended and requested mode is not OFF, keep waiting
          nextEventTime = sleeperState->rtcMem.overrideEndTime;
        }
        else
        {
          // valve is closed and override has ended or requested mode is OFF, unlock
          ets_uart_printf("valve: override end time reached\r\n");
          sleeperState->rtcMem.override = false;

          // activate set mode and immediately reexecute valve control
          sleeperState->rtcMem.mode = setMode;
          nextEventTime = valveControl(sleeperState, false, sleeperState->rtcMem.mode, startTime);
        }
      }
    }
    else
    {
      ets_uart_printf("valveControl: remote mode\r\n");

      // no override operation pending: remote operation
      switch (setMode)
      {
        case MODE_AUTO:
        {
          if (calculateValveTiming(sleeperState, MODE_AUTO, 0, 0))
          {
            // operate valve
            uint8 fallback;
            nextEventTime = operateValve(sleeperState, &fallback);
          }
          else
          {
            // no new activity found, finalize pending activity
            if (sleeperState->rtcMem.valveOpen)
            {
              if (now >= sleeperState->rtcMem.valveCloseTime)
              {
                // end time reached: close valve
                ets_uart_printf("valve: end time reached\r\n");
                valveClose(sleeperState);
                sleeperState->rtcMem.valveCloseTime = 0;
              }
              else
              {
                // start time reached: keep valve open
                nextEventTime = sleeperState->rtcMem.valveCloseTime;
              }
            }
          }
          sleeperState->rtcMem.mode = MODE_AUTO;
          break;
        }

        case MODE_MANUAL:
        {
          // manual mode: abort auto program, wait for start and enable valve for duration
          uint8 fallback = false;
          calculateValveTiming(sleeperState, MODE_MANUAL, startTime, sleeperState->rtcMem.defaultDuration);
          nextEventTime = operateValve(sleeperState, &fallback);
          if (!sleeperState->rtcMem.override)
          {
            if (!fallback)
            {
              // manual operation start
              if (sleeperState->rtcMem.mode == MODE_OFF || sleeperState->rtcMem.mode == MODE_AUTO)
              {
                // remember previous mode
                sleeperState->rtcMem.offMode = sleeperState->rtcMem.mode;
              }
              sleeperState->rtcMem.mode = MODE_MANUAL;
            }
            else
            {
              // manual operation completed, fallback to previous mode
              sleeperState->rtcMem.mode = sleeperState->rtcMem.offMode;
            }
          }
          break;
        }

        case MODE_OFF:
          if (sleeperState->rtcMem.valveOpen)
          {
            ets_uart_printf("valve: shutting down\r\n");
            valveClose(sleeperState);
            sleeperState->rtcMem.valveCloseTime = 0;
          }
          sleeperState->rtcMem.mode = MODE_OFF;
          break;
      }
    }
  }

  if (nextEventTime == 0 && !sleeperState->rtcMem.lowBattery && !sleeperState->rtcMem.override && sleeperState->rtcMem.mode != MODE_OFF)
  {
    // operational but nothing to do, try find next activity
    nextEventTime = getNextActivityStart(sleeperState);
  }

  ets_uart_printf("valve: now %lld next %lld\r\n", now, nextEventTime);

  return nextEventTime;
}




