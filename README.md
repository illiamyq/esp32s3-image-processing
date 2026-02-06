# esp32s3-server-image-processing
Low level image processing on ESP32S3 with ESP-IDF

## Setup

1. ESP-IDF v5.x [lke: vscode ESP-IDF extension, with templates provided]
2. git clone https://github.com/illiamyq/esp32s3-server-image-processing.git
3. Configure WiFi:
```bash
   idf.py menuconfig
   # wifi ap data in wifi.c component
```
4. Build and flash:
```bash
   idf.py build
   idf.py flash monitor
```

## Usage

Upload image:
```bash
```

## Partition Table

Custom partition table in `partitions.csv` - includes 512KB SPIFFS.

# TODO
- low level image processing
  1. Grayscale vs RGB buffer layout
  2. esp Fixed-point arithmetic
  3. Histogram equalization
  4. Kernels (blur, edge detection, sharpening, etc.)
  5. erosion
  6. compression & encoding
  7. geometric transformations
- future project: server-client image processing on MCU with integrated camera (API-server to enable basic low-level processing) & AI assisted analysis and generation of acquisition parameter recommendations (exposure time, analog/ISO gain)
