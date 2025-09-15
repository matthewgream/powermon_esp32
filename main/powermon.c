
/*
 * ESP32-S3 AC Power Monitor - 300VAC, 30A via. ACS712 and ZMPT101B sensors: 5 devices (10 sensors), output over USB
 */

// ------------------------------------------------------------------------------------------------------------------------

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------------------------------

#define __MIN(a, b)  ((a) < (b) ? (a) : (b))

#define US_PER_MS    1000
#define MAX_STR_SIZE 256

void __delay(const int64_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static char __app_serial_str[(6 * 3) + 1] = "";
static const char *__app_serial(void) { return __app_serial_str; }

void __app_init(void) {
    uint8_t mac_addr[6] = { 0 };
    (void)esp_read_mac(mac_addr, ESP_MAC_EFUSE_FACTORY);
    for (int i = 0, o = 0; i < sizeof(mac_addr); i++)
        o += snprintf(&__app_serial_str[o], sizeof(__app_serial_str) - (size_t)o, "%s%02X", i == 0 ? "" : ":", mac_addr[i]);
}

// ------------------------------------------------------------------------------------------------------------------------

#define SYSTEM_TYPE                       "power-ac"
#define SYSTEM_VERSION                    "1.00"
#define SYSTEM_PLATFORM                   "esp32s3"
#define SYSTEM_SENSOR_VOLTAGE             "zmpt101b"
#define SYSTEM_SENSOR_CURRENT             "acs712-30"

// ------------------------------------------------------------------------------------------------------------------------

// System
#define NUM_DEVICES                       5
#define NUM_SENSORS                       (NUM_DEVICES + NUM_DEVICES)
#define REPORTINGS_PERIOD_MS              5000                                              // Output every 5 seconds
#define AC_FREQUENCY_HZ                   60                                                // 60Hz AC mains
#define SAMPLES_PER_CYCLE                 64                                                // Samples per AC cycle
#define NUM_CYCLES_TO_SAMPLE              5                                                 // Sample 5 cycles for accuracy (~83ms @ 60Hz)
#define SAMPLE_DURATION_MS                ((NUM_CYCLES_TO_SAMPLE * 1000) / AC_FREQUENCY_HZ) // Sampling duration (5 cycles at 60Hz = ~83ms)
#define NUM_SAMPLES                       (SAMPLES_PER_CYCLE * NUM_CYCLES_TO_SAMPLE)        // 320 samples per sensor
#define DIAGNOSTIC_PERIOD_MS              60000                                             // Output diagnostics every 60 seconds
#define STARTUP_DELAY_MS                  2500                                              // Startup delay MS
#define MIN_SAMPLES_PER_SECOND_PER_SENSOR (AC_FREQUENCY_HZ * SAMPLES_PER_CYCLE)             // 3,840 Hz per sensor
#define MIN_SAMPLE_RATE                   (MIN_SAMPLES_PER_SECOND_PER_SENSOR * NUM_SENSORS) // 38,400 Hz total minimum
#define GPIO_DEBUG_MODE                   GPIO_NUM_13                                       // tie low for debug output

// Voltage Divider (5V -> 3V)
#define VDIV_R1                           20000                                  // 20k ohm
#define VDIV_R2                           30000                                  // 30k ohm
#define VDIV_RATIO                        ((float)(VDIV_R1 + VDIV_R2) / VDIV_R2) // 1.667

// ADC
#define ADC_BIT_SIZE                      SOC_ADC_DIGI_MAX_BITWIDTH            // 12-bit ADC (ESP32-S3 specific)
#define ADC_MAX_VALUE                     ((1 << ADC_BIT_SIZE) - 1)            // ADC specific
#define ADC_VREF                          3.3                                  // 3.3V reference (ESP32)
#define ADC_MIDPOINT                      (ADC_MAX_VALUE / 2)                  // Expected midpoint for AC signal
#define ADC_RESULT_BYTES                  SOC_ADC_DIGI_RESULT_BYTES            // ADC result size (ESP32-S3 specific) per read, 4 bytes
#define ADC_SAMPLE_RATE_HZ                40000                                // Above minimum
#define ADC_SAMPLE_SIZE                   250                                  // Should be multiple of NUM_SENSORS (10) for even distribution
#define ADC_FRAME_SIZE                    (ADC_SAMPLE_SIZE * ADC_RESULT_BYTES) // ADC DMA transfer frame size in bytes, 1024 bytes
#define ADC_NUM_FRAMES                    16                                   // ADC frames to buffer (for smooth operation), use 4
#define ADC_POOL_SIZE                     (ADC_FRAME_SIZE * ADC_NUM_FRAMES)    // Total buffer pool size, 4096 bytes
#if ADC_SAMPLE_RATE_HZ < CONFIG_SOC_ADC_SAMPLE_FREQ_THRES_LOW || ADC_SAMPLE_RATE_HZ > CONFIG_SOC_ADC_SAMPLE_FREQ_THRES_HIGH
#error ADC_SAMPLE_RATE_HZ outside of SOC specification
#endif
#define ADC_SAMPLE_THRESHOLD_MIN          10
#define ADC_READINGS_PER_SENSOR_PER_FRAME (ADC_SAMPLE_SIZE / NUM_SENSORS)                   // 25 per sensor
#define ADC_FRAMES_NEEDED                 (NUM_SAMPLES / ADC_READINGS_PER_SENSOR_PER_FRAME) // 320/25 = 13 frames

// Sensor ACS712
#define ACS712_MV_PER_AMP_5A              185.0                 // 185 mV/A
#define ACS712_MV_PER_AMP_20A             100.0                 // 100 mv/A
#define ACS712_MV_PER_AMP_30A             66.0                  // 66 mv/A
#define ACS712_MV_PER_AMP                 ACS712_MV_PER_AMP_30A // 30A version
#define ACS712_SUPPLY_V                   5.0                   // 5V supply to sensor

// Sensor ZMPT101B
#define ZMPT101B_RATIO                    0.00166
#define ZMPT101B_SUPPLY_V                 5.0 // 5V supply to sensor

// Limits (for fault detection)
#define MAX_VOLTAGE_V                     500.0 // Max 500V (ZMPT101B rated up to 1000V)
#define MAX_CURRENT_A                     50.0  // Max 50A (ACS712 modules up to 30A)
#define ZERO_OFFSET_LOWER                 1000
#define ZERO_OFFSET_UPPER                 2800

// Pin Mapping - GPIO pins for ADC channels (GPIO1 to 10 only allowed on ADC unit 1)
static const gpio_num_t adc_sensor_pins[NUM_SENSORS] = {
    GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_8, GPIO_NUM_10, // Current sensors (ACS712)
    GPIO_NUM_1, GPIO_NUM_3, GPIO_NUM_5, GPIO_NUM_7, GPIO_NUM_9   // Voltage sensors (ZMPT101B)
};

// ------------------------------------------------------------------------------------------------------------------------

static bool __debug_enabled = false;
static void __debug_init(void) {
    gpio_input_enable(GPIO_DEBUG_MODE);
    gpio_set_pull_mode(GPIO_DEBUG_MODE, GPIO_PULLUP_ONLY);
    if (gpio_get_level(GPIO_DEBUG_MODE) == 0)
        __debug_enabled = true;
}
#define debug_enabled() (__debug_enabled)
#define DEBUG_PRINT                                                                                                                                                                \
    if (__debug_enabled)                                                                                                                                                           \
    printf

// ------------------------------------------------------------------------------------------------------------------------

typedef enum {
    FAULT_NONE = 0,
    FAULT_SAMPLES_CNT,
    FAULT_ABOVE_RANGE,
    FAULT_BELOW_RANGE,
    FAULT_ISNOTNUMBER,
    FAULT_ZERO_OFFSET,
    NUM_FAULTS,
} adc_fault_t;

static const char *fault2str(const adc_fault_t fault) {
    switch (fault) {
    case FAULT_NONE:
        return "OK";
    case FAULT_SAMPLES_CNT:
        return "E_COUNT";
    case FAULT_ABOVE_RANGE:
        return "E_ABOVE";
    case FAULT_BELOW_RANGE:
        return "E_BELOW";
    case FAULT_ISNOTNUMBER:
        return "E_ISNAN";
    case FAULT_ZERO_OFFSET:
        return "E_ZOFFS";
    case NUM_FAULTS:
    default:
        return "E_UNKNW";
    }
}

static const char *faults2str(const uint32_t faults[], const size_t faults_size, char *string, const size_t string_size) {
    for (int i = 0, o = 0; i < faults_size; i++)
        o += snprintf(&string[o], string_size - (size_t)o, "%s%lu", i == 0 ? "" : "/", faults[i]);
    return string;
}

// ------------------------------------------------------------------------------------------------------------------------

typedef struct {
    float voltage_rms;
    float current_rms;
    float phase_angle;
    adc_fault_t voltage_fault;
    adc_fault_t current_fault;
} adc_result_t;

typedef struct {
    adc_continuous_handle_t handle;
    uint8_t *buffer;
    size_t buffer_size;
    uint32_t samples[NUM_SENSORS][NUM_SAMPLES];
    uint32_t sample_count[NUM_SENSORS];
    uint32_t fault_count[NUM_SENSORS][NUM_FAULTS];
    float zero_offset[NUM_SENSORS];
} adc_system_t;

// ------------------------------------------------------------------------------------------------------------------------

static adc_channel_t adc_sensor_to_channel[NUM_SENSORS];
static int __adc_sensor_for_channel(const adc_channel_t channel) {
    for (int i = 0; i < NUM_SENSORS; i++)
        if (adc_sensor_to_channel[i] == channel)
            return i;
    return -1;
}

static esp_err_t readings_init(adc_system_t *adc) {

    adc->buffer_size = ADC_FRAME_SIZE;
    adc->buffer      = (uint8_t *)malloc(adc->buffer_size);
    if (!adc->buffer)
        return ESP_ERR_NO_MEM;

    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = ADC_POOL_SIZE,
        .conv_frame_size    = ADC_FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc->handle));

    adc_digi_pattern_config_t patterns[NUM_SENSORS];
    for (int i = 0; i < NUM_SENSORS; i++) {
        adc_unit_t unit;
        ESP_ERROR_CHECK(adc_continuous_io_to_channel(adc_sensor_pins[i], &unit, &adc_sensor_to_channel[i]));
        if (unit != ADC_UNIT_1)
            return ESP_FAIL;
        patterns[i].atten     = ADC_ATTEN_DB_12;
        patterns[i].channel   = adc_sensor_to_channel[i];
        patterns[i].unit      = unit;
        patterns[i].bit_width = ADC_BIT_SIZE;
    }
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num    = NUM_SENSORS,
        .adc_pattern    = patterns,
    };
    ESP_ERROR_CHECK(adc_continuous_config(adc->handle, &dig_cfg));

    return ESP_OK;
}

