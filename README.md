
Simple mains-level AC power monitor implemented on ESP32 (ESP-IDF) using ACS712 (current) and ZMPT101B (voltage) sensors providing voltage, current and phase over USB.

**DETAILS**

Supports 5 devices (using 10 sensors) to read up to 300VAC at 50A on each.
Using an ESP32-S3 which has sufficient GPIO pins and 10 ADC channels for ADC continuous read functionality to simplify implementation.
Sensors are 5V level and outputs are voltage divided (20K/30K) to 3V for ESP32 GPIO ADCs.
Prototyped using generic ESP32-S3 supermini board with generic ACS712 and ZMPT101B breakout boards.
ESP32-S3 is powered from USB and sensors are 5V powered from ESP32-S3 5V pin: no additional power circuitry.
No other components needed other than micro, sensors and voltage divider resistors (and wiring terminals).

Outputs single lines of text with the format ``timestamp type counter content``, where ``timestamp`` is nanoseconds since startup, ``type`` is a string (``INIT``, ``TERM``, ``READ``, ``DIAG``, ``FAIL``), ``counter`` is the ``READ`` counter (even if not the ``READ`` line), and ``content`` depends on the type.
* ``INIT`` provides hardware and software details and parameters and is issued once when the powermon starts/restarts.
* ``TERM`` is issued when the powermon stops gracefully (which it will not do for now, as it only stops on errors).
* ``READ`` provides each of the 5 devices voltage, current, phase-angle and fault status and is issued every 5 seconds.
* ``DIAG`` provides each of the 5 devices total fault types and details and is issued every 60 seconds.
* ``FAIL`` is issued if a fatal error occurs (typically an ADC error) with detailed code/message, after which the powermon restarts.

The hardware and software is very simple by intent.
The software is largely configurable through #defines.
The build uses ESP-IDF and Linux toolchain.

The functionality is intentionally minimal: any more functions (e.g. VA calculations and kWh tracking) are expected to be carried out by the powermon client. This may change in future.

Currently used in a Linux based embedded system to monitor power status of mains fed devices by feeding data into etcd.

Provided in the "example" directory are udev rules (for an ESP32-S3 supermini) and systemd service files to start an application which will read and deliver to stdout (system log / journal). This could be adapted to deliver into MQTT.

Please note the LICENSE (Attribution-NonCommercial-ShareAlike).

**NOTES**

The ESP32-S3 supports ADC1 with 10 channels and ADC2 with 10 channels. This implementation uses ADC1 only. ADC2 is unavailable if WiFi is enabled. With some additional work, this implementation could support 10 devices (20 channels). 

**RESOURCES**

* ESP32-S3 microcontroller [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) and [Zero Board](https://www.waveshare.com/esp32-s3-zero.htm) (waveshare).
* ACS712 current sensor [product](https://www.allegromicro.com/en/products/sense/current-sensor-ics/integrated-current-sensors/acs712), [datasheet](https://www.allegromicro.com/-/media/files/datasheets/acs712-datasheet.pdf), [board](https://www.aliexpress.com/item/1005007381850382.html) (generic) and [guide](https://www.edn.com/current-sensor-module-microcontrollers) (EDN).
* ZMPT101B voltage sensor [product](https://www.micro-transformer.com/2ma-2ma-voltage-transformer-ZMPT101B.html), [datasheet](https://5krorwxhmqqirik.leadongcdn.com/ZMPT101B+specification-aidijBqoKomRilSqqokpjkp.pdf), [board](https://www.aliexpress.com/item/1005009144961238.html) (generic) and [guide](https://www.edn.com/voltage-sensor) (EDN).

**IMAGES**

