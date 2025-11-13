#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_system.h"             // esp_init functions esp_err_t
#include "esp_wifi.h"               // esp_wifi_init functions and wifi operations
#include "esp_log.h"                // for showinf logs
#include "esp_event.h"              // for wifi event
#include "nvs_flash.h"              // non volatile storage
#include <esp_http_server.h>
#include <cJSON.h>

#include "driver/i2c_types.h"
#include "driver/i2c_master.h"

#define LIGHT_1             1
#define LIGHT_2             2
#define LIGHT_3             3
#define LIGHT_4             4
#define LIGHT_5             5
#define LIGHT_6             6
#define LIGHT_ALL           7
#define LIGHT_FIRST_HALF    8
#define LIGHT_SECOND_HALF   9

const char* logTAG = "I2C";

TaskHandle_t i2cTaskHandle = NULL;
TaskHandle_t webServerTaskHandle = NULL;

static uint8_t relay = 0x00;

static cJSON *lightJSON;
static const char *TAG = "Light-App";

const char *SSID = "";
const char *PASS = "";

static char *state_relay = "false";

int retry_num = 0;

typedef struct {
    int number;
    bool state;
} light;

light lightforQueueTX;
light lightforQueueRX;

QueueHandle_t lightQueue;

// =============================================================================================================================
// FREERTOS_TASK_I2C_PCF8574 
// =============================================================================================================================
void I2C_Task(void *arg) {
    // Настройка и запуск шины I2C #0
    i2c_master_bus_config_t i2c_master_config;
    i2c_master_config.clk_source = I2C_CLK_SRC_DEFAULT;     // Clock source for the bus
    i2c_master_config.i2c_port = I2C_NUM_0;                 // Bus number (I2C_NUM_0 or I2C_NUM_1)
    i2c_master_config.scl_io_num = GPIO_NUM_15;             // Number GPIO SCL
    i2c_master_config.sda_io_num = GPIO_NUM_4;              // Number GPIO SDА
    i2c_master_config.flags.enable_internal_pullup = 1;     // Use the built-in GPIO pull-up.
    i2c_master_config.glitch_ignore_cnt = 7;                // Data bus failure period, default value 7
    i2c_master_config.intr_priority = 0;                    // Interrupt priority: auto
    i2c_master_config.trans_queue_depth = 0;                // Internal queue depth. Valid only for asynchronous transmissions.
    // Setup bus
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_config, &bus_handle)); 
    ESP_LOGI(logTAG, "I2C bus is configured");
    // Сканирование шины (поиск устройств)
    for (uint8_t i = 1; i < 128; i++) {
        if (i2c_master_probe(bus_handle, i, -1) == ESP_OK) {
        ESP_LOGI(logTAG, "Found device on bus 0 at address 0x%.2X", i);
        };
    };
    // Setup slave-device
    i2c_device_config_t i2c_device_config;
    i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; 
    i2c_device_config.device_address = 0x24;                
    i2c_device_config.scl_speed_hz = 100000;                // Bus clock frequency 100 kHz
    i2c_device_config.scl_wait_us = 0;                      // Default timeout
    i2c_device_config.flags.disable_ack_check = 0;          // Do not disable ACK checking
    // Setup PCF8574
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &i2c_device_config, &dev_handle));
    ESP_LOGI(logTAG, "PCF8574 is configured");

    uint8_t value = 0xFF;
    while(1){
        if( xQueueReceive(lightQueue, &lightforQueueRX, portMAX_DELAY)) {
            // Записываем значение в PCF8574
            ESP_LOGI(logTAG, "PCF8574 RX: 0x%.2X", lightforQueueRX.number);
            if (lightforQueueRX.state) {
                value &= lightforQueueRX.number;
                ESP_LOGI(logTAG, "PCF8574 true: 0x%.2X", value);
            } else {
                value |= ~lightforQueueRX.number;
                ESP_LOGI(logTAG, "PCF8574 false: 0x%.2X", value);
            }
             
            
            i2c_master_transmit(dev_handle, &value, sizeof(value), -1);
        }
    }
}
// =============================================================================================================================
// SETUP_WEB_SERVER
// =============================================================================================================================
// Callback function of the HTTP GET request
esp_err_t esp_light_get_handler(httpd_req_t *req) {
    cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "state", cJSON_CreateString(state_relay));
    //Send data in JSON containing the status of smart lights to the client
    httpd_resp_send(req, cJSON_Print(lightJSON), strlen(cJSON_Print(lightJSON)));
    return ESP_OK;
}

//Callback function of the HTTP POST request
static char buffer[100];