static void readings_term(adc_system_t *adc) {

    if (adc->handle) {
        (void)adc_continuous_stop(adc->handle);
        (void)adc_continuous_deinit(adc->handle);
        adc->handle = NULL;
    }
    if (adc->buffer) {
        free(adc->buffer);
        adc->buffer = NULL;
    }
}

static void readings_extract(const uint8_t *buffer, const uint32_t length, adc_system_t *adc) {

    adc_channel_t channel_last = -1;
    int sensor                 = 0;
    for (int i = 0; i < length; i += SOC_ADC_DIGI_RESULT_BYTES) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        const adc_digi_output_data_t *p = (const adc_digi_output_data_t *)&buffer[i];
#pragma GCC diagnostic pop
        const adc_channel_t channel = (adc_channel_t)p->type2.channel;
        if (channel != channel_last)
            sensor = __adc_sensor_for_channel(channel_last = channel);
        if (sensor >= 0 && sensor < NUM_SENSORS && adc->sample_count[sensor] < NUM_SAMPLES)
            adc->samples[sensor][adc->sample_count[sensor]++] = p->type2.data;
    }
}

static esp_err_t readings_collect(adc_system_t *adc) {

    memset(adc->sample_count, 0, sizeof(adc->sample_count));
    ESP_ERROR_CHECK(adc_continuous_start(adc->handle));
    const int64_t start_time = esp_timer_get_time();
    uint32_t bytes_read      = 0;
    while ((esp_timer_get_time() - start_time) < (SAMPLE_DURATION_MS * US_PER_MS)) // Fixed timing
        if (adc_continuous_read(adc->handle, adc->buffer, adc->buffer_size, &bytes_read, 0) == ESP_OK && bytes_read > 0)
            readings_extract(adc->buffer, bytes_read, adc);
    ESP_ERROR_CHECK(adc_continuous_stop(adc->handle));

    return ESP_OK;
}

