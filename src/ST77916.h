#pragma once
#include <Arduino.h>
#include <SPI.h>

#define ST77916_WIDTH  360
#define ST77916_HEIGHT 360

#define ST77916_NOP     0x00
#define ST77916_SWRESET 0x01
#define ST77916_SLPIN   0x10
#define ST77916_SLPOUT  0x11
#define ST77916_INVOFF  0x20
#define ST77916_INVON   0x21
#define ST77916_DISPOFF 0x28
#define ST77916_DISPON  0x29
#define ST77916_CASET   0x2A
#define ST77916_RASET   0x2B
#define ST77916_RAMWR   0x2C
#define ST77916_MADCTL  0x36
#define ST77916_COLMOD  0x3A

class ST77916 {
public:
    ST77916(int8_t cs, int8_t dc, int8_t rst, int8_t sck, int8_t mosi);
    void begin(uint32_t freq = 40000000);
    void setRotation(uint8_t r);
    void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void pushColors(uint16_t *data, uint32_t len);
    void fillScreen(uint16_t color);
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    void drawRGBBitmap(uint16_t x, uint16_t y, uint16_t *bitmap, uint16_t w, uint16_t h);

    uint16_t width()  { return _width; }
    uint16_t height() { return _height; }

private:
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void writeData16(uint16_t data);
    void sendInitSequence();

    int8_t _cs, _dc, _rst, _sck, _mosi;
    uint16_t _width, _height;
    SPIClass *_spi;
};
