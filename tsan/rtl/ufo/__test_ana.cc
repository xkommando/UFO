//
// Created by cbw on 10/4/17.
//


#include <unistd.h>
#include <pthread.h>
#include "../../../sanitizer_common/sanitizer_atomic.h"
#include "../../../sanitizer_common/sanitizer_common.h"
#include "../../../sanitizer_common/sanitizer_posix.h"

#include "../../../interception/interception.h"
#include "../tsan_defs.h"
#include "../tsan_mman.h"

#include "ufo.h"
#include "defs.h"
#include "tlbuffer.h"

#include "__test_ana.h"

#include "__test_matching.h"

// declare the functions needed within this file
// if used in another file, need to declare again

DECLARE_REAL(int, pthread_mutex_init, pthread_mutex_t*, const pthread_mutexattr_t*)
DECLARE_REAL(int, pthread_mutex_lock, pthread_mutex_t*)
DECLARE_REAL(int, pthread_mutex_unlock, pthread_mutex_t*)
DECLARE_REAL(int, pthread_mutex_destroy, pthread_mutex_t*)

DECLARE_REAL(int, pthread_cond_init, pthread_cond_t*, const pthread_condattr_t*)
DECLARE_REAL(int, pthread_cond_wait, pthread_cond_t*, pthread_mutex_t*)
DECLARE_REAL(int, pthread_cond_signal, pthread_cond_t*)
DECLARE_REAL(int, pthread_cond_destroy, pthread_cond_t*)


namespace bw {
namespace ufo {


extern UFOContext *uctx;
extern TLBuffer *G_BUF_BASE;

static pthread_t pt_worker_;
static volatile bool worker_is_running = false;

bool __do_ana() {

  Printf("\r\n>>> __do_ana\r\n");

  const u32 ct_before = uctx->ana_count;
  uctx->tlbuf_lock.Lock();
  const u32 ct_after = uctx->ana_count;

  if (ct_after > ct_before) {
    uctx->tlbuf_lock.Unlock();
    return false;
  } else if (ct_before == ct_after) {
    uctx->stop_trace();
    uctx->is_on = false;

    TLBuffer *tlbufs_old = uctx->tlbufs;

    // step1: prepare new tl buffer
    TLBuffer *buf_new = (TLBuffer *) __tsan::internal_alloc(__tsan::MBlockScopedBuf, MAX_THREAD * sizeof(TLBuffer));
    for (u32 i = 0; i < MAX_THREAD; ++i) {
      buf_new[i].init();
    }

    uctx->tlbufs = buf_new;
    G_BUF_BASE = uctx->tlbufs;

    uctx->mem_hold = 0;

    uctx->ana_count = ct_after + 1;
    uctx->is_on = true;
    uctx->start_trace();
    uctx->tlbuf_lock.Unlock();

    if (pt_worker_ == -1) {
      //first time
      int rt = __sanitizer::real_pthread_create(&pt_worker_, NULL, __demo_detect, tlbufs_old);
    } else {
      // wait for last taks, blocking
      int rt = __sanitizer::real_pthread_join((void *) pt_worker_, NULL);
      int rt = __sanitizer::real_pthread_create(&pt_worker_, NULL, __demo_detect, tlbufs_old);

    }
    return true;
  }

}


void __finish_remaining() {
  if (pt_worker_ != -1) {
    int rt = __sanitizer::real_pthread_join((void *) pt_worker_, NULL);
    
  }
  __demo_detect(uctx->tlbufs);
}


}
}