//

static float calculate_rms(const uint32_t *samples, const uint32_t count, const float zero_offset) {

    if (count == 0)
        return 0.0;
    float sum_squares = 0.0;
    for (int i = 0; i < count; i++)
        sum_squares += ((float)samples[i] - zero_offset) * ((float)samples[i] - zero_offset);
    return sqrtf(sum_squares / (float)count);
}

static float convert_adc_to_current(const float rms_adc) {

    // Convert ADC RMS to voltage at ADC pin
    const float v_adc_rms = (rms_adc / (float)ADC_MAX_VALUE) * (float)ADC_VREF;
    // Account for voltage divider to get actual sensor output voltage
    const float v_sensor_rms = v_adc_rms * (float)VDIV_RATIO;
    // Convert voltage to current using ACS712 sensitivity: outputs 2.5V at 0A, deviation from 2.5V indicates current
    const float current = (v_sensor_rms * (float)1000.0) / (float)ACS712_MV_PER_AMP;

    return current;
}

static float convert_adc_to_voltage(const float rms_adc) {

    // Convert ADC RMS to voltage at ADC pin
    const float v_adc_rms = (rms_adc / (float)ADC_MAX_VALUE) * (float)ADC_VREF;
    // Account for voltage divider to get actual sensor output voltage
    const float v_sensor_rms = v_adc_rms * (float)VDIV_RATIO;
    // Convert sensor voltage to actual AC voltage using ZMPT101B ratio
    const float voltage = v_sensor_rms / (float)ZMPT101B_RATIO;

    return voltage;
}

