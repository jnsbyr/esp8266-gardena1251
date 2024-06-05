/*****************************************************************************
 *
 * Copyright (c) 2015-2019 jnsbyr
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
 * file:    main.c
 *
 * created: 11.04.2015
 *
 *
 * This application is intended for an Espressif ESP8266 SoC to control the
 * Gardena solenoid irrigation valve no. 1251 via WLAN. The management
 * network protocol uses JSON for all telegrams.
 *
 *****************************************************************************
 *
 * WARNING:
 *
 * Because this project is geared to be used with a latching solenoid
 * irrigation valve there exists an inherent danger of flooding in case of
 * control unit operation errors or misconfiguration. The valve is not
 * intrinsically safe because it will not close by itself when power fails.
 * On the other hand the WLAN control unit cannot "see" the true state of the
 * valve and cannot take countermeasures in case of a discrepancy.
 *
 * Therefore: Using this project is COMPLETELY AT YOUR OWN RISK!
 *
 *****************************************************************************
 *
 * @todo improve estimate of totalOpenDuration in manual override mode
 * @todo config of access point parameters in AP mode on initial startup (via mini-webserver?) -> flash
 * @todo UDP service advertisement broadcast to receive config for server/port
 * @todo reset flash config (via GPIO? e.g. very long press?)
 * @todo make UART debug output optional
 * @todo support modification of runtime while valve is open
 *
 *****************************************************************************/

#include "main.h"

#include <eagle_soc.h>
#include <gpio.h>
#include <osapi.h>
#include <user_interface.h>
#include <version.h>
#include <json/jsonparse.h>
#include "esp_time.h"
#include "adc.h"
#include "valve.h"
#include "uplink.h"

#define VERSION SLEEPER_VERSION

// manual start/stop GPIO input
#define USER_WAKEUP_GPIO_MUX PERIPHS_IO_MUX_MTMS_U
#define USER_WAKEUP_GPIO_FUNC FUNC_GPIO14
#define USER_WAKEUP_GPIO 14

// ESP8266 deep sleep options
#define RF_DEFAULT  0 // RF calibration after deep-sleep depends on init data byte 108
#define RF_CAL      1 // RF calibration after deep-sleep
#define RF_NO_CAL   2 // no RF calibration after deep-sleep
#define RF_DISABLED 4 // no RF after deep-sleep

// variables
LOCAL os_timer_t comTimer;
LOCAL int32 comTimeout;
LOCAL SleeperStateT state;
LOCAL struct ets_tm tms;
LOCAL struct ets_tm nowTMS;
LOCAL uint8 uplinkSocketConnected;
LOCAL uint8 statusSent;
LOCAL uint8 readyForShutdown;
LOCAL char txMessage[288];
LOCAL uint64 nextEventTime;

/**
 * estimate current time in milliseconds
 */
uint64 getTime()
{
  return state.rtcMem.lastShutdownTime + state.rtcMem.lastDowntime + state.rtcMem.boottime + system_get_time()/1000;
}

LOCAL const char* ICACHE_FLASH_ATTR getSleeperModeAsText()
{
  if (state.rtcMem.lowBattery)
  {
    return "LOW BAT";
  }
  else if (state.rtcMem.lastValveOperationStatus != VALVE_STATUS_OK)
  {
    switch (state.rtcMem.lastValveOperationStatus)
    {
      case VALVE_STATUS_BAD_WIRING:        return "BAD VALVE WIRING";
      case VALVE_STATUS_LOW_OPEN_VOLTAGE:  return "LOW OPEN VOLTAGE";
      case VALVE_STATUS_LOW_CLOSE_VOLTAGE: return "LOW CLOSE VOLTAGE";
      default:                             return "UNDEFINED VALVE STATUS";
    }
  }
  else if (state.rtcMem.override)
  {
    return "OVERRIDE";
  }
  else
  {
    switch (state.rtcMem.mode)
    {
      case MODE_OFF:         return "OFF";
      case MODE_MANUAL:      return "MANUAL";
      case MODE_AUTO:        return "AUTO";
      default:               return "UNDEFINED MODE";
    }
  }
}

/**
 * check for external reset
 */
LOCAL uint8 isUserWakeup()
{
  // rstInfo->reason will always be REASON_DEEP_SLEEP_AWAKE
  PIN_FUNC_SELECT(USER_WAKEUP_GPIO_MUX, USER_WAKEUP_GPIO_FUNC);
  return GPIO_INPUT_GET(USER_WAKEUP_GPIO) == 0;
}

