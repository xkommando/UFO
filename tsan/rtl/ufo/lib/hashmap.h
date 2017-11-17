//
// Created by cbw on 7/14/16.
//
// WARN: not thread safe, cannot handle big map (Bowen 2017-10-13)
//
//

#ifndef UFO_HASHMAP_H
#define UFO_HASHMAP_H

#include "../ufo.h"
#include "../../tsan_mman.h"

namespace bw {
namespace ufo {


template<typename K, typename V>
struct HashNode {
  K key_;
  V value_;
  HashNode<K, V> *next_;

  HashNode(const K &key, const V &value) :
      key_(key), value_(value), next_(nullptr) {
  }
};



template<typename K, typename V, typename F/*, unsigned int TABLE_SIZE=64*/>
class HashMap {

public:
//private:
  HashNode<K, V> **table_;
  F hashFunc; // from key to unsigned 32-bit int
  u32 size_;
  u32 cap_;

public:
  typedef HashNode<K,V> Node;
  typedef Node  entry_type;
  typedef K     key_type;
  typedef V     value_type;
  typedef void* (*FuncVisitor)(Node*);


  HashMap(u32 cap)
    : size_(0),
      cap_(cap) {
    u32 sz = cap_ * sizeof(Node *);
    table_ = (HashNode<K, V> **)internal_alloc(__tsan::MBlockScopedBuf, sz);
    __sanitizer::internal_memset(table_, 0, sz);
  }

  ~HashMap() {
    clear();
    // destroy the hash table_
//    delete[] table_;
    __tsan::internal_free(table_);
  }
  void clear() {
    for (u32 i = 0; i < cap_; ++i) {
      Node *entry = table_[i];
      while (entry != nullptr) {
        Node *prev = entry;
        entry = entry->next_;
        prev->~Node();
        __tsan::internal_free(prev);
      }
    }
  }

  void Accept(FuncVisitor visit) {
    for (u32 i = 0; i < cap_; ++i) {
      Node* entry = table_[i];
      while (entry != nullptr) {
        visit(entry);
        entry = entry->next_;
      }
    }
  }

  void PrintMe() const {
    for (u32 i = 0; i < cap_; ++i) {
      Node* entry = table_[i];
      while (entry != nullptr) {
        Printf("[%d] => [%d]\r\n", (u32)entry->key_, *reinterpret_cast<u32*>(&entry->value_));
        entry = entry->next_;
      }
    }
  }

  V* get(const K &key) {
    u32 idx = hashFunc(key) % cap_;
    HashNode<K, V> *entry = table_[idx];
    while (entry != nullptr) {
      if (entry->key_ == key) {
        return &entry->value_;
      }
      entry = entry->next_;
    }
    return nullptr;
  }

  // return true add new value, false reset existing value
  bool put(const K &key, const V &value) {
    const u32 idx = hashFunc(key) % cap_;
    HashNode<K, V> *prev = nullptr;
    HashNode<K, V> *entry = table_[idx];
    while (entry != nullptr && entry->key_ != key) {
      prev = entry;
      entry = entry->next_;
    }
    if (entry == nullptr) {
//      entry = new HashNode<K, V>(key, value);
      entry = (HashNode<K, V> *)internal_alloc(__tsan::MBlockScopedBuf, sizeof(Node));
      entry->next_ = nullptr;
      entry->key_ = key;
      entry->value_ = value;
      if (prev == nullptr) {
        table_[idx] = entry;
      } else {
        prev->next_ = entry;
      }
      size_++;
      return true;
    } else {
      entry->value_ = value;
      return false;
    }
  }

  u32 size() const {
    return size_;
  }

  bool remove(const K &key) {
    u32 idx = hashFunc(key) % cap_;
    HashNode<K, V> *prev = nullptr;
    HashNode<K, V> *entry = table_[idx];

    while (entry != nullptr && entry->key_ != key) {
      prev = entry;
      entry = entry->next_;
    }

    if (entry == nullptr) {
      return false;
    } else {
      if (prev == nullptr) {
        table_[idx] = entry->next_;
      } else {
        prev->next_ = entry->next_;
      }
//      delete entry;
      entry->~Node();
      __tsan::internal_free(entry);
      size_--;
      return true;
    }
  }

};



}
}

#endif //HASHMAP_H
