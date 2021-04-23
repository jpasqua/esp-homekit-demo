//
// HomeKit Accessory Utilities
//     A collection of functions that are (hopefully) generally useful
//     for implementing HomeKit Accessories
//     

// ----- Standard C
#include <stdio.h>
// ----- Espressif / FreeRTOS
#include <esp/uart.h>
#include <FreeRTOS.h>
#include <task.h>
#include <espressif/esp_system.h>
// ----- HomeKit
#include <homekit/homekit.h>
// ----- Third Party
#include <wifi_config.h>
#include <ws2812.h>
#include <pwm.h>
// ----- App-specific
#include "utils.h"


// ----- Module Global State
uint8_t Utils_Pin_LED = 0;
bool ledIsNeoPixel = false;
bool performingPWM = false;


// ----- General Utlitities -----
void delayMS(uint32_t delayMillis) { vTaskDelay(delayMillis / portTICK_PERIOD_MS); }
void prepLogging() {   uart_set_baud(0, 115200); }

// ----- LED-related -----
void setLEDColor(uint32_t color) {
  // printf("setLEDColor(0x%06x)\n", color);
  if (ledIsNeoPixel) {
    ws2812_set(Utils_Pin_LED, color);
  } else {
    if (color != LED_WHITE) {
      if (color == LED_BLACK) {
        if (performingPWM) { pwm_stop(); performingPWM = false; }
        gpio_write(Utils_Pin_LED, 0);
        return;
      }
      // Very crude conversion to grayscale
      uint32_t gray = ((color >> 16) + ((color >> 8) & 0xff) + (color & 0xff))/3;
      uint32_t dutyCycle = (UINT16_MAX * gray)/255;
      pwm_set_duty(dutyCycle);
      if (!performingPWM) { pwm_start(); performingPWM = true; }
    } else {
      gpio_write(Utils_Pin_LED, 1);
    }
  }
}

void setLED(bool on) { setLEDColor(on ? LED_WHITE : LED_BLACK); }

struct BlinkParams {
  uint32_t color;
  uint8_t cycles;
  uint32_t delayMillis;
} blinkParams;

void blinkIt(uint32_t color, uint8_t cycles, uint32_t delayMillis) {
  for (int i = 0; i < cycles; i++) {
    if (i) { delayMS(delayMillis); }
    setLEDColor(ledIsNeoPixel ? color : LED_WHITE);
    delayMS(delayMillis);
    setLEDColor(LED_BLACK);
  }
}

void blinkItTask(void *_args) {
  struct BlinkParams* blinkParams = _args;
  blinkIt(blinkParams->color, blinkParams->cycles, blinkParams->delayMillis);
  free(blinkParams);
  vTaskDelete(NULL);
}

void blinkInBackground(uint32_t color, uint8_t cycles, uint32_t delayMillis) {
  // printf("blinkInBackground\n");
  struct BlinkParams* blinkParams = malloc(sizeof(struct BlinkParams));
  blinkParams->color = color;
  blinkParams->cycles = cycles;
  blinkParams->delayMillis = delayMillis;
  xTaskCreate(blinkItTask, "BlinkIt", 256, blinkParams, 2, NULL);
}

void identifyDeviceTask(void *_args) {
  for (int i = 0; i < 3; i++) {
    blinkIt(LED_PURPLE, 3, 200);
    delayMS(500);
  }
  setLED(false);

  vTaskDelete(NULL);
}

void identifyDevice(homekit_value_t _value) {
  // printf("LED identify\n");
  xTaskCreate(identifyDeviceTask, "Identify Device", 128, NULL, 2, NULL);
}

void indicateStationModeTask(void *_args) {
  while (true) {
    blinkIt(LED_ORANGE, 4, 200);
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

void prepLED(uint8_t ledPin, bool isNeoPixel) {
  Utils_Pin_LED = ledPin;
  ledIsNeoPixel = isNeoPixel;
  gpio_enable(Utils_Pin_LED, GPIO_OUTPUT);
  if (!ledIsNeoPixel) {
    pwm_init(1, &Utils_Pin_LED, false);
    pwm_set_freq(1000);
    performingPWM = false;
  }
  setLED(false);
}

// ----- Reset Handling -----
void resetConfigTask() {
  // Flash the LED first before we start the reset
  blinkIt(LED_RED, 5, 100);

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

// ----- Common Callback Handlers
void homekitEventHandler(homekit_event_t event) {
  switch(event) {
    case HOMEKIT_EVENT_SERVER_INITIALIZED:
      printf("Server Initialized\n");
      break;
    case HOMEKIT_EVENT_CLIENT_CONNECTED:
      printf("Client Connected\n");
      break;
    case HOMEKIT_EVENT_CLIENT_VERIFIED:
      printf("Client Verified\n");
      break;
    case HOMEKIT_EVENT_CLIENT_DISCONNECTED:
      printf("Client Disconnected\n");
      break;
    case HOMEKIT_EVENT_PAIRING_ADDED:
      printf("Pairing was added\n");
      break;
    case HOMEKIT_EVENT_PAIRING_REMOVED:
      printf("Pairing was removed\n");
      break;
    default:
      printf("Unknown event type: %d\n", event);
      break;
  }
}

void logWiFiEvent(wifi_config_event_t event) {
  switch (event) {
    case WIFI_CONFIG_CONNECTED:
      printf("Connected to WiFi\n");
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

// ----- Debugging -----
void dumpCharacteristics(homekit_accessory_t **firstAccessory) {
  for (homekit_accessory_t **accessory_it = firstAccessory; *accessory_it; accessory_it++) {
      homekit_accessory_t *accessory = *accessory_it;

      for (homekit_service_t **service_it = accessory->services; *service_it; service_it++) {
          homekit_service_t *service = *service_it;

          for (homekit_characteristic_t **ch_it = service->characteristics; *ch_it; ch_it++) {
              homekit_characteristic_t *ch = *ch_it;

            printf("id: 0x%x, type: %s, desc: %s\n", ch->id, ch->type, ch->description);
          }
      }
  }
}