LOCAL void parseReply(char* reply, uint8* mode, void* start)
{
  uint32 rxTime = system_get_time();
  ets_uart_printf("TCP received reply at %lu ms: %s\r\n", rxTime/1000, reply);

  uint64* startTime = (uint64*)start;
  uint64 serverTime = 0;
  uint16 activityCount = MAX_ACTIVITIES; // prevent clearing of current activities
  uint8 setTime = state.rtcMem.lastShutdownTime < 946684800000; // invalid if before 01.01.2000;

  int type;
  struct jsonparse_state jsonParser;
  jsonparse_setup(&jsonParser, reply, uplink_getReplySize());
  while ((type = jsonparse_next(&jsonParser)) != JSON_TYPE_ERROR)
  {
    char buffer[64];
    os_bzero(buffer, 64);

    if (type == JSON_TYPE_PAIR_NAME)
    {
      if (jsonparse_strcmp_value(&jsonParser, "time") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        jsonparse_copy_value(&jsonParser, buffer, sizeof(buffer));
        if (esp_strptime(buffer, NULL, &tms))
        {
          serverTime = esp_mktime(&tms);
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "timeOffset") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int timeOffset = jsonparse_get_value_as_int(&jsonParser); // milliseconds
        if (timeOffset >= -500 && timeOffset <= 500)
        {
          setTime = setTime || timeOffset != state.rtcMem.boottime;
          state.rtcMem.boottime = timeOffset; // milliseconds
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "setTime") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int set = jsonparse_get_value_as_int(&jsonParser);
        if (set >= 0 && set <= 1)
        {
          // sync time if requested or if time is invalid (after cold boot)
          setTime = setTime || set;
          //ets_uart_printf("JSON setTime %d shutdown time %llu => %u\r\n", set, state.rtcMem.lastShutdownTime, setTime);
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "wakeup") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int downtime = jsonparse_get_value_as_int(&jsonParser); // seconds
        if (downtime > 0 && downtime <= 3600)
        {
          state.rtcMem.downtime = 1000*downtime; // milliseconds
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "mode") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        jsonparse_copy_value(&jsonParser, buffer, sizeof(buffer));
        if (os_strcmp(buffer, "AUTO") == 0)
        {
          *mode = MODE_AUTO;
        }
        else if (os_strcmp(buffer, "MANUAL") == 0)
        {
          *mode = MODE_MANUAL;
        }
        else if (os_strcmp(buffer, "OFF") == 0)
        {
          *mode = MODE_OFF;
        }
        else
        {
          // keep mode unchanged
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "start") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        jsonparse_copy_value(&jsonParser, buffer, sizeof(buffer));
        if (esp_strptime(buffer, NULL, &tms))
        {
          *startTime = esp_mktime(&tms);
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "duration") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int duration = jsonparse_get_value_as_int(&jsonParser); // seconds
        if (duration > 0 && duration <= 7200)
        {
          state.rtcMem.defaultDuration = duration;
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "timeScale") == 0)
      {
        jsonparse_next(&jsonParser);
        type = jsonparse_next(&jsonParser);
        uint8 negative = false;
        if (type == JSON_TYPE_ERROR)
        {
          // workaround: jsonparse_next is unable to handle sign, assume negative
          negative = true;
          type = jsonparse_next(&jsonParser);
        }
        int timeScale = negative? -jsonparse_get_value_as_int(&jsonParser) : jsonparse_get_value_as_int(&jsonParser);
        if (timeScale >= -1000 && timeScale <= 1000)
        {
          timeScale += 10000; // 10000 = 1.0
          setTime = setTime || timeScale != state.rtcMem.downtimeScale;
          state.rtcMem.downtimeScale = timeScale; //
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "voltageOffset") == 0)
      {
        jsonparse_next(&jsonParser);
        type = jsonparse_next(&jsonParser);
        uint8 negative = false;
        if (type == JSON_TYPE_ERROR)
        {
          // workaround: jsonparse_next is unable to handle sign, assume negative
          negative = true;
          type = jsonparse_next(&jsonParser);
        }
        int voltageOffset = negative? -jsonparse_get_value_as_int(&jsonParser) : jsonparse_get_value_as_int(&jsonParser);
        if (voltageOffset != state.rtcMem.batteryOffset && voltageOffset >= -500 && voltageOffset <= 500)
        {
          // adjust battery level and save new offset
          state.batteryVoltage -= state.rtcMem.batteryOffset;
          state.rtcMem.batteryOffset = voltageOffset; // [mV]
          state.batteryVoltage += state.rtcMem.batteryOffset;

          // low battery check
          state.rtcMem.lowBattery = state.batteryVoltage < MIN_BATTERY_VOLTAGE;
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "maxResistance") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int maxResistance = jsonparse_get_value_as_int(&jsonParser);
        if (maxResistance != state.rtcMem.maxValveResistance && maxResistance > 0)
        {
          state.rtcMem.maxValveResistance = maxResistance;
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "programId") == 0)
      {
        jsonparse_next(&jsonParser);
        jsonparse_next(&jsonParser);
        int programId = jsonparse_get_value_as_int(&jsonParser);
        if (programId != state.rtcMem.activityProgramId && programId >= 0)
        {
          state.rtcMem.activityProgramId = programId;
          activityCount = 0; // when program id changes new activities must be supplied - otherwise old activities will be cleared
        }
      }
      else if (jsonparse_strcmp_value(&jsonParser, "activities") == 0 && state.rtcMem.activityProgramId > 0 && activityCount == 0)
      {
        jsonparse_next(&jsonParser);
        if (jsonparse_next(&jsonParser) == JSON_TYPE_ARRAY)
        {
          // start of activity array
          uint8 activityDay;
          uint16 activityStart;
          uint16 activityDuration;
          do
          {
            if ((type = jsonparse_next(&jsonParser)) == JSON_TYPE_OBJECT)
            {
              // start of new activity object
              activityDay = DAY_INVALID;
              activityStart = 0;
              activityDuration = 0;
              do
              {
                if ((type = jsonparse_next(&jsonParser)) == JSON_TYPE_PAIR_NAME)
                {
                  if (jsonparse_strcmp_value(&jsonParser, "day") == 0)
                  {
                    jsonparse_next(&jsonParser);
                    jsonparse_next(&jsonParser);
                    if (jsonparse_strcmp_value(&jsonParser, "all") == 0)
                    {
                      activityDay = DAY_EVERY; // every day
                    }
                    else if (jsonparse_strcmp_value(&jsonParser, "2nd") == 0)
                    {
                      activityDay = DAY_SECOND; // every 2nd day
                    }
                    else if (jsonparse_strcmp_value(&jsonParser, "3rd") == 0)
                    {
                      activityDay = DAY_THIRD; // every 3nd day
                    }
                    else
                    {
                      int wday = jsonparse_get_value_as_int(&jsonParser);
                      if (wday >= 0 && wday <= 6)
                      {
                        activityDay = DAY_SUNDAY + wday; // 0 = Sunday -> 2, 1 = Monday -> 3 ...
                      }
                    }
                  }
                  else if (jsonparse_strcmp_value(&jsonParser, "start") == 0)
                  {
                    jsonparse_next(&jsonParser);
                    jsonparse_next(&jsonParser);
                    jsonparse_copy_value(&jsonParser, buffer, sizeof(buffer));
                    if (esp_strptime(buffer, NULL, &tms))
                    {
                      activityStart = 60*tms.tm_hour + tms.tm_min;
                    }
                    else
                    {
                      activityDay = DAY_INVALID;
                    }
                  }
                  else if (jsonparse_strcmp_value(&jsonParser, "duration") == 0)
                  {
                    jsonparse_next(&jsonParser);
                    jsonparse_next(&jsonParser);
                    int d = jsonparse_get_value_as_int(&jsonParser); // seconds
                    if (d >= 0 && d <= 3600)
                    {
                      activityDuration = d;
                    }
                    else
                    {
                      activityDay = DAY_INVALID;
                    }
                  }
                }
              } while (type != JSON_TYPE_ERROR && type != '}'); // end of object or error
              ets_uart_printf("JSON activity: day %u minute %u duration %u\r\n", activityDay, activityStart, activityDuration);
              if (type == '}' && activityDay > DAY_INVALID && activityDuration > 0 && activityCount < MAX_ACTIVITIES)
              {
                // add activity to state
                ActivityT* activity = &state.rtcMem.activities[activityCount];
                activity->day       = activityDay;
                activity->startTime = activityStart;
                activity->duration  = activityDuration;
                activityCount++;
              }
            }
          } while (type != JSON_TYPE_ERROR && type != ']'); // end of array or error
        }
      }
    }
  }

  // mark all remaining activity slots as invalid
  for (uint16 i = activityCount; i < MAX_ACTIVITIES; i++)
  {
    ActivityT* activity = &state.rtcMem.activities[i];
    activity->day       = DAY_INVALID;
  }

  // synchronize time if sync is requested
  if (serverTime > 0)
  {
    if (setTime) // || state.rtcMem.override || *startTime >= serverTime || (*startTime + 1000UL*state.rtcMem.manualDuration <= serverTime)))
    {
      // fix last shutdown time
      uint64 lastShutdownTime = state.rtcMem.lastShutdownTime;
      state.rtcMem.lastShutdownTime = serverTime - (rxTime/1000 + state.rtcMem.lastDowntime + state.rtcMem.boottime);
      state.timeSynchronized = true;
      //ets_uart_printf("time synchronized\r\n");

      // fix valve close time
      if (state.rtcMem.valveCloseTimeEstimated && state.rtcMem.valveCloseTime > 0)
      {
        if (state.rtcMem.lastShutdownTime >= lastShutdownTime)
        {
          // time advanced
          state.rtcMem.valveCloseTime += state.rtcMem.lastShutdownTime - lastShutdownTime;
        }
        else
        {
          // time reversed
          state.rtcMem.valveCloseTime -= lastShutdownTime - state.rtcMem.lastShutdownTime;
        }
        state.rtcMem.valveCloseTimeEstimated = false;
      }

      // fix override end time
      if (state.rtcMem.overrideEndTimeEstimated && state.rtcMem.overrideEndTime > 0)
      {
        if (state.rtcMem.lastShutdownTime >= lastShutdownTime)
        {
          // time advanced
          state.rtcMem.overrideEndTime += state.rtcMem.lastShutdownTime - lastShutdownTime;
        }
        else
        {
          // time reversed
          state.rtcMem.overrideEndTime -= lastShutdownTime - state.rtcMem.lastShutdownTime;
        }
        state.rtcMem.overrideEndTimeEstimated = false;
      }

      // fix low battery time
      if (state.rtcMem.lowBatteryTimeEstimated && state.rtcMem.lowBatteryTime > 0)
      {
        if (state.rtcMem.lastShutdownTime >= lastShutdownTime)
        {
          // time advanced
          state.rtcMem.lowBatteryTimeEstimated += state.rtcMem.lastShutdownTime - lastShutdownTime;
        }
        else
        {
          // time reversed
          state.rtcMem.lowBatteryTimeEstimated -= lastShutdownTime - state.rtcMem.lastShutdownTime;
        }
        state.rtcMem.lowBatteryTimeEstimated = false;
      }
    }
  }
}

