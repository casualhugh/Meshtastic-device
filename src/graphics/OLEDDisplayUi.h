#ifndef OLEDDISPLAYUI_h
#define OLEDDISPLAYUI_h

#include <Arduino.h>
#include "OLEDDisplay.h"
#include <vector>
#include <algorithm>
#ifndef DEBUG_OLEDDISPLAYUI
#define DEBUG_OLEDDISPLAYUI(...)
#endif

enum AnimationDirection {
  SLIDE_UP,
  SLIDE_DOWN,
  SLIDE_LEFT,
  SLIDE_RIGHT
};

enum IndicatorPosition {
  TOP,
  RIGHT,
  BOTTOM,
  LEFT
};

enum IndicatorDirection {
  LEFT_RIGHT,
  RIGHT_LEFT
};

enum FrameState {
  IN_TRANSITION,
  FIXED
};

enum TransitionRelationship {
  NONE,
  INCOMING,
  OUTGOING,
};

const uint8_t ANIMATION_activeSymbol[] PROGMEM = {
  0x00, 0x18, 0x3c, 0x7e, 0x7e, 0x3c, 0x18, 0x00
};

const uint8_t ANIMATION_inactiveSymbol[] PROGMEM = {
  0x00, 0x0, 0x0, 0x18, 0x18, 0x0, 0x0, 0x00
};


// Structure of the UiState
struct OLEDDisplayUiState {
  uint64_t     lastUpdate;
  uint16_t      ticksSinceLastStateSwitch;
  uint16_t      ticks;


  FrameState    frameState;
  uint8_t       currentFrame;
  uint8_t       transitionFrameTarget;
  TransitionRelationship transitionFrameRelationship;

  std::vector<uint32_t>     notifyingFrames;
  bool          isIndicatorDrawn;

  // Normal = 1, Inverse = -1;
  int8_t        frameTransitionDirection;

  bool          manualControl;

  // Custom data that can be used by the user
  void*         userData;
};

struct LoadingStage {
  const char* process;
  void (*callback)();
};

typedef void (*FrameCallback)(OLEDDisplay *display,  OLEDDisplayUiState* state, int16_t x, int16_t y);
typedef void (*OverlayCallback)(OLEDDisplay *display,  OLEDDisplayUiState* state);
typedef void (*LoadingDrawFunction)(OLEDDisplay *display, LoadingStage* stage, uint8_t progress);
typedef void (*FrameNotificationCallback)(uint32_t FrameNumber, void* UI);

class OLEDDisplayUi {
  private:
    OLEDDisplay             *display;

    // Symbols for the Indicator
    IndicatorPosition   indicatorPosition;
    IndicatorDirection  indicatorDirection;

    const uint8_t*         activeSymbol;
    const uint8_t*         inactiveSymbol;

    bool                shouldDrawIndicators;

    // Values for the Frames
    AnimationDirection  frameAnimationDirection;

    int8_t              lastTransitionDirection;

    uint16_t            ticksPerFrame; 		// ~ 5000ms at 30 FPS
    uint16_t            ticksPerTransition;	// ~  500ms at 30 FPS

    bool                autoTransition;

    FrameCallback*      frameFunctions;
    FrameNotificationCallback* frameNotificationCallbackFunction;
    uint8_t             frameCount;
    uint8_t             notifyingFrameOffsetAmplitude;

    // Internally used to transition to a specific frame
    int8_t              nextFrameNumber;

    // Values for Overlays
    OverlayCallback*    overlayFunctions;
    uint8_t             overlayCount;

    // Will the Indicator be drawn
    // 3 Not drawn in both frames
    // 2 Drawn this frame but not next
    // 1 Not drawn this frame but next
    // 0 Not known yet
    uint8_t                indicatorDrawState;

    // Loading screen
    LoadingDrawFunction loadingDrawFunction;
	
    // UI State
    OLEDDisplayUiState      state;

    // Bookeeping for update
    uint16_t            updateInterval            = 33;

    uint16_t            timePerFrame;
    uint16_t            timePerTransition;

