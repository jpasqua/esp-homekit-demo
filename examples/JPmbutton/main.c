//
// Homekit MultiButton
//     A single accessory with multiple STATELESS_PROGRAMMABLE_SWITCHes
//     
//
// Notes:
// o Acts as multiple Programmable Switches in a single unit
// o One button per programmable device
// o Presses result in the following Homekit actions:
//   - Single press: Generates a HomeKit "Single Press" event
//   - Long press:   Generates a HomeKit "Double Press" event
//   - Double press: Part of a reset sequence - see below
//   - Triple press: Generates a HomeKit "Triple Press" event
// o Why not just use "double press" directly rather than a long press? Because unlike lights,
//   it is a big deal to accidentally turn off some devices (e.g. a 3D printer plugged into a
//   controllable outlet). I wanted the off gesture to be very intentional
// o To use this device to trigger an on/off action in Homekit, map
//   + Single press to ON  for the device under control (e.g. light, outlet, etc.)
//   + Double press to Off for the device under control (e.g. light, outlet, etc.)
//   + Triple Press to any other desired scene
// o Reseting the device: To remove pairings and wifi configuration, the device can be reset
//   using a sequence of double presses on any button. At the moment, (3) double-presses in
//   a row are required with no intervening button presses.
// o User Feedback
//   + Power on, but not connected to wifi yet: Steady gray color
//   + Device needs to be configured via wifi: A repeating pattern of 4 short orange pulses
//   + Connected to wifi and ready to go: 5 short green pulses
//   + Single Button Press: 1 long green pulse
//   + Long Button Press: 2 medium red pulses
//   + Triple Button Press: 3 shorter blue pulses
//   + Double Button Press: 1 shorter gray pulse
//   + Error (unreconized button press): 5 short yellow pulses
//   + Device Identification triggered from App: 3 short purple pulses repeated 3 times
//   + TYPICAL STARTUP SEQUENCE
//     - LED illuminates steady gray to indicate power is on and the device is initializing
//     - 5 short green pulses and then LED off indicating it is connected to wifi and ready to go
//     - [User presses a button once] 1 long green pulse and then off
//   + FIRST TIME STARTUP SEQUENCE
//     - LED illuminates steady gray to indicate power is on and the device is initializing
//     - A repeating pattern of 4 short orange pulses.
//     - [User configures via WiFi] 5 short green pulses and then LED off - device is ready
//     - [User presses a button once] 1 long green pulse and then off
//
// Building:
// o Sample make command:
//   make -C examples/button all -DDEV_PASS="123-45-678" -DDEV_SERIAL="1200345" -DDEV_SETUP="J81Q"
// o Generating a qrcode
//   esp-homekit-demo/components/common/homekit/tools/gen_qrcode 15 123-45-678 J81Q qrcode.png
//

// ----- Standard C
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// ----- Espressif
#include <espressif/esp_wifi.h>
// ----- HomeKit
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
// ----- Third Party
#include <wifi_config.h>
// ----- esp-homekit-demo
#include <button.h>
// ----- App-specific
#include "utils.h"


/*------------------------------------------------------------------------------
 *
 * Sanity Check
 *
 *----------------------------------------------------------------------------*/

#if !defined(DEV_SERIAL) || !defined(DEV_PASS) || !defined(DEV_SETUP)
    #error The following must be defined: DEV_SERIAL, DEV_PASS, DEV_SETUP
#endif


/*------------------------------------------------------------------------------
 *
 * General Configuration
 *
 *----------------------------------------------------------------------------*/

#define NButtons (4)

struct {
  uint8_t pin;
  char name[4];
  homekit_characteristic_t* event;
} buttonInfo[NButtons] = {
  {  2 /*D4*/, "B01", NULL },
  {  4 /*D2*/, "B02", NULL },
  {  5 /*D1*/, "B03", NULL },
  { 14 /*D5*/, "B04", NULL }
};

static const uint8_t Pin_LED = 15;  // D1 Mini: D8

static const uint8_t ResetSequenceThreshold = 2;
  // Determines how many double-presses of a button are required to trigger a reset

static const uint32_t LongPressTime = 4000;

/*------------------------------------------------------------------------------
 *
 * HomeKit Configuration
 *
 *----------------------------------------------------------------------------*/

char DeviceModel[]    = "MultiB";

homekit_accessory_t *accessories[2];  // List w/ 1 accessory & NULL termination
homekit_server_config_t config = {
    .accessories = accessories,
    .password = QUOTE(DEV_PASS),
    .setupId = QUOTE(DEV_SETUP),
    .on_event = homekitEventHandler
};