/**
 * trigger immediate host communication processing in timer context
 * note: directly calling comTimerCallback e.g. from socket context
 *       prevents required idle processing
 */
void comProcessing()
{
  os_timer_disarm(&comTimer);
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
  os_timer_arm(&comTimer, 1, false);
#else
  os_timer_arm(&comTimer, 1, NULL);
#endif
}

/**
 * host communication processing
 * - wait for AP connect
 * - connect to host
 * - send current state to host
 * - wait for host command
 * - send new state to host
 * - enter deep sleep mode
 */
LOCAL void comTimerCallback(void *arg)
{
  os_timer_disarm(&comTimer);

  //ets_uart_printf("comTimerCallback uptime %lu ms\r\n", system_get_time()/1000);

  // log WLAN station connect status
  uint8 wlanConnecting = false;
  if (!uplinkSocketConnected)
  {
    switch (wifi_station_get_connect_status())
    {
      case STATION_GOT_IP:
        if (state.rtcMem.ipConfig.ip.addr)
        {
          state.rssi = wifi_station_get_rssi();
          ets_uart_printf("IP up after %lu ms, RSSI %d dB\r\n", system_get_time()/1000, state.rssi);
        }
        else
        {
          // save DHCP IP address (but clear gateway)
          if (wifi_get_ip_info(STATION_IF, &state.rtcMem.ipConfig))
          {
            ets_uart_printf("DHCP got IP " IPSTR " after %lu ms, RSSI %d dB\r\n", IP2STR(&state.rtcMem.ipConfig.ip), system_get_time()/1000, wifi_station_get_rssi());
            state.rtcMem.ipConfig.gw.addr = 0;

            // disable WLAN DHCP client
            ets_uart_printf("disabling WLAN station DHCP client\r\n");
            if (!wifi_station_dhcpc_stop())
            {
              ets_uart_printf("ERROR: disabling WLAN station DHCP client failed\r\n");
            }
          }
          else
          {
            ets_uart_printf("ERROR: getting DHCP IP address failed\r\n");
            state.rtcMem.ipConfig.ip.addr = 0;
          }
        }

        // convert end timestamp of manual override
        esp_gmtime(&state.rtcMem.overrideEndTime, &tms);

        // estimate current time
        state.now = getTime();
        esp_gmtime(&state.now, &nowTMS);

        // create and send TCP request
        os_sprintf(txMessage, "{\"name\":\"SleeperRequest\", \"version\":\"%s%c\", \"time\":\"%u-%02u-%02uT%02u:%02u:%02u.%03uZ\", \"overrideEnd\":\"%u-%02u-%02uT%02u:%02u:%02u.%03uZ\", \"mode\":\"%s\", \"state\":\"%s\", \"programId\":%lu, \"opened\":%u, \"totalOpen\":%lu, \"resistance\":%u, \"voltage\":%d, \"RSSI\":%d}",
                              VERSION, VALVE_DRIVER_TYPE==2? 'H' : 'C',
                              1900 + nowTMS.tm_year, 1 + nowTMS.tm_mon, nowTMS.tm_mday, nowTMS.tm_hour, nowTMS.tm_min, nowTMS.tm_sec, nowTMS.tm_msec,
                              1900 + tms.tm_year, 1 + tms.tm_mon, tms.tm_mday, tms.tm_hour, tms.tm_min, tms.tm_sec, tms.tm_msec,
                              getSleeperModeAsText(),
                              state.rtcMem.valveOpen? "ON" : "OFF",
                              state.rtcMem.activityProgramId,
                              state.rtcMem.totalOpenCount,
                              state.rtcMem.totalOpenDuration,
                              state.rtcMem.valveResistance,
                              state.batteryVoltage,
                              state.rssi);
        uplink_sendRequest(REMOTE_IP, REMOTE_PORT, txMessage);

        // update state and wait for TCP reply
        uplinkSocketConnected  = true;
        comTimeout = MAX_UPLINK_TIME;
        break;

      case STATION_WRONG_PASSWORD:
        ets_uart_printf("ERROR: WLAN wrong password, aborting\r\n");
        comTimeout = 0;
        break;

      case STATION_NO_AP_FOUND:
        ets_uart_printf("ERROR: WLAN AP not found, aborting\r\n");
        comTimeout = 0;
        break;

      case STATION_CONNECT_FAIL:
        ets_uart_printf("ERROR: WLAN connect failed, aborting\r\n");
        comTimeout = 0;
        break;

      default:
        // waiting for AP connect
        ets_uart_printf(".");
        wlanConnecting = true;
        comTimeout -= WLAN_TIMER_PERIOD;
    }
  }
  else
  {
    // connected to AP, communicating via TCP/IP
    comTimeout -= UPLINK_TIMER_PERIOD;
  }

  if (wlanConnecting && comTimeout > 0)
  {
    // passive wait for WLAN link to AP
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
    os_timer_arm(&comTimer, WLAN_TIMER_PERIOD, false);
#else
    os_timer_arm(&comTimer, WLAN_TIMER_PERIOD, NULL);
#endif
  }
  else if (!readyForShutdown)
  {
    // WLAN link to AP established or timeout
    if (!uplink_hasReceived() && comTimeout > 0)
    {
      // passive wait for TCP reply
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
      os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, false);
#else
      os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, NULL);
#endif
    }
    else if (statusSent)
    {
      // wait for status to be sent and connection to be closed or timeout
      if (uplink_isClosed() || comTimeout <= 0)
      {
        // status sent and socket closed or timeout, shutdown
        readyForShutdown = true;
      }
      else
      {
        // keep waiting for confirmation or timeout
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
        os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, false);
#else
        os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, NULL);
