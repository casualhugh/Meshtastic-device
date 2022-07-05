#pragma once
#include "Status.h"
#include "configuration.h"
#include <Arduino.h>

// extern Button nodeDB;

namespace meshtastic
{

/// Describes the state of the GPS system.
class ButtonStatus : public Status
{

  private:
    CallbackObserver<ButtonStatus, const ButtonStatus *> statusObserver =
        CallbackObserver<ButtonStatus, const ButtonStatus *>(this, &ButtonStatus::updateStatus);
    
    bool button1_pressed = false;
    bool button2_pressed = false;
    bool button1_long = false;
    bool button2_long = false;
    bool button1_triple = false;
    bool button2_triple = false;
    bool button1_down = false;
    bool button2_down = false;
    bool button1_up = false;
    bool button2_up = false;
  public:
    ButtonStatus() {statusType = STATUS_TYPE_BUTTON; }

    ButtonStatus(bool button1_pressed,
                bool button2_pressed,
                bool button1_long,
                bool button2_long,
                bool button1_triple,
                bool button2_triple,
                bool button1_up,
                bool button2_up,
                bool button1_down,
                bool button2_down) : Status() 
    { 
        
        this->button1_pressed = button1_pressed;
        this->button2_pressed = button2_pressed;
        this->button1_long = button1_long;
        this->button2_long = button2_long;
        this->button1_triple = button1_triple;
        this->button2_triple = button2_triple;
        this->button1_up = button1_up;
        this->button2_up = button2_up;
        this->button1_down = button1_down;
        this->button2_down = button2_down;
    }
    ButtonStatus(const ButtonStatus &);
    ButtonStatus &operator=(const ButtonStatus &);

    void observe(Observable<const ButtonStatus *> *source) { statusObserver.observe(source); }

    bool getButton1Pressed() const {return button1_pressed;}
    bool getButton2Pressed() const {return button2_pressed;}
    bool getButton1Long() const {return button1_long;}
    bool getButton2Long() const {return button2_long;}
    bool getButton1Triple() const {return button1_triple;}
    bool getButton2Triple() const {return button2_triple;}
    bool getButton1Up() const {return button1_triple;}
    bool getButton2Up() const {return button2_triple;}
    bool getButton1Down() const {return button1_triple;}
    bool getButton2Down() const {return button2_triple;}

    bool matches(const ButtonStatus *newStatus) const
    {
        return 
        newStatus->button1_pressed != button1_pressed || newStatus->button2_pressed != button2_pressed ||
        newStatus->button1_long != button1_long || newStatus->button2_long != button2_long || 
        newStatus->button1_triple != button1_triple || newStatus->button2_triple != button2_triple ||
        newStatus->button1_up != button1_up || newStatus->button2_up != button2_up ||
        newStatus->button1_down != button1_down || newStatus->button2_down != button2_down;
    }

    int updateStatus(const ButtonStatus *newStatus)
    {
        // Only update the status if values have actually changed
        bool isDirty = matches(newStatus);
        button1_pressed = newStatus->button1_pressed;
        button2_pressed = newStatus->button2_pressed;
        button1_long = newStatus->button1_pressed;
        button2_long = newStatus->button2_pressed;
        button1_triple = newStatus->button1_pressed;
        button2_triple = newStatus->button2_pressed;
        button1_up = newStatus->button1_up;
        button2_up = newStatus->button2_up;
        button1_down = newStatus->button1_down;
        button2_down = newStatus->button2_down;

        if (isDirty) {
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::ButtonStatus *buttonStatus;
