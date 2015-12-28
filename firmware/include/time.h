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
 * file:    time.h
 *
 * created: 13.04.2015
 *
 *****************************************************************************/

#ifndef __USER_TIME_H__
#define __USER_TIME_H__

#include <c_types.h>

/*
 * structure for storing time information
 */
struct ets_tm
{
  uint32 tm_msec;  /* milliseconds: 0-999 */
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

/**
 * convert struct ets_tm to milliseconds since 1970 (tm_isdst, tm_wday and tm_yday will be ignored)
 */
uint64 ICACHE_FLASH_ATTR esp_mktime(struct ets_tm* tms);

/**
 * convert milliseconds since 1970 to struct ets_tm (tm_isdst is always zero)
 */
void ICACHE_FLASH_ATTR esp_gmtime(uint64* t, struct ets_tm* tms);

/**
 * convert string with fixed format [YYYY-MM-DDT]HH:MI[:SS[[.FFF]Z]] into struct tm (tm_yday and tm_isdst will not be set)
 */
const char* ICACHE_FLASH_ATTR esp_strptime(const char *s, const char *format, struct ets_tm* tms);

#endif /* __USER_TIME_H__ */
