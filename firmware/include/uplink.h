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
 * file:    uplink.h
 *
 * created: 14.04.2015
 *
 *****************************************************************************/

#ifndef __USER_UPLINK_H__
#define __USER_UPLINK_H__

#include <c_types.h>

void ICACHE_FLASH_ATTR uplink_sendRequest(char* remoteIP, uint16 remotePort, char* message);
uint8 ICACHE_FLASH_ATTR uplink_hasReceived();
char* ICACHE_FLASH_ATTR uplink_getReply();
uint16 ICACHE_FLASH_ATTR uplink_getReplySize();

void ICACHE_FLASH_ATTR uplink_sendMessage(char* message);
uint8 ICACHE_FLASH_ATTR uplink_isSend();

void ICACHE_FLASH_ATTR uplink_close();
uint8 ICACHE_FLASH_ATTR uplink_isClosed();

#endif /* __USER_UPLINK_H__ */