static float calculate_zero_offset(const uint32_t *samples, const uint32_t count) {

    if (count == 0)
        return 0.0;
    float sum = 0.0;
    for (int i = 0; i < count; i++)
        sum += (float)samples[i];
    return sum / (float)count;
}

static float calculate_phase_angle(const uint32_t *voltage_samples, const uint32_t *current_samples, const uint32_t count, const float voltage_offset, const float current_offset) {

    if (count < SAMPLES_PER_CYCLE * 2)
        return 0.0;

    int start = SAMPLES_PER_CYCLE, end = (int)count - SAMPLES_PER_CYCLE;
    int voltage_crossing = -1;
    for (int i = start; i < end && voltage_crossing < 0; i++)
        if ((float)voltage_samples[i - 1] < voltage_offset && (float)voltage_samples[i] >= voltage_offset)
            voltage_crossing = i;
    if (voltage_crossing < 0)
        return 0.0;
    if (voltage_crossing + SAMPLES_PER_CYCLE < end)
        end = voltage_crossing + SAMPLES_PER_CYCLE;

    for (int i = voltage_crossing; i < end; i++)
        if ((float)current_samples[i - 1] < current_offset && (float)current_samples[i] >= current_offset) {
            const float phase_angle = (((float)(i - voltage_crossing)) / (float)SAMPLES_PER_CYCLE) * (float)360.0;
            return phase_angle > (float)180.0 ? phase_angle - (float)360.0 : phase_angle; // Normalize to ±180°
        }

    return 0.0;
}

