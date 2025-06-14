/**
 * @file opt_map.h
 * @brief use vector and map to optimize performence
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <map>
#include <type_traits>
#include <cstdlib>
#include "../util/macros_func.h"

/**
 * @brief stable_infra namespace
 */
namespace ezio {
    /**
     * @brief data structure namespace
     */
    namespace data_struct {
        /**
         * @brief optimized map
         * use vector and map to optimize performence, if key < array_size, vector will be used,
         * Otherwise, map will be used.
         * @note VALUE_TYPE must be a pointer type
         *       KEY_TYPE must be an unsigned type
         */
        template<typename VALUE_TYPE, typename KEY_TYPE = uint32_t,
            uint32_t ARRAY_SIZE = 65536, typename MAP_TYPE = std::unordered_map<KEY_TYPE, VALUE_TYPE>>
        class opt_map
        {
            static_assert(std::is_assignable<VALUE_TYPE, std::nullptr_t>::value, "VALUE_TYPE must be a pointer type");
            static_assert(std::is_unsigned<KEY_TYPE>::value, "KEY_TYPE must be a unsigned type");
            static_assert(std::is_same<MAP_TYPE, std::map<KEY_TYPE, VALUE_TYPE>>::value
                          || std::is_same<MAP_TYPE, std::unordered_map<KEY_TYPE, VALUE_TYPE>>::value
                          , "MAP_TYPE must be std::map or std::unordered_map");
            public:
                /**
                 * @brief construction function
                 */
                opt_map() {
                    vec_.resize(ARRAY_SIZE, nullptr);
                }

                /**
                 * @brief destruction function
                 */
                ~opt_map() {
                }

                /**
                 * @brief find value by key
                 * @param[in] key key for finding
                 * @return result
                 * @retval nullptr can not find
                 * @retval VALUE_TYPE value
                 */
                const VALUE_TYPE& find(const KEY_TYPE& key) const
                {
                    if (key < ARRAY_SIZE) {
                        return vec_[key];
                    } else {
                        auto iter = map_.find(key);
                        if (iter != map_.end()) {
                            return iter->second;
                        }
                        return null_value_;
                    }
                }

                /**
                 * @brief insert new value
                 * @param[in] key new key
                 * @param[in] value new value 
                 * @return result
                 * @retval true successful
                 * @retval false failed
                 */
                bool insert(KEY_TYPE key, const VALUE_TYPE& value)
                {
                    if (value == nullptr) {
                        return false;
                    }
                    if (key < ARRAY_SIZE) {
                        if (vec_[key] == nullptr) {
                            vec_[key] = value;
                            return true;
                        }
                    } else {
                        auto iter = map_.find(key);
                        if (iter == map_.end()) {
                            map_[key] = value;
                            return true;
                        }
                    }
                    return false;
                }

                /**
                 * @brief erase value by key
                 * @param[in] key key need to be erased
                 */
                void erase(KEY_TYPE key)
                {
                    if (key < ARRAY_SIZE) {
                        vec_[key] = nullptr;
                    } else {
                        map_.erase(key);
                    }
                }

                VALUE_TYPE& operator [] (KEY_TYPE key) {
                    if (key < ARRAY_SIZE) {
                        return vec_[key];
                    } else {
                        return map_[key];
                    }
                }

                /**
                 * @brief clear all data
                 */
                void clear()
                {
                    vec_.clear();
                    vec_.resize(ARRAY_SIZE, nullptr);
                    map_.clear();
                }
            private:
                std::vector<VALUE_TYPE> vec_; ///< if key < ARRAY_SIZE, this vector will be used, index is key
                MAP_TYPE map_;                ///< if key >= ARRAY_SIZE, this map will be used
                VALUE_TYPE null_value_{ nullptr }; ///< nullptr for return reference
        };
    }
}
