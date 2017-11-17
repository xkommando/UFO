//
// Created by xkommando on 10/9/16.
//


#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <cerrno>
//#include "../../../sanitizer_common/sanitizer_allocator_interface.h"
#include "../../../sanitizer_common/sanitizer_common.h"
#include "../../../sanitizer_common/sanitizer_posix.h"
//#include "../../../sanitizer_common/sanitizer_allocator_interface.h"
//#include "../../../sanitizer_common/sanitizer_allocator.h"

//#include "../tsan_rtl.h"

#include "defs.h"
#include "ufo.h"
#include "tlbuffer.h"

#include "snappy/snappy.h"

namespace bw {
namespace ufo {

//using namespace __tsan;
using __sanitizer::internal_write;
using __tsan::internal_alloc;
using __tsan::internal_free;
using __sanitizer::Die;


// defined in ufo_interface.cc
extern UFOContext *uctx;

TLBuffer *G_BUF_BASE;

// just init buf, no thread is associated.
void TLBuffer::init() {
  buf_ = nullptr;
  size_ = 0;
  capacity_ = 0,
  e_counter_ = 0;

  tls_height = -1;
  tls_bottom = -1;// lower address
  stack_height = -1;
  stack_bottom = -1;// lower address

  memptr_buf = nullptr;
  dealloc_ptr_count = 0;
//  p_alloc_cache = nullptr;
  thr = nullptr;
  tl_alloc_size = 0;
  memptr_cap = DEFAULT_PTR_BUF_SIZE;
  tl_mem_threshold = (u64)(DEFAULT_HOLD_MEM_SIZE / DEFAULT_ONLINE_THR_NUM);

  sstack_buf = nullptr;
  sstack_cap = 0;
  sstack_size = 0;
}

bool TLBuffer::
mem_exceeded() const {
  DPrintf(">>> mem_exceeded? tl_alloc_size %llu  tl_mem_threshold %llu   uctx->mem_hold %llu %d\r\n",
         tl_alloc_size,
         tl_mem_threshold,
         uctx->mem_hold, uctx->mem_hold > DEFAULT_HOLD_MEM_SIZE);

  if (tl_alloc_size < tl_mem_threshold)
    return false; // fast return
  return uctx->mem_hold > DEFAULT_HOLD_MEM_SIZE;
}

void TLBuffer::release_all() {
  if (memptr_buf == nullptr || dealloc_ptr_count <= 0)
    return;
  u32 ct = __sync_add_and_fetch(&uctx->batchr_count, 1);
  if (thr == nullptr) {
    thr = __tsan::cur_thread();
  }

  DPrintf("UFO>>>#%d batch_dealloc proc(%d %d)#%d  size:%d  num:%d  total %d\r\n",
         thr->tid,
         uctx->cur_pid_, (uctx->is_subproc),
         this - G_BUF_BASE, tl_alloc_size, dealloc_ptr_count, ct);

  __tsan::ufo_batch_dealloc(memptr_buf, dealloc_ptr_count, &this->thr->proc()->alloc_cache);
  dealloc_ptr_count = 0;
  tl_alloc_size = 0;
  __sync_sub_and_fetch(&uctx->mem_hold, tl_alloc_size);
}

// thread created
void TLBuffer::open_buf() {

  u32 sz = uctx->get_buf_size();
  buf_ = (Byte *) internal_alloc(__tsan::MBlockScopedBuf, sz);
  capacity_ = sz;
  size_ = 0;

  memptr_buf = (void**) internal_alloc(__tsan::MBlockScopedBuf, memptr_cap);
  dealloc_ptr_count = 0;
  tl_mem_threshold = (u64)(DEFAULT_HOLD_MEM_SIZE / DEFAULT_ONLINE_THR_NUM);
}

void TLBuffer::put_free_ptr(void *ptr) {
  if (dealloc_ptr_count >= memptr_cap) {
    memptr_cap *= 2;
    auto n_buf = (void **) internal_alloc(__tsan::MBlockScopedBuf, memptr_cap);
    __sanitizer::internal_memcpy(n_buf, memptr_buf, dealloc_ptr_count * sizeof(void *));
    internal_free(memptr_buf);
    memptr_buf = n_buf;
  }
  memptr_buf[dealloc_ptr_count] = ptr;
  dealloc_ptr_count++;
}
void TLBuffer::flush() {
//  if (UNLIKELY( ! is_file_open())) {
//    int tid = this - G_BUF_BASE;
//    open_file(tid);
//  }
//
//  if (uctx->use_io_q) {
//    uctx->out_queue->push(this);
//  } else {
//    write_file(trace_fd_, buf_, size_);
//  }

  size_ = 0;
  e_counter_ = 0;
  last_fe = 0;
}

void TLBuffer::finish() {

  if (memptr_buf != nullptr) {
    release_all();
    internal_free(memptr_buf);
  }
  memptr_buf = nullptr;
  dealloc_ptr_count = 0;
//  p_alloc_cache = nullptr;
  thr = nullptr;
  tl_alloc_size = 0;
  tl_mem_threshold = (u64)(DEFAULT_HOLD_MEM_SIZE / DEFAULT_ONLINE_THR_NUM);

  if (buf_ != nullptr) {
    internal_free(buf_);
    buf_ = nullptr;
  }
  size_ = 0;
  capacity_ = 0;

  tls_height = -1;
  tls_bottom = -1;// lower address
  stack_height = -1;
  stack_bottom = -1;// lower address
}

// called usually after fork in child proc
void TLBuffer::reset() {
  size_ = 0;
  e_counter_ = 0;

  tls_height = -1;
  tls_bottom = -1;// lower address
  stack_height = -1;
  stack_bottom = -1;// lower address

  dealloc_ptr_count = 0;
//  p_alloc_cache = nullptr;
  thr = nullptr;
  tl_alloc_size = 0;
  tl_mem_threshold = (u64)(DEFAULT_HOLD_MEM_SIZE / DEFAULT_ONLINE_THR_NUM);
}




} // ns ufo_bench
} // ns bw

