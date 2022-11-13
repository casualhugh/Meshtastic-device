#include "Screen.h"
#include "modules/AdminModule.h"
#define NUM_SETTINGS 9
#define LETTERS_SIZE 26
namespace graphics
{
typedef void (*SaveCallback)(char* buf, uint8_t length);

enum SETTING_TYPES{
    PREDEFINED,
    WORD,
    BOOL
};
void saveChannel(char* buf, uint8_t length);
void saveMode(char* buf, uint8_t length);
void saveLongName(char* buf, uint8_t length);
void saveShortName(char* buf, uint8_t length);
void saveCannedMsgOne(char* buf, uint8_t length);
void saveCannedMsgTwo(char* buf, uint8_t length);
void saveCannedMsgThree(char* buf, uint8_t length);
void saveCannedMsgFour(char* buf, uint8_t length);
void resetCannedMsgs(char* buf, uint8_t length);
void enableDebugMenu(char* buf, uint8_t length);
void restart(char* buf, uint8_t length);
// Forward declarations
class Screen;
/// Handles gathering and displaying debug information.
class DebugInfo
{
  public:
    DebugInfo(const DebugInfo &) = delete;
    DebugInfo &operator=(const DebugInfo &) = delete;
    void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    
  private:
    
    friend Screen;
    const char settings[NUM_SETTINGS][20] = {
//        "Channel",
//        "Mode",
        "Set name",
        "Choose name",
        "Reset msgs",
        "Canned msg 1",
        "Canned msg 2",
        "Canned msg 3",
        "Canned msg 4",
        "Enable features",
        "Restart"
    };
    const uint8_t setting_type[NUM_SETTINGS] = {
//        PREDEFINED,
//        PREDEFINED,
        WORD,
        PREDEFINED,
        PREDEFINED,
        WORD,
        WORD,
        WORD,
        WORD,
        PREDEFINED,
        PREDEFINED
    };
    /// Saves
    
    const SaveCallback settingSaves[NUM_SETTINGS] {
//      saveChannel,
//      saveMode,
      saveLongName,
      saveShortName,
      resetCannedMsgs,
      saveCannedMsgOne,
      saveCannedMsgTwo,
      saveCannedMsgThree,
      saveCannedMsgFour,
      enableDebugMenu,
      restart
    };
    const char alphabet[LETTERS_SIZE] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
    int currentLetter = 0;
    char tempStore[64];
    DebugInfo() {}

    /// Renders the debug screen.
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    //void drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawWordSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void doSwitchSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameTestNode(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void resetFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void setName(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    /// Protects all of internal state.
    concurrency::Lock lock;
};
}