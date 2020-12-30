//
// Homekit MultiButton
//     A single accessory with multiple STATELESS_PROGRAMMABLE_SWITCHes
//     
//
// Notes:
// o Each STATELESS_PROGRAMMABLE_SWITCH has two physical buttons: 
//   one for ON and the other for OFF
// o Presses result in the following Homekit actions:
//   + ON Button
//     - Single press: "Single Press"
//     - Double press: NONE
//     - Long press:   "Triple Press"
//   + OFF Button
//     - Single press: "Double Press"
//     - Double press: NONE
//     - Long press:   NONE, but triggers reset of device
// o Thus to use this device to trigger an on/off action in Homekit, map
//   + Single press to ON  for the device under control (e.g. light, outlet, etc.)
//   + Double press to Off for the device under control (e.g. light, outlet, etc.)
//   + Triple Press to any other desired scene
//
//


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <espressif/esp_system.h>
#include <espressif/esp_wifi.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include <button.h>


/*------------------------------------------------------------------------------
 *
 * Constants
 *
 *----------------------------------------------------------------------------*/

#define Q(x) #x
#define QUOTE(x) Q(x)

#ifndef DEV_SERIAL
    #define DEV_SERIAL "0012345"
#endif
#ifndef DEV_PASS
    #define DEV_PASS "111-11-111"
#endif
#ifndef DEV_SETUP
    #define DEV_SETUP "1QJ8"
#endif

char DeviceModel[]    = "MultiB";
char DeviceSetupID[]  = QUOTE(DEV_SETUP);
char DevicePassword[] = QUOTE(DEV_PASS);
char DeviceSerial[]   = QUOTE(DEV_SERIAL);
char DeviceName[]     = QUOTE(DEV_NAME);

/*------------------------------------------------------------------------------
 *
 * HW Configuration
 *
 *----------------------------------------------------------------------------*/

static const uint8_t Pin_LED = 15;  // D1 Mini: D8

#define NButtonPairs (4)

typedef struct {
  uint8_t onPin;
  uint8_t offPin;
} ButtonPair;

ButtonPair button[NButtonPairs] = {
  { 0,  2}, // D1 Mini: D3, D4
  { 4,  5}, // D1 Mini: D2, D1
  {12, 13}, // D1 Mini: D6, D7
  {14, 16}  // D1 Mini: D5, D0
};
char* accessoryNames[NButtonPairs];


/*------------------------------------------------------------------------------
 *
 * HomeKit Configuration
 *
 *----------------------------------------------------------------------------*/

void homekitEventHandler(homekit_event_t event);

homekit_accessory_t *accessories[2];  // List w/ 1 accessory & NULL termination
homekit_server_config_t config = {
    .accessories = accessories,
    .password = DevicePassword,
    .setupId = DeviceSetupID,
    .on_event = homekitEventHandler
};
homekit_characteristic_t* buttonEvent[NButtonPairs];

/*------------------------------------------------------------------------------
 *
 * Utility functions
 *
 *----------------------------------------------------------------------------*/

// ----- General Utlitities -----
void delayMS(uint32_t delayMillis) { vTaskDelay(delayMillis / portTICK_PERIOD_MS); }

// ----- LED-related -----
void setLED(bool on) { gpio_write(Pin_LED, on ? 0 : 1); }

void blinkIt(uint8_t cycles, uint32_t delayMillis) {
  for (int i  = 0; i < cycles; i++) {
    setLED(true);
    delayMS(delayMillis);
    setLED(false);
    delayMS(delayMillis);
  }
}

void identifyDeviceTask(void *_args) {
  for (int i = 0; i < 3; i++) {
    blinkIt(3, 100);
    delayMS(250);
  }
  setLED(false);

  vTaskDelete(NULL);
}

void identifyDevice(homekit_value_t _value) {
  printf("LED identify\n");
  xTaskCreate(identifyDeviceTask, "Identify Device", 128, NULL, 2, NULL);
}

