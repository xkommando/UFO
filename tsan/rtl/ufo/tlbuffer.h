//
// Created by xkommando on 10/9/16.
//
//
// buffer for each thread
// each buffer is init for all $MAX_THREAD threads, but only allocated when thread starts.
//(Bowen 2017-10-13)
//

#ifndef UFO_TLBUFFER_H
#define UFO_TLBUFFER_H



#include "../tsan_defs.h"
#include "defs.h"

#include "__test_ana.h"

namespace bw {
namespace ufo {

struct TLBuffer;
extern  TLBuffer *G_BUF_BASE;


// thread safe, COMPRESS_ON
//void write_file(int fd, Byte* data, u64 len);

struct TLBuffer {

  bool stopped;

  Byte *buf_;
  u32 size_;
  u32 capacity_;
  u64 last_fe; // last e_count_ value at function entry, used to eliminate empty calls

  // useless, info saved to thrbegin
  u32 tls_height;
  u64 tls_bottom;// lower address
  u32 stack_height;
  u64 stack_bottom;// lower address

  // number of event saved in this buffer
  u64 e_counter_;

  // deallocate pointers
  void** memptr_buf;
  u32 dealloc_ptr_count;
  u32 memptr_cap;
  u64 tl_mem_threshold;
  u64 tl_alloc_size;

  __tsan::ThreadState* thr;

  u64* sstack_buf;
  u32 sstack_cap;
  u32 sstack_size;

  void init();

  void open_buf();

  void flush();

  void finish();

  void reset();

  bool mem_exceeded() const;
  void release_all();

#pragma GCC diagnostic ignored "-Wcast-qual"
  template<typename E, u32 SZ = sizeof(E)>
  __HOT_CODE
  ALWAYS_INLINE
  void put_event(const E &event) {
    if (UNLIKELY(buf_ == nullptr)) {
      open_buf();
//      this->p_alloc_cache = & __tsan::cur_thread()->proc()->alloc_cache;
      this->thr = __tsan::cur_thread();
    } else if (UNLIKELY(size_ + SZ >= capacity_)) {
//      flush();
      __do_ana();
      return ;
    }
    Byte *pdata = (Byte *) (&event);
    Byte *pbuf = buf_ + size_;

    // write index (8 byte)
    *pbuf = *pdata;
    u32 offset = 1;

    while (offset < SZ) {
      *((u16 *) (pbuf + offset)) = *((u16 *) (pdata + offset));
      offset += 2;
    }
    size_ += offset;

    this->e_counter_++;
  }
#pragma GCC diagnostic warning "-Wcast-qual"

  void put_free_ptr(void* ptr);
};


} // ns ufo_bench
} // ns bw

#endif //UFO_TLBUFFER_H