#endif
      }
    }
    else
    {
      // process TCP reply or reply receive timeout
      uint8 mode = state.rtcMem.mode;
      uint64 start = 0;

      char* reply = (char*)uplink_getReply();
      if (reply[0])
      {
        // reply received, parse (takes about 30 ms)
        parseReply(reply, &mode, &start);
        //ets_uart_printf("JSON parsing reply completed at %lu ms\r\n", system_get_time()/1000);
      }
      else if (wlanConnecting)
      {
        // WLAN link timeout
        ets_uart_printf("ERROR: WLAN connect timeout\r\n");
      }
      else
      {
        // TCP reply timeout
        ets_uart_printf("ERROR: TCP reply timeout\r\n");
      }

      // operate valve
      nextEventTime = valveControl(&state, mode, start, false, false);

      if (reply[0])
      {
        // reply received, create and send TCP status message
        state.now = getTime();
        esp_gmtime(&state.now, &nowTMS);
        os_sprintf(txMessage, "{\"name\":\"SleeperStatus\", \"time\":\"%u-%02u-%02uT%02u:%02u:%02u.%03uZ\", \"mode\":\"%s\", \"state\":\"%s\", \"programId\":%lu, \"opened\":%u,  \"totalOpen\":%lu, \"voltage\":%d}",
                              1900 + nowTMS.tm_year, 1 + nowTMS.tm_mon, nowTMS.tm_mday, nowTMS.tm_hour, nowTMS.tm_min, nowTMS.tm_sec, nowTMS.tm_msec,
                              getSleeperModeAsText(),
                              state.rtcMem.valveOpen? "ON" : "OFF",
                              state.rtcMem.activityProgramId,
                              state.rtcMem.totalOpenCount,
                              state.rtcMem.totalOpenDuration,
                              state.batteryVoltage);
        uplink_sendMessage(txMessage);

        // passive wait for TCP transmit and disconnect confirmation
        statusSent = true;
        comTimeout = UPLINK_TIMER_PERIOD; // limit max. time for status transmission and socket close to one timer period
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
        os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, false);
#else
        os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, NULL);