static void readings_calculate(adc_system_t *adc, adc_result_t *readings) {

    for (int d = 0, c = 0, v = NUM_DEVICES; d < NUM_DEVICES; d++, c++, v++) {

        if (adc->sample_count[c] < ADC_SAMPLE_THRESHOLD_MIN) {
            adc->zero_offset[c]       = 0.0;
            readings[d].current_fault = FAULT_SAMPLES_CNT;
            adc->fault_count[c][FAULT_SAMPLES_CNT]++;
        } else {
            const float zero_offset = calculate_zero_offset(adc->samples[c], adc->sample_count[c]);
            const float current_rms = convert_adc_to_current(calculate_rms(adc->samples[c], adc->sample_count[c], zero_offset));
            adc->zero_offset[c]     = zero_offset;
            readings[d].current_rms = current_rms;

            readings[d].current_fault = FAULT_NONE;
            if (zero_offset < ZERO_OFFSET_LOWER || zero_offset > ZERO_OFFSET_UPPER) {
                adc->fault_count[c][FAULT_ZERO_OFFSET]++;
                readings[d].current_fault = FAULT_ZERO_OFFSET;
            }
            if (isnan(current_rms)) {
                adc->fault_count[c][FAULT_ISNOTNUMBER]++;
                readings[d].current_fault = FAULT_ISNOTNUMBER;
            } else if (current_rms < 0) {
                adc->fault_count[c][FAULT_BELOW_RANGE]++;
                readings[d].current_fault = FAULT_BELOW_RANGE;
            } else if (current_rms > MAX_CURRENT_A) {
                adc->fault_count[c][FAULT_ABOVE_RANGE]++;
                readings[d].current_fault = FAULT_ABOVE_RANGE;
            }
        }

        if (adc->sample_count[v] < ADC_SAMPLE_THRESHOLD_MIN) {
            adc->zero_offset[v]       = 0.0;
            readings[d].voltage_fault = FAULT_SAMPLES_CNT;
            adc->fault_count[v][FAULT_SAMPLES_CNT]++;
        } else {
            const float zero_offset = calculate_zero_offset(adc->samples[v], adc->sample_count[v]);
            const float voltage_rms = convert_adc_to_voltage(calculate_rms(adc->samples[v], adc->sample_count[v], zero_offset));
            adc->zero_offset[v]     = zero_offset;
            readings[d].voltage_rms = voltage_rms;

            readings[d].voltage_fault = FAULT_NONE;
            if (zero_offset < ZERO_OFFSET_LOWER || zero_offset > ZERO_OFFSET_UPPER) {
                adc->fault_count[v][FAULT_ZERO_OFFSET]++;
                readings[d].voltage_fault = FAULT_ZERO_OFFSET;
            }
            if (isnan(voltage_rms)) {
                adc->fault_count[v][FAULT_ISNOTNUMBER]++;
                readings[d].voltage_fault = FAULT_ISNOTNUMBER;
            } else if (voltage_rms < 0) {
                adc->fault_count[v][FAULT_BELOW_RANGE]++;
                readings[d].voltage_fault = FAULT_BELOW_RANGE;
            } else if (voltage_rms > MAX_VOLTAGE_V) {
                adc->fault_count[v][FAULT_ABOVE_RANGE]++;
                readings[d].voltage_fault = FAULT_ABOVE_RANGE;
            }
        }

        readings[d].phase_angle =
            (readings[d].current_fault == FAULT_NONE && readings[d].voltage_fault == FAULT_NONE)
                ? calculate_phase_angle(adc->samples[v], adc->samples[c], __MIN(adc->sample_count[v], adc->sample_count[c]), adc->zero_offset[v], adc->zero_offset[c])
                : (float)0.0;
    }
}

