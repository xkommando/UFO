//
// Created by cbw on 9/26/17.
//

#ifndef UFO_TEST_DETECT_H
#define UFO_TEST_DETECT_H


//#include <unordered_map>
#include "lib/hashmap.h"
#include "lib/hash.h"
#include "report.h"

namespace bw {
namespace ufo {

/**
 * matching heap and malloc
 * (Bowen 2017-10-13)
 */

#pragma pack(push, 1)
PACKED_STRUCT(EventIdx) {
  u16 tid;
  //10
  Byte typ;
  //5
  Byte len;
  u32 idx;//27
};
static_assert(sizeof(EventIdx) == 8, "compact struct (align 8) not supported, please use clang 3.8.1");
#pragma pack(pop)


//#define DPrintf Printf

static const u64 MASK_48 = 0x0000ffffffffffff;

static bool find_events(TLBuffer &buf,
                        HashMap<u64, EventIdx, TWang_32from64> &alloc_map,
                        __tsan::Vector<EventIdx> &acc_vec) {

  Byte *ptr = buf.buf_;
  u32 size = buf.size_;

  Byte *pend = ptr + size;

  DPrintf("Parse>>> %d:\r\n", buf.thr->tid);

  while (ptr <= pend) {
    const Byte type_idx = *ptr;
//    Printf(">>>type1 %d  ", type_idx);
    switch (type_idx) {
      case EventType::ThreadBegin: {
        ThreadBeginEvent* pe = reinterpret_cast<ThreadBeginEvent*>(ptr);
        DPrintf("Parse>>>%p %s  parent %d\r\n",
                ptr, "ThreadBeginEvent", pe->tid_parent);
      }
        ptr += sizeof(ThreadBeginEvent);
        break;
      case EventType::ThreadEnd:
        DPrintf("Parse>>>%p %s\r\n", ptr, "ThreadEndEvent");
        ptr += sizeof(ThreadEndEvent);
        break;
      case EventType::ThreadCreate: {
        CreateThreadEvent* pe = reinterpret_cast<CreateThreadEvent*>(ptr);
        DPrintf("Parse>>>%p %s  kid %d\r\n",
                ptr, "CreateThreadEvent", pe->tid_kid);
      }
        ptr += sizeof(CreateThreadEvent);
        break;
      case EventType::ThreadJoin: {
        JoinThreadEvent* pe = reinterpret_cast<JoinThreadEvent*>(ptr);
        DPrintf("Parse>>>%p %s tid_joiner %d\r\n",
                ptr, "JoinThreadEvent", pe->tid_joiner);
      }
        ptr += sizeof(JoinThreadEvent);
        break;
      case EventType::ThreadAcqLock:
        DPrintf("Parse>>>%p %s\r\n", ptr, "LockEvent");
        ptr += sizeof(LockEvent);
        break;
      case EventType::ThreadRelLock:
        DPrintf("Parse>>>%p %s\r\n", ptr, "UnlockEvent");
        ptr += sizeof(UnlockEvent);
        break;
      case EventType::MemAlloc: {
        AllocEvent *pea = (AllocEvent *) ptr;
//        bool addnew = alloc_map.put(pea->addr, pea->size);
        EventIdx ei;
        ei.tid = buf.thr->tid;
        ei.typ = EventType::MemAlloc;
        ei.idx = (u32) (ptr - buf.buf_);
        bool addnew = alloc_map.put(pea->addr, ei);
        if (!addnew) {
          Printf(">>> Redundant alloc addr at %p, size: %d\r\n", pea->addr, pea->size);
        }
        DPrintf("Parse>>>%p %s addr %p size %d\r\n",
                ptr, "AllocEvent", pea->addr, pea->size);
      }
        ptr += sizeof(AllocEvent);
        break;
      case EventType::MemDealloc: {
        DeallocEvent* pe = reinterpret_cast<DeallocEvent*>(ptr);
//        DeallocEvent *ped = (DeallocEvent *) ptr;
//        u32 *plen = alloc_map.get(ped->addr);
//        if (plen != nullptr) {
//          u32 len = *plen;
//        }
        DPrintf("Parse>>>%p %s addr %p\r\n",
                ptr, "DeallocEvent", pe->addr);
      }
        ptr += sizeof(DeallocEvent);
        break;

      case EventType::MemRangeRead: {
        MemRangeAccEvent* pe = reinterpret_cast<MemRangeAccEvent*>(ptr);
        DPrintf("Parse>>>%p %s  addr %p  len %d\r\n",
                ptr, "MemRangeRead", pe->addr, pe->size);
      }
        ptr += sizeof(MemRangeAccEvent);
        break;
      case EventType::MemRangeWrite: {
        MemRangeAccEvent* pe = reinterpret_cast<MemRangeAccEvent*>(ptr);
        DPrintf("Parse>>>%p %s  addr %p  len %d\r\n",
                ptr, "MemRangeWrite", pe->addr, pe->size);
      }
        ptr += sizeof(MemRangeAccEvent);
        break;

      case EventType::PtrAssignment:
        DPrintf("Parse>>>%p %s\r\n", ptr, "PtrAssignEvent");
        ptr += sizeof(PtrAssignEvent);
        break;
      case EventType::TLHeader:
        DPrintf("Parse>>>%p %s\r\n", ptr, "UFOHeader");
        ptr += sizeof(UFOHeader);
        break;
      case EventType::InfoPacket:
        DPrintf("Parse>>>%p %s\r\n", ptr, "UFOPkt");
        ptr += sizeof(UFOPkt);
        break;
      case EventType::EnterFunc:
        DPrintf("Parse>>>%p %s\r\n", ptr, "FuncEntryEvent");
        ptr += sizeof(FuncEntryEvent);
        break;
      case EventType::ExitFunc:
        DPrintf("Parse>>>%p %s\r\n", ptr, "FuncExitEvent");
        ptr += sizeof(FuncExitEvent);
        break;
      case EventType::ThrCondWait:
        DPrintf("Parse>>>%p %s\r\n", ptr, "ThrCondWaitEvent");
        ptr += sizeof(ThrCondWaitEvent);
        break;
      case EventType::ThrCondSignal:
        DPrintf("Parse>>>%p %s\r\n", ptr, "ThrCondSignalEvent");
        ptr += sizeof(ThrCondSignalEvent);
        break;
      case EventType::ThrCondBC:
        DPrintf("Parse>>>%p %s\r\n", ptr, "ThrCondBCEvent");
        ptr += sizeof(ThrCondBCEvent);
        break;
      case EventType::PtrDeRef:
        DPrintf("Parse>>>%p %s\r\n", ptr, "PtrDeRefEvent");
        ptr += sizeof(PtrDeRefEvent);
        break;

      default: {
        MemAccEvent *pacc = (MemAccEvent *) ptr;
        u64 acc_addr = pacc->addr;
        const Byte type2 = type_idx & 0x3f;
//        Printf("\r\ntype2  %d  ", type2);
        if (type2 == 8 || type2 == 9) {
          bool is_r = type2 == 8;
          EventType etp = is_r ? EventType::MemRead : EventType::MemWrite;
          const Byte tmp = type_idx >> 6;
//          {
//            int __v = tmp;
//            DPrintf("\r\n>>>%d", __v);
//          }
          const u32 val_len = 1 << tmp;
//          MemAccEvent *pac = (MemAccEvent *) ptr;
//          acc_vec.PushBack(*pac);
          if ((acc_addr < buf.stack_bottom || buf.stack_bottom + buf.stack_height < acc_addr)
              && (acc_addr < buf.tls_bottom || buf.tls_bottom + buf.tls_height < acc_addr)) {

            EventIdx ei{buf.thr->tid, etp, 0, ptr - buf.buf_};
            acc_vec.PushBack(ei);
          } else {
            DPrintf("\r\n>>> non heap addr: %p\r\n", acc_addr);
          }
          u64 val = 0;
          __sanitizer::internal_memcpy(&val, ptr + sizeof(MemAccEvent), val_len);
          DPrintf("Parse>>>%p %s  addr %p  len:%d  val %lld\r\n",
                  ptr, "MemAccEvent", pacc->addr, val_len, val);

          ptr += sizeof(MemAccEvent) + val_len;
        } else {
          Printf("\r\n>>> Parsing Error #%d e_count %d  loc %d   type_idx %p\r\n",
                 buf.thr->tid, buf.e_counter_, buf.size_, type_idx);
//          ptr = pend;// end loop
          return false;
        }
      }
    }// switch
  } // while move ptr
  return true;
}


void *__demo_detect(void *_buf) {
  Printf(">>>__demo_detect\r\n");

  TLBuffer *tlbufs = static_cast<TLBuffer *>(_buf);

  HashMap<u64, EventIdx, TWang_32from64> alloc_map(300);
  __tsan::Vector<EventIdx> acc_vec(500);
//  std::unordered_map<u64, u64> alloc_map;
  for (u32 i = 0; i < MAX_THREAD; ++i) {
//    tlbufs[i].finish();
    if (tlbufs[i].buf_ == nullptr)
      continue;
//    Printf("\r\nsearching %d\r\n", i);
    find_events(tlbufs[i], alloc_map, acc_vec);
  }
  Printf("\r\n>>> Vec size %d\r\n", acc_vec.Size());
  Printf(">>> MAP size %d\r\n", alloc_map.size());
//  alloc_map.PrintMe();
//  {
//    for (u32 i = 0; i < alloc_map.cap_; ++i) {
//      auto entry = alloc_map.table_[i];
//      while (entry != nullptr) {
////        Printf("[%p] => [%d]\r\n", entry->key_, *reinterpret_cast<u32 *>(&entry->value_));
//        Printf("[%p]\r\n", entry->key_);// *reinterpret_cast<u32 *>(&entry->value_));
//        entry = entry->next_;
//      }
//    }
//  }
  for (u32 j = 0; j < acc_vec.Size(); ++j) {
    EventIdx eia = acc_vec[j];
    MemAccEvent *pae = (MemAccEvent *) (tlbufs[eia.tid].buf_ + eia.idx);
//    Printf("%d\r\n", pae.addr);
    bool found = false;
    for (u32 i = 0; i < alloc_map.cap_; ++i) {
      auto entry = alloc_map.table_[i];
      while (entry != nullptr) {
//        Printf("[%d] => [%d]\r\n", (u32)entry->key_, *reinterpret_cast<u32*>(&entry->value_));
        EventIdx ei = entry->value_;
        AllocEvent *palloc = (AllocEvent *) (tlbufs[ei.tid].buf_ + ei.idx);
        u64 addr = palloc->addr;
        u32 len = palloc->size;

        if (addr <= pae->addr && pae->addr < addr + len) {
          Printf("Match %p to %p[%d]\r\n   Memory Access:\r\n", pae->addr, addr, len);
          print_callstack(tlbufs[eia.tid].thr, pae->pc);
          Printf("   Alloc:\r\n");
          print_callstack(tlbufs[ei.tid].thr, palloc->pc);
          found = true;
          goto OUT;
        }
        entry = entry->next_;
      }
    }
    OUT:
    if (!found) {
      Printf("Alloc Not found for addr: %p\r\n", pae->addr);
    }
  }

  // WARN: this function is called from a TLBuffer, i.e., this object destroyed itself.
  for (u32 i = 0; i < MAX_THREAD; ++i) {
    if (tlbufs[i].buf_ != nullptr) {
      tlbufs[i].finish();
//      tlbufs[i].release_all();
    }
  }
  __tsan::internal_free(tlbufs);

  return nullptr;
}


}
}

#endif //PROJECT_TEST_DETECT_H
