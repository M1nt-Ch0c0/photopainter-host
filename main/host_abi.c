/* Host ABI v1: stable exports, independent of any payload build. */
#include "esp_elf.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
extern int longjmp;
extern int setjmp;
extern int photoframe_host_report_result;
extern int i2c_master_bus_add_device;
extern int spi_device_polling_transmit;
extern int gpio_config;
extern int floor;
extern int i2c_master_transmit;
extern int i2c_master_bus_rm_device;
extern int __bswapsi2;
extern int memcpy;
extern int __floatsidf;
extern int malloc;
extern int __fixunsdfsi;
extern int __adddf3;
extern int gpio_set_level;
extern int abort;
extern int __floatunsidf;
extern int xTaskGetTickCount;
extern int spi_bus_free;
extern int __fixdfsi;
extern int calloc;
extern int fprintf;
extern int __ledf2;
extern int photoframe_host_png_size;
extern int pow;
extern int heap_caps_malloc;
extern int memcmp;
extern int spi_bus_remove_device;
extern int __divdf3;
extern int __muldf3;
extern int fread;
extern int spi_bus_add_device;
extern int memset;
extern int heap_caps_free;
extern int i2c_new_master_bus;
extern int i2c_del_master_bus;
extern int stderr;
extern int fputc;
extern int photoframe_host_png_data;
extern int vTaskDelay;
extern int spi_bus_initialize;
extern int strlen;
extern int __gedf2;
extern int gpio_get_level;
extern int i2c_master_transmit_receive;
extern int free;
#pragma GCC diagnostic pop
esp_elf_symbol_table_t g_esp_photoframe_elfsyms[] = {
    ESP_ELFSYM_EXPORT(longjmp),
    ESP_ELFSYM_EXPORT(setjmp),
    ESP_ELFSYM_EXPORT(photoframe_host_report_result),
    ESP_ELFSYM_EXPORT(i2c_master_bus_add_device),
    ESP_ELFSYM_EXPORT(spi_device_polling_transmit),
    ESP_ELFSYM_EXPORT(gpio_config),
    ESP_ELFSYM_EXPORT(floor),
    ESP_ELFSYM_EXPORT(i2c_master_transmit),
    ESP_ELFSYM_EXPORT(i2c_master_bus_rm_device),
    ESP_ELFSYM_EXPORT(__bswapsi2),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(__floatsidf),
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(__fixunsdfsi),
    ESP_ELFSYM_EXPORT(__adddf3),
    ESP_ELFSYM_EXPORT(gpio_set_level),
    ESP_ELFSYM_EXPORT(abort),
    ESP_ELFSYM_EXPORT(__floatunsidf),
    ESP_ELFSYM_EXPORT(xTaskGetTickCount),
    ESP_ELFSYM_EXPORT(spi_bus_free),
    ESP_ELFSYM_EXPORT(__fixdfsi),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(fprintf),
    ESP_ELFSYM_EXPORT(__ledf2),
    ESP_ELFSYM_EXPORT(photoframe_host_png_size),
    ESP_ELFSYM_EXPORT(pow),
    ESP_ELFSYM_EXPORT(heap_caps_malloc),
    ESP_ELFSYM_EXPORT(memcmp),
    ESP_ELFSYM_EXPORT(spi_bus_remove_device),
    ESP_ELFSYM_EXPORT(__divdf3),
    ESP_ELFSYM_EXPORT(__muldf3),
    ESP_ELFSYM_EXPORT(fread),
    ESP_ELFSYM_EXPORT(spi_bus_add_device),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(heap_caps_free),
    ESP_ELFSYM_EXPORT(i2c_new_master_bus),
    ESP_ELFSYM_EXPORT(i2c_del_master_bus),
    ESP_ELFSYM_EXPORT(stderr),
    ESP_ELFSYM_EXPORT(fputc),
    ESP_ELFSYM_EXPORT(photoframe_host_png_data),
    ESP_ELFSYM_EXPORT(vTaskDelay),
    ESP_ELFSYM_EXPORT(spi_bus_initialize),
    ESP_ELFSYM_EXPORT(strlen),
    ESP_ELFSYM_EXPORT(__gedf2),
    ESP_ELFSYM_EXPORT(gpio_get_level),
    ESP_ELFSYM_EXPORT(i2c_master_transmit_receive),
    ESP_ELFSYM_EXPORT(free),
    ESP_ELFSYM_END};
