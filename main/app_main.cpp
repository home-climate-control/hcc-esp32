/*
 * Home Climate Control ESP-32 based edge device firmware.
 *
 * https://www.homeclimatecontrol.com/ - the Home Climate Control project family
 * https://github.com/home-climate-control/hcc-esp32 - this project on GitHub
 *
 * 1-Wire code based on https://github.com/DavidAntliff/esp32-ds18b20-example
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"

#include "cJSON.h"

#include "stepper_api.h"

#if !(CONFIG_HCC_ESP32_ONE_WIRE_ENABLE || CONFIG_HCC_ESP32_A4988_ENABLE)
#error "No components enabled, configuration doesn't make sense. Run 'idf.py menuconfig' to enable."
#endif

static const char *TAG = "hcc-esp32";

#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE

#define GPIO_DS18B20_0       (CONFIG_ONE_WIRE_GPIO)
#define MAX_DEVICES          (CONFIG_HCC_ESP32_ONE_WIRE_MAX_DEVICES)
#define DS18B20_RESOLUTION   (DS18B20_RESOLUTION_12_BIT)
#define SAMPLE_PERIOD_MILLIS        (1000 * CONFIG_ONE_WIRE_POLL_SECONDS)


owb_rmt_driver_info rmt_driver_info;
OneWireBus *owb;
int devices_found = 0;
DS18B20_Info *devices[MAX_DEVICES] = {0};

typedef struct sensor_t {
    char address[17];
    char *topic;
} sensor;

sensor sensors[MAX_DEVICES] = {};
#endif

char device_id[19];
char *edge_pub_topic;
char *mqtt_hello;

struct hello {
    const char *entity_type;
    const char *device_id;
    const char *sources[];
};

struct sensor_sample {
    const char *entity_type;
    const char *name;
    const char *signature;
    const float signal;
    const long timestamp;
    const char *device_id;
};

esp_mqtt_client_handle_t mqtt_client;

void log_component_setup()
{
#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE
    ESP_LOGI(TAG, "[conf/component] 1-Wire: enabled");
#else
    ESP_LOGI(TAG, "[conf/component] 1-Wire: DISABLED");
#endif

#ifdef CONFIG_HCC_ESP32_A4988_ENABLE
    ESP_LOGI(TAG, "[conf/component] A4988:  enabled");
#else
    ESP_LOGI(TAG, "[conf/component] A4988:  DISABLED");
#endif
}

void log_onewire_configuration()
{
#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE

    ESP_LOGI(TAG, "[conf/1-Wire] GPIO pin: %d", CONFIG_ONE_WIRE_GPIO);
    ESP_LOGI(TAG, "[conf/1-Wire] sampling interval: %ds", CONFIG_ONE_WIRE_POLL_SECONDS);

#endif
}

void log_a4988_configuration()
{
#ifdef CONFIG_HCC_ESP32_A4988_ENABLE

    ESP_LOGI(TAG, "[conf/A4988] DIR pin:  %d", CONFIG_HCC_ESP32_A4988_PIN_DIR);
    ESP_LOGI(TAG, "[conf/A4988] STEP pin: %d", CONFIG_HCC_ESP32_A4988_PIN_STEP);

#ifdef CONFIG_HCC_ESP32_A4988_MICROSTEPPING
    ESP_LOGI(TAG, "[conf/A4988] microstepping enabled");
    ESP_LOGI(TAG, "[conf/A4988] MS1 pin:  %d", CONFIG_HCC_ESP32_A4988_PIN_MS1);
    ESP_LOGI(TAG, "[conf/A4988] MS2 pin:  %d", CONFIG_HCC_ESP32_A4988_PIN_MS2);
    ESP_LOGI(TAG, "[conf/A4988] MS3 pin:  %d", CONFIG_HCC_ESP32_A4988_PIN_MS3);
#else
    ESP_LOGI(TAG, "[conf/A4988] microstepping disabled");
#endif

#ifdef CONFIG_HCC_ESP32_A4988_POWERSAVE
    ESP_LOGI(TAG, "[conf/A4988] power save enabled");
    ESP_LOGI(TAG, "[conf/A4988] SLP pin:  %d", CONFIG_HCC_ESP32_A4988_PIN_SLP);
#else
    ESP_LOGI(TAG, "[conf/A4988] power save disabled");
#endif

#endif
}

void log_configuration(void)
{
    log_component_setup();

    ESP_LOGI(TAG, "[conf/MQTT] broker: %s", CONFIG_BROKER_URL);
    ESP_LOGI(TAG, "[conf/MQTT] pub root: %s", CONFIG_BROKER_PUB_ROOT);
    ESP_LOGI(TAG, "[conf/MQTT] sub root: %s", CONFIG_BROKER_SUB_ROOT);

    log_onewire_configuration();
    log_a4988_configuration();
}

/**
 * Allocates memory and returns the MQTT message rendered as follows in the example below,
 * but in one line (multiline for readability).
 *
 * {
 *  "deviceId": "ESP32-246F28A7C53C",
 *  "entityType": "sensor",
 *  "sources": [
 *      "D90301A2792B0528",
 *      "E40300A27970F728"
 *  ]
 * }
 */