void indicateStationModeTask(void *_args) {
  while (true) {
    blinkIt(4, 125);
    delayMS(1000);
  }
}

void indicateStationMode(bool on) {
  static TaskHandle_t xHandle = NULL;
  if (on) {
    xTaskCreate(indicateStationModeTask, "StationMode", 128, NULL, 2, &xHandle);
  } else {
    if (xHandle != NULL) {
       vTaskDelete(xHandle);
    }
    setLED(false);
  }
}

void prepLED() {
  gpio_enable(Pin_LED, GPIO_OUTPUT);
  setLED(false);
}

// ----- Reset Handling -----
void resetConfigTask() {
  // Flash the LED first before we start the reset
  blinkIt(5, 100);

  printf("Resetting Wifi Config\n");
  wifi_config_reset();
  delayMS(1000);

  printf("Resetting HomeKit Config\n");
  homekit_server_reset();
  delayMS(1000);

  printf("Restarting\n");
  sdk_system_restart();
  vTaskDelete(NULL);
}

void resetConfig() {
  printf("Resetting configuration\n");
  xTaskCreate(resetConfigTask, "Reset configuration", 256, NULL, 2, NULL);
}


/*------------------------------------------------------------------------------
 *
 * Button Handling
 *
 *----------------------------------------------------------------------------*/

homekit_characteristic_t button_event = HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0);

void onButtonCallback(button_event_t event, void *context) {
  homekit_characteristic_t* button = (homekit_characteristic_t*)context;
  if (event == button_event_single_press) {
    printf("single press of on button\n");
    homekit_characteristic_notify(button, HOMEKIT_UINT8(0));
    blinkIt(1, 50);
  } else if (event == button_event_long_press) {
    printf("long press of on button\n");
    homekit_characteristic_notify(button, HOMEKIT_UINT8(2));
    blinkIt(2, 75);
  } else {
    printf("Unused button event: %d\n", event);        
  }
}

void offButtonCallback(button_event_t event, void *context) {
  homekit_characteristic_t* button = (homekit_characteristic_t*)context;
  if (event == button_event_single_press) {
    printf("single press of off button\n");
    homekit_characteristic_notify(button, HOMEKIT_UINT8(1));
    blinkIt(3, 50);
  } else if (event == button_event_long_press) {
    resetConfig();
  } else {
    printf("Unused button event: %d\n", event);        
  }
}


/*------------------------------------------------------------------------------
 *
 * Homekit Callbacks
 *
 *----------------------------------------------------------------------------*/

void homekitEventHandler(homekit_event_t event) {
  switch(event) {
    case HOMEKIT_EVENT_SERVER_INITIALIZED:
      printf("HOMEKIT_EVENT_SERVER_INITIALIZED\n");
      break;
    case HOMEKIT_EVENT_CLIENT_CONNECTED:
      printf("HOMEKIT_EVENT_CLIENT_CONNECTED\n");
      break;
    case HOMEKIT_EVENT_CLIENT_VERIFIED:
      printf("HOMEKIT_EVENT_CLIENT_VERIFIED\n");
      break;
    case HOMEKIT_EVENT_CLIENT_DISCONNECTED:
      printf("HOMEKIT_EVENT_CLIENT_DISCONNECTED\n");
      break;
    case HOMEKIT_EVENT_PAIRING_ADDED:
      printf("HOMEKIT_EVENT_PAIRING_ADDED\n");
      break;
    case HOMEKIT_EVENT_PAIRING_REMOVED:
      printf("HOMEKIT_EVENT_PAIRING_REMOVED\n");
      break;
    default:
      printf("Unknown event type: %d\n", event);
      break;
  }
}

