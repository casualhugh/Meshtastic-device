#include "OLEDDisplay.h"

Arduino_DataBus *bus = new Arduino_ESP32SPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, MISO_PIN, VSPI);
Arduino_G *tft = new Arduino_GC9A01(bus, RST_PIN , 0 , true );
Arduino_GFX *canvas = new Arduino_Canvas_Mono(240 , 240 , tft, 0 , 0 );

OLEDDisplay::OLEDDisplay() {

	displayWidth = 240;
	displayHeight = 240;
	displayBufferSize = displayWidth * displayHeight / 8;
	color = WHITE1;
	textAlignment = TEXT_ALIGN_LEFT;
	fontData = 1;
	// fontTableLookupFunction = DefaultFontTableLookup;
	buffer = NULL;
#ifdef OLEDDISPLAY_DOUBLE_BUFFER
	buffer_back = NULL;
#endif 
  init();
}

OLEDDisplay::~OLEDDisplay() {
  end();
}

bool OLEDDisplay::allocateBuffer() {
  
  return true;
}

bool OLEDDisplay::init() {
  pinMode(PULLUP_LCD, OUTPUT);
  digitalWrite(PULLUP_LCD, HIGH);
  pinMode(SCREEN_BL, OUTPUT);
  
  ledcSetup(0, 5000, 8);
  ledcAttachPin(SCREEN_BL, 0);
  canvas->begin();
  canvas->fillScreen(BLACK);
  canvas->flush();
  
  return true;
}

void OLEDDisplay::end() {
}

void OLEDDisplay::resetDisplay(void) {
}

void OLEDDisplay::setColor(OLEDDISPLAY_COLOR color) {
  this->color = color;
}

OLEDDISPLAY_COLOR OLEDDisplay::getColor() {
  return this->color;
}

void OLEDDisplay::setPixel(int16_t x, int16_t y) {
    canvas->drawPixel(x, y, BLACK);
}

void OLEDDisplay::setPixelColor(int16_t x, int16_t y, OLEDDISPLAY_COLOR color) {
    setPixel(x, y);
}

void OLEDDisplay::clearPixel(int16_t x, int16_t y) {
    canvas->drawPixel(x, y, WHITE);
}


// Bresenham's algorithm - thx wikipedia and Adafruit_GFX
void OLEDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    canvas->drawLine(x0, y1, x1, y1, BLACK);
}

void OLEDDisplay::drawRect(int16_t x, int16_t y, int16_t width, int16_t height) {
    canvas->drawRect(x, y, width, height, BLACK);
}

void OLEDDisplay::fillRect(int16_t xMove, int16_t yMove, int16_t width, int16_t height) {
    canvas->fillRect(xMove, yMove, width, height, BLACK);
}

void OLEDDisplay::drawCircle(int16_t x0, int16_t y0, int16_t radius) {
    canvas->drawCircle(x0, y0, radius, BLACK);
}

void OLEDDisplay::drawArc(int16_t x, int16_t y, int16_t r1, int16_t r2, float start, float end){
    canvas->fillArc(x, y, r1, r2, start, end, BLACK);
}


void OLEDDisplay::drawCircleQuads(int16_t x0, int16_t y0, int16_t radius, uint8_t quads) {
    //TODO
    drawCircle(x0, y0, radius);
}


void OLEDDisplay::fillCircle(int16_t x0, int16_t y0, int16_t radius) {
    canvas->fillCircle(x0, y0, radius, BLACK);
}

void OLEDDisplay::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int16_t x2, int16_t y2) {
    canvas->drawTriangle(x0, y0, x1, y1, x2, y2, BLACK);
}

void OLEDDisplay::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int16_t x2, int16_t y2) {
    canvas->fillTriangle(x0, y0, x1, y1, x2, y2, BLACK);
}

void OLEDDisplay::drawHorizontalLine(int16_t x, int16_t y, int16_t length) {
    canvas->drawFastHLine(x, y, length, BLACK);
}

void OLEDDisplay::drawVerticalLine(int16_t x, int16_t y, int16_t length) {
    canvas->drawFastVLine(x, y, length, BLACK);
}

void OLEDDisplay::drawProgressBar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress) {
    int16_t length = progress * width;
    drawRect(x, y - height/2, width, height);
    drawHorizontalLine(x, y, length);
}

void OLEDDisplay::drawFastImage(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *image) {
    canvas->drawBitmap(xMove, yMove, image, width, height, BLACK);
}

void OLEDDisplay::drawXbm(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *xbm) {
  canvas->drawBitmap(xMove, yMove, xbm, width, height, BLACK);
}

void OLEDDisplay::drawIco16x16(int16_t xMove, int16_t yMove, const uint8_t *ico, bool inverse) {
  canvas->drawBitmap(xMove, yMove, ico, 16, 16, BLACK);
}