void create_hello()
{

    cJSON *json_root = cJSON_CreateObject();
    cJSON_AddItemToObject(json_root, "entity_type", cJSON_CreateString("sensor"));
    cJSON_AddItemToObject(json_root, "device_id", cJSON_CreateString(device_id));

#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE
    const char *sources[devices_found];
    for (int offset = 0; offset < devices_found; offset++) {
        sources[offset] = (char *) &sensors[offset].address;
    }

    cJSON *json_sources = cJSON_CreateStringArray(sources, devices_found);
#else
    const char *sources[0];
    cJSON *json_sources = cJSON_CreateStringArray(sources, 0);
#endif

    cJSON_AddItemToObject(json_root, "sources", json_sources);

    // VT: NOTE: Careful, this allocates memory, need to delete it if we're going to re-render it
    mqtt_hello = cJSON_PrintUnformatted(json_root);
    ESP_LOGI(TAG, "[mqtt] %s %s", edge_pub_topic, mqtt_hello);

    cJSON_Delete(json_root);
}

/**
 * Sets device_id to "ESP32-${esp_read_mac()}";
 * Sets edge_pub_topic to "{@CONFIG_BROKER_PUB_ROOT}/edge/".
 */
void create_identity()
{

    uint8_t id[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(id, ESP_MAC_WIFI_STA));

    strcpy(device_id, "ESP32-");
    sprintf(device_id + 6, "%02X%02X%02X%02X%02X%02X", id[0], id[1], id[2], id[3], id[4], id[5]);

    edge_pub_topic = (char *)malloc(strlen(CONFIG_BROKER_PUB_ROOT) + 5 + 1);

    strcpy(edge_pub_topic, CONFIG_BROKER_PUB_ROOT);
    strcpy(edge_pub_topic + strlen(CONFIG_BROKER_PUB_ROOT), "/edge");

    ESP_LOGI(TAG, "[id] device id: %s", device_id);

    create_hello();
}

/**
 * Allocates memory and returns the sensor topic rendered as "${CONFIG_BROKER_PUB_ROOT}/sensor/${ADDRESS}"
 */
char *create_topic_from_address(const char *address)
{

    const char *sensor = "/sensor/";
    int bufsize = strlen(CONFIG_BROKER_PUB_ROOT) + strlen(sensor) + strlen(address) + 1;
    char *result = (char *) malloc(bufsize);

    strcpy(result, CONFIG_BROKER_PUB_ROOT);
    strcpy(result + strlen(CONFIG_BROKER_PUB_ROOT), sensor);
    strcpy(result + strlen(CONFIG_BROKER_PUB_ROOT) + strlen(sensor), address);

    return result;
}

