/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#if defined(USBJ_EX)
#include "usb_joystick.h"
#else
#include "opentx_helpers.h"
#endif

extern "C" {
#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_msc.h"
#include "usbd_desc.h"
#include "usbd_hid.h"
#include "usbd_cdc.h"
}

#include "stm32_hal_ll.h"
#include "stm32_hal.h"

#include "stm32_gpio.h"

#include "hal/gpio.h"
#include "hal/usb_driver.h"

#include "hal.h"
#include "debug.h"

static bool usbDriverStarted = false;
#if defined(BOOT)
static usbMode selectedUsbMode = USB_MASS_STORAGE_MODE;
#else
static usbMode selectedUsbMode = USB_UNSELECTED_MODE;
#endif

USBD_HandleTypeDef hUsbDeviceFS;

int getSelectedUsbMode()
{
  return selectedUsbMode;
}

void setSelectedUsbMode(int mode)
{
  selectedUsbMode = usbMode(mode);
}

#if defined(USB_GPIO_VBUS)
int usbPlugged()
{
#if defined(DEBUG_DISABLE_USB)
  return(false);
#endif

  static uint8_t debouncedState = 0;
  static uint8_t lastState = 0;

  // uint8_t state = GPIO_ReadInputDataBit(USB_GPIO, USB_GPIO_PIN_VBUS);
  uint8_t state = gpio_read(USB_GPIO_VBUS) ? 1 : 0;
  if (state == lastState)
    debouncedState = state;
  else
    lastState = state;

  return debouncedState;
}
#endif

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

extern "C" void OTG_FS_IRQHandler()
{
  DEBUG_INTERRUPT(INT_OTG_FS);
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

void usbInit()
{
  gpio_init_af(USB_GPIO_DM, USB_GPIO_AF, GPIO_PIN_SPEED_VERY_HIGH);
  gpio_init_af(USB_GPIO_DP, USB_GPIO_AF, GPIO_PIN_SPEED_VERY_HIGH);
  
#if defined(USB_GPIO_VBUS)
  gpio_init(USB_GPIO_VBUS, GPIO_IN, GPIO_PIN_SPEED_LOW);
#endif

#if defined(LL_APB2_GRP1_PERIPH_SYSCFG)
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
#elif defined(LL_APB4_GRP1_PERIPH_SYSCFG)
  LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);
#else
  #error "Unable to enable SYSCFG peripheral clock"
#endif

#if defined(LL_AHB2_GRP1_PERIPH_OTGFS)
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_OTGFS);
#elif defined(LL_AHB1_GRP1_PERIPH_USB2OTGHS)
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_USB2OTGHS);
#else
  #error "Unable to enable USB peripheral clock"
#endif

  usbDriverStarted = false;
}

extern void usbInitLUNs();
extern USBD_HandleTypeDef hUsbDeviceFS;
extern "C" USBD_StorageTypeDef USBD_Storage_Interface_fops_FS;
extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

void usbStart()
{
  USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
  switch (getSelectedUsbMode()) {
#if !defined(BOOT)
    case USB_JOYSTICK_MODE:
      // initialize USB as HID device
#if defined(USBJ_EX)
      setupUSBJoystick();
#endif
      //USBD_Init(&hUsbDeviceFS, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_HID_cb, &USR_cb);
      //MX_USB_DEVICE_Init();
      USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID);
      break;
#if defined(USB_SERIAL)
    case USB_SERIAL_MODE:
      // initialize USB as CDC device (virtual serial port)
      //USBD_Init(&hUsbDeviceFS, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_CDC_cb, &USR_cb);
      //MX_USB_DEVICE_Init();
      USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
      USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
      break;
#endif
#endif
    default:
    case USB_MASS_STORAGE_MODE:
      // initialize USB as MSC device
      usbInitLUNs();
      //MX_USB_DEVICE_Init();
      USBD_RegisterClass(&hUsbDeviceFS, &USBD_MSC);
      USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_Storage_Interface_fops_FS);
      //USBD_Init(&hUsbDeviceFS, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_MSC_cb, &USR_cb);
      break;
  }
  USBD_Start(&hUsbDeviceFS);
  usbDriverStarted = true;
}

void usbStop()
{
  usbDriverStarted = false;
  USBD_DeInit(&hUsbDeviceFS);
}


bool usbStarted()
{
  return usbDriverStarted;
}

#if !defined(BOOT)

#if defined(USBJ_EX)
extern "C" void delay_ms(uint32_t count);
void usbJoystickRestart()
{
  if (!usbDriverStarted || getSelectedUsbMode() != USB_JOYSTICK_MODE) return;

  USBD_DeInit(&hUsbDeviceFS);
  delay_ms(100);
  USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
  USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID);
  USBD_Start(&hUsbDeviceFS);
}
#else
// TODO: fix after HAL conversion is complete
#warning channelOutputs should come from "globals.h"

#define MAX_OUTPUT_CHANNELS 32
extern int16_t channelOutputs[MAX_OUTPUT_CHANNELS];
#endif



/*
  Prepare and send new USB data packet

  The format of HID_Buffer is defined by
  USB endpoint description can be found in
  file usb_hid_joystick.c, variable HID_JOYSTICK_ReportDesc
*/
void usbJoystickUpdate()
{
#if !defined(USBJ_EX)
   static uint8_t HID_Buffer[HID_IN_PACKET];

   //buttons
   HID_Buffer[0] = 0;
   HID_Buffer[1] = 0;
   HID_Buffer[2] = 0;
   for (int i = 0; i < 8; ++i) {
     if ( channelOutputs[i+8] > 0 ) {
       HID_Buffer[0] |= (1 << i);
     }
     if ( channelOutputs[i+16] > 0 ) {
       HID_Buffer[1] |= (1 << i);
     }
     if ( channelOutputs[i+24] > 0 ) {
       HID_Buffer[2] |= (1 << i);
     }
   }

   //analog values
   //uint8_t * p = HID_Buffer + 1;
   for (int i = 0; i < 8; ++i) {

     int16_t value = limit<int16_t>(0, channelOutputs[i] + 1024, 2048);;

     HID_Buffer[i*2 +3] = static_cast<uint8_t>(value & 0xFF);
     HID_Buffer[i*2 +4] = static_cast<uint8_t>(value >> 8);

   }
   USBD_HID_SendReport(&hUsbDeviceFS, HID_Buffer, HID_IN_PACKET);
#else
  usbReport_t ret = usbReport();
  USBD_HID_SendReport(&hUsbDeviceFS, ret.ptr, ret.size);
#endif
}
#endif
