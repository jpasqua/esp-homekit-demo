#ifndef UTILS_H
#define UTILS_H

// ----- Macros -----
#define Q(x) #x
#define QUOTE(x) Q(x)

// ----- General Utlitities -----
void delayMS(uint32_t delayMillis);
void prepLogging();

// ----- LED-related -----
void setLED(bool on);
  // on==true  -> Set the LED to LED_WHITE (meaning ON for a mono LED)
  // on==false -> Set the LED to LED_BLACK (meaning OFF for a mono LED)

void setLEDColor(uint32_t color);
  // Set the LED to the specified color. If it is a mono LED, the color
  // will be converted to grayscale and displayed using PWM

void blinkInBackground(uint32_t color, uint8_t cycles, uint32_t delayMillis);
  // Switch the LED between the specified color and LED_BLACK 'cycles' times
  // Pause for 'delayMillis' between each cycle. If it is a mono LED, then
  // it will cycle between ON and OFF - not converted to grayscale and PWM'd.

void identifyDevice(homekit_value_t _value);

void indicateStationMode(bool on);

void prepLED(uint8_t pin, bool isNeoPixel);

#define LED_WHITE   (0xFFFFFF)
#define LED_BLACK   (0x000000)
#define LED_RED     (0xFF0000)
#define LED_GREEN   (0x00FF00)
#define LED_BLUE    (0x0000FF)
#define LED_YELLOW  (0xFFFF00)
#define LED_LTGRAY  (0xC0C0C0)
#define LED_GRAY    (0x808080)
#define LED_PURPLE  (0xB603FC)
#define LED_ORANGE  (0xFCB103)

// ----- Reset Handling -----
void resetConfig();


// ----- Common Callback Handlers
void homekitEventHandler(homekit_event_t event);
void logWiFiEvent(wifi_config_event_t event);

// ----- Debugging -----
void dumpCharacteristics(homekit_accessory_t **firstAccessory);

#endif  // UTILS_H