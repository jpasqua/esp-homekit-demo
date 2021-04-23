//
// Homekit Multi-Button
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
//  
//
// Building:
// o Sample make command:
//   make -C examples/button all -DDEV_PASS="123-45-678" -DDEV_SERIAL="1200345" -DDEV_SETUP="J81Q"
// o Generating a qrcode
//   esp-homekit-demo/components/common/homekit/tools/gen_qrcode 15 123-45-678 J81Q qrcode.png
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
 * Macros, Constants
 *
 *----------------------------------------------------------------------------*/

#define SWITCH_SERVICE(_INDEX, _IS_PRIMARY)                                   \
            HOMEKIT_SERVICE(                                                  \
                STATELESS_PROGRAMMABLE_SWITCH,                                \
                .primary=_IS_PRIMARY,                                         \
                .characteristics=(homekit_characteristic_t*[]) {              \
                    HOMEKIT_CHARACTERISTIC(NAME, "B ## _INDEX"),              \
                    &buttons[_INDEX],                                        \
                    NULL                                                      \
                },                                                            \
            )

#if !defined(DEV_SERIAL) || !defined(DEV_PASS) || !defined(DEV_SETUP)
  #error The following must be defined: DEV_SERIAL, DEV_PASS, DEV_SETUP
#endif

#define Q(x) #x
#define QUOTE(x) Q(x)

char DeviceModel[]    = "JP2B";
char DeviceSetupID[]  = QUOTE(DEV_SETUP);
char DevicePassword[] = QUOTE(DEV_PASS);
char DeviceSerial[]   = QUOTE(DEV_SERIAL);
char DeviceName[]     = QUOTE(DEV_NAME);

#define N_BUTTONS (4)

static const uint8_t ButtonPins[] = {
   2, // D4
   4, // D2
   5, // D1
  14  // D5
};

homekit_characteristic_t buttons[] = {
  HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0),
  HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0),
  HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0),
  HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0),
};

static const uint8_t Pin_LED = 15;          // D8

static const uint8_t ResetSequenceThreshold = 2;
  // Determines how many double-presses of a button are required to trigger a reset

void identifyDevice(homekit_value_t _value);
void homekit_event_handler(homekit_event_t event);

/*------------------------------------------------------------------------------
 *
 * HomeKit Device Configuration Structures
 *
 *----------------------------------------------------------------------------*/

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id=1,
        .category=homekit_accessory_category_programmable_switch,
        .services=(homekit_service_t*[]) {
            HOMEKIT_SERVICE(
                ACCESSORY_INFORMATION,
                .characteristics=(homekit_characteristic_t*[]) {
                    HOMEKIT_CHARACTERISTIC(NAME, DeviceName),
                    HOMEKIT_CHARACTERISTIC(MANUFACTURER, "BitsPlusAtoms"),
                    HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, DeviceSerial),
                    HOMEKIT_CHARACTERISTIC(MODEL, DeviceModel),
                    HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, identifyDevice),
                    NULL
                },
            ),
            SWITCH_SERVICE(1, true),
            SWITCH_SERVICE(2, false),
            SWITCH_SERVICE(3, false),
            SWITCH_SERVICE(4, false),
            NULL
        },
    ),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = DevicePassword,
    .setupId = DeviceSetupID,
    .on_event = homekit_event_handler
};


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
    blinkIt(2, 200);
    delayMS(500);
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
    blinkIt(4, 250);
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


// Think of a way to trigger a reset - maybe two triple presses in a row
void buttonCallback(button_event_t event, void *context) {
  static uint8_t resetSequenceCount = 0;
  homekit_characteristic_t* button = (homekit_characteristic_t*)context;
  if (event == button_event_single_press) {
      printf("single press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(0));
      blinkIt(1, 75);
  } else if (event == button_event_long_press) {
      printf("long press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(1));
      blinkIt(2, 75);
  } else if (event == button_event_tripple_press) {
      printf("triple press of on button\n");
      resetSequenceCount = 0;
      homekit_characteristic_notify(button, HOMEKIT_UINT8(2));
      blinkIt(3, 75);
  } else if (event == button_event_double_press) {
      printf("double press of on button\n");
      resetSequenceCount++;
      if (resetSequenceCount == ResetSequenceThreshold) {
        resetConfig();
      }
  } else {
      resetSequenceCount = 0;
      printf("Unused button event: %d\n", event);        
  }
}



/*------------------------------------------------------------------------------
 *
 * Homekit Callbacks
 *
 *----------------------------------------------------------------------------*/

void homekit_event_handler(homekit_event_t event) {
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

  // accessories[0]->services[N_BUTTONS+1] = NULL;
  for (int i = 0; i < 6; i++) {
    printf("Service addr = 0x%lx\n", (unsigned long)accessories[0]->services[i]);
  }

  wifi_config_init2(DeviceModel, NULL, handleWiFiEvent);

  button_config_t button_config = BUTTON_CONFIG(
      button_active_low, 
      .max_repeat_presses=3,
      .long_press_time=4500,
  );
  for (int i = 0; i < N_BUTTONS; i++) {
    if (button_create(ButtonPins[i], button_config, buttonCallback, &buttons[i])) {
      printf("Failed to initialize on button\n");
    }
  }

}