void onewire_start(void)
{
#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE
    // To debug OWB, use 'make menuconfig' to set default Log level to DEBUG, then uncomment:
    //esp_log_level_set("owb", ESP_LOG_DEBUG);

    // Stable readings require a brief period before communication
    vTaskDelay(2000.0 / portTICK_PERIOD_MS);

    // Create a 1-Wire bus, using the RMT timeslot driver
    owb = owb_rmt_initialize(&rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);

    // enable CRC check for ROM code
    owb_use_crc(owb, true);

    ESP_LOGI(TAG, "[1-Wire] looking for connected devices on pin %d...", GPIO_DS18B20_0);

    OneWireBus_ROMCode device_rom_codes[MAX_DEVICES] = {};
    OneWireBus_SearchState search_state = {};
    bool found = false;
    owb_search_first(owb, &search_state, &found);
    while (found) {
        char rom_code_s[17];
        owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
        strupr(rom_code_s);
        ESP_LOGI(TAG, "[1-Wire] %d: %s", devices_found, rom_code_s);
        device_rom_codes[devices_found] = search_state.rom_code;

        strcpy(sensors[devices_found].address, rom_code_s);
        sensors[devices_found].topic = create_topic_from_address(rom_code_s);

        ++devices_found;
        owb_search_next(owb, &search_state, &found);
    }
    ESP_LOGI(TAG, "[1-Wire] found %d device%s", devices_found, devices_found == 1 ? "" : "s");

    // Create DS18B20 devices on the 1-Wire bus
    for (int i = 0; i < devices_found; ++i) {
        DS18B20_Info *ds18b20_info = ds18b20_malloc();
        devices[i] = ds18b20_info;

        if (devices_found == 1) {
            ESP_LOGD(TAG, "[1-Wire] single device optimizations enabled");
            ds18b20_init_solo(ds18b20_info, owb);
        } else {
            // associate with bus and device
            ds18b20_init(ds18b20_info, owb, device_rom_codes[i]);
        }

        // enable CRC check for temperature readings
        ds18b20_use_crc(ds18b20_info, true);
        ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION);
    }
#endif
}

void mqtt_send_sample(int offset, float signal)
{

#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE

    sensor s = sensors[offset];

    // VT: NOTE: For now, we just have temperature sensors, this may change in the future
    char signature[strlen(s.address) + 2];
    strcpy((char *)&signature[0], "T");
    strcpy((char *)&signature[1], s.address);

    cJSON *json_root = cJSON_CreateObject();
    cJSON_AddItemToObject(json_root, "entity_type", cJSON_CreateString("sensor"));
    cJSON_AddItemToObject(json_root, "name", cJSON_CreateString(s.address));
    cJSON_AddItemToObject(json_root, "signature", cJSON_CreateString(signature));
    cJSON_AddNumberToObject(json_root, "signal", signal);
    cJSON_AddItemToObject(json_root, "device_id", cJSON_CreateString(device_id));

    char *message = cJSON_PrintUnformatted(json_root);
    ESP_LOGI(TAG, "[mqtt] %s %s", s.topic, message);

    int msg_id = esp_mqtt_client_publish(mqtt_client, s.topic, message, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

    free(message);

    cJSON_Delete(json_root);

#else
    ESP_LOGE(TAG, "1-Wire not enabled, not sending anything");
#endif

}

void onewire_poll(void)
{
#ifdef CONFIG_HCC_ESP32_ONE_WIRE_ENABLE
    // Read temperatures more efficiently by starting conversions on all devices at the same time
    int errors_count[MAX_DEVICES] = {0};

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        last_wake_time = xTaskGetTickCount();

        ds18b20_convert_all(owb);

        // In this application all devices use the same resolution,
        // so use the first device to determine the delay
        ds18b20_wait_for_conversion(devices[0]);

        // Read the results immediately after conversion otherwise it may fail
        // (using printf before reading may take too long)
        float readings[MAX_DEVICES] = { 0 };
        DS18B20_ERROR errors[MAX_DEVICES] = {};

        for (int offset = 0; offset < devices_found; ++offset) {
            errors[offset] = ds18b20_read_temp(devices[offset], &readings[offset]);
        }

        // Report results in a separate loop, after all have been read
        for (int offset = 0; offset < devices_found; ++offset) {
            if (errors[offset] != DS18B20_OK) {
                // VT: NOTE: What exactly does the error count indicate here? Need to RTFM again.
                ++errors_count[offset];
            }

            ESP_LOGI(TAG, "[1-Wire] %s: %.1fC, %d errors", sensors[offset].topic, readings[offset], errors_count[offset]);

            mqtt_send_sample(offset, readings[offset]);
        }

        // VT: NOTE: This call will block and make parallel processing impossible
        vTaskDelayUntil(&last_wake_time, SAMPLE_PERIOD_MILLIS / portTICK_PERIOD_MS);
    }
#endif
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[mqtt] connected to %s", CONFIG_BROKER_URL);
        msg_id = esp_mqtt_client_publish(client, edge_pub_topic, mqtt_hello, 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb((esp_mqtt_event_handle_t) event_data);
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = CONFIG_BROKER_URL;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "[core] Oh, hai");
    ESP_LOGI(TAG, "[core] free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[core] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    log_configuration();

    onewire_start();
    create_identity();

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    mqtt_start();
    onewire_poll();
}
