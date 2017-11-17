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


#ifndef STARTER_TREEMAP_H_
#define STARTER_TREEMAP_H_

/**
 * Decorator for GenMap (Bowen 2017-10-13)
 */

//#include "StarterCfgDef.h"

#include "SetBase.h"
#include "RBTree.h"

namespace Starter {

namespace detail_ {
template<typename K, typename V>
struct MapNode : public BaseNode {
  typedef K KeyType;
  typedef V ValueType;

  typedef MapNode<K, V> Self_;

  KeyType key;
  ValueType value;

  template<typename rcKey, typename rcVal>
  MapNode(rcKey &&ok, rcVal &&ov)
      : key(std::forward<rcKey>(ok)),
        value(std::forward<rcVal>(ov)) { }

  MapNode(const Self_ &n)
      : key(n.key),
        value(n.value) { }

  MapNode(Self_ &&n)
      : key(std::move(n.key)),
        value(std::move(n.value)) { }

  KeyType &getKey() const noexcept { return key; }
};
}


template<typename K, typename V, typename C = std::less <K>>
using TreeMap = detail_::GenMap<K, V, C,
    RBTree < detail_::MapNode<const K, V>, C>
>;


template<typename K, typename V, typename C = std::less <K>>
using MultiTreeMap = detail_::GenMultiMap<K, V, C,
    RBTree < detail_::MapNode<const K, V>, C>
>;

}
#endif // STARTER_TREEMAP_H_
