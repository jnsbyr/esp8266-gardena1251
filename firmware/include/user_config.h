/*****************************************************************************
 *
 * Copyright (c) 2015-2018 jnsbyr
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

#define DEFAULT_DOWNTIME           10000    // [ms] - default 10 s initial deep sleep duration while not configured
#define DEFAULT_MANUAL_DURATION      600    // [s] - default 10 min manual override valve open duration while not configured
#define MAX_VALVE_OPEN_DOWNTIME   300000    // [ms] - default 5 min maximum downtime while valve is open

#define LOW_BATTERY_REPORTING_DURATION (24LU*60*60*1000) // 24 h -> [ms] - max. delay before entering permanent deep sleep after detecting low batter condition

#define VALVE_DRIVER_TYPE              1    // 1=capacitor, 2=H-bridge

#if (VALVE_DRIVER_TYPE == 1)

#define ADC_DIVIDER_RATIO             11    // ADC input voltage divider ratio

#define CAPACITANCE               0.001f    // [F]
#define RC_CONSTANT      CAPACITANCE*240    // RC constant (R = 150 ohm resistor + 33 ohm valve + 57 ohm other)

#define NOMINAL_SUPPLY_VOLTAGE      9000    // [mV]
#define TYPICAL_SUPPLY_VOLTAGE      9350    // [mV] - value derived from tests
#define MAX_VALID_SUPPLY_VOLTAGE    9500    // [mV]
#define MAX_DISCHARGE_VOLTAGE_1     6800    // [mV] - with valve, value derived from tests
#define MAX_DISCHARGE_VOLTAGE_2      800    // [mV] - without valve, value derived from tests
#define CHARGING_VOLTAGE_TOLERANCE   100    // [mV] - with valve

#define VALVE_OPEN_PULSE_DURATION 250000    // [us] - pulse duration to open valve, Gardena valve timing: 250 ms
#define VALVE_CLOSE_PULSE_DURATION 62500    // [us] - pulse duration to close valve, Gardena valve timing: 62.5 ms
#define MAX_DISCHARGE_TIMEOUT    1000000    // [us] - value derived from tests
#define RECHARGE_TIMEOUT           90000    // [us] - value derived from tests

#define MIN_RESISTANCE                25    // [ohm] - typically 40 ohm
#define MAX_RESISTANCE                75    // [ohm] - typically 40 ohm

#else

#define VALVE_OPEN_PULSE_DURATION 200000    // [us] - pulse duration to open valve, Gardena valve timing: 250 ms
#define VALVE_CLOSE_PULSE_DURATION 62500    // [us] - pulse duration to close valve, Gardena valve timing: 62.5 ms

#endif /* VALVE_DRIVER_TYPE == 1 */

#endif /* __USER_CONFIG_H__ */
