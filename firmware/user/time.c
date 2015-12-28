/*****************************************************************************
 *
 * Copyright (c) 2015 jnsbyr
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
 * The author of this file does not claim any copyright or authorship of the
 * algorithms and mathematics used in this file. They are either trivial or
 * based on well documented public knowledge. The credits go to:
 *
 *     Gregorian calendar (1582 AD)
 *     Zeller's congruence algorithm (1882 AD)
 *
 *****************************************************************************
 *
 * project: ESP8266 library enhancements
 *
 * file:    time.c
 *
 * created: 13.04.2015
 *
 *
 * This file provides a partial implementation of the ISO C standard
 * methods mktime, gmtime and strptime enhanced with milliseconds support
 * but does not claim to be ISO C standard compliant. Leap seconds and
 * other special exception of the Gregorian calendar are not taken into
 * account, so accuracy is limited in this respect.
 *
 *****************************************************************************/

#include "time.h"

#include <osapi.h>

// leap year rule (Gregorian calendar)
#define isLeapYear(year) (year%400 == 0 || (year%4 == 0 && year%100 != 0))

/**
 * @return array of days per month (Gregorian calendar)
 */
LOCAL const uint32* getDaysPerMonth(uint32 year)
{
  LOCAL const uint32 daysPerMonthInCommonYear[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  LOCAL const uint32 daysPerMonthInLeapYear[]   = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  return isLeapYear(year)? daysPerMonthInLeapYear : daysPerMonthInCommonYear;
}

/**
 * compute the number of the day in the week using Zeller's congruence algorithm
 *
 * @return number of day (0 = Sunday)
 */
LOCAL uint32 ICACHE_FLASH_ATTR getWeekday(uint32 year, uint32 month, uint32 day)
{
  uint32 zellerAdjustment = (14 - month)/12;
  uint32 m = month + 12*zellerAdjustment - 2; // March = 1, January = 11
  uint32 y = year - zellerAdjustment;         // use previous year for January and February
  uint32 d = y%100; // last two digits of year
  uint32 c = y/100; // century
  uint32 h = day + (13*m - 1)/5 + d + d/4 + c/4 - 2*c;
  return h%7;
}

/*
 * mktime implementation with milliseconds support
 */
uint64 ICACHE_FLASH_ATTR esp_mktime(struct ets_tm* tms)
{
  return system_mktime(1900 + tms->tm_year, 1 + tms->tm_mon, tms->tm_mday, tms->tm_hour, tms->tm_min, tms->tm_sec)*1000ULL + tms->tm_msec;
}

/*
 * gmtime implementation with milliseconds support
 */
void ICACHE_FLASH_ATTR esp_gmtime(uint64* t, struct ets_tm* tms)
{
  LOCAL const uint32 secondsPerDay = 86400;

  // year
  uint32 secs = *t/1000ULL;  // [ms] -> [s]
  uint32 year = 70;
  while (true)
  {
    uint32 spy = secondsPerDay*(isLeapYear(year)? 366 : 365);
    if (secs <= spy)
    {
      break;
    }
    secs -= spy;
    year++;
  }
  tms->tm_year = year;

  // month
  uint32 mon = 0;
  uint32 yday = 0;
  const uint32* dpm = getDaysPerMonth(year);
  while (true)
  {
    uint32 spm = secondsPerDay*dpm[mon];
    if (secs <= spm)
    {
      break;
    }
    secs -= spm;
    yday += dpm[mon];
    mon++;
  }
  tms->tm_mon = mon;

  // day
  tms->tm_mday = 1 + (secs/secondsPerDay);
  tms->tm_yday = yday + tms->tm_mday;
  tms->tm_wday = getWeekday(1900 + tms->tm_year, tms->tm_mon + 1, tms->tm_mday);

  // time
  secs %= secondsPerDay;
  tms->tm_hour = secs/3600;
  secs %= 3600;
  tms->tm_min  = secs/60;
  tms->tm_sec  = secs%60;
  tms->tm_msec = *t%1000ULL;
  tms->tm_isdst = 0;
}

/**
 * strptime subset implementation with milliseconds support
 *
 * @param s must comply to format [YYYY-MM-DDT]HH:MI[:SS[[.FFF]Z]]
 * @param format ignored
 * @param tms return value, tm_wday, tm_yday and tm_isdst will not be set
 * @return pointer to first unprocessed input character or NULL on error
 */
const char* ICACHE_FLASH_ATTR esp_strptime(const char *s, const char *format, struct ets_tm* tms)
{
  size_t len = os_strlen(s);
  if (len == 5 && s[2] == ':')
  {
    tms->tm_hour = 10*(s[0]-'0') + (s[1]-'0');
    tms->tm_min  = 10*(s[3]-'0') + (s[4]-'0');
    tms->tm_sec  = 0;
    tms->tm_msec = 0;
    return &s[5];
  }
  else if (len >= 19 && s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' && s[16] == ':')
  {
    tms->tm_year = 1000*(s[0]-'0') + 100*(s[1]-'0') + 10*(s[2]-'0') + (s[3]-'0') - 1900;
    tms->tm_mon  = 10*(s[5]-'0') + (s[6]-'0') - 1;
    tms->tm_mday = 10*(s[8]-'0') + (s[9]-'0');
    tms->tm_hour = 10*(s[11]-'0') + (s[12]-'0');
    tms->tm_min  = 10*(s[14]-'0') + (s[15]-'0');
    tms->tm_sec  = 10*(s[17]-'0') + (s[18]-'0');
    if (len == 19)
    {
      tms->tm_msec = 0;
      return &s[19];
    }
    else if (len == 20 && s[19] == 'Z')
    {
      tms->tm_msec = 0;
      return &s[20];
    }
    else if (len == 24 && s[19] == '.' && s[23] == 'Z')
    {
      tms->tm_msec = 100*(s[20]-'0') + 10*(s[21]-'0') + (s[22]-'0');
      return &s[24];
    }
    else
    {
      // length or format error
      return NULL;
    }
  }
  else
  {
    // length or format error
    return NULL;
  }
}
