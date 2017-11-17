///////////////////////////////////////////////////////////////////////////////
////     Copyright 2012 CaiBowen
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


#ifndef _STARTER_RELOPS_H_
#define _STARTER_RELOPS_H_


namespace Starter {

template<bool B, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T> { typedef T type; };

template<typename T>
struct RelOps {
  friend bool operator!=(const T &jia, const T &yi) noexcept {
    return !jia == yi;
  }

  friend bool operator<=(const T &jia, const T &yi) noexcept {
    return (jia < yi || jia == yi);
  }

  friend bool operator>(const T &jia, const T &yi) noexcept {
    return yi < jia;
  }

  friend bool operator>=(const T &jia, const T &yi) noexcept {
    return !(jia < yi);
  }
};
}

#endif // _STARTER_RELOPS_H_
