//
// Homekit Button
//
// Notes:
// o Uses two physical buttons: one for ON and the other for OFF
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
// Building:
// o Sample make command:
//   make -C examples/button all -DDEV_PASS="123-45-678" -DDEV_SERIAL="1200345" -DDEV_SETUP="J81Q"
// o Generating a qrcode
//   esp-homekit-demo/components/common/homekit/tools/gen_qrcode 15 123-45-678 J81Q qrcode.png
//


#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include <button.h>


extern void sdk_system_restart();   // TO DO: What is the right include file for this?


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

static const uint8_t BUTTON_PIN_ON = 4;     // D2
static const uint8_t BUTTON_PIN_OFF = 2;    // D4
static const uint8_t LED_PIN = 15;          // D8

char dev_model_name[] = "JPButton";
char dev_setup_id[] = QUOTE(DEV_SETUP);
char dev_password[] = QUOTE(DEV_PASS);
char dev_serial[]   = QUOTE(DEV_SERIAL);
char dev_name[]     = QUOTE(DEV_NAME);


/*------------------------------------------------------------------------------
 *
 * LED Handling
 *
 *----------------------------------------------------------------------------*/

void blinkIt(uint8_t cycles, uint32_t delayMillis) {
    for (int i  = 0; i < cycles; i++) {
        gpio_write(LED_PIN, 1);
        vTaskDelay(delayMillis / portTICK_PERIOD_MS);
        gpio_write(LED_PIN, 0);
        vTaskDelay(delayMillis / portTICK_PERIOD_MS);
    }
}

void led_init() {
    gpio_enable(LED_PIN, GPIO_OUTPUT);
    gpio_write(LED_PIN, 0);
}

void led_identify_task(void *_args) {
    for (int i = 0; i < 3; i++) {
        blinkIt(3, 100);
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
    gpio_write(LED_PIN, 0);

    vTaskDelete(NULL);
}

void led_identify(homekit_value_t _value) {
    printf("LED identify\n");
    xTaskCreate(led_identify_task, "LED identify", 128, NULL, 2, NULL);
}


/*------------------------------------------------------------------------------
 *
 * Reset Handling
 *
 *----------------------------------------------------------------------------*/

void reset_configuration_task() {
    //Flash the LED first before we start the reset
    blinkIt(5, 100);

    printf("Resetting Wifi Config\n");
    wifi_config_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Resetting HomeKit Config\n");
    homekit_server_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Restarting\n");
    sdk_system_restart();
    vTaskDelete(NULL);
}

void reset_configuration() {
  printf("Resetting configuration\n");
  xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}

/*------------------------------------------------------------------------------
 *
 * Button Handling
 *
 *----------------------------------------------------------------------------*/

homekit_characteristic_t button_event = HOMEKIT_CHARACTERISTIC_(PROGRAMMABLE_SWITCH_EVENT, 0);

void on_button_callback(button_event_t event, void *context) {
    if (event == button_event_single_press) {
        printf("single press of on button\n");
        homekit_characteristic_notify(&button_event, HOMEKIT_UINT8(0));
        blinkIt(1, 50);
    } else if (event == button_event_long_press) {
        printf("long press of on button\n");
        homekit_characteristic_notify(&button_event, HOMEKIT_UINT8(2));
        blinkIt(2, 75);
    } else {
        printf("Unused button event: %d\n", event);        
    }
}

void off_button_callback(button_event_t event, void *context) {
    if (event == button_event_single_press) {
        printf("single press of off button\n");
        homekit_characteristic_notify(&button_event, HOMEKIT_UINT8(1));
        blinkIt(3, 50);
    } else if (event == button_event_long_press) {
        reset_configuration();
    } else {
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



/*------------------------------------------------------------------------------
 *
 * Setup
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
                    HOMEKIT_CHARACTERISTIC(NAME, dev_name),
                    HOMEKIT_CHARACTERISTIC(MANUFACTURER, "HaPK"),
                    HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, dev_serial),
                    HOMEKIT_CHARACTERISTIC(MODEL, dev_model_name),
                    HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                    NULL
                },
            ),
            HOMEKIT_SERVICE(
                STATELESS_PROGRAMMABLE_SWITCH,
                .primary=true,
                .characteristics=(homekit_characteristic_t*[]) {
                    HOMEKIT_CHARACTERISTIC(NAME, dev_name),
                    &button_event,
                    NULL
                },
            ),
            NULL
        },
    ),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = dev_password,
    .setupId = dev_setup_id,
    .on_event = homekit_event_handler
};

void on_wifi_ready() {
    homekit_server_init(&config);
}

void user_init(void) {
    uart_set_baud(0, 115200);

    printf("dev_setup_id = %s\n", dev_setup_id);
    printf("dev_password = %s\n", dev_password);
    printf("dev_serial = %s\n", dev_serial);
    printf("dev_name = %s\n", dev_name);

    wifi_config_init(dev_model_name, NULL, on_wifi_ready);

    button_config_t button_config = BUTTON_CONFIG(
        button_active_low, 
        .max_repeat_presses=2,
        .long_press_time=2000,
    );
    if (button_create(BUTTON_PIN_ON, button_config, on_button_callback, NULL)) {
        printf("Failed to initialize on button\n");
    }
    button_config.long_press_time  = 8000;
    if (button_create(BUTTON_PIN_OFF, button_config, off_button_callback, NULL)) {
        printf("Failed to initialize off button\n");
    }
    led_init();
}
