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

#include "exprs/agg/avg_map.h"
#include "exprs/agg/factory/aggregate_resolver.hpp"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType kt>
struct AvgMapValueTypeDispatcher {
    template <LogicalType vt>
    void operator()(AggregateFuncResolver* resolver) {
        // type_dispatch_all instantiates this dispatcher for every LogicalType. Keep the
        // implementation cartesian restricted to the value types exposed by the FE.
        if constexpr (lt_is_numeric<vt> || lt_is_decimalv2<vt>) {
            using KeyCppType = RunTimeCppType<kt>;
            if constexpr (lt_is_largeint<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, Hash128WithSeed<PhmapSeed1>>;
                auto function = std::make_shared<AvgMapAggregateFunction<kt, vt, HashMap>>();
                using State = AvgMapAggregateFunctionState<kt, vt, HashMap>;
                resolver->add_aggregate_mapping<kt, vt, State>("avg_map", false, function);
            } else if constexpr (lt_is_fixedlength<kt>) {
                using HashMap = phmap::flat_hash_map<KeyCppType, size_t, StdHash<KeyCppType>>;
                auto function = std::make_shared<AvgMapAggregateFunction<kt, vt, HashMap>>();
                using State = AvgMapAggregateFunctionState<kt, vt, HashMap>;
                resolver->add_aggregate_mapping<kt, vt, State>("avg_map", false, function);
            } else if constexpr (lt_is_string<kt>) {
                using HashMap = phmap::flat_hash_map<SliceWithHash, size_t, HashOnSliceWithHash, EqualOnSliceWithHash>;
                auto function = std::make_shared<AvgMapAggregateFunction<kt, vt, HashMap>>();
                using State = AvgMapAggregateFunctionState<kt, vt, HashMap>;
                resolver->add_aggregate_mapping<kt, vt, State>("avg_map", false, function);
            }
        }
    }
};

struct AvgMapDispatcher {
    template <LogicalType kt>
    void operator()(AggregateFuncResolver* resolver) {
        type_dispatch_all(TYPE_BOOLEAN, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_TINYINT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_SMALLINT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_INT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_BIGINT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_LARGEINT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_FLOAT, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DOUBLE, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMALV2, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL32, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL64, AvgMapValueTypeDispatcher<kt>(), resolver);
        type_dispatch_all(TYPE_DECIMAL128, AvgMapValueTypeDispatcher<kt>(), resolver);
    }
};

template <LogicalType... kts>
inline void register_avg_map_for_keys(AggregateFuncResolver* resolver) {
    (AvgMapDispatcher{}.template operator()<kts>(resolver), ...);
}

} // namespace starrocks
