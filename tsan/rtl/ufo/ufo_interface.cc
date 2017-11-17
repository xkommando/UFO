//
// Created by cbw on 9/26/16.
//
// a layer between interfaces and UFO control center
// all functionality is controlled by UFOContext (singleton)
//
// WARN: do not execute any static operations except POD assignment
//
// (Bowen 2017-10-13)
//
#include <stdio.h>
#include <stdlib.h>

#include "../tsan_interface.h"
#include "defs.h"
#include "ufo_interface.h"
#include "ufo.h"


namespace bw {
namespace ufo {

using namespace __tsan;

// this is just a flag indicating ufo status
// ufo is enable/disabled by UFOContext::start_trace() stop_trace()
static volatile bool g_started = false;

UFOContext* uctx;

//rtl/tsan_interface.cc:32:  bw::ufo_bench::init_ufo();
bool init_ufo() {
  if (g_started)
    return true;

  __tsan::flags()->history_size = 0;

  uctx = (UFOContext*)internal_alloc(MBlockScopedBuf, sizeof(UFOContext));
  uctx->init_start();

  g_started = true;
  return true;
}

using namespace __sanitizer;

//rtl/tsan_rtl.cc:383:  bw::ufo_bench::finish_ufo();
bool finish_ufo() {
  if (!g_started)
    return false;

  uctx->destroy();
  // when finish ufo, set g_started to false firstly.
  g_started = false;

  internal_free(uctx);
  uctx = nullptr;
  return false;
}


// might loss some events
void before_fork() {
  uctx->stop_trace();
  g_started = false;
}

void parent_after_fork() {
  uctx->start_trace();
  g_started = true;
}
void child_after_fork() {
  uctx->child_after_fork();
  g_started = true;
}


///////////////////////////////////////////////////////////////////////////////////////////////

void on_mtx_lock(__tsan::ThreadState *thr, uptr pc, u64 mutex_id) {
  (*UFOContext::fn_mtx_lock)(thr, pc, mutex_id);
}

void on_mtx_unlock(__tsan::ThreadState *thr, uptr pc, u64 mutex_id) {
  (*UFOContext::fn_mtx_unlock)(thr, pc, mutex_id);
}

void on_cond_wait(__tsan::ThreadState* thr, uptr pc, u64 addr_cond, u64 addr_mtx) {
  (*UFOContext::fn_cond_wait)(thr, pc, addr_cond, addr_mtx);
}

void on_cond_signal(__tsan::ThreadState* thr, uptr pc, u64 addr_cond) {
  (*UFOContext::fn_cond_signal)(thr, pc, addr_cond);
}

void on_cond_broadcast(__tsan::ThreadState* thr, uptr pc, u64 addr_cond) {
  (*UFOContext::fn_cond_bc)(thr, pc, addr_cond);
}

//void on_rd_lock(__tsan::ThreadState *thr, uptr pc, u64 mutex_id) {
//  MC_STAT(thr, c_rd_lock)
//  (*UFOContext::fn_rd_lock)(thr, pc, mutex_id);
//}
//
//void on_rd_unlock(__tsan::ThreadState *thr, uptr pc, u64 mutex_id) {
//  MC_STAT(thr, c_rd_unlock)
//  (*UFOContext::fn_rd_unlock)(thr, pc, mutex_id);
//}
//
//void on_rw_unlock(__tsan::ThreadState *thr, uptr pc, u64 mutex_id) {
//  MC_STAT(thr, c_rw_unlock)
//  (*UFOContext::fn_rw_unlock)(thr, pc, mutex_id);
//}


void *on_alloc(ThreadState *thr, uptr pc, void *addr_left, uptr size) {
  return (*UFOContext::fn_alloc)(thr, pc, addr_left, size);
}

void on_dealloc(ThreadState *thr, uptr pc, void *addr) {
  (*UFOContext::fn_dealloc)(thr, pc, addr);
}


__HOT_CODE
void on_mem_acc(ThreadState *thr, uptr pc, uptr addr, int kAccessSizeLog, volatile bool is_write) {
  (*UFOContext::fn_mem_acc)(thr, pc, addr, kAccessSizeLog, is_write);
}

__HOT_CODE
void on_mem_range_acc(__tsan::ThreadState *thr, uptr pc, uptr addr, uptr size, volatile bool is_write) {
  (*UFOContext::fn_mem_range_acc)(thr, pc, addr, size, is_write);
}


void on_thread_created(int tid_parent, int tid_kid, uptr pc) {
  (*UFOContext::fn_thread_created)(tid_parent, tid_kid, pc);
}
void on_thread_start(__tsan::ThreadState* thr, uptr stk_addr, uptr stk_size, uptr tls_addr, uptr tls_size) {
  (*UFOContext::fn_thread_started)(thr, stk_addr, stk_size, tls_addr, tls_size);
}

void on_thread_join(int tid_main, int tid_joiner, uptr pc) {
  (*UFOContext::fn_thread_join)(tid_main, tid_joiner, pc);
}


void on_thread_end(__tsan::ThreadState* thr) {
  (*UFOContext::fn_thread_end)(thr);
}

void enter_func(__tsan::ThreadState *thr, uptr pc) {
  (*UFOContext::fn_enter_func)(thr, pc);
}

void exit_func(__tsan::ThreadState *thr) {
  (*UFOContext::fn_exit_func)(thr);
}

void on_ptr_prop(__tsan::ThreadState *thr, uptr pc, uptr addr_src, uptr addr_dest) {
  (*UFOContext::fn_ptr_prop)(thr, pc, addr_src, addr_dest);
}
void on_ptr_deref(__tsan::ThreadState *thr, uptr pc, uptr addr_ptr) {
  (*UFOContext::fn_ptr_deref)(thr, pc, addr_ptr);
}

} // ns ufo_bench
} // ns bw
















