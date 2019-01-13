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
 * file:    adc.c
 *
 * created: 30.12.2018
 *
 *****************************************************************************/

#include "adc.h"

#if VALVE_DRIVER_TYPE == 1

#include <math.h>
#include <eagle_soc.h>
#include <gpio.h>
#include <osapi.h>

#define SAMPLES 20

// ADC input
#define ADC_GPIO_MUX PERIPHS_IO_MUX_MTDI_U
#define ADC_GPIO_FUNC FUNC_GPIO12
#define ADC_GPIO 12

LOCAL uint16 buffer[SAMPLES];

#endif /* VALVE_DRIVER_TYPE == 1 */


/**
 * called at OS init
 */
void ICACHE_FLASH_ATTR adcDriverInit()
{
#if VALVE_DRIVER_TYPE == 1

  // configure GPIO output
  PIN_FUNC_SELECT(ADC_GPIO_MUX, ADC_GPIO_FUNC);

  // initial output state: disable measurement at capacitor
  GPIO_OUTPUT_SET(ADC_GPIO, 0);

#endif /* VALVE_DRIVER_TYPE == 1 */
}

/**
 * called before OS shutdown
 */
void ICACHE_FLASH_ATTR adcDriverShutdown()
{
#if VALVE_DRIVER_TYPE == 1

  // configure output state: disable measurement at capacitor
  GPIO_OUTPUT_SET(ADC_GPIO, 0);

#endif /* VALVE_DRIVER_TYPE == 1 */
}

#if VALVE_DRIVER_TYPE == 1

/**
 * read TOUT input
 *
 * 20 samples take about 1.8 ms
 *
 * @return voltage at ADC input divider [mV]
 */
uint16 adcRead()
{
  // enable measurement at capacitor
  if (!GPIO_INPUT_GET(ADC_GPIO))
  {
    GPIO_OUTPUT_SET(ADC_GPIO, 1);
  }

  // oversample ADC
  system_adc_read_fast(buffer, SAMPLES, 8);

  // skipping initial samples is faster than a delay for input voltage to settle
  uint32 sum = 0;
  uint16 skip = 10;
  for (uint16 i=0; i<SAMPLES; i++)
  {
    //ets_uart_printf("ADC %d=%u\r\n", i, buffer[i]);
    if (i >= skip)
    {
      sum += buffer[i];
    }
  }

  // ADC samples -> input voltage
  uint16 average = round((float)sum/(SAMPLES - skip)/1024*1000*ADC_DIVIDER_RATIO); // [mV]
  //ets_uart_printf("ADC avg=%u\r\n", average);

  return average;
}

#endif /* VALVE_DRIVER_TYPE == 1 */