#endif
      }
      else
      {
        // no reply, skip sending status and close uplink
        if (!uplink_isClosed())
        {
          uplink_close();

          // passive wait for disconnect confirmation
          statusSent = true;
          comTimeout = UPLINK_TIMER_PERIOD; // limit max. time for socket close to one timer period
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
          os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, false);
#else
          os_timer_arm(&comTimer, UPLINK_TIMER_PERIOD, NULL);
#endif
        }
        else
        {
          // uplink already closed, shutdown
          readyForShutdown = true;
        }
      }
    }
  }

  // ready for shutdown
  if (readyForShutdown)
  {
    //ets_uart_printf("Sleeper preparing for shutdown at %lu ms\r\n", system_get_time()/1000);

    // check uplink connection
    if (!uplink_isClosed())
    {
      ets_uart_printf("ERROR: TCP connection still open\r\n");
    }

    // explicitly shutdown WLAN early to prevent sporadically increased quiescent current
    // wifi_station_disconnect() will prolong next AP reconnect by about 1000 ms
    // @todo needs idle state to be effective?
    if (!wifi_set_sleep_type(MODEM_SLEEP_T))
    {
      ets_uart_printf("ERROR: enabling WLAN modem sleep failed\r\n");
    }

    // shutdown valve GPIOs
    valveDriverShutdown();

    // shutdown ADC GPIO
    adcDriverShutdown();

    // estimate current time
    state.now = getTime();
    state.rtcMem.lastShutdownTime = state.now;

    // calculate next downtime
    uint8 needRFCal = true;
    if (state.rtcMem.valveOpen && state.rtcMem.downtime > MAX_VALVE_OPEN_DOWNTIME)
    {
      // valve is open, limit downtime
      state.rtcMem.lastDowntime = MAX_VALVE_OPEN_DOWNTIME;
    }
    else
    {
      state.rtcMem.lastDowntime = state.rtcMem.downtime;
    }
    uint64 nextValeOperationTime = state.rtcMem.lastShutdownTime + state.rtcMem.lastDowntime + SLEEPER_COMMANDTIME;
    if (nextEventTime > 0 && nextValeOperationTime > (nextEventTime + 500U))
    {
      // next valve operation time will be too late for next event: try to cut back on downtime to hit event
      uint32 cutBackTime = nextValeOperationTime - (nextEventTime + 500U);
      if (state.rtcMem.lastDowntime > (SLEEPER_MIN_DOWNTIME + cutBackTime))
      {
        // required cut back leaves at least 1 second downtime: apply cut back
        state.rtcMem.lastDowntime -= cutBackTime;
      }
      else
      {
        // required cut back does not leave at least 1 second downtime: limit cut back and accept delay
        state.rtcMem.lastDowntime = SLEEPER_MIN_DOWNTIME;
      }

      // skip RF calibration if downtime is less than quarter of regular downtime
      needRFCal = 4*state.rtcMem.lastDowntime < state.rtcMem.downtime;
    }

    // backup state to RTC memory
    if (!system_rtc_mem_write(64, &state.rtcMem, sizeof(state.rtcMem)))
    {
      ets_uart_printf("ERROR: writing to RTC memory failed\r\n");
    }

    // say goodbye
    esp_gmtime(&state.rtcMem.lastShutdownTime, &tms);
    uint8 deepSleepOption = needRFCal? RF_DEFAULT : RF_NO_CAL;
    ets_uart_printf("going to sleep for %lu seconds at %02u:%02u:%02u.%03uZ %02u.%02u.%u with deep sleep option %u (uptime %lu ms)\r\n", state.rtcMem.lastDowntime/1000, tms.tm_hour, tms.tm_min, tms.tm_sec, tms.tm_msec, tms.tm_mday, 1 + tms.tm_mon, 1900 + tms.tm_year, deepSleepOption, system_get_time()/1000);

    // go to deep sleep (set init_data byte 108 to the number of wakeups for next RF_CAL)
    system_deep_sleep_set_option(deepSleepOption);
    system_deep_sleep_instant(((uint64)state.rtcMem.lastDowntime*state.rtcMem.downtimeScale)/10U); // microseconds
  }
}

