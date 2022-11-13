#ifndef OLEDDISPLAY_h
#define OLEDDISPLAY_h

#include <cstdarg>

#include <Arduino.h>

#include "Arduino_GFX_Library.h"
#include "configuration.h"

//#include <OLEDDisplayFonts.h>

#ifndef DEBUG_OLEDDISPLAY
#define DEBUG_OLEDDISPLAY(...)
#endif

// Use DOUBLE BUFFERING by default
#ifndef OLEDDISPLAY_REDUCE_MEMORY
#define OLEDDISPLAY_DOUBLE_BUFFER
#endif

enum OLEDDISPLAY_COLOR {
  BLACK1 = 0,
  WHITE1 = 1,
  INVERSE = 2
};

enum OLEDDISPLAY_TEXT_ALIGNMENT {
  TEXT_ALIGN_LEFT = 0,
  TEXT_ALIGN_RIGHT = 1,
  TEXT_ALIGN_CENTER = 2,
  TEXT_ALIGN_CENTER_BOTH = 3
};
typedef char (*FontTableLookupFunction)(const uint8_t ch);
class OLEDDisplay : public Print  {
  public:
	OLEDDisplay();
    virtual ~OLEDDisplay();

	uint16_t width(void) const { return displayWidth; };
	uint16_t height(void) const { return displayHeight; };

    // Use this to resume after a deep sleep without resetting the display (what init() would do).
    // Returns true if connection to the display was established and the buffer allocated, false otherwise.
    bool allocateBuffer();

    // Allocates the buffer and initializes the driver & display. Resets the display!
    // Returns false if buffer allocation failed, true otherwise.
    bool init();

    // Free the memory used by the display
    void end();

    // Cycle through the initialization
    void resetDisplay(void);

    /* Drawing functions */
    // Sets the color of all pixel operations
    void setColor(OLEDDISPLAY_COLOR color);

    // Returns the current color.
    OLEDDISPLAY_COLOR getColor();

    // Draw a pixel at given position
    void setPixel(int16_t x, int16_t y);

    // Draw a pixel at given position and color
    void setPixelColor(int16_t x, int16_t y, OLEDDISPLAY_COLOR color);

    // Clear a pixel at given position FIXME: INVERSE is untested with this function
    void clearPixel(int16_t x, int16_t y);

    // Draw a line from position 0 to position 1
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

    // Draw the border of a rectangle at the given location
    void drawRect(int16_t x, int16_t y, int16_t width, int16_t height);

