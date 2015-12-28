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
 * file:    uplink.c
 *
 * created: 14.04.2015
 *
 *****************************************************************************/

#include "uplink.h"

#include <ip_addr.h>
#include <espconn.h>
#include <osapi.h>

typedef enum {
  TCP_UNDEFINED,
  TCP_DISCONNECTED,
  TCP_CONNECTING,
  TCP_CONNECTED,
  TCP_CONNECT_ERROR,
  TCP_SENDING,
  TCP_SEND_ERROR,
  TCP_SENT,
  TCP_RECEIVED
} tConnState;

LOCAL struct espconn connection;
LOCAL esp_tcp tcp;
LOCAL tConnState connState = TCP_UNDEFINED;
LOCAL char* txPayload;
LOCAL char rxPayload[1024];
LOCAL uint16 rxPayloadSize;

LOCAL void ICACHE_FLASH_ATTR clientSentCallback(void *arg)
{
  struct espconn *pespconn = arg;
  connState = TCP_SENT;

//  ets_uart_printf("TCP sent\r\n");

  if (rxPayload[0])
  {
    // 2nd transmit complete, close connection
    ets_uart_printf("TCP disconnecting ...\r\n");
    espconn_disconnect(pespconn);
  }
}

LOCAL void ICACHE_FLASH_ATTR clientReceiveCallback(void *arg, char *pdata, unsigned short len)
{
  struct espconn *pespconn = arg;

  os_memcpy(rxPayload, pdata, len);
  rxPayloadSize = len;
  connState = TCP_RECEIVED;

//  ets_uart_printf("TCP message received: %s\r\n", rxPayload);
}

LOCAL void ICACHE_FLASH_ATTR clientConnectedCallback(void *arg)
{
  struct espconn *pespconn = arg;
  connState = TCP_CONNECTED;

  espconn_regist_sentcb(pespconn, clientSentCallback);
  espconn_regist_recvcb(pespconn, clientReceiveCallback);

  sint8 sentStatus = espconn_sent(pespconn, txPayload, os_strlen(txPayload));
  if (sentStatus == ESPCONN_OK) {
    connState = TCP_SENDING;
    ets_uart_printf("TCP connected, sending request: %s\r\n", txPayload);
  } else {
    connState = TCP_SEND_ERROR;
    ets_uart_printf("ERROR: TCP send failed, disconnecting ...\r\n");
    espconn_disconnect(pespconn);
  }
}

LOCAL void ICACHE_FLASH_ATTR clientDisconnectedCallback(void *arg)
{
  struct espconn *pespconn = arg;
  connState = TCP_DISCONNECTED;

  ets_uart_printf("TCP connection terminated\r\n");

  if (pespconn == NULL)
  {
    ets_uart_printf("ERROR: TCP connection is NULL!\r\n");
  }
}

LOCAL void ICACHE_FLASH_ATTR clientErrorCallback(void *arg, sint8 err)
{
  struct espconn *pespconn = arg;
  connState = TCP_CONNECT_ERROR;

  if (err != ESPCONN_OK)
  {
    ets_uart_printf("ERROR: TCP connect failed: %d\r\n", err);
    if (pespconn != NULL)
    {
      // try to close connection anyway
      espconn_disconnect(pespconn);
    }
  }
  else
  {
    ets_uart_printf("clientErrorCallback\r\n");
  }
}

void ICACHE_FLASH_ATTR uplink_sendRequest(char* remoteIP, uint16 remotePort, char* message)
{
  txPayload = message;
  rxPayload[0] = '\0';
  rxPayloadSize = 0;
  connState = TCP_CONNECTING;

  // define TCP client connection
  connection.proto.tcp = &tcp;
  connection.type      = ESPCONN_TCP;
  connection.state     = ESPCONN_NONE;
  uint32 ip = ipaddr_addr(remoteIP);
  os_memcpy(connection.proto.tcp->remote_ip, &ip, 4);
  connection.proto.tcp->local_port  = espconn_port();
  connection.proto.tcp->remote_port = remotePort;

  // register callbacks
  espconn_regist_connectcb(&connection, clientConnectedCallback);
  espconn_regist_disconcb(&connection, clientDisconnectedCallback);
  espconn_regist_reconcb(&connection, clientErrorCallback);

  // connect (blocking)
  ets_uart_printf("TCP connecting to " IPSTR ":%d\r\n", IP2STR(connection.proto.tcp->remote_ip), connection.proto.tcp->remote_port);
  sint8 espcon_status = espconn_connect(&connection);
  switch (espcon_status)
  {
    case ESPCONN_OK:
//      ets_uart_printf("TCP connnection created.\r\n");
      break;
    case ESPCONN_RTE:
      ets_uart_printf("ERROR: TCP connect - no route to host.\r\n");
      break;
    case ESPCONN_TIMEOUT:
      ets_uart_printf("ERROR: TCP connect - timeout\r\n");
      break;
    default:
      ets_uart_printf("ERROR: TCP connect - error %d\r\n", espcon_status);
  }
}

uint8 ICACHE_FLASH_ATTR uplink_hasReceived()
{
  return uplink_isClosed() ||  connState == TCP_RECEIVED;
}

char* ICACHE_FLASH_ATTR uplink_getReply()
{
  return rxPayload;
}

uint16 ICACHE_FLASH_ATTR uplink_getReplySize()
{
  return rxPayloadSize;
}

void ICACHE_FLASH_ATTR uplink_sendMessage(char* message)
{
  txPayload = message;

  sint8 sentStatus = espconn_sent(&connection, txPayload, os_strlen(txPayload));
  if (sentStatus == ESPCONN_OK) {
    connState = TCP_SENDING;
    ets_uart_printf("TCP sending message: %s\r\n", txPayload);
  } else {
    connState = TCP_SEND_ERROR;
    ets_uart_printf("ERROR: TCP send failed, disconnecting ...\r\n");
    espconn_disconnect(&connection);
  }
}

uint8 ICACHE_FLASH_ATTR uplink_isSend()
{
  return uplink_isClosed() || connState == TCP_SENT || connState == TCP_RECEIVED;
}


void ICACHE_FLASH_ATTR uplink_close()
{
  if (!uplink_isClosed())
  {
    ets_uart_printf("TCP disconnecting ...\r\n");
    espconn_disconnect(&connection);
  }
}

uint8 ICACHE_FLASH_ATTR uplink_isClosed()
{
  return connState == TCP_DISCONNECTED || connState == TCP_CONNECT_ERROR;
}





