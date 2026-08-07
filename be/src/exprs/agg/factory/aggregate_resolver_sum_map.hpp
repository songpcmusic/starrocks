// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

// sum_map registration is a key x value cartesian. Keep it in several small
// translation units so a single compiler process does not instantiate the full matrix.

#include "exprs/agg/factory/aggregate_resolver.hpp"
#include "exprs/agg/sum_map.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType kt>
struct SumMapValueTypeDispatcher {
    template <LogicalType vt>
    void operator()(AggregateFuncResolver* resolver) {
        if constexpr (lt_is_numeric<vt>) {
            using KeyCppType = RunTimeCppType<kt>;
            if constexpr (lt_is_largeint<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, Hash128WithSeed<PhmapSeed1>>;
                register_function<vt, HashMap>(resolver);
            } else if constexpr (lt_is_fixedlength<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, StdHash<KeyCppType>>;
                register_function<vt, HashMap>(resolver);
            } else if constexpr (lt_is_string<kt>) {
                using HashMap =
                        phmap::flat_hash_map<SliceWithHash, size_t, HashOnSliceWithHash, EqualOnSliceWithHash>;
                register_function<vt, HashMap>(resolver);
            }
        }
    }

private:
    template <LogicalType vt, typename HashMap>
    static void register_function(AggregateFuncResolver* resolver) {
        using State = SumMapAggregateFunctionState<kt, vt, HashMap>;
        auto function = std::make_shared<SumMapAggregateFunction<kt, vt, HashMap>>();
        resolver->add_aggregate_mapping<kt, vt, State>("sum_map", false, function);
    }
};

struct SumMapDispatcher {
    template <LogicalType kt>
    void operator()(AggregateFuncResolver* resolver) {
        type_dispatch_all(TYPE_BOOLEAN, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_TINYINT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_SMALLINT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_INT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_BIGINT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_LARGEINT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_FLOAT, SumMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DOUBLE, SumMapValueTypeDispatcher<kt>(), resolver);
    }
};

template <LogicalType... kts>
inline void register_sum_map_for_keys(AggregateFuncResolver* resolver) {
    (SumMapDispatcher{}.template operator()<kts>(resolver), ...);
}

} // namespace starrocks
