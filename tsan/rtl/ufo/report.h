//
// Created by cbw on 9/22/17.
//
// report stack tracing in Sanitizer style
//(Bowen 2017-10-13)
//
//

#ifndef UFO_REPORT_H
#define UFO_REPORT_H



#include "defs.h"
#include "ufo_interface.h"
#include "ufo.h"

//#define DPrintf Printf

namespace bw {
namespace ufo {

extern UFOContext *uctx;


void print_callstack(__tsan::ThreadState *thr, uptr pc);


}
}


#endif //UFO_REPORT_H
