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
 * project: ESP8266 library enhancements
 *
 * file:    esp_time.c
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

#include "esp_time.h"

#include <osapi.h>
#include <c_types.h>

struct tm
{
  uint32 tm_sec;   /* seconds: 0-59 */
  uint32 tm_min;   /* minutes: 0-59 */
  uint32 tm_hour;  /* hours since midnight: 0-23 */
  uint32 tm_mday;  /* day of the month: 1-31 */
  uint32 tm_mon;   /* months since January: 0-11 */
  uint32 tm_year;  /* years since 1900 */
  uint32 tm_wday;  /* days since Sunday (0-6) */
  uint32 tm_yday;  /* days since January 1: 0-365 */
  uint32 tm_isdst; /* +1 DST, 0 no DST, -1 unknown */
};

// enable undocumented SDK function (works as gmtime if timezone is not set with sntp_set_timezone)
extern struct tm* sntp_localtime(const uint32*);

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
  uint32 secs = *t/1000ULL;  // [ms] -> [s]
  struct tm* tmp = sntp_localtime(&secs);
  tms->tm_msec  = *t%1000ULL;
  tms->tm_sec   = tmp->tm_sec;
  tms->tm_min   = tmp->tm_min;
  tms->tm_hour  = tmp->tm_hour;
  tms->tm_mday  = tmp->tm_mday;
  tms->tm_mon   = tmp->tm_mon;
  tms->tm_year  = tmp->tm_year;
  tms->tm_wday  = tmp->tm_wday;
  tms->tm_yday  = tmp->tm_yday;
  tms->tm_isdst = tmp->tm_isdst;
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
