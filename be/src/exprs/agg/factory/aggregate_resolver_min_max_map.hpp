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

#include "exprs/agg/factory/aggregate_resolver.hpp"
#include "exprs/agg/min_max_map.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType kt>
struct MinMaxMapValueTypeDispatcher {
    template <LogicalType vt>
    void operator()(AggregateFuncResolver* resolver) {
        if constexpr (lt_is_numeric<vt> || lt_is_decimalv2<vt> || lt_is_string<vt> || lt_is_date_or_datetime<vt>) {
            using KeyCppType = RunTimeCppType<kt>;
            if constexpr (lt_is_largeint<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, Hash128WithSeed<PhmapSeed1>>;
                register_functions<vt, HashMap>(resolver);
            } else if constexpr (lt_is_fixedlength<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, StdHash<KeyCppType>>;
                register_functions<vt, HashMap>(resolver);
            } else if constexpr (lt_is_string<kt>) {
                using HashMap = phmap::flat_hash_map<SliceWithHash, size_t, HashOnSliceWithHash, EqualOnSliceWithHash>;
                register_functions<vt, HashMap>(resolver);
            }
        }
    }

private:
    template <LogicalType vt, typename HashMap>
    static void register_functions(AggregateFuncResolver* resolver) {
        using State = MinMaxMapAggregateFunctionState<kt, vt, HashMap>;
        resolver->add_aggregate_mapping<kt, vt, State>("min_map", false,
                                                       new MinMaxMapAggregateFunction<kt, vt, HashMap>(true));
        resolver->add_aggregate_mapping<kt, vt, State>("max_map", false,
                                                       new MinMaxMapAggregateFunction<kt, vt, HashMap>(false));
    }
};

struct MinMaxMapDispatcher {
    template <LogicalType kt>
    void operator()(AggregateFuncResolver* resolver) {
        type_dispatch_all(TYPE_BOOLEAN, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_TINYINT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_SMALLINT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_INT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_BIGINT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_LARGEINT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_FLOAT, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DOUBLE, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMALV2, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL32, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL64, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL128, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL256, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_CHAR, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_VARCHAR, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DATE, MinMaxMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DATETIME, MinMaxMapValueTypeDispatcher<kt>(), resolver);
    }
};

template <LogicalType... kts>
inline void register_min_max_map_for_keys(AggregateFuncResolver* resolver) {
    (MinMaxMapDispatcher{}.template operator()<kts>(resolver), ...);
}

} // namespace starrocks