static esp_err_t readings_process(adc_system_t *adc, adc_result_t *readings) {

    ESP_ERROR_CHECK(readings_collect(adc));

    readings_calculate(adc, readings);

    if (debug_enabled())
        for (int i = 0; i < NUM_SENSORS; i++) {
            uint32_t min = 0xFFFFFFFF, max = 0;
            for (int j = 0; j < adc->sample_count[i]; j++) {
                if (adc->samples[i][j] < min)
                    min = adc->samples[i][j];
                if (adc->samples[i][j] > max)
                    max = adc->samples[i][j];
            }
            DEBUG_PRINT("# sensor[%d] gpio%02d (device %d, %s): samples=%lu, offset=%.1f, min=%lu, max=%lu, range=%lu\n", i, adc_sensor_pins[i],
                        (i < NUM_DEVICES) ? i + 1 : i - NUM_DEVICES + 1, (i < NUM_DEVICES) ? "current" : "voltage", adc->sample_count[i], adc->zero_offset[i], min, max, max - min);
        }

    return ESP_OK;
}

// ------------------------------------------------------------------------------------------------------------------------

#define OUTPUT_PRINT                           printf
#define OUTPUT_FLUSH()                         fflush(stdout)
#define OUTPUT_BEGIN(type, timestamp, counter) OUTPUT_PRINT("%016" PRIx64 " " type " %016" PRIx64, timestamp, counter)
#define OUTPUT_END()                           OUTPUT_PRINT("\n"), OUTPUT_FLUSH()

static void output_display_init(const int64_t timestamp, const uint64_t counter) {
    OUTPUT_BEGIN("INIT", timestamp, counter);
    OUTPUT_PRINT(" type=%s,vers=%s,arch=%s,serial=%s,hw-voltage=%s,hw-current=%s", SYSTEM_TYPE, SYSTEM_VERSION, SYSTEM_PLATFORM, __app_serial(), SYSTEM_SENSOR_VOLTAGE,
                 SYSTEM_SENSOR_CURRENT);
    OUTPUT_PRINT(",voltage-freq=%d,voltage-max=%.0f,current-max=%.0f", AC_FREQUENCY_HZ, MAX_VOLTAGE_V, MAX_CURRENT_A);
    OUTPUT_PRINT(",devices=%d,period-read=%d,period-diag=%d,debug-pin=%s", NUM_DEVICES, REPORTINGS_PERIOD_MS, DIAGNOSTIC_PERIOD_MS, debug_enabled() ? "yes" : "no");
    char pins_str[MAX_STR_SIZE];
    for (int i = 0, o = 0; i < NUM_SENSORS; i++)
        o += snprintf(&pins_str[o], sizeof(pins_str) - (size_t)o, "%s%d", i == 0 ? "" : "/", adc_sensor_pins[i]);
    OUTPUT_PRINT(",adc-bits=%d,adc-rate=%dkHz,adc-size-frame=%d,adc-size-pool=%d,adc-pins=%s", ADC_BIT_SIZE, ADC_SAMPLE_RATE_HZ / 1000, ADC_FRAME_SIZE, ADC_POOL_SIZE, pins_str);
    OUTPUT_END();
}

static void output_display_term(const int64_t timestamp, const uint64_t counter) {
    OUTPUT_BEGIN("TERM", timestamp, counter);
    OUTPUT_END();
}

static void output_display_read(const int64_t timestamp, const uint64_t counter, const adc_result_t *read_data) {
    OUTPUT_BEGIN("READ", timestamp, counter);
    for (int d = 0; d < NUM_DEVICES; d++)
        OUTPUT_PRINT(" %03.6f,%02.6f,%+04.0f,%s,%s", read_data[d].voltage_fault != FAULT_NONE ? 999.999999 : read_data[d].voltage_rms,
                     read_data[d].current_fault != FAULT_NONE ? 99.999999 : read_data[d].current_rms,
                     read_data[d].voltage_fault != FAULT_NONE || read_data[d].current_fault != FAULT_NONE ? 999.0 : read_data[d].phase_angle, fault2str(read_data[d].voltage_fault),
                     fault2str(read_data[d].current_fault));
    OUTPUT_END();
}

