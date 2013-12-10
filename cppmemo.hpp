/**
 * @file
 * @author  Giacomo Drago <giacomo@giacomodrago.com>
 * @version 0.9.5 beta
 *
 *
 * @section LICENSE
 *
 * Copyright (c) 2013, Giacomo Drago <giacomo@giacomodrago.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes C++Memo, a software developed by Giacomo Drago.
 *      Website: http://projects.giacomodrago.com/c++memo
 * 4. Neither the name of Giacomo Drago nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GIACOMO DRAGO "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL GIACOMO DRAGO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * @section DESCRIPTION
 *
 * This header file contains a generic framework for memoization (see
 * @link http://en.wikipedia.org/wiki/Memoization @endlink) supporting parallel
 * execution.
 *
 * A C++11-compliant compiler is required to compile this file.
 *
 * Please read the documentation on the project website:
 * @link http://projects.giacomodrago.com/c++memo @endlink
 *
 * This product includes Fcmm, a software developed by Giacomo Drago.
 * Website: @link http://projects.giacomodrago.com/fcmm @endlink
 *
 */

#ifndef CPPMEMO_H_
#define CPPMEMO_H_

#include <vector> // std::vector
#include <random> // std::minstd_rand
#include <thread> // std::thread
#include <functional> // std::function
#include <algorithm> // std::shuffle
#include <cstddef> // std::nullptr_t
#include <stdexcept> // std::logic_error, std::runtime_error

#ifdef CPPMEMO_DETECT_CIRCULAR_DEPENDENCIES
#include <unordered_set>
#endif

#include <fcmm/fcmm.hpp>

namespace cppmemo {

/**
 * This class implements a generic framework for memoization supporting
 * automatic parallel execution.
 * 
 * Please read the documentation on the project website:
 * @link http://projects.giacomodrago.com/c++memo @endlink
 */
template<
    typename Key,
    typename Value,
    typename KeyHash1 = std::hash<Key>,
    typename KeyHash2 = fcmm::SecondHashFunction<Key>,
    typename KeyEqual = std::equal_to<Key>
>
class CppMemo {
    
private:
    
    int defaultNumThreads;
    fcmm::Fcmm<Key, Value, KeyHash1, KeyHash2, KeyEqual> values;
    
    static void unspecifiedDeclarePrerequisites(
            const Key& /* unused */,
            std::function<void(const Key&)> /* unused */) {
    }
    
    class ThreadItemsStack {
        
    public:
        
        struct Item {
            Key key;
            bool ready;
        };
        
    private:
        
#ifdef CPPMEMO_DETECT_CIRCULAR_DEPENDENCIES
        std::unordered_set<Key, KeyHash1, KeyEqual> itemsSet;
#endif
        std::vector<Item> items;        
        int threadNo;
        std::minstd_rand randGen;
        std::size_t groupSize;
        
    public:
        
        ThreadItemsStack(int threadNo) : threadNo(threadNo), randGen(threadNo), groupSize(0) {
        }
        
        void push(const Key& key) {
#ifdef CPPMEMO_DETECT_CIRCULAR_DEPENDENCIES
            if (itemsSet.find(key) != itemsSet.end()) {
                throw std::runtime_error("Circular dependency detected.");
            }
#endif
            items.push_back({ key, false });
            groupSize++;
        }

        void pop() {
            if (groupSize != 0) {
                throw std::runtime_error("The current group is not finalized.");
            }
#ifdef CPPMEMO_DETECT_CIRCULAR_DEPENDENCIES
            const Item& item = items.back();
            itemsSet.erase(item.key);
#endif
            items.pop_back();
        }

        void finalizeGroup() {
            if (threadNo > 0 && groupSize > 1) {
                // shuffle the added prerequisites for improving parallel speedup
                std::shuffle(items.end() - groupSize, items.end(), randGen);
            }
#ifdef CPPMEMO_DETECT_CIRCULAR_DEPENDENCIES
            for (int i = 1; i <= groupSize; i++) {
                const Item& addedItem = *(items.end() - i);
                itemsSet.insert(addedItem.key);
            }
#endif
            groupSize = 0;
        }
        
        bool empty() const {
            return items.empty();
        }
        
        const Item& back() const {
            return items.back();
        }
        
        Item& back() {
            return items.back();
        }
        
        std::size_t getGroupSize() const {
            return groupSize;
        }

    };