    uint8_t             getNextFrameNumber();
    void                drawIndicator();
    void                drawFrame();
    void                drawOverlays();
    void                tick();
    void                resetState();

  public:

    OLEDDisplayUi(OLEDDisplay *display);

    /**
     * Initialise the display
     */
    void init();

    /**
     * Configure the internal used target FPS
     */
    void setTargetFPS(uint8_t fps);

    // Automatic Control
    /**
     * Enable automatic transition to next frame after the some time can be configured with `setTimePerFrame` and `setTimePerTransition`.
     */
    void enableAutoTransition();

    /**
     * Disable automatic transition to next frame.
     */
    void disableAutoTransition();

    /**
     * Set the direction if the automatic transitioning
     */
    void setAutoTransitionForwards();
    void setAutoTransitionBackwards();

    /**
     *  Set the approximate time a frame is displayed
     */
    void setTimePerFrame(uint16_t time);

    /**
     * Set the approximate time a transition will take
     */
    void setTimePerTransition(uint16_t time);

    // Customize indicator position and style

    /**
     * Draw the indicator.
     * This is the defaut state for all frames if
     * the indicator was hidden on the previous frame
     * it will be slided in.
     */
    void enableIndicator();

    /**
     * Don't draw the indicator.
     * This will slide out the indicator
     * when transitioning to the next frame.
     */
    void disableIndicator();

    /**
     * Enable drawing of indicators
     */
    void enableAllIndicators();

    /**
     * Disable draw of indicators.
     */
    void disableAllIndicators();

    /**
     * Set the position of the indicator bar.
     */
    void setIndicatorPosition(IndicatorPosition pos);

    /**
     * Set the direction of the indicator bar. Defining the order of frames ASCENDING / DESCENDING
     */
    void setIndicatorDirection(IndicatorDirection dir);

    /**
     * Set the symbol to indicate an active frame in the indicator bar.
     */
    void setActiveSymbol(const uint8_t* symbol);

    /**
     * Set the symbol to indicate an inactive frame in the indicator bar.
     */
    void setInactiveSymbol(const uint8_t* symbol);

    /**
     * Adds a frame to the list of frames with active notifications
     */ 
    bool addFrameToNotifications(uint32_t frameToAdd, bool force=false);

    /**
     * Removes a frame from the list of frames with active notifications
     */
    bool removeFrameFromNotifications(uint32_t frameToRemove);

    /**
     * Sets a callback function to be called when a frame comes into focus
     * Normally this function will remove the frame from the list of 
     * active notifications
     */

    void setFrameNotificationCallback(FrameNotificationCallback* frameNotificationCallbackFunction);

    /**
     * Returns the number of the frist frame having notifications
     * This is most likely to be used when attempting to "jump"
     * to the oldest notification
     */
    uint32_t getFirstNotifyingFrame();

    // Frame settings

    /**
     * Configure what animation is used to transition from one frame to another
     */
    void setFrameAnimation(AnimationDirection dir);

    /**
     * Add frame drawing functions
     */
    void setFrames(FrameCallback* frameFunctions, uint8_t frameCount);

    // Overlay

    /**
     * Add overlays drawing functions that are draw independent of the Frames
     */
    void setOverlays(OverlayCallback* overlayFunctions, uint8_t overlayCount);


    // Loading animation
    /**
     * Set the function that will draw each step
     * in the loading animation
     */
    void setLoadingDrawFunction(LoadingDrawFunction loadingFunction);


    /**
     * Run the loading process
     */
    void runLoadingProcess(LoadingStage* stages, uint8_t stagesCount);


    // Manual Control
    void nextFrame();
    void previousFrame();

    /**
     * Switch without transition to frame `frame`.
     */
    void switchToFrame(uint8_t frame);

    /**
     * Transition to frame `frame`, when the `frame` number is bigger than the current
     * frame the forward animation will be used, otherwise the backwards animation is used.
     */
    void transitionToFrame(uint8_t frame);

    // State Info
    OLEDDisplayUiState* getUiState();

    int16_t update();
};
#endif
