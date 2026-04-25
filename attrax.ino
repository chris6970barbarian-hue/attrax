#include <SPI.h>
#include <LittleFS.h>
#include <JPEGDEC.h>
#include "src/ST77916.h"

// Pin definitions - ESP32-S3 WaveShare module
#define TFT_SCK   12
#define TFT_MOSI  11
#define TFT_DC    10
#define TFT_RST   9
#define TFT_CS    8

#define DISPLAY_INTERVAL 3000  // ms between images

ST77916 tft(TFT_CS, TFT_DC, TFT_RST, TFT_SCK, TFT_MOSI);
JPEGDEC jpeg;

const char *imageFiles[] = {
    "/0f2c850b8acf93f3f8600550c78b8d2.jpg",
    "/f9f87ca0f278ed8ff7b35081eded5e9.jpg",
    "/82c5d22314c75537f5fb5faa223be9b.jpg",
    "/deb4613a31d6513010dff9066557b36.jpg"
};
const int imageCount = sizeof(imageFiles) / sizeof(imageFiles[0]);
int currentImage = 0;

// JPEGDEC callback - draws decoded MCU blocks to display
int jpegDrawCallback(JPEGDRAW *pDraw) {
    tft.setAddrWindow(pDraw->x, pDraw->y,
                      pDraw->x + pDraw->iWidth - 1,
                      pDraw->y + pDraw->iHeight - 1);
    tft.pushColors(pDraw->pPixels, pDraw->iWidth * pDraw->iHeight);
    return 1;
}

// File access callbacks for JPEGDEC
File jpegFile;

void *jpegOpen(const char *filename, int32_t *pFileSize) {
    jpegFile = LittleFS.open(filename, "r");
    if (!jpegFile) return nullptr;
    *pFileSize = jpegFile.size();
    return &jpegFile;
}

void jpegClose(void *pHandle) {
    File *f = (File *)pHandle;
    if (f) f->close();
}

int32_t jpegRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    File *f = (File *)pFile->fHandle;
    return f->read(pBuf, iLen);
}

int32_t jpegSeek(JPEGFILE *pFile, int32_t iPosition) {
    File *f = (File *)pFile->fHandle;
    return f->seek(iPosition);
}

void displayImage(const char *path) {
    Serial.printf("Displaying: %s\n", path);

    void *handle = jpegOpen(path, nullptr);
    int32_t fileSize = 0;
    jpegFile.close();

    jpegFile = LittleFS.open(path, "r");
    if (!jpegFile) {
        Serial.printf("Failed to open: %s\n", path);
        return;
    }
    fileSize = jpegFile.size();

    if (jpeg.open(path, jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback)) {
        Serial.printf("Image: %dx%d\n", jpeg.getWidth(), jpeg.getHeight());
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.decode(0, 0, 0);
        jpeg.close();
    } else {
        Serial.println("JPEG decode failed");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ST77916 360x360 Image Viewer");

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
        return;
    }
    Serial.println("LittleFS mounted");

    tft.begin(40000000);
    tft.setRotation(0);
    tft.fillScreen(0x0000);
    Serial.println("Display initialized");
}

void loop() {
    displayImage(imageFiles[currentImage]);
    currentImage = (currentImage + 1) % imageCount;
    delay(DISPLAY_INTERVAL);
}