/**
 * WLAN event handler
 */
LOCAL void wifiEventCallback(System_Event_t *evt)
{
  switch (evt->event)
  {
    case EVENT_STAMODE_CONNECTED:
      //ets_uart_printf("WLAN event: connected\r\n");
      if (wifi_station_dhcpc_status() == DHCP_STOPPED)
      {
        // open uplink immediately after connecting to AP
        comProcessing();
      }
      break;
    case EVENT_STAMODE_GOT_IP:
      //ets_uart_printf("WLAN event: got IP\r\n");
      if (wifi_station_dhcpc_status() == DHCP_STARTED)
      {
        // open uplink after receiving IP address
        comProcessing();
      }
      break;
  }
}

/**
 * dummy implementation
 *
 * required for ESP8266_NONOS_SDK_v1.5.2 to ESP8266_NONOS_SDK_v2.2.1
 */
void ICACHE_FLASH_ATTR user_rf_pre_init()
{
  // e.g. system_phy_set_rfoption()
}

/**
 * non-OTA flash map: set the 5th 4k sector from the end of the flash to store the RF_CAL parameter
 *
 * required for ESP8266_NONOS_SDK_v1.5.2 to ESP8266_NONOS_SDK_v2.2.1
 */
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set()
{
  uint32 rf_cal_sector = 0;

  enum flash_size_map size_map = system_get_flash_size_map();
  switch (size_map) {
    case FLASH_SIZE_4M_MAP_256_256:
      rf_cal_sector = 128 - 5;
      break;
    case FLASH_SIZE_8M_MAP_512_512:
      rf_cal_sector = 256 - 5;
      break;
    case FLASH_SIZE_16M_MAP_512_512:
    case FLASH_SIZE_16M_MAP_1024_1024:
      rf_cal_sector = 512 - 5;
      break;
    case FLASH_SIZE_32M_MAP_512_512:
    case FLASH_SIZE_32M_MAP_1024_1024:
      rf_cal_sector = 512 - 5;
      break;
    case FLASH_SIZE_64M_MAP_1024_1024:
      rf_cal_sector = 2048 - 5;
      break;
    case FLASH_SIZE_128M_MAP_1024_1024:
      rf_cal_sector = 4096 - 5;
      break;
  }

  return rf_cal_sector;
}

/**
 * system setup
 */
