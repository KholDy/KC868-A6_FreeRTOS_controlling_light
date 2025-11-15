#include "info.h"

static cJSON *infoJSON;

const char* get_chip_model(esp_chip_model_t model) {
    switch (model) {
        case CHIP_ESP32:
            return "ESP32";
        case CHIP_ESP32S2:
            return "ESP32-S2";
        case CHIP_ESP32S3:
            return "ESP32-S3";
        case CHIP_ESP32C3:
            return "ESP32-C3";
        case CHIP_ESP32C2:
            return "ESP32-C2";
        case CHIP_ESP32C6:
            return "ESP32-C6";
        case CHIP_ESP32H2:
            return "ESP32-H2";
        case CHIP_ESP32P4:
            return "ESP32-P4";
        case CHIP_POSIX_LINUX:
            return "POSIX/Linux Simulator";
        default:
            return "Unknown Model";
    }
}

cJSON * get_chip_info() {
    infoJSON = cJSON_CreateObject();

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON_AddStringToObject(infoJSON, "chip_info", get_chip_model(chip_info.model));
    cJSON_AddNumberToObject(infoJSON, "chip_core", chip_info.cores);
    cJSON_AddNumberToObject(infoJSON, "chip_revision", chip_info.revision);

    rtc_cpu_freq_config_t freq_config;
    rtc_clk_cpu_freq_get_config(&freq_config);

    const char* clk_source_str;
    switch (freq_config.source) {
        case SOC_CPU_CLK_SRC_PLL:
            clk_source_str = "PLL";
            break;
        case SOC_CPU_CLK_SRC_APLL:
            clk_source_str = "APLL";
            break;
        case SOC_CPU_CLK_SRC_XTAL:
            clk_source_str = "XTAL";
            break;
        default:
            clk_source_str = "Unknown";
            break;
    }

    cJSON_AddStringToObject(infoJSON, "CPU Clock Source", clk_source_str);
    cJSON_AddNumberToObject(infoJSON, "Source Clock Frequency(MHz):", freq_config.source_freq_mhz);
    cJSON_AddNumberToObject(infoJSON, "Divider:", freq_config.div);
    cJSON_AddNumberToObject(infoJSON, "Effective CPU Frequency(MHz):", freq_config.freq_mhz);

    uint32_t features = chip_info.features;
    char binary_str[33]; // 32 bits + null terminator
    for (int i = 31; i >= 0; i--) {
        binary_str[31 - i] = (features & (1U << i)) ? '1' : '0';
    }
    binary_str[32] = '\0'; // Null terminate the string

    cJSON_AddStringToObject(infoJSON, "Features Bitmap:", binary_str);
    cJSON_AddStringToObject(infoJSON, "Embedded Flash:", (features & CHIP_FEATURE_EMB_FLASH) ? "Yes" : "No");
    cJSON_AddStringToObject(infoJSON, "Embedded PSRAM:", (features & CHIP_FEATURE_EMB_PSRAM) ? "Yes" : "No");
    cJSON_AddStringToObject(infoJSON, "Wi-Fi 2.4GHz support:", (features & CHIP_FEATURE_WIFI_BGN) ? "Yes" : "No");
    cJSON_AddStringToObject(infoJSON, "IEEE 802.15.4 support:", (features & CHIP_FEATURE_IEEE802154) ? "Yes" : "No");
    cJSON_AddStringToObject(infoJSON, "Bluetooth Classic support:", (features & CHIP_FEATURE_BT) ? "Yes" : "No");

    // Flash Size
    cJSON *memoryJSON;
    cJSON_AddItemToObject(infoJSON, "memory", memoryJSON = cJSON_CreateObject());
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (partition) {
        cJSON_AddStringToObject(memoryJSON, "Partition Label:", partition->label);
        cJSON_AddNumberToObject(memoryJSON, "Partition Type:", partition->type);
        cJSON_AddNumberToObject(memoryJSON, "Partition Subtype:", partition->subtype);
        cJSON_AddNumberToObject(memoryJSON, "Partition Size(Bytes):", partition->size);
    } else {
        cJSON_AddStringToObject(memoryJSON, "Memory Info", "Failed to get the App partition");
    }

    // Total SPIRAM (PSRAM) Size
    size_t spiram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (spiram_size) {
        cJSON_AddNumberToObject(memoryJSON, "PSRAM Size(Bytes):", spiram_size);
    } else {
        cJSON_AddStringToObject(memoryJSON, "Memory Info", "No PSRAM detected");
    }

    uint32_t total_internal_memory = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint32_t free_internal_memory = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest_contig_internal_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    cJSON_AddNumberToObject(memoryJSON, "Total DRAM (internal memory(Bytes)):", total_internal_memory);
    cJSON_AddNumberToObject(memoryJSON, "Free DRAM (internal memory(Bytes))", free_internal_memory);
    cJSON_AddNumberToObject(memoryJSON, "Largest free contiguous DRAM block(Bytes):", largest_contig_internal_block);

    return infoJSON;
}