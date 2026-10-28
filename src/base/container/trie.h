// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Trie tree library

#ifndef MOZC_BASE_CONTAINER_TRIE_H_
#define MOZC_BASE_CONTAINER_TRIE_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc {

template <typename T>
class Trie final {
 public:
  // This needs to be public for GCC 9 with '-std=c++17'.
  using SubTrie = absl::flat_hash_map<char32_t, std::unique_ptr<Trie<T>>>;

  // Destroys children iteratively so a long chain (e.g. a table entry with
  // a very long input) does not exhaust the stack through the recursive
  // destruction of unique_ptr.
  ~Trie() {
    absl::InlinedVector<std::unique_ptr<Trie<T>>, 32> work;
    for (auto &[unused, trie] : trie_) {
      if (trie) {
        work.push_back(std::move(trie));
        (void)unused;
      }
    }
    trie_.clear();
    while (!work.empty()) {
      std::unique_ptr<Trie<T>> current = std::move(work.back());
      work.pop_back();
      for (auto &[unused, trie] : current->trie_) {
        work.push_back(std::move(trie));
        (void)unused;
      }
      current->trie_.clear();
      // current's destructor runs here against an emptied map, so the
      // unique_ptr chain cascade is bounded to a single level.
    }
  }


  template <typename U>
  void AddEntry(absl::string_view key, U &&data) {
    Trie<T> *node = this;
    while (!key.empty()) {
      FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        auto t = std::make_unique<Trie>();
        res.trie = t.get();
        node->trie_[res.first_char] = std::move(t);
      }
      node = res.trie;
      key = res.rest;
    }
    node->data_ = std::forward<U>(data);
  }

  bool DeleteEntry(absl::string_view key) {
    if (key.empty()) {
      if (trie_.empty()) {
        return true;
      }
      data_.reset();
      return false;
    }
    Trie<T> *node = this;
    std::vector<std::pair<Trie<T> *, char32_t>> trail;
    while (!key.empty()) {
      FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        return false;
      }
      trail.emplace_back(node, res.first_char);
      node = res.trie;
      key = res.rest;
    }
    bool erase_child = node->trie_.empty();
    if (!erase_child) {
      node->data_.reset();
    }
    for (auto it = trail.rbegin(); it != trail.rend(); ++it) {
      Trie<T> *parent = it->first;
      if (erase_child) {
        parent->trie_.erase(it->second);
      }
      erase_child = parent->trie_.empty();
      if (!erase_child) {
        break;
      }
    }
    return erase_child;
  }

  bool LookUp(absl::string_view key, T *data) const {
    const Trie<T> *node = this;
    while (!key.empty()) {
      const FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        return false;
      }
      node = res.trie;
      key = res.rest;
    }
    if (!node->data_.has_value()) {
      return false;
    }
    *data = *node->data_;
    return true;
  }

  bool LookUpPrefix(absl::string_view key, T *data, size_t *key_length,
                    bool *fixed) const {
    const Trie<T> *node = this;
    size_t total = 0;
    while (!key.empty()) {
      const FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        *key_length = total;
        if (node->data_.has_value()) {
          *data = *node->data_;
          *fixed = node->trie_.empty();
          return true;
        }
        *fixed = true;
        return false;
      }
      total += key.size() - res.rest.size();
      node = res.trie;
      key = res.rest;
    }
    *key_length = total;
    if (node->data_.has_value()) {
      *data = *node->data_;
      *fixed = node->trie_.empty();
      return true;
    }
    *fixed = true;
    return false;
  }

  bool LongestMatch(absl::string_view key, T *data, size_t *key_length) const {
    const Trie<T> *node = this;
    size_t total = 0;
    size_t matched_len = 0;
    bool found = false;
    while (true) {
      if (node->data_.has_value()) {
        *data = *node->data_;
        matched_len = total;
        found = true;
      }
      if (key.empty()) {
        break;
      }
      const FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        break;
      }
      total += key.size() - res.rest.size();
      node = res.trie;
      key = res.rest;
    }
    *key_length = matched_len;
    return found;
  }

  // If key's prefix matches to trie with data, return true and set data
  // For example, if a trie has data for 'abc', 'abd', and 'a';
  //  - Return 'abc', 'abd', 'a', for the key 'a'
  //  - Return 'abc', 'abd' for the key 'ab'
  //  - Return nothing for the key 'b'
  void LookUpPredictiveAll(absl::string_view key,
                           std::vector<T> *data_list) const {
    DCHECK(data_list);
    const Trie<T> *node = this;
    while (!key.empty()) {
      const FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        return;
      }
      node = res.trie;
      key = res.rest;
    }
    // Enumerate the subtree depth-first in the same pre-order as the
    // previous recursive implementation, without recursion on the key
    // length.
    absl::InlinedVector<const Trie<T> *, 32> stack;
    stack.push_back(node);
    while (!stack.empty()) {
      const Trie<T> *current = stack.back();
      stack.pop_back();
      if (current->data_.has_value()) {
        data_list->push_back(*current->data_);
      }
      absl::InlinedVector<const Trie<T> *, 16> children;
      for (const auto &[unused, trie] : current->trie_) {
        children.push_back(&*trie);
      }
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.push_back(*it);
      }
    }
  }

  bool HasSubTrie(absl::string_view key) const {
    const Trie<T> *node = this;
    while (!key.empty()) {
      const FindResult res = node->FindSubTrie(key);
      if (res.trie == nullptr) {
        return false;
      }
      node = res.trie;
      key = res.rest;
    }
    return true;
  }

  void swap(Trie &other) noexcept(std::is_nothrow_swappable_v<SubTrie> &&
                                  std::is_nothrow_swappable_v<T>) {
    using std::swap;
    swap(trie_, other.trie_);
    swap(data_, other.data_);
  }

  friend void swap(Trie &lhs, Trie &rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

 private:
  struct FindResult {
    Trie<T> *trie = nullptr;
    char32_t first_char = 0;
    absl::string_view rest;
  };

  // Finds the subtrie that is reachable by the first UTF8 character of `key`.
  // If `key` is empty or no such trie exists, `FindResult::trie` is set to
  // nullptr. If `key` is nonempty, this method also sets the value of char32
  // for the first character and the rest of strings regardless of the find
  // result.
  FindResult FindSubTrie(absl::string_view key) const {
    FindResult res;
    if (!key.empty()) {
      Util::SplitFirstChar32(key, &res.first_char, &res.rest);
      if (const auto it = trie_.find(res.first_char); it != trie_.end()) {
        res.trie = it->second.get();
      }
    }
    return res;
  }

  SubTrie trie_;
  std::optional<T> data_;
};

}  // namespace mozc

#endif  // MOZC_BASE_CONTAINER_TRIE_H_
