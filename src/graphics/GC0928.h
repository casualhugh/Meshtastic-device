#ifndef GC0928_h
#define GC0928_h

#include "OLEDDisplay.h"
#include <Wire.h>
#include <algorithm>

class GC0928 : public OLEDDisplay {
  public:
    GC0928() {
    }

    bool connect() {
      return true;
    }

    void display(void) {
        canvas->flush();
    }

};

#endif
