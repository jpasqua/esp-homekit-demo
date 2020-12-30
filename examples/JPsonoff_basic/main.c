/*
 * Example of using esp-homekit library to control
 * a simple $5 Sonoff Basic using HomeKit.
 * The esp-wifi-config library is also used in this
 * example. This means you don't have to specify
 * your network's SSID and password before building.
 *
 * In order to flash the sonoff basic you will have to
 * have a 3,3v (logic level) FTDI adapter.
 *
 * To flash this example connect 3,3v, TX, RX, GND
 * in this order, beginning in the (square) pin header
 * next to the button.
 * Next hold down the button and connect the FTDI adapter
 * to your computer. The sonoff is now in flash mode and
 * you can flash the custom firmware.
 *
 * WARNING: Do not connect the sonoff to AC while it's
 * connected to the FTDI adapter! This may fry your
 * computer and sonoff.
 *
 */

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include "button.h"

/*------------------------------------------------------------------------------
 *
 * Constants
 *
 *----------------------------------------------------------------------------*/

#define Q(x) #x
#define QUOTE(x) Q(x)

#ifndef DEV_SERIAL
#define DeviceSerial "0012345"
#endif
#ifndef DEV_PASS
#define DEV_PASS "111-11-111"
#endif
#ifndef DEV_SETUP
#define DEV_SETUP "1QJ8"
#endif

// Pins connected to components on the Sonoff Basic
static const uint8_t Pin_Relay = 12;
static const uint8_t Pin_LED = 13;
static const uint8_t Pin_Button = 0;

char DeviceModel[]    = "Basic";
char DeviceSetupID[]  = QUOTE(DEV_SETUP);
char DevicePassword[] = QUOTE(DEV_PASS);
char DeviceSerial[]   = QUOTE(DEV_SERIAL);
char DeviceName[]     = QUOTE(DEV_NAME);

/*------------------------------------------------------------------------------
 *
 * Forward Declarations
 *
 *----------------------------------------------------------------------------*/

void switchOnCallback(homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void button_callback(uint8_t gpio, button_event_t event);
void identifyDevice(homekit_value_t _value);
void homekitEventHandler(homekit_event_t event);


/*------------------------------------------------------------------------------
 *
 * HomeKit Configuration
 *
 *----------------------------------------------------------------------------*/

homekit_characteristic_t switch_on =
  HOMEKIT_CHARACTERISTIC_(
    ON, false, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(switchOnCallback));

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Sonoff Switch");

homekit_accessory_t *accessories[] = {
  HOMEKIT_ACCESSORY(
    .id = 1,
    .category = homekit_accessory_category_switch,
  .services = (homekit_service_t*[]) {
    HOMEKIT_SERVICE(
      ACCESSORY_INFORMATION,
    .characteristics = (homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, DeviceName),
      HOMEKIT_CHARACTERISTIC(MANUFACTURER, "iTEAD"),
      HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, DeviceSerial),
      HOMEKIT_CHARACTERISTIC(MODEL, DeviceModel),
      HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1.6"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, identifyDevice),
      NULL
    }
    ),
    HOMEKIT_SERVICE(
      SWITCH,
      .primary = true,
      .characteristics = (homekit_characteristic_t*[]) {
        HOMEKIT_CHARACTERISTIC(NAME, DeviceName),
        &switch_on,
        NULL
      }
    ),
    NULL
  }),
  NULL
};

homekit_server_config_t config = {
  .accessories = accessories,
  .password = DevicePassword,
  .setupId = DeviceSetupID,
  .on_event = homekitEventHandler
};

/*------------------------------------------------------------------------------
 *
 * Utility functions
 *
 *----------------------------------------------------------------------------*/

void delayMS(uint32_t delayMillis) { vTaskDelay(delayMillis / portTICK_PERIOD_MS); }

void setLED(bool on) { gpio_write(Pin_LED, on ? 0 : 1); }

void setRelay(bool on) { gpio_write(Pin_Relay, on); }

void setState(bool on) {
  setLED(on);
  setRelay(on);
}

void prepIO() {
  gpio_enable(Pin_LED, GPIO_OUTPUT);
  gpio_enable(Pin_Relay, GPIO_OUTPUT);
  setState(switch_on.value.bool_value);
}

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
  setLED(switch_on.value.bool_value);

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
    setLED(switch_on.value.bool_value);
  }
}

/*------------------------------------------------------------------------------
 *
 * Reset Handling
 *
 *----------------------------------------------------------------------------*/

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

void switchOnCallback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
  setState(switch_on.value.bool_value);
}

void button_callback(uint8_t gpio, button_event_t event) {
  switch (event) {
  case button_event_single_press:
    printf("Toggling relay\n");
    switch_on.value.bool_value = !switch_on.value.bool_value;
    setState(switch_on.value.bool_value);
    homekit_characteristic_notify(&switch_on, switch_on.value);
    break;
  case button_event_long_press:
    resetConfig();
    break;
  default:
    printf("Unknown button event: %d\n", event);
  }
}

/*------------------------------------------------------------------------------
 *
 * Homekit Callbacks
 *
 *----------------------------------------------------------------------------*/

void homekitEventHandler(homekit_event_t event) {
  switch (event) {
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

void genUniqueAccessoryName() {
    uint8_t macaddr[6];
    sdk_wifi_get_macaddr(STATION_IF, macaddr);
    
    int len = snprintf(
        NULL, 0, "%s-%02X%02X%02X",
        DeviceName, macaddr[3], macaddr[4], macaddr[5]);
    char *theName = malloc(len+1);
    snprintf(theName, len+1, "%s-%02X%02X%02X",
             DeviceName, macaddr[3], macaddr[4], macaddr[5]);
    
    name.value = HOMEKIT_STRING(theName);
}

void user_init(void) {
  uart_set_baud(0, 115200);

  genUniqueAccessoryName();
  printf("deviceSetupID = %s\n", DeviceSetupID);
  printf("devicePassword = %s\n", DevicePassword);
  printf("deviceSerial = %s\n", DeviceSerial);
  printf("deviceName = %s\n", DeviceName);

  wifi_config_init2(DeviceModel, NULL, handleWiFiEvent);
  prepIO();

  if (button_create(Pin_Button, 0, 4000, button_callback)) {
    printf("Failed to initialize button\n");
  }
}
