#ifndef INFO_H
#define INFO_H

#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/rtc.h"	
#include "esp_pm.h"		
#include "esp_partition.h"
#include "spi_flash_mmap.h"

#include <cJSON.h>

cJSON * get_chip_info();

#endif