/*------------------------------------------------------------------------------
 *
 * Callback Handlers
 *
 *----------------------------------------------------------------------------*/

void buttonCallback(button_event_t event, void *context) {
  static uint8_t resetSequenceCount = 0;
  homekit_characteristic_t* button = (homekit_characteristic_t*)context;
  if (event == button_event_single_press) {
      blinkInBackground(LED_GREEN, 1, 600);
      printf("single press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(0));
  } else if (event == button_event_long_press) {
      blinkInBackground(LED_RED, 2, 300);
      printf("long press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(1));
  } else if (event == button_event_tripple_press) {
      blinkInBackground(LED_BLUE, 3, 200);
      printf("triple press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(2));
  } else if (event == button_event_double_press) {
      blinkInBackground(LED_GRAY, 1, 200);
      printf("double press of on button\n");
      resetSequenceCount++;
      if (resetSequenceCount == ResetSequenceThreshold) {
        resetConfig();
      }
  } else {
      blinkInBackground(LED_YELLOW, 5, 120);
      resetSequenceCount = 0;
      printf("Unused button event: %d\n", event);        
  }
}

void handleWiFiEvent(wifi_config_event_t event) {
  logWiFiEvent(event);
  if (event == WIFI_CONFIG_CONNECTED) {
    blinkInBackground(LED_GREEN, 5, 200);
    homekit_server_init(&config);
  }
}


/*------------------------------------------------------------------------------
 *
 * Build the Accessory and Initialize
 *
 *----------------------------------------------------------------------------*/

void buildAccessory() {
  uint8_t macaddr[6];
  sdk_wifi_get_macaddr(STATION_IF, macaddr);

  // Accessory name is of the form: DeviceModel-NNNNNN\0
  char *accName = malloc(strlen(DeviceModel) + 7 + 1);
  sprintf(
    accName, "%s-%02X%02X%02X",
    DeviceModel, macaddr[3], macaddr[4], macaddr[5]);
  printf("Accessory Name = %s\n", accName);

  homekit_service_t* services[1 + NButtons + 1];
    // 1 entry for the accessory information
    // NButtons entries for the buttons
    // 1 entry for NULL termination of the list
  homekit_service_t** s = services;

  *(s++) = NEW_HOMEKIT_SERVICE(
    ACCESSORY_INFORMATION,
    .characteristics=(homekit_characteristic_t*[]) {
      NEW_HOMEKIT_CHARACTERISTIC(NAME, accName),
      NEW_HOMEKIT_CHARACTERISTIC(MANUFACTURER, "BitsPlusAtoms"),
      NEW_HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, QUOTE(DEV_SERIAL)),
      NEW_HOMEKIT_CHARACTERISTIC(MODEL, DeviceModel),
      NEW_HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
      NEW_HOMEKIT_CHARACTERISTIC(IDENTIFY, identifyDevice),
      NULL
    }
  );

  for (int i = 0; i < NButtons; i++) {
    *(s++) = NEW_HOMEKIT_SERVICE(
      STATELESS_PROGRAMMABLE_SWITCH,
      .primary=(i == 0) ? true : false,
      .characteristics=(homekit_characteristic_t*[]){
        (buttonInfo[i].event = NEW_HOMEKIT_CHARACTERISTIC(PROGRAMMABLE_SWITCH_EVENT, 0)),
        NEW_HOMEKIT_CHARACTERISTIC(NAME, buttonInfo[i].name),
        NULL
      }
    );
  }

  *(s++) = NULL;  // Terminate the list of services

  accessories[0] = NEW_HOMEKIT_ACCESSORY(
    .id=1,
    .category=homekit_accessory_category_other,
    .services=services);
  accessories[1] = NULL;  // Terminate the list of accessories
}

void user_init(void) {
  prepLogging();
  prepLED(Pin_LED, false);

  setLEDColor(LED_GRAY);
  printf("DeviceSetupID = %s\n", config.setupId);
  printf("DevicePassword = %s\n", config.password);
  printf("DeviceSerial = %s\n", QUOTE(DEV_SERIAL));
  buildAccessory();

  button_config_t button_config = BUTTON_CONFIG(button_active_low, .long_press_time=LongPressTime, .max_repeat_presses=3);
  for (int i = 0; i < NButtons; i++) {
    if (button_create(buttonInfo[i].pin, button_config, buttonCallback, &buttonInfo[i].event[i])) {
        printf("Failed to initialize button %d\n", i);
    }
  }

  wifi_config_init2(DeviceModel, NULL, handleWiFiEvent);
}