esp_err_t esp_light_set_handler(httpd_req_t *req) {
    int ret, remaining = req->content_len;
    memset(buffer, 0 , sizeof(buffer));

    while (remaining > 0) {
        //Read HTTP request data
        if ((ret = httpd_req_recv(req, buffer, remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    
    cJSON *json = cJSON_Parse(buffer);
    cJSON *number = cJSON_GetObjectItemCaseSensitive(json, "number");

    if(cJSON_IsNumber(number) && (number->valueint != NULL)) {
        if(number->valueint == LIGHT_1) {
            relay = 0b11111110;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_1));
        }
        if(number->valueint == LIGHT_2) {
            relay = 0b11111101;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_2));
        }
        if(number->valueint == LIGHT_3) {
            relay = 0b11111011;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_3));
        }
        if(number->valueint == LIGHT_4) {
            relay = 0b11110111;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_4));
        }
        if(number->valueint == LIGHT_5) {
            relay = 0b11101111;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_5));
        }
        if(number->valueint == LIGHT_6) {
            relay = 0b11011111;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_6));
        }
        if(number->valueint == LIGHT_ALL) {
            relay = 0b11000000;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_ALL));
        }
        if(number->valueint == LIGHT_FIRST_HALF) {
            relay = 0b11111000;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_FIRST_HALF));
        }
        if(number->valueint == LIGHT_SECOND_HALF) {
            relay = 0b11000111;
            cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "number", cJSON_CreateNumber(LIGHT_SECOND_HALF));
        }
    }

    cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
    if(cJSON_IsString(state) && (state->valuestring != NULL)) {
        if(!strcmp(state->valuestring, "true")) {
            state_relay = "true";
            lightforQueueTX.number = relay;
            lightforQueueTX.state = true;
            xQueueSend(lightQueue, &lightforQueueTX, (TickType_t)0);
        } else if(!strcmp(state->valuestring, "false")) {
            state_relay = "false";
            lightforQueueTX.number = relay;
            lightforQueueTX.state = false;
            xQueueSend(lightQueue, &lightforQueueTX, (TickType_t)0);
        }
    } 
    
    cJSON_ReplaceItemInObjectCaseSensitive(lightJSON, "state", cJSON_CreateString(state_relay));
    httpd_resp_send(req, cJSON_Print(lightJSON), strlen(cJSON_Print(lightJSON)));

    return ESP_OK;
}

//Callback function corresponding to GET
static const httpd_uri_t status = {
    .uri = "/lights",
    .method = HTTP_GET,
    .handler = esp_light_get_handler,
};

//Callback function corresponding to POST
static const httpd_uri_t ctrl = {
    .uri = "/lights",
    .method = HTTP_POST,
    .handler = esp_light_set_handler,
};

esp_err_t esp_start_webserver() {

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    //Start the HTTP server
    ESP_LOGI(TAG, "Starting server on port: ’%d’", config. server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        //Set the callback function corresponding to the HTTP URI
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &ctrl);

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!" );

    return ESP_FAIL;
}
// =============================================================================================================================
// SETUP_WIFI
// =============================================================================================================================
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,void *event_data) {
    if(event_id == WIFI_EVENT_STA_START) {      
        printf("WIFI CONNECTING....\n");        
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {     
        printf("WiFi CONNECTED\n");             
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {  
        printf("WiFi lost connection\n");       
        if(retry_num<5) {
            esp_wifi_connect();                 
            retry_num++;printf("Retrying to Connect...\n");
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        printf("Wifi got IP...\n\n");

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        //printf("My IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        char *ip = (char*)malloc(24 * sizeof(char)); 
        sprintf(ip, "%d.%d.%d.%d", IP2STR(&event->ip_info.ip));
        cJSON_AddStringToObject(lightJSON, "ip", ip);
    }
} 

void wifi_connection() {

    // network interdace initialization
    esp_netif_init();
    ESP_LOGI(TAG, "esp_netif_init()");

    // responsible for handling and dispatching events
    esp_event_loop_create_default();

    // sets up necessaru data struct for wifi station interface
    esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "esp_netif_create_default_wifi_sta()");

    // sets up wifi_init_config struct with default values
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT()");

    // wifi initialised with dafault wifi_initiation
    esp_wifi_init(&wifi_init);
    ESP_LOGI(TAG, "esp_wifi_init(&wifi_init)");

    // creating event handler register for wifi
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    ESP_LOGI(TAG, "esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL)");

    // creating event handler register for ip event
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    ESP_LOGI(TAG, "esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL)");

    // struct wifi_config_t var wifi_configuration
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = "",
            .password = ""
        }
    };
    ESP_LOGI(TAG, " wifi_config_t wifi_configuration = {}");

    //gpio_set_level(relay, 1);
    strcpy((char*) wifi_configuration.sta.ssid, SSID);
    strcpy((char*) wifi_configuration.sta.password, PASS);
    ESP_LOGI(TAG, "strcpy");

    // setting up configs when event ESP_IF_WIFI_STA
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    ESP_LOGI(TAG, "esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration)");

    // start connection with configurations provided in funtion
    esp_wifi_start();
    ESP_LOGI(TAG, "esp_wifi_start()");

    // station mode selected
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "esp_wifi_set_mode(WIFI_MODE_STA)");

    // connect with saved ssid and pass
    esp_wifi_connect();
    ESP_LOGI(TAG, "esp_wifi_connect()");
    printf( "wifi_init_softap finished. SSID:%s  password:%s", wifi_configuration.sta.ssid,
                                                               wifi_configuration.sta.password);
}      
// =============================================================================================================================
// FREERTOS_TASK_WIFI_CONNECT_AND_WEB_SERVER
// =============================================================================================================================
void webServer_Task(void *arg) {
    wifi_connection();
    esp_start_webserver();

    vTaskDelete(NULL);
}

void app_main(void) {
    lightJSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(lightJSON, "id", 1);
    cJSON_AddStringToObject(lightJSON, "description", "Balcony lighting");
    cJSON_AddNumberToObject(lightJSON, "number", 1);
    cJSON_AddStringToObject(lightJSON, "state", state_relay);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lightQueue = xQueueCreate(10, sizeof(light));

    xTaskCreate(webServer_Task, "webServer_Task", 4096, NULL, 5, &webServerTaskHandle);
    xTaskCreate(I2C_Task, "I2C_Task", 2048, NULL, 5, &i2cTaskHandle);
}