static void output_display_diag(const int64_t timestamp, const uint64_t counter, const adc_system_t *read_adcs) {
    OUTPUT_BEGIN("DIAG", timestamp, counter);
    for (int d = 0, c = 0, v = NUM_DEVICES; d < NUM_DEVICES; d++, c++, v++) {
        char faults_str[MAX_STR_SIZE];
        OUTPUT_PRINT(" %lu,%.0f,%s", read_adcs->sample_count[v], read_adcs->zero_offset[v], faults2str(read_adcs->fault_count[v], NUM_FAULTS, faults_str, sizeof(faults_str)));
        OUTPUT_PRINT(";%lu,%.0f,%s", read_adcs->sample_count[c], read_adcs->zero_offset[c], faults2str(read_adcs->fault_count[c], NUM_FAULTS, faults_str, sizeof(faults_str)));
    }
    OUTPUT_END();
}

static void output_display_fail(const int64_t timestamp, const uint64_t counter, const char *message, const esp_err_t error) {
    OUTPUT_BEGIN("FAIL", timestamp, counter);
    OUTPUT_PRINT(" %s, error %d (%s)", message, error, esp_err_to_name(error));
    OUTPUT_END();
}

static esp_err_t output_init_usb(void) {

    const tusb_desc_device_t config_descriptor_device = {
        .bLength            = sizeof(config_descriptor_device),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = TUSB_CLASS_MISC,
        .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol    = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor           = 0x303A, // Espressif
        .idProduct          = 0x1001, // ESP32-S3
        .bcdDevice          = 0x0101,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01,
    };
    const char *config_descriptor_strings[] = {
        (const char[]){ 0x09, 0x04 }, // 0: supported language is English (0x0409)
        "Espressif",                  // 1: Manufacturer
        "powermon_esp32",             // 2: Product
        __app_serial(),               // 3: Serial
        "powermon",                   // 4: MSC
    };
    const tinyusb_config_t config_usb = {
        .device_descriptor = &config_descriptor_device,
        .string_descriptor = config_descriptor_strings,
        .external_phy      = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&config_usb));

    const tinyusb_config_cdcacm_t config_usbcdcacm = { 0 };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&config_usbcdcacm));

    ESP_ERROR_CHECK(esp_tusb_init_console(TINYUSB_CDC_ACM_0));

    __delay(STARTUP_DELAY_MS);

    return ESP_OK;
}

static void output_term_usb(void) { esp_tusb_deinit_console(TINYUSB_CDC_ACM_0); }

// ------------------------------------------------------------------------------------------------------------------------

void app_main(void) {

    static adc_system_t read_adcs;
    int64_t read_time  = 0;
    uint64_t read_cntr = 0;
    esp_err_t ret;

    __app_init();
    __debug_init();

    ESP_ERROR_CHECK(output_init_usb());
    output_display_init(read_time, read_cntr);
    if ((ret = readings_init(&read_adcs)) != ESP_OK) {
        output_display_fail(read_time, read_cntr, "adc failed to initialise", ret);
        return; // will reboot
    }

    int64_t diag_time = esp_timer_get_time();
    while (true) {

        const int64_t read_time_current = esp_timer_get_time(), read_time_waiting = (REPORTINGS_PERIOD_MS * US_PER_MS) - (read_time_current - read_time);
        if (read_time_waiting > 0)
            __delay(read_time_waiting / US_PER_MS);
        read_time = esp_timer_get_time();

        adc_result_t read_data[NUM_DEVICES];
        if ((ret = readings_process(&read_adcs, read_data)) != ESP_OK) {
            output_display_fail(read_time, read_cntr, "adc failed to process", ret);
            return; // will reboot
        }
        if (++read_cntr > 9999999999999999ULL)
            read_cntr = 1;
        output_display_read(read_time, read_cntr, read_data);

        const int64_t diag_time_current = esp_timer_get_time(), diag_time_waiting = (DIAGNOSTIC_PERIOD_MS * US_PER_MS) - (diag_time_current - diag_time);
        if (diag_time_waiting <= 0) {
            output_display_diag(read_time, read_cntr, &read_adcs);
            diag_time = diag_time_current;
        }
    }

    readings_term(&read_adcs);
    output_display_term(esp_timer_get_time(), read_cntr);
    output_term_usb();
    esp_restart();
}

// ------------------------------------------------------------------------------------------------------------------------