    template<typename Compute, typename DeclarePrerequisites>
    void run(int threadNo, const Key& key, Compute compute, DeclarePrerequisites declarePrerequisites) {

        ThreadItemsStack stack(threadNo);

        stack.push(key);
        stack.finalizeGroup();

        while (!stack.empty()) {

            typename ThreadItemsStack::Item& item = stack.back();

            if (item.ready) {

                const auto prerequisites = [&](const Key& key) -> const Value& {
                    return values[key];
                };

                values.insert(item.key, [&](const Key& key) -> Value {
                    return compute(key, prerequisites);
                });

                stack.pop();

            } else {

                item.ready = true;

                const Key itemKey = item.key; // copy item key

                if (values.find(itemKey) == values.end()) {

                    if (declarePrerequisites != reinterpret_cast<DeclarePrerequisites>(unspecifiedDeclarePrerequisites)) {

                        // execute the declarePrerequisites function to get prerequisites

                        declarePrerequisites(itemKey, [&](const Key& prerequisite) {
                            if (values.find(prerequisite) == values.end()) {
                                stack.push(prerequisite);
                            }
                        });

                    } else {

                        // dry-run the compute function to capture prerequisites

                        const Value dummy = Value();
                        const Value itemValue = compute(itemKey, [&](const Key& prerequisite) -> const Value& {
                            const auto findIt = values.find(prerequisite);
                            if (findIt == values.end()) {
                                stack.push(prerequisite);
                                return dummy; // return an invalid value
                            } else {
                                return findIt->second; // return a valid value
                            }
                        });

                        if (stack.getGroupSize() == 0) { // the computed value is valid
                            values.emplace(itemKey, itemValue);
                            stack.pop();
                        }

                    }

                    stack.finalizeGroup();

                }

            }

        }

    }

public:

    CppMemo(int defaultNumThreads = 1, std::size_t estimatedNumEntries = 0) : values(estimatedNumEntries) {
        setDefaultNumThreads(defaultNumThreads);
    }

    int getDefaultNumThreads() const {
        return defaultNumThreads;
    }

    void setDefaultNumThreads(int defaultNumThreads) {
        if (defaultNumThreads < 1) {
            throw std::logic_error("The default number of threads must be >= 1");
        }
        this->defaultNumThreads = defaultNumThreads;
    }

    template<typename Compute, typename DeclarePrerequisites>
    const Value& getValue(const Key& key, Compute compute, DeclarePrerequisites declarePrerequisites, int numThreads) {

        const auto findIt = values.find(key);
        if (findIt != values.end()) {
            return findIt->second;
        }

        if (numThreads > 1) { // multi-thread execution

            std::vector<std::thread> threads;
            threads.reserve(numThreads);

            for (int threadNo = 0; threadNo < numThreads; threadNo++) {

                typedef CppMemo<Key, Value, KeyHash1, KeyHash2, KeyEqual> self;
                std::thread thread(&self::run<Compute, DeclarePrerequisites>,
                                   this, threadNo, std::ref(key), compute, declarePrerequisites);

                threads.push_back(std::move(thread));

            }

            for (std::thread& thread : threads) {
                thread.join();
            }

        } else { // single thread execution

            run(0, key, compute, declarePrerequisites);

        }

        return values[key];

    }

    template<typename Compute, typename DeclarePrerequisites>
    const Value& getValue(const Key& key, Compute compute, DeclarePrerequisites declarePrerequisites) {
        return getValue(key, compute, declarePrerequisites, getDefaultNumThreads());
    }

    template<typename Compute>
    const Value& getValue(const Key& key, Compute compute) {
        return getValue(key, compute, unspecifiedDeclarePrerequisites);
    }

    const Value& getValue(const Key& key) const {
        const auto findIt = values.find(key);
        if (findIt != values.end()) {
            return findIt->second;
        } else {
            throw std::logic_error("The value is not memoized");
        }
    }

    template<typename Compute, typename DeclarePrerequisites>
    const Value& operator()(const Key& key, Compute compute, DeclarePrerequisites declarePrerequisites, int numThreads) {
        return getValue(key, compute, declarePrerequisites, numThreads);
    }

    template<typename Compute, typename DeclarePrerequisites>
    const Value& operator()(const Key& key, Compute compute, DeclarePrerequisites declarePrerequisites) {
        return getValue(key, compute, declarePrerequisites);
    }

    template<typename Compute>
    const Value& operator()(const Key& key, Compute compute) {
        return getValue(key, compute);
    }

    const Value& operator()(const Key& key) const {
        return getValue(key);
    }

    // the class shall not be copied or moved (because of Fcmm)
    CppMemo(const CppMemo&) = delete;
    CppMemo(CppMemo&&) = delete;

};

} // namespace cppmemo

#endif // CPPMEMO_H_