void ICACHE_FLASH_ATTR user_init()
{
  ets_uart_printf("Gardena 9V solenoid irrigation valve controller ver: " VERSION "\r\n");
  ets_uart_printf("Copyright (c) 2015-2019 jnsbyr, Germany\r\n\r\n");

  // configure valve GPIOs
  valveDriverInit();

  // configure ADC GPIO
  adcDriverInit();

  // read RTC memory
  uint8 reinitState = false;
  if (system_rtc_mem_read(64, &state.rtcMem, sizeof(state.rtcMem)))
  {
    if (state.rtcMem.magic != SLEEPER_STATE_MAGIC)
    {
      ets_uart_printf("WARNING: RTC memory lost\r\n");
      state.rtcMem.batteryOffset = 0; // config, millivolt, every chip seems to have different ADC offset up to 200 mV
      reinitState = true;
    }
  }
  else
  {
    ets_uart_printf("ERROR: reading from RTC memory failed\r\n");
    reinitState = true;
  }

  //ets_uart_printf("readvdd33 %u\r\n", readvdd33());
  //ets_uart_printf("system_get_vdd33 %u\r\n", system_get_vdd33());
  //ets_uart_printf("phy_get_vdd33 %u\r\n", phy_get_vdd33());

  // read vdd before operating valve and entering station mode (system_get_vdd33() requires modifying the default esp init data byte 107 0->255 and RF to be up)
  os_delay_us(30); // settle time [us]
  state.batteryVoltage = readvdd33() + state.rtcMem.batteryOffset;

  // init state
  state.timeSynchronized = false;

  // cold boot init required?
  if (reinitState)
  {
    // RTC memory is invalid, initialize config and state
    state.rtcMem.magic           = SLEEPER_STATE_MAGIC;      // static
    state.rtcMem.boottime        = SLEEPER_BOOTTIME;         // config
    state.rtcMem.downtime        = DEFAULT_DOWNTIME;         // config
    state.rtcMem.downtimeScale   = DEFAULT_DEEP_SLEEP_SCALE; // config
    state.rtcMem.defaultDuration = DEFAULT_MANUAL_DURATION;  // config
    state.rtcMem.mode            = MODE_OFF;                 // config
    state.rtcMem.activityProgramId  = 0;                     // config
    state.rtcMem.maxValveResistance = 0;                     // config
    tms.tm_mday = 1;
    tms.tm_mon  = 0;
    tms.tm_year = 70;
    tms.tm_hour = 0;
    tms.tm_min  = 0;
    tms.tm_sec  = 0;
    tms.tm_msec = 0;
    state.rtcMem.lastShutdownTime = esp_mktime(&tms);
    state.rtcMem.lastDowntime = 0;
    state.rtcMem.offMode = MODE_OFF;
    state.rtcMem.overriddenMode = MODE_OFF;
    state.rtcMem.lastValveOperationStatus = VALVE_STATUS_UNKNOWN;
    state.rtcMem.valveOpen = true;  // preset to force immediate closing
    state.rtcMem.override = false;
    state.rtcMem.lowBattery = false;
    state.rtcMem.ipConfig.ip.addr = 0;
    state.rtcMem.valveSupplyVoltage = 0;  // preset to force detection
    state.rtcMem.valveResistance = 0;
    state.rtcMem.valveOpenTime = 0;
    state.rtcMem.valveCloseTime = 0;
    state.rtcMem.valveCloseTimeEstimated = 0;
    state.rtcMem.overrideEndTime = 0;
    state.rtcMem.overrideEndTimeEstimated = false;
    state.rtcMem.lowBatteryTime = 0;
    state.rtcMem.lowBatteryTimeEstimated = false;
    state.rtcMem.totalOpenCount = 0;
    state.rtcMem.totalOpenDuration = 0;
    for (uint16 i = 0; i < MAX_ACTIVITIES; i++)
    {
      // mark all activity slots as invalid
      ActivityT* activity = &state.rtcMem.activities[i];
      activity->day       = DAY_INVALID;
    }
    ets_uart_printf("WARNING: time set to %02u:%02u:%02u.%03uZ %02u.%02u.%u\r\n", tms.tm_hour, tms.tm_min, tms.tm_sec, tms.tm_msec, tms.tm_mday, 1 + tms.tm_mon, 1900 + tms.tm_year);

    // backup initial state to RTC memory
    if (!system_rtc_mem_write(64, &state.rtcMem, sizeof(state.rtcMem)))
    {
      ets_uart_printf("ERROR: writing to RTC memory failed\r\n");
    }

    ets_uart_printf("sleeper: uptime %lu ms, valve %s\r\n", system_get_time()/1000, state.rtcMem.valveOpen? "open" : "closed");
  }

  // check battery voltage
  bool userWakeup = isUserWakeup();
  state.now = getTime();
  if (!state.rtcMem.lowBattery && state.batteryVoltage < MIN_BATTERY_VOLTAGE)
  {
    // low battery condition
    ets_uart_printf("WARNING: low battery voltage %d mV (required %d mV)\r\n", state.batteryVoltage, MIN_BATTERY_VOLTAGE);
    state.rtcMem.lowBattery = true;
    state.rtcMem.lowBatteryTime = state.now + LOW_BATTERY_REPORTING_DURATION;
    state.rtcMem.lowBatteryTimeEstimated = true;
  }
  if (state.rtcMem.lowBattery)
  {
    // try to close open valve
    if (state.rtcMem.valveOpen)
    {
      valveControl(&state, MODE_OFF, 0, false, false);
    }

    // backup new valve state to RTC memory
    if (!system_rtc_mem_write(64, &state.rtcMem, sizeof(state.rtcMem)))
    {
      ets_uart_printf("ERROR: writing to RTC memory failed\r\n");
    }

    // enter permanent deep sleep for maximum battery lifetime after reporting duration has expired
    if (!userWakeup && state.now >= state.rtcMem.lowBatteryTime + LOW_BATTERY_REPORTING_DURATION)
    {
      ets_uart_printf("WARNING: low battery shutdown\r\n");

      system_deep_sleep_set_option(RF_DISABLED);
      system_deep_sleep_instant(0);
    }
    else
    {
      ets_uart_printf("WARNING: LOW BATTERY\r\n");
    }
  }

  // wakeup caused by user?
  if (userWakeup)
  {
    ets_uart_printf("wakeup by user\r\n");

    // try to toggle valve
    valveControl(&state, MODE_OFF, state.now, true, false);

    // precompensate timekeeping for early wakeup by one runtime in case of setTime = false or no WLAN
    state.rtcMem.lastDowntime -= SLEEPER_COMMANDTIME;

    // backup new valve state immediately to RTC memory to provide full manual control even if WLAN connect fails
    if (!system_rtc_mem_write(64, &state.rtcMem, sizeof(state.rtcMem)))
    {
      ets_uart_printf("ERROR: writing to RTC memory failed\r\n");
    }
  }

  // configure WLAN operation mode
  uint8 setWLANOpMode = STATION_MODE;
  if (wifi_get_opmode() != setWLANOpMode)
  {
    ets_uart_printf("setting WLAN operation mode %u\r\n", setWLANOpMode);
    if (!wifi_set_opmode(setWLANOpMode)) // persistent, default SOFTAP_MODE
    {
      ets_uart_printf("ERROR: changing WLAN operation mode failed\r\n");
    }
  }

  // reuse last DHCP IP address to speed up ready state (saves about 3000 ms)
  if (state.rtcMem.ipConfig.ip.addr)
  {
    // disable WLAN DHCP client
    ets_uart_printf("WLAN disabling DHCP client\r\n");
    if (!wifi_station_dhcpc_stop())
    {
      ets_uart_printf("ERROR: disabling WLAN station DHCP client failed\r\n");
    }

    // set WLAN station IP address
    ets_uart_printf("WLAN setting station IP address to " IPSTR "\r\n", IP2STR(&state.rtcMem.ipConfig.ip));
    if (!wifi_set_ip_info(STATION_IF, &state.rtcMem.ipConfig))
    {
      ets_uart_printf("ERROR: changing WLAN station IP address failed\r\n");
    }

    comTimeout = MAX_WLAN_TIME/2; // milliseconds ~4 s
  }
  else
  {
    comTimeout = MAX_WLAN_TIME; // milliseconds ~8 s
  }

  // configure WLAN station
  struct station_config actStationConfig;
  if (wifi_station_get_config(&actStationConfig))
  {
    struct station_config setStationConfig;
    os_memset(&setStationConfig, 0, sizeof(setStationConfig));
    os_sprintf(setStationConfig.ssid, "%s", WLAN_SSID);
    os_sprintf(setStationConfig.password, "%s", WLAN_PSK);
    if (os_memcmp(actStationConfig.password, setStationConfig.password, sizeof(setStationConfig.password)))
    {
      ets_uart_printf("updating WLAN station configuration\r\n");
      reinitState = true;
      if (!wifi_station_set_config(&setStationConfig)) // persistent
      {
        ets_uart_printf("ERROR: changing WLAN station configuration failed\r\n");
      }
    }
  }
  else
  {
    ets_uart_printf("ERROR: getting WLAN station configuration failed\r\n");
  }

  // enable WLAN station auto connect
  if (!wifi_station_get_auto_connect())
  {
    ets_uart_printf("enabling WLAN station auto connect at power on\r\n");
    if (!wifi_station_set_auto_connect(true)) // persistent, default true
    {
      ets_uart_printf("ERROR: enabling WLAN station auto connect at power failed\r\n");
    }
  }

  // limit WLAN speed to save power
  if (wifi_get_phy_mode() != PHY_MODE_11G)
  {
    ets_uart_printf("forcing IEEE 802.11G mode\r\n");
    if (!wifi_set_phy_mode(PHY_MODE_11G)) // persistent
    {
      ets_uart_printf("ERROR: forcing IEEE 802.11G mode failed\r\n");
    }
  }

  // init state
  uplinkSocketConnected = false;
  statusSent            = false;
  readyForShutdown      = false;
  nextEventTime         = 0;

  // register WLAN event handler
  wifi_set_event_handler_cb(wifiEventCallback);

  // passive wait for WLAN connection
  os_timer_disarm(&comTimer);
  os_timer_setfn(&comTimer, (os_timer_func_t*) comTimerCallback, NULL);
#if defined(ESP_SDK_VERSION_NUMBER) && (ESP_SDK_VERSION_NUMBER >= 2)
  os_timer_arm(&comTimer, MAX_WLAN_TIME/2, false); // milliseconds timeout
#else
  os_timer_arm(&comTimer, MAX_WLAN_TIME/2, NULL); // milliseconds timeout
#endif

  //ets_uart_printf("Sleeper init completed in %lu ms!\r\n", system_get_time()/1000);
}