uint16_t OLEDDisplay::drawStringInternal(int16_t xMove, int16_t yMove, const char* text, uint16_t textLength, uint16_t textWidth, bool utf8) {
  uint16_t charDrawn = 0;
    int16_t x1, y1;
    uint16_t w, h;
    
    canvas->setTextSize(2);
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    canvas->setTextColor(BLACK);
    uint16_t cursor_x = xMove - w/2;
    uint16_t cursor_y = yMove - h/2;
    // switch (textAlignment)
    // {
    //      case TEXT_ALIGN_LEFT:
    //         cursor_x = xMove;
    //         break;
    //     case TEXT_ALIGN_RIGHT:
    //         cursor_x = xMove - w;
    //         break;
    //     case TEXT_ALIGN_CENTER:
            
    //         break;
    //     case TEXT_ALIGN_CENTER_BOTH:
    //         cursor_x = xMove - w/2;
    //         break;
    //     default:
    //         cursor_x = xMove - w/2;
    //         cursor_y = yMove - h/2;
    // }
    if (cursor_x < getWidth() && cursor_y < getHeight()){
      canvas->fillRect(cursor_x, cursor_y, w, h, WHITE);
      canvas->setCursor(cursor_x, cursor_y);
      canvas->println(text);
    }
    return charDrawn;
}


uint16_t OLEDDisplay::drawString(int16_t xMove, int16_t yMove, const String &strUser) {
  uint16_t charDrawn = 0;
    int16_t x1, y1;
    uint16_t w, h;
    
    canvas->setTextSize(this->fontData);
    canvas->getTextBounds(strUser, 0, 0, &x1, &y1, &w, &h);
    canvas->setTextColor(BLACK);
    uint16_t cursor_x = xMove - w/2;
    uint16_t cursor_y = yMove - h/2;

    switch (textAlignment)
    {
         case TEXT_ALIGN_LEFT:
            cursor_x = xMove;
            break;
        case TEXT_ALIGN_RIGHT:
            cursor_x = xMove - w;
            break;
        case TEXT_ALIGN_CENTER:
            cursor_x = xMove - w/2;
            break;
        case TEXT_ALIGN_CENTER_BOTH:
            cursor_x = xMove - w/2;
            break;
        default:
            cursor_x = xMove - w/2;
            cursor_y = yMove - h/2;
    }
    
    canvas->fillRect(cursor_x, cursor_y, w, h, WHITE);
    canvas->setCursor(cursor_x, cursor_y);
    canvas->println(strUser);
    return charDrawn;
}

void OLEDDisplay::drawStringf( int16_t x, int16_t y, char* buffer, String format, ... )
{
}

uint16_t OLEDDisplay::drawStringMaxWidth(int16_t xMove, int16_t yMove, uint16_t maxLineWidth, const String &strUser) {
  drawString(xMove, yMove, strUser);
  return 0; // everything was drawn
}

uint16_t OLEDDisplay::getStringWidth(const char* text, uint16_t length, bool utf8) {
    uint16_t stringWidth = 0;
    uint16_t maxWidth = 0;
    int16_t x1, y1;
    uint16_t h;
    canvas->setTextSize(2);
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &stringWidth, &h);

    return max(maxWidth, stringWidth);
}

uint16_t OLEDDisplay::getStringWidth(const String &strUser) {
  uint16_t width = getStringWidth(strUser.c_str(), strUser.length());
  return width;
}

void OLEDDisplay::setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT textAlignment) {
  this->textAlignment = textAlignment;
}

void OLEDDisplay::setFont(const uint8_t fontSize) {
  this->fontData = fontSize;
}

void OLEDDisplay::displayOn(void) {
    digitalWrite(PULLUP_LCD, HIGH);
    isDisplayOn = true;
}

void OLEDDisplay::displayOff(void) {
    digitalWrite(PULLUP_LCD, LOW);
    ledcWrite(0, 0); 
    isDisplayOn = false;
}

void OLEDDisplay::invertDisplay(void) {
}

void OLEDDisplay::normalDisplay(void) {
}

void OLEDDisplay::setContrast(uint8_t contrast, uint8_t precharge, uint8_t comdetect) {
}

void OLEDDisplay::setBrightness(uint8_t brightness) {
  this->brightness = brightness;
  ledcWrite(0, brightness); 
}

void OLEDDisplay::resetOrientation() {

}

void OLEDDisplay::flipScreenVertically() {

}

void OLEDDisplay::mirrorScreen() {

}

void OLEDDisplay::clear(void) {
    canvas->fillScreen(WHITE);
}

void OLEDDisplay::drawLogBuffer(uint16_t xMove, uint16_t yMove) {
    canvas->flush();
}

uint16_t OLEDDisplay::getWidth(void) {
  return displayWidth;
}

uint16_t OLEDDisplay::getHeight(void) {
  return displayHeight;
}

bool OLEDDisplay::setLogBuffer(uint16_t lines, uint16_t chars){
  return true;
}

size_t OLEDDisplay::write(uint8_t c) {
  return 1;
}

size_t OLEDDisplay::write(const char* str) {
  if (str == NULL) return 0;
  size_t length = strlen(str);
  return length;
}

void inline OLEDDisplay::drawInternal(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *data, uint16_t offset, uint16_t bytesInData) {
  if (width < 0 || height < 0) return;
  if (yMove + height < 0 || yMove > this->height())  return;
  if (xMove + width  < 0 || xMove > this->width())   return;
  
}

// You need to free the char!
char* OLEDDisplay::utf8ascii(const String &str) {
  return nullptr;
}

void OLEDDisplay::setFontTableLookupFunction(FontTableLookupFunction function) {
  this->fontTableLookupFunction = function;
}
