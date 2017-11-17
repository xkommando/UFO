
Basic Structure:

**ufo_interface.h**: functions inserted into *compiler-rt*, called at runtime
see tsan_modification.txt for insertion locations.

**UFOContext**: 
UFOContext is a singleton object (**uctx**), it is the control center for UFO runtime.
see `ufo.cc::init_start`


**TLBuffer**
event buffer for each thread
* $MAX_THREAD threads <==> $MAX_THREAD tlbuffer, stored in an array
* tsan thread id (ThreadState::tid) starts from 0 to $MAX_THREAD,
the corresponding tlbuffer is tlbuffers\[tid\]

* all buffers are init at beginning, but only allocated when thread starts.

**Event IO**

read
https://github.tamu.edu/bowen-cai/TEMP/blob/master/tsan/rtl/ufo/tlbuffer.cc#LC142
and
https://github.tamu.edu/bowen-cai/TEMP/blob/master/tsan/rtl/ufo/io_queue.h

**Mem dellocations**

* when ___free()___ is called, pointer is stored in TLBuffer::memptr_buf,
updated ___tl_alloc_size___ in tlbuffer, updated ___mem_hold___ in **uctx**

* when ___malloc()___ is called, check tlbuffer ___mem_exceeded()___ , 
if needed release all heap mem in held


**Print Stack Trace**

read ___report.h___

**Event Processing**

if any thread local buffer(tlbuffer) is full, call _____do_ana()___:
1. lock uctx
2. re-check after lock acquisition, if other thread has already started the anaylsis for current tlbuffer (during the ___stop_tracing()___ period), unlock and return.
3. disable tracing ( ___stop_tracing()___ )
4. create new *tlbuffer* and init them for all threads
5. enable tracing
6. unlock
7. waite previous worker -> spawn new worker -> process old tlbuffer

**Others**

* read `UFOContext::child_after_fork()` for multi-process handling

* All sanitizers (and UFO) are build upon Compiler-RunTime, 
no _system call_, _cpp stl_, _stdlib_ is available. 
Read *sanitizer_common* and invent your own wheels 
 
* read *tsan/tests/rtl/CMakeLists.txt* on how to call sanitizer functions in test code