    // Fill the rectangle
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height);

    // Draw the border of a circle
    void drawCircle(int16_t x, int16_t y, int16_t radius);
    
    void drawArc(int16_t x, int16_t y, int16_t r1, int16_t r2, float start, float end);

    // Draw all Quadrants specified in the quads bit mask
    void drawCircleQuads(int16_t x0, int16_t y0, int16_t radius, uint8_t quads);

    // Fill circle
    void fillCircle(int16_t x, int16_t y, int16_t radius);

    // Draw an empty triangle i.e. only the outline
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);

    // Draw a solid triangle i.e. filled
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);

    // Draw a line horizontally
    void drawHorizontalLine(int16_t x, int16_t y, int16_t length);

    // Draw a line vertically
    void drawVerticalLine(int16_t x, int16_t y, int16_t length);

    // Draws a rounded progress bar with the outer dimensions given by width and height. Progress is
    // a unsigned byte value between 0 and 100
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress);

    // Draw a bitmap in the internal image format
    void drawFastImage(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t *image);

    // Draw a XBM
    void drawXbm(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t *xbm);

    // Draw icon 16x16 xbm format
    void drawIco16x16(int16_t x, int16_t y, const uint8_t *ico, bool inverse = false);

    /* Text functions */

    // Draws a string at the given location, returns how many chars have been written
    uint16_t drawString(int16_t x, int16_t y, const String &text);

    // Draws a formatted string (like printf) at the given location
    void drawStringf(int16_t x, int16_t y, char* buffer, String format, ... );

    // Draws a String with a maximum width at the given location.
    // If the given String is wider than the specified width
    // The text will be wrapped to the next line at a space or dash
    // returns 0 if everything fits on the screen or the numbers of characters in the
    // first line if not
    uint16_t drawStringMaxWidth(int16_t x, int16_t y, uint16_t maxLineWidth, const String &text);

    // Returns the width of the const char* with the current
    // font settings
    uint16_t getStringWidth(const char* text, uint16_t length, bool utf8 = false);

    // Convencience method for the const char version
    uint16_t getStringWidth(const String &text);

    // Specifies relative to which anchor point
    // the text is rendered. Available constants:
    // TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT, TEXT_ALIGN_CENTER_BOTH
    void setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT textAlignment);

    // Sets the current font. Available default fonts
    // ArialMT_Plain_10, ArialMT_Plain_16, ArialMT_Plain_24
    void setFont(const uint8_t fontSize);

    // Set the function that will convert utf-8 to font table index
    void setFontTableLookupFunction(FontTableLookupFunction function);

    /* Display functions */

    // Turn the display on
    void displayOn(void);

    // Turn the display offs
    void displayOff(void);

    // Inverted display mode
    void invertDisplay(void);

    // Normal display mode
    void normalDisplay(void);

    // Set display contrast
    // really low brightness & contrast: contrast = 10, precharge = 5, comdetect = 0
    // normal brightness & contrast:  contrast = 100
    void setContrast(uint8_t contrast, uint8_t precharge = 241, uint8_t comdetect = 64);

    // Convenience method to access 
    virtual void setBrightness(uint8_t);

    // Reset display rotation or mirroring
    void resetOrientation();

    // Turn the display upside down
    void flipScreenVertically();

    // Mirror the display (to be used in a mirror or as a projector)
    void mirrorScreen();
    
    // Write the buffer to the display memory
    virtual void display(void) = 0;

    // Clear the local pixel buffer
    void clear(void);

    // Log buffer implementation

    // This will define the lines and characters you can
    // print to the screen. When you exeed the buffer size (lines * chars)
    // the output may be truncated due to the size constraint.
    bool setLogBuffer(uint16_t lines, uint16_t chars);

    // Draw the log buffer at position (x, y)
    void drawLogBuffer(uint16_t x, uint16_t y);

    // Get screen geometry
    uint16_t getWidth(void);
    uint16_t getHeight(void);

    // Implement needed function to be compatible with Print class
    size_t write(uint8_t c);
    size_t write(const char* s);

    uint8_t            *buffer;

    #ifdef OLEDDISPLAY_DOUBLE_BUFFER
    uint8_t            *buffer_back;
    #endif
    bool isOn() { return isDisplayOn;}
  protected:
    uint16_t  displayWidth;
    uint16_t  displayHeight;
    uint16_t  displayBufferSize;
    uint8_t brightness;
    bool isDisplayOn;
    OLEDDISPLAY_TEXT_ALIGNMENT   textAlignment;
    OLEDDISPLAY_COLOR            color;

    uint8_t	 fontData;

    // State values for logBuffer
    uint16_t   logBufferSize;
    uint16_t   logBufferFilled;
    uint16_t   logBufferLine;
    uint16_t   logBufferMaxLines;
    char      *logBuffer;

    // Connect to the display
    virtual bool connect() { return false; };
    // converts utf8 characters to extended ascii
    char* utf8ascii(const String &s);

    void inline drawInternal(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *data, uint16_t offset, uint16_t bytesInData) __attribute__((always_inline));

    uint16_t drawStringInternal(int16_t xMove, int16_t yMove, const char* text, uint16_t textLength, uint16_t textWidth, bool utf8);

	FontTableLookupFunction fontTableLookupFunction;
};
extern Arduino_DataBus *bus;
extern Arduino_G *tft;
extern Arduino_GFX *canvas;
#endif
