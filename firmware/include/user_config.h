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
 * project: WLAN control unit for Gardena solenoid irrigation valve no. 1251
 *
 * file:    user_config.h
 *
 * created: 11.04.2015
 *
 *****************************************************************************/

#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#define WLAN_SSID   "WLAN-SSID"             // SSID of WLAN access point
#define WLAN_PSK    "WLAN-PSK"              // PSK of WLAN access point

#define REMOTE_IP   "192.168.0.1"           // IP address of control server
#define REMOTE_PORT 3030                    // port of control server

#define DEFAULT_DOWNTIME          10000     // milliseconds, 10 s initial deep sleep duration while not configured
#define DEFAULT_MANUAL_DURATION     600     // seconds, 10 min manual override valve open duration while not configured
#define MAX_VALVE_OPEN_DOWNTIME  300000     // milliseconds, 5 min maximum downtime while valve is open

#endif
