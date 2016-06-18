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
 * file:    valve.h
 *
 * created: 24.04.2015
 *
 *****************************************************************************/

#ifndef __USER_VALVE_H__
#define __USER_VALVE_H__

#include "main.h"

void   ICACHE_FLASH_ATTR valveDriverInit(void);
uint64 ICACHE_FLASH_ATTR valveControl(SleeperStateT* sleeperState, uint8 override, uint8 mode, uint64 startTime);
void   ICACHE_FLASH_ATTR valveDriverShutdown(void);

#endif /* __USER_VALVE_H__ */
