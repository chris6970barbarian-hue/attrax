#include "ST77916.h"

ST77916::ST77916(int8_t cs, int8_t dc, int8_t rst, int8_t sck, int8_t mosi)
    : _cs(cs), _dc(dc), _rst(rst), _sck(sck), _mosi(mosi),
      _width(ST77916_WIDTH), _height(ST77916_HEIGHT), _spi(nullptr) {}

void ST77916::writeCommand(uint8_t cmd) {
    digitalWrite(_dc, LOW);
    digitalWrite(_cs, LOW);
    _spi->transfer(cmd);
    digitalWrite(_cs, HIGH);
}

void ST77916::writeData(uint8_t data) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    _spi->transfer(data);
    digitalWrite(_cs, HIGH);
}

void ST77916::writeData16(uint16_t data) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    _spi->transfer16(data);
    digitalWrite(_cs, HIGH);
}

void ST77916::sendInitSequence() {
    // Software reset
    writeCommand(ST77916_SWRESET);
    delay(120);

    // Sleep out
    writeCommand(ST77916_SLPOUT);
    delay(120);

    // Pixel format: 16bit/pixel (RGB565)
    writeCommand(ST77916_COLMOD);
    writeData(0x55);

    // Memory access control
    writeCommand(ST77916_MADCTL);
    writeData(0x00);

    // Display inversion on (typical for IPS panels)
    writeCommand(ST77916_INVON);

    // Column address set
    writeCommand(ST77916_CASET);
    writeData(0x00);
    writeData(0x00);
    writeData((ST77916_WIDTH - 1) >> 8);
    writeData((ST77916_WIDTH - 1) & 0xFF);

    // Row address set
    writeCommand(ST77916_RASET);
    writeData(0x00);
    writeData(0x00);
    writeData((ST77916_HEIGHT - 1) >> 8);
    writeData((ST77916_HEIGHT - 1) & 0xFF);

    // Display on
    writeCommand(ST77916_DISPON);
    delay(20);
}

void ST77916::begin(uint32_t freq) {
    pinMode(_cs, OUTPUT);
    pinMode(_dc, OUTPUT);
    pinMode(_rst, OUTPUT);
    digitalWrite(_cs, HIGH);

    _spi = new SPIClass(FSPI);
    _spi->begin(_sck, -1, _mosi, _cs);
    _spi->setFrequency(freq);
    _spi->setDataMode(SPI_MODE0);

    // Hardware reset
    digitalWrite(_rst, HIGH);
    delay(10);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(120);

    sendInitSequence();
}

void ST77916::setRotation(uint8_t r) {
    writeCommand(ST77916_MADCTL);
    switch (r & 0x03) {
        case 0: writeData(0x00); _width = ST77916_WIDTH; _height = ST77916_HEIGHT; break;
        case 1: writeData(0x60); _width = ST77916_HEIGHT; _height = ST77916_WIDTH; break;
        case 2: writeData(0xC0); _width = ST77916_WIDTH; _height = ST77916_HEIGHT; break;
        case 3: writeData(0xA0); _width = ST77916_HEIGHT; _height = ST77916_WIDTH; break;
    }
}

void ST77916::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writeCommand(ST77916_CASET);
    writeData16(x0);
    writeData16(x1);
    writeCommand(ST77916_RASET);
    writeData16(y0);
    writeData16(y1);
    writeCommand(ST77916_RAMWR);
}

void ST77916::pushColors(uint16_t *data, uint32_t len) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    for (uint32_t i = 0; i < len; i++) {
        _spi->transfer16(data[i]);
    }
    digitalWrite(_cs, HIGH);
}

void ST77916::fillScreen(uint16_t color) {
    setAddrWindow(0, 0, _width - 1, _height - 1);
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    for (uint32_t i = 0; i < (uint32_t)_width * _height; i++) {
        _spi->transfer16(color);
    }
    digitalWrite(_cs, HIGH);
}

void ST77916::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= _width || y >= _height) return;
    setAddrWindow(x, y, x, y);
    writeData16(color);
}

void ST77916::drawRGBBitmap(uint16_t x, uint16_t y, uint16_t *bitmap, uint16_t w, uint16_t h) {
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    pushColors(bitmap, (uint32_t)w * h);
}