void handleWiFiEvent(wifi_config_event_t event) {
  switch (event) {
    case WIFI_CONFIG_CONNECTED:
      printf("Connected to WiFi\n");
      homekit_server_init(&config);
      break;
    case WIFI_CONFIG_DISCONNECTED:
      printf("Disconnected from WiFi\n");
      break;
    case WIFI_CONFIG_AP_START:
      printf("Entering Station Mode\n");
      indicateStationMode(true);
      break;
    case WIFI_CONFIG_AP_STOP:
      printf("Leaving Station Mode\n");
      indicateStationMode(false);
      break;
    default:
      printf("Unknown event type: %d", event);
      break;
  }
}

void buildAccessory() {
  uint8_t macaddr[6];
  sdk_wifi_get_macaddr(STATION_IF, macaddr);

  int accNameLen = snprintf(
    NULL, 0, "%s-%02X%02X%02X",
    DeviceName, macaddr[3], macaddr[4], macaddr[5]);
  char *accName = malloc(accNameLen+1);
  snprintf(
    accName, accNameLen+1, "%s-%02X%02X%02X",
    DeviceName, macaddr[3], macaddr[4], macaddr[5]);

  homekit_service_t* services[1 + NButtonPairs + 1];
    // 1 entry to for the accessory information
    // NButtonPairs entriesf ro the button pairs
    // 1 entry for NULL termination of the list
  homekit_service_t** s = services;

  *(s++) = NEW_HOMEKIT_SERVICE(
    ACCESSORY_INFORMATION,
    .characteristics=(homekit_characteristic_t*[]) {
      NEW_HOMEKIT_CHARACTERISTIC(NAME, accName),
      NEW_HOMEKIT_CHARACTERISTIC(MANUFACTURER, "BitsPlusAtoms"),
      NEW_HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, DeviceSerial),
      NEW_HOMEKIT_CHARACTERISTIC(MODEL, DeviceModel),
      NEW_HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
      NEW_HOMEKIT_CHARACTERISTIC(IDENTIFY, identifyDevice),
      NULL
    }
  );

  for (int i=0; i < NButtonPairs; i++) {
    int buttonNameLen = snprintf(NULL, 0, "Button %d", i + 1);
    char *buttonName = malloc(buttonNameLen+1);
    snprintf(buttonName, buttonNameLen+1, "Button %d", i + 1);

    buttonEvent[i] =
      NEW_HOMEKIT_CHARACTERISTIC(PROGRAMMABLE_SWITCH_EVENT, i);

    *(s++) = NEW_HOMEKIT_SERVICE(
      STATELESS_PROGRAMMABLE_SWITCH,
      .primary=(i == 0) ? true : false,
      .characteristics=(homekit_characteristic_t*[]){
        HOMEKIT_CHARACTERISTIC(NAME, buttonName),
        buttonEvent[i],
        NULL
      }
    );
  }
  *(s++) = NULL;  // Terminate the list of services

  accessories[0] = NEW_HOMEKIT_ACCESSORY(
    .category=homekit_accessory_category_other,
    .services=services);
  accessories[1] = NULL;  // Terminate the list of accessories
}

/*------------------------------------------------------------------------------
 *
 * Setup
 *
 *----------------------------------------------------------------------------*/

void user_init(void) {
  uart_set_baud(0, 115200);

  prepLED();

  printf("DeviceSetupID = %s\n", DeviceSetupID);
  printf("DevicePassword = %s\n", DevicePassword);
  printf("DeviceSerial = %s\n", DeviceSerial);
  printf("DeviceName = %s\n", DeviceName);
  buildAccessory();

  wifi_config_init2(DeviceModel, NULL, handleWiFiEvent);

  button_config_t button_config = BUTTON_CONFIG(button_active_low,  .max_repeat_presses=2);
  for (int i = 0; i < NButtonPairs; i++) {
    button_config.long_press_time = 2000;
    if (button_create(button[i].onPin, button_config, onButtonCallback, &buttonEvent[i])) {
        printf("Failed to initialize on button %d\n", i);
    }
    button_config.long_press_time = 8000;
    if (button_create(button[i].offPin, button_config, offButtonCallback, &buttonEvent[i])) {
        printf("Failed to initialize off button %d\n", i);
    }
  }
}
