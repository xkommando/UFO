//
// Created by xkommando on 11/1/16.
//

#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/time.h>

#include "../../../sanitizer_common/sanitizer_common.h"
#include <stdio.h>

#include "../../../sanitizer_common/sanitizer_posix.h"
#include "../../../sanitizer_common/sanitizer_libc.h"

#include "../tsan_defs.h"
#include "../tsan_flags.h"
#include "../tsan_mman.h"
#include "../tsan_symbolize.h"

#include "defs.h"
#include "ufo.h"

#include "dummy_rtl.h"
#include "rtl_impl.h"

#include "__test_ana.h"

namespace bw {
namespace ufo {


//static
u64 UFOContext::get_time_ms() {
  timeval tv;
  gettimeofday(&tv, 0);
  u64 ms = (u64) (tv.tv_sec) * 1000 + (u64) (tv.tv_usec) / 1000;
  return ms;
}
//void* (*fn_alloc)(__tsan::ThreadState *thr, uptr pc, void *addr, uptr size);
//void (*fn_dealloc)(__tsan::ThreadState *thr, uptr pc, void *addr);
//void (*fn_thread_start)(int tid_parent, int tid_kid, uptr pc);
//void (*fn_thread_join)(int tid_main, int tid_joiner, uptr pc);
//
//void (*fn_mtx_lock)(__tsan::ThreadState *thr, uptr pc, u64 mutex_id);
//void (*fn_mtx_unlock)(__tsan::ThreadState *thr, uptr pc, u64 mutex_id);
//
//void (*fn_rd_lock)(__tsan::ThreadState *thr, uptr pc, u64 mutex_id);
//void (*fn_rd_unlock)(__tsan::ThreadState *thr, uptr pc, u64 mutex_id);
//void (*fn_rw_unlock)(__tsan::ThreadState *thr, uptr pc, u64 mutex_id);
//
//void (*fn_mem_acc)(__tsan::ThreadState *thr, uptr pc, uptr addr, int kAccessSizeLog, bool is_write);
//void (*fn_mem_range_acc)(__tsan::ThreadState *thr, uptr pc, uptr addr, uptr size, bool is_write);
//void (*fn_enter_func)(__tsan::ThreadState *thr, uptr pc);
//void (*fn_exit_func)(__tsan::ThreadState *thr);

FPAlloc UFOContext::fn_alloc = &nop_alloc;

FPDealloc UFOContext::fn_dealloc = &nop_dealloc;
FPThr UFOContext::fn_thread_created = &nop_thread_created;
FPThrStart UFOContext::fn_thread_started = &nop_thread_start;
FPThr UFOContext::fn_thread_join = &nop_thread_join;
FPThrEnd UFOContext::fn_thread_end = &nop_thread_end;

FPMtxLock UFOContext::fn_mtx_lock = &nop_mtx_lock;
FPMtxLock UFOContext::fn_mtx_unlock = &nop_mtx_unlock;

FPMtxLock UFOContext::fn_rd_lock = &nop_rd_lock;
FPMtxLock UFOContext::fn_rd_unlock = &nop_rd_unlock;
FPMtxLock UFOContext::fn_rw_unlock = &nop_rw_unlock;


FPCondWait UFOContext::fn_cond_wait = &nop_cond_wait;
FPCondSignal UFOContext::fn_cond_signal = &nop_cond_signal;
FPCondSignal UFOContext::fn_cond_bc = &nop_cond_broadcast;

FPMemAcc UFOContext::fn_mem_acc = &nop_mem_acc;
FPMemRangeAcc UFOContext::fn_mem_range_acc = &nop_mem_range_acc;

FPFuncEnter UFOContext::fn_enter_func = &nop_enter_func;
FPFuncExit UFOContext::fn_exit_func = &nop_exit_func;
FPPtrProp UFOContext::fn_ptr_prop = &nop_ptr_prop;
FPPtrDeRef UFOContext::fn_ptr_deref = &nop_ptr_deref;

// static
s64 UFOContext::get_int_opt(const char *name, s64 default_val) {
  const char *str_int = __sanitizer::GetEnv(name);
  if (str_int == nullptr
      || internal_strnlen(str_int, 40) < 1) {
    return default_val;
  } else {
    return internal_atoll(str_int);
  }
}


void UFOContext::start_trace() {
  if ( !is_on) {
    return;
  }
  fn_alloc = &impl_alloc;
  fn_dealloc = &impl_dealloc;
  fn_thread_created = &impl_thread_created;
  fn_thread_started = &impl_thread_started;
  fn_thread_join = &impl_thread_join;
  fn_thread_end = &impl_thread_end;

  fn_mtx_lock = &impl_mtx_lock;
  fn_mtx_unlock = &impl_mtx_unlock;
  fn_rd_lock = &impl_rd_lock;
  fn_rd_unlock = &impl_rd_unlock;
  fn_rw_unlock = &impl_rw_unlock;

  fn_cond_wait = &impl_cond_wait;
  fn_cond_signal = &impl_cond_signal;
  fn_cond_bc = &impl_cond_broadcast;

  s64 set_no_stack = get_int_opt(ENV_NO_STACK_ACC, 0);
  s64 set_no_value = get_int_opt(ENV_NO_VALUE, 0);

  if (this->trace_func_call) {
    fn_enter_func = &impl_enter_func;
    fn_exit_func = &impl_exit_func;
  }
  if (this->trace_ptr_prop) {
    fn_ptr_prop = &impl_ptr_prop;
    fn_ptr_deref = &impl_ptr_deref;
  }

  this->no_data_value = set_no_value != 0;
  if (set_no_stack == 0) {
    this->no_stack = false;
    fn_mem_range_acc = &impl_mem_range_acc;
    if (no_data_value) {
      fn_mem_acc = &impl_mem_acc_nv;
    } else {
      fn_mem_acc = &impl_mem_acc;
    }
  } else {
    this->no_stack = true;
    fn_mem_range_acc = &ns_mem_range_acc;
    if (no_data_value) {
      fn_mem_acc = &ns_mem_acc_nv;
    } else {
      fn_mem_acc = &ns_mem_acc;
    }
  }
  DPrintf("UFO>>>start tracing\r\n");
}

void UFOContext::stop_trace() {
  fn_alloc = &nop_alloc;
  fn_dealloc = &nop_dealloc;
  fn_thread_created = &nop_thread_created;
  fn_thread_started = &nop_thread_start;
  fn_thread_join = &nop_thread_join;
  fn_thread_end = &nop_thread_end;

  fn_mtx_lock = &nop_mtx_lock;
  fn_mtx_unlock = &nop_mtx_unlock;
  fn_rd_lock = &nop_rd_lock;
  fn_rd_unlock = &nop_rd_unlock;
  fn_rw_unlock = &nop_rw_unlock;

  fn_cond_wait = &nop_cond_wait;
  fn_cond_signal = &nop_cond_signal;
  fn_cond_bc = &nop_cond_broadcast;

  fn_mem_acc = &nop_mem_acc;
  fn_mem_range_acc = &nop_mem_range_acc;
  fn_enter_func = &nop_enter_func;
  fn_exit_func = &nop_exit_func;
  fn_ptr_prop = &nop_ptr_prop;
  fn_ptr_deref = &nop_ptr_deref;
}
void UFOContext::read_config() {

  this->tl_buf_size_ = {0};
  this->use_compression = 0;
  this->use_io_q = 0;
  this->out_queue_length = -1;

  this->do_print_stat_ = get_int_opt(ENV_PRINT_STAT, 0);

  // buffer size
  u64 bufsz = (u64)get_int_opt(ENV_TL_BUF_SIZE, DEFAULT_BUF_PRE_THR);
  if (0 < bufsz && bufsz < 4096) { // 4G
//    this->tl_buf_size = (u64) bufsz * 1024 * 1024; // to MB
    u32 sz = bufsz * 1024 * 1024;
    atomic_store_relaxed(&this->tl_buf_size_, sz);
  } else {
    Printf("!!! Could not read thread local buffer size or size is illegal:  %ld\r\n", bufsz);
    Die();
  }

  bufsz = (u64)get_int_opt(ENV_MAX_MEM_HOLD, DEFAULT_HOLD_MEM_SIZE);
  bufsz *= 1024 * 1024;
  this->max_mem_hold = bufsz;

  // use snappy compression
  s64 use_comp = get_int_opt(ENV_USE_COMPRESS, COMPRESS_ON);
  this->use_compression = use_comp;

  // use async io queue
  s64 do_use_q = get_int_opt(ENV_USE_IO_Q, ASYNC_IO_ON);
  this->use_io_q = do_use_q;
  if (use_io_q) {
    // queue size
    s64 io_q_sz = get_int_opt(ENV_IO_Q_SIZE, DEFAULT_IO_Q_SIZE);
    if (0 < io_q_sz && io_q_sz < 1024) {
      this->out_queue_length = io_q_sz;
    } else {
      Printf("!!! Could not read out queue length or length is illegal:  %d\r\n", io_q_sz);
      Die();
    }
  }

  s64 do_trace_call = get_int_opt(ENV_TRACE_FUNC, 0);
  this->trace_func_call = do_trace_call;

  this->trace_ptr_prop = false;
}

void UFOContext::print_config() {

  Printf("UFO>>> UFO (pid:%u from:%u) enabled: ", this->cur_pid_, this->p_pid_);
  if (this->no_stack) {
    Printf("do not record stack access; ");
  } else {
    Printf("record stack access; ");
  }
  if (this->no_data_value) {
    Printf("do not record read/write value; ");
  } else {
    Printf("record value; ");
  }

  #ifdef STAT_ON
  Printf("statistic set on; ");
#endif
  if (this->do_print_stat_) {
    Printf("print stat; ");
  } else {
    Printf("do not print stat; ");
  }
  if (this->use_compression) {
    Printf("compress buffer; ");
  } else {
    Printf("do not compress; ");
  }
  if (this->use_io_q) {
    Printf("async IO queue enabled, length: %d; ", out_queue_length);
  } else {
    Printf("async IO queue disabled; ");
  }

  u32 sz = get_buf_size();
  Printf("initial per thread buffer size:%u (%u MB), ", sz, (sz / 1024 / 1024));
  Printf("max mem in hold: %d MB", (this->max_mem_hold / 1024 / 1024));
}

void UFOContext::init_start() {
  // this process id
  this->cur_pid_ = (u32)__sanitizer::internal_getpid();
  this->p_pid_ = (u32)__sanitizer::internal_getppid();
  this->is_subproc = false;
  this->mudule_length_ = 0;
  this->sync_seq = 0;

  this->batchr_count = 0;
  this->mem_hold = 0;

  s64 set_on = get_int_opt(ENV_UFO_ON, 0);
  this->is_on = (set_on != 0);

  // step 0: prepare statistic
#ifdef STAT_ON
  this->stat = (TLStatistic *) __tsan::internal_alloc(__tsan::MBlockScopedBuf, MAX_THREAD * sizeof(TLStatistic));
  for (u32 i = 0; i < MAX_THREAD; ++i) {
    stat[i].init();
  }
#endif

  // read before return (if not on), stat config
  read_config(); // before openbuf

  if (!this->is_on) {
    Printf("UFO>>> UFO (pid:%u from:%u) disabled\r\n", this->cur_pid_);
    return;
  }

  time_started = get_time_ms();
  print_config();

  // step1: prepare tl buffer
  tlbufs = (TLBuffer *) __tsan::internal_alloc(__tsan::MBlockScopedBuf, MAX_THREAD * sizeof(TLBuffer));
  for (u32 i = 0; i < MAX_THREAD; ++i) {
    tlbufs[i].init();
  }
  G_BUF_BASE = tlbufs;
  this->ana_count = 0;

  // step 3: prepare async io queue
  if (use_io_q) {
    this->out_queue = (OutQueue *) __tsan::internal_alloc(__tsan::MBlockScopedBuf, sizeof(OutQueue));
    out_queue->start(this->out_queue_length);
  }

  // step 4: create trace dir
//  open_trace_dir();
  // step 5: save loaded module info
//  save_module_info();

  // step create buffer for main thread
  tlbufs[0].open_buf();
  tlbufs[0].thr = cur_thread();

  { // put ThreadStarted event to main thread trace
    uptr stk_addr;
    uptr stk_size;
    uptr tls_addr;
    uptr tls_size;

    __sanitizer::GetThreadStackAndTls(false,
                                      (uptr *) &stk_addr, &stk_size,
                                      (uptr *) &tls_addr, &tls_size);
    ThreadBeginEvent be(u16(0), u64(0), u32(0));
    be.stk_addr = (u64) stk_addr;
    be.stk_size = (u32) stk_size;
    be.tls_addr = (u64) tls_addr;
    be.tls_size = (u32) tls_size;
    tlbufs[0].put_event(be);

    tlbufs[0].tls_height = (u32)tls_size;
    tlbufs[0].tls_bottom = (u64)tls_addr;// lower address
    tlbufs[0].stack_height = (u32)stk_size;
    tlbufs[0].stack_bottom = (u64)stk_addr;// lower address
  }
  this->start_trace();
}


void UFOContext::destroy() {
  stop_trace();

//  __demo_detect();

#ifdef STAT_ON
  if (stat != nullptr) {
    if (this->do_print_stat_ && this->is_on) {
      output_stat();
    }
    __tsan::internal_free(stat);
  }
  stat = nullptr;
#endif
  stat = nullptr;

  if (!this->is_on)
    return;

  // first flush io queue
  if (use_io_q) {
    out_queue->stop();
    __tsan::internal_free(out_queue);
  }

  __finish_remaining();
    // then flush remaining buffer & close file
//    for (u32 i = 0; i < MAX_THREAD; ++i) {
//      tlbufs[i].finish();
//    }
//
//    __tsan::internal_free(tlbufs);
//    tlbufs = nullptr;


  // save module info if new modules have been loaded
//  save_module_info();
}


void UFOContext::output_stat() {
//  char path[DIR_MAX_LEN];
//
//  internal_strncpy(path, this->trace_dir, 200);
//  internal_strncat(path, NAME_STAT_FILE, 50);
//  FILE *f_pp = fopen(path, "w+");
//  summary_stat(f_pp, this->stat, MAX_THREAD, cur_pid_, p_pid_);
  summary_stat(nullptr, this->stat, MAX_THREAD, cur_pid_, p_pid_);
//  fclose(f_pp);
//
//  internal_memset(path, '\0', DIR_MAX_LEN);
//
//  internal_strncpy(path, trace_dir, 200);
//  internal_strncat(path, NAME_STAT_CSV, 50);
//  FILE* f_csv = fopen(path, "w+");
//  print_csv(f_csv, this->stat, MAX_THREAD, cur_pid_, p_pid_);
//  fclose(f_csv);
}

/**
 * if is on, re-init:
 * update this pid and parent pid;
 * update trace dir str and create new folder for child process;
 * release all memory in the old out-queue, then start a new queue.
 * reset each buffer and stat.
 *
 *
 * what happens to resources?
 * for files:
 * simply reset all file descriptors, when flush is called, new file will be created.
 * for memory:
 * do nothing and keep the addresses in the pointers, memory will be re-mapped if accessed later.
 * reset all 'size' to zero.
 * for pthread stuff:
 * do nothing, discard the whole out-queue and create a new one.
 *
 */
void UFOContext::child_after_fork() {
  if (!is_on)
    return;

  this->cur_pid_ = (u32)__sanitizer::internal_getpid();
  this->p_pid_ = (u32)__sanitizer::internal_getppid();
  this->is_subproc = true;
  this->mudule_length_ = 0;

  read_config();
  time_started = get_time_ms();
  print_config();
//  open_trace_dir();
//  save_module_info();

  if (use_io_q) {
    this->out_queue->release_mem();
    __tsan::internal_free(out_queue);
    this->out_queue = (OutQueue *) __tsan::internal_alloc(__tsan::MBlockScopedBuf, sizeof(OutQueue));
    out_queue->start(this->out_queue_length);
  }

  for (u32 i = 0; i < MAX_THREAD; ++i) {
    this->tlbufs[i].reset();

#ifdef STAT_ON
    this->stat[i].init();
#endif
  }

  this->start_trace();
}

//void UFOContext::mem_acquired(u32 n_bytes) {
//  this->lock.Lock();
//  this->total_mem_ += n_bytes;
//  if (total_mem_ >= this->mem_t2_) {
//    mem_t2_ = mem_t2_ * 4 / 3;
//
//    u32 buf_sz = get_buf_size();
//    buf_sz /= 4;
//    if (buf_sz <= MIN_BUF_SZ)
//      buf_sz = MIN_BUF_SZ;
//    atomic_store_relaxed(&this->tl_buf_size_, buf_sz);
//    Printf(">>>>>>>>>> tl buf  / 4\r\n");
//
//    ratio_mem_x4 += 1;
//  } else if (total_mem_ > this->mem_t1_) {
//    mem_t1_ = mem_t1_ * 3 / 2;
//
//    u32 buf_sz = get_buf_size();
//    buf_sz /= 2;
//    if (buf_sz <= MIN_BUF_SZ)
//      buf_sz = MIN_BUF_SZ;
//    atomic_store_relaxed(&this->tl_buf_size_, buf_sz);
//    Printf(">>>>>>>>>> tl buf  / 2\r\n");
//
//    ratio_mem_x2 += 1;
//  }
//  lock.Unlock();
//}

//void UFOContext::mem_released(u32 n_bytes) {
//  lock.Lock();
//  this->total_mem_ += n_bytes;
//
//  if (ratio_mem_x4 > 1 && total_mem_ < mem_t2_) {
//    ratio_mem_x4 -= 1;
//
//    u32 buf_sz = get_buf_size();
//    buf_sz *= 4;
//    atomic_store_relaxed(&this->tl_buf_size_, buf_sz);
//    Printf(">>>>>>>>>> tl buf X4\r\n");
//
//  } else if (ratio_mem_x2 > 1 && total_mem_ < mem_t1_) {
//
//    ratio_mem_x2 -= 1;
//
//    u32 buf_sz = get_buf_size();
//    buf_sz *= 2;
//    atomic_store_relaxed(&this->tl_buf_size_, buf_sz);
//    Printf(">>>>>>>>>> tl buf X2\r\n");
//  }
//  lock.Unlock();
//}

}
}

