
Simple mains-level AC power monitor implemented on ESP32 (ESP-IDF) using ACS712 (current) and ZMPT101B (voltage) sensors providing voltage, current and phase over USB.

**DETAILS**

Supports 5 devices (using 10 sensors) to read up to 500VAC at 50A on each.
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

**EXAMPLE**

This is the example raw output with device 3 connected, a 500W heat gun, registring at ~240V and at full power (~2.1A); then reduced to half power (~1.2A) or off (~0.0A). This devices shows OK for both voltage and current readings. Sensors are not connected for devices 1, 2, 4 and 5, showing a zero offset error.

```
0000000000000000 INIT 0000000000000000 type=power-ac,vers=1.00,arch=esp32s3,serial=D0:CF:13:0B:96:5C,hw-voltage=zmpt101b,hw-current=acs712-30,voltage-freq=60,voltage-max=500,current-max=50,devices=5,period-read=5000,period-diag=60000,debug-pin=no,adc-bits=12,adc-rate=40kHz,adc-size-frame=1000,adc-size-pool=16000,adc-pins=1/3/5/7/9/2/4/6/8/10
0000000004987149 READ 0000000000000001 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.265549,2.156977,-096,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000009977149 READ 0000000000000002 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.365646,1.197311,-118,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000014967149 READ 0000000000000003 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 241.730179,1.212321,-118,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000019957149 READ 0000000000000004 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 243.047958,1.225099,-118,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000024947149 READ 0000000000000005 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 243.832443,0.097675,+000,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000029937149 READ 0000000000000006 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 240.746826,2.147905,-090,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000034927149 READ 0000000000000007 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.362961,1.209134,-124,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000039917149 READ 0000000000000008 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.734467,1.206039,-124,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000044907149 READ 0000000000000009 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.133972,1.216968,-118,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000049897149 READ 0000000000000010 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.083847,1.201242,-118,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000054887149 READ 0000000000000011 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.343979,1.200715,+073,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000059877149 READ 0000000000000012 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 243.498627,1.204173,-124,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000064867149 READ 0000000000000013 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 241.731995,2.151586,-096,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
0000000064867149 DIAG 0000000000000013 320,0,0/0/0/0/0/13;320,0,0/0/0/0/0/13 320,0,0/0/0/0/0/13;320,0,0/0/0/0/0/13 320,1765,0/0/0/0/0/0;320,1774,0/0/0/0/0/0 320,0,0/0/0/0/0/13;320,0,0/0/0/0/0/13 320,0,0/0/0/0/0/13;320,0,0/0/0/0/0/13
0000000069857149 READ 0000000000000014 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 242.427139,0.119139,+017,OK,OK 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS 999.999999,99.999999,+999,E_ZOFFS,E_ZOFFS
```

This is the example processed output (from the example/powermon.c application):

```
root@workshop:/opt/powermon_esp32/example# make test
./powermon --config powermon.default /dev/powermon
started on '/dev/powermon' (verbose=true)
294407149 59 READ [0] -,-,- (E_ZOFFS,E_ZOFFS) [1] -,-,- (E_ZOFFS,E_ZOFFS) [2] 243.604767V,0.067564A,+000° (OK,OK) [3] -,-,- (E_ZOFFS,E_ZOFFS) [4] -,-,- (E_ZOFFS,E_ZOFFS)
299397149 60 READ [0] -,-,- (E_ZOFFS,E_ZOFFS) [1] -,-,- (E_ZOFFS,E_ZOFFS) [2] 241.851349V,0.104504A,+011° (OK,OK) [3] -,-,- (E_ZOFFS,E_ZOFFS) [4] -,-,- (E_ZOFFS,E_ZOFFS)
304387149 61 READ [0] -,-,- (E_ZOFFS,E_ZOFFS) [1] -,-,- (E_ZOFFS,E_ZOFFS) [2] 240.328369V,2.147507A,-096° (OK,OK) [3] -,-,- (E_ZOFFS,E_ZOFFS) [4] -,-,- (E_ZOFFS,E_ZOFFS)
309377149 62 READ [0] -,-,- (E_ZOFFS,E_ZOFFS) [1] -,-,- (E_ZOFFS,E_ZOFFS) [2] 242.589005V,1.214934A,-112° (OK,OK) [3] -,-,- (E_ZOFFS,E_ZOFFS) [4] -,-,- (E_ZOFFS,E_ZOFFS)
314367149 63 READ [0] -,-,- (E_ZOFFS,E_ZOFFS) [1] -,-,- (E_ZOFFS,E_ZOFFS) [2] 241.825745V,0.113029A,+017° (OK,OK) [3] -,-,- (E_ZOFFS,E_ZOFFS) [4] -,-,- (E_ZOFFS,E_ZOFFS)
^Cstopped
```

**IMAGES**

