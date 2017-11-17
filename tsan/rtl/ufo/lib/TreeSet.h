///////////////////////////////////////////////////////////////////////////////
////     Copyright 2013 CaiBowen
////     All rights reserved.
////
////     Author: Cai Bowen
////       contact/bug report/get new version
////           at
////       feedback2bowen@outlook.com
////
////
////     Licensed under the Apache License, Version 2.0 (the "License");
////     you may not use this file except in compliance with the License.
////     You may obtain a copy of the License at
////
////              http://www.apache.org/licenses/LICENSE-2.0
////
////     Unless required by applicable law or agreed to in writing, software
////     distributed under the License is distributed on an "AS IS" BASIS,
////     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
////     See the License for the specific language governing permissions and
////     limitations under the License.
///////////////////////////////////////////////////////////////////////////////


#ifndef STARTER_TREESEY_H_
#define STARTER_TREESEY_H_

//#include "StarterCfgDef.h"

//* Decorator for GenSet (Bowen 2017-10-13)

#include "SetBase.h"
#include "RBTree.h"

namespace Starter {

namespace detail_ {
// to use Set , just custom a node
template<typename K>
struct SetNode : public BaseNode {
  typedef K KeyType;
  typedef K ValueType;

  KeyType key;
  KeyType &value;

  SetNode(const SetNode<K> &n)
      : key(n.key),
        value(key) { }

  SetNode(SetNode<K> &&n)
      : key(std::move(n.key)),
        value(key) { }

  SetNode(const K &k)
      : key(k),
        value(key) { }

  SetNode(K &&rk)
      : key(rk),
        value(key) { }

  KeyType &getKey() const noexcept { return key; }
};

}// namespace detail_


template<typename K, typename C = std::less <K>>
using TreeSet = detail_::GenSet<K,
    C,
    RBTree < detail_::SetNode<const K>, C>
>;


template<typename K, typename C = std::less <K>>
using MultiTreeSet = detail_::GenMultiSet<K,
    C,
    RBTree < detail_::SetNode<const K>, C>
>;

}
#endif // STARTER_TREESEY_H_
