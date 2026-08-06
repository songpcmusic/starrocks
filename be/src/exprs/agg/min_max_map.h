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

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/phmap/phmap.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "column/map_column.h"
#include "column/nullable_column.h"
#include "column/runtime_type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType VT, bool = lt_is_string<VT>>
struct MinMaxMapValueState {
    using ValueCppType = RunTimeCppType<VT>;

    bool has_value = false;
    ValueCppType value{};

    void assign(const ValueCppType& candidate) {
        value = candidate;
        has_value = true;
    }

    const ValueCppType& get_value() const { return value; }
};

template <LogicalType VT>
struct MinMaxMapValueState<VT, true> {
    bool has_value = false;
    std::string value;

    void assign(const Slice& candidate) {
        if (candidate.size == 0) {
            value.clear();
        } else {
            value.assign(reinterpret_cast<const char*>(candidate.data), candidate.size);
        }
        has_value = true;
    }

    Slice get_value() const { return Slice(value); }
};

template <LogicalType KT, LogicalType VT, typename MyHashMap = std::map<int, size_t>>
struct MinMaxMapAggregateFunctionState : public AggregateFunctionEmptyState {
    using ValueState = MinMaxMapValueState<VT>;

    MyHashMap hash_map;
    std::vector<ValueState> value_states;
    bool has_null_key = false;
    ValueState null_key_state;

    void reset() {
        hash_map.clear();
        value_states.clear();
        has_null_key = false;
        null_key_state = {};
    }

    template <typename KeyViewer>
    ValueState& get_or_create(MemPool* mem_pool, const KeyViewer& key_viewer, size_t index) {
        if (key_viewer.is_null(index)) {
            has_null_key = true;
            return null_key_state;
        }

        if constexpr (lt_is_string<KT>) {
            SliceWithHash probe(key_viewer.value(index));
            auto it = hash_map.find(probe);
            if (it != hash_map.end()) {
                return value_states[it->second];
            }

            uint8_t* copied = mem_pool->allocate(probe.size);
            if (probe.size > 0) {
                memcpy(copied, probe.data, probe.size);
            }
            size_t state_index = value_states.size();
            value_states.emplace_back();
            hash_map.emplace(SliceWithHash(Slice(copied, probe.size)), state_index);
            return value_states[state_index];
        } else {
            auto [it, inserted] = hash_map.try_emplace(key_viewer.value(index), value_states.size());
            if (inserted) {
                value_states.emplace_back();
            }
            return value_states[it->second];
        }
    }
};

template <LogicalType KT, LogicalType VT, typename MyHashMap = std::map<int, size_t>>
class MinMaxMapAggregateFunction final
        : public AggregateFunctionBatchHelper<MinMaxMapAggregateFunctionState<KT, VT, MyHashMap>,
                                              MinMaxMapAggregateFunction<KT, VT, MyHashMap>> {
public:
    using State = MinMaxMapAggregateFunctionState<KT, VT, MyHashMap>;
    using ValueState = typename State::ValueState;
    using KeyColumnType = RunTimeColumnType<KT>;
    using ValueColumnType = RunTimeColumnType<VT>;
    using ValueCppType = RunTimeCppType<VT>;
    using HashKeyType = typename MyHashMap::key_type;

    explicit MinMaxMapAggregateFunction(bool is_min) : _is_min(is_min) {}

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).reset();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        DCHECK(!columns[0]->is_nullable());
        update_from_map(ctx, down_cast<const MapColumn*>(ColumnHelper::get_data_column(columns[0])), row_num,
                        &this->data(state));
    }

    AggStateTableKind agg_state_table_kind(bool is_append_only) const override {
        return AggStateTableKind::INTERMEDIATE;
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        DCHECK(!column->is_nullable());
        update_from_map(ctx, down_cast<const MapColumn*>(ColumnHelper::get_data_column(column)), row_num,
                        &this->data(state));
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        append_state(this->data(state), to);
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        append_state(this->data(state), to);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     MutableColumnPtr& dst) const override {
        DCHECK(!src[0]->is_nullable());
        DCHECK(!dst->is_nullable());
        const auto* src_map_column = down_cast<const MapColumn*>(ColumnHelper::get_data_column(src[0]));
        auto* dst_map_column = down_cast<MapColumn*>(ColumnHelper::get_data_column(dst.get()));
        const auto& src_offsets = src_map_column->offsets().immutable_data();
        size_t entry_begin = src_offsets[0];
        size_t entry_count = src_offsets[chunk_size] - entry_begin;

        dst_map_column->keys_column()->as_mutable_raw_ptr()->append(*src_map_column->keys_column(), entry_begin,
                                                                    entry_count);
        dst_map_column->values_column()->as_mutable_raw_ptr()->append(*src_map_column->values_column(), entry_begin,
                                                                      entry_count);

        auto& dst_offsets = dst_map_column->offsets().get_data();
        size_t dst_entry_begin = dst_offsets.back();
        for (size_t row = 0; row < chunk_size; ++row) {
            dst_offsets.push_back(dst_entry_begin + src_offsets[row + 1] - entry_begin);
        }
        dst_map_column->check_or_die();
    }

    std::string get_name() const override { return _is_min ? "min_map" : "max_map"; }

private:
    bool should_replace(const ValueCppType& current, const ValueCppType& candidate) const {
        if constexpr (lt_is_float<VT>) {
            bool current_nan = std::isnan(current);
            bool candidate_nan = std::isnan(candidate);
            if (current_nan != candidate_nan) {
                return current_nan;
            }
            if (current_nan) {
                return false;
            }
        }
        if constexpr (lt_is_string<VT>) {
            int comparison = candidate.compare(current);
            return _is_min ? comparison < 0 : comparison > 0;
        } else {
            return _is_min ? candidate < current : candidate > current;
        }
    }

    void update_value(ValueState* state, const ValueCppType& candidate) const {
        if (!state->has_value || should_replace(state->get_value(), candidate)) {
            state->assign(candidate);
        }
    }

    void update_from_map(FunctionContext* ctx, const MapColumn* map_column, size_t row_num, State* state) const {
        const auto& offsets = map_column->offsets().immutable_data();
        ColumnViewer<KT> key_viewer(map_column->keys_column());
        ColumnViewer<VT> value_viewer(map_column->values_column());
        update_entries(ctx, key_viewer, value_viewer, offsets[row_num], offsets[row_num + 1], state);
    }

    template <typename KeyViewer, typename ValueViewer>
    void update_entries(FunctionContext* ctx, const KeyViewer& key_viewer, const ValueViewer& value_viewer,
                        size_t begin, size_t end, State* state) const {
        for (size_t i = begin; i < end; ++i) {
            ValueState& value_state = state->get_or_create(ctx->mem_pool(), key_viewer, i);
            if (!value_viewer.is_null(i)) {
                update_value(&value_state, value_viewer.value(i));
            }
        }
    }

    static void append_key(const HashKeyType& key, Column* output) {
        DCHECK(output->is_nullable());
        auto* nullable = down_cast<NullableColumn*>(output);
        auto* data = down_cast<KeyColumnType*>(nullable->data_column()->as_mutable_raw_ptr());
        if constexpr (lt_is_string<KT>) {
            data->append(Slice(key.data, key.size));
        } else {
            data->append(key);
        }
        nullable->null_column_data().push_back(0);
    }

    static void append_value(const ValueState& state, Column* output) {
        DCHECK(output->is_nullable());
        if (!state.has_value) {
            output->append_nulls(1);
            return;
        }
        auto* nullable = down_cast<NullableColumn*>(output);
        auto* data = down_cast<ValueColumnType*>(nullable->data_column()->as_mutable_raw_ptr());
        data->append(state.get_value());
        nullable->null_column_data().push_back(0);
    }

    static void append_state(const State& state, Column* output) {
        DCHECK(!output->is_nullable());
        auto* map_column = down_cast<MapColumn*>(ColumnHelper::get_data_column(output));
        Column* key_column = map_column->keys_column_raw_ptr();
        Column* value_column = map_column->values_column_raw_ptr();
        size_t appended = 0;

        if (state.has_null_key) {
            key_column->append_nulls(1);
            append_value(state.null_key_state, value_column);
            ++appended;
        }

        for (const auto& entry : state.hash_map) {
            append_key(entry.first, key_column);
            append_value(state.value_states[entry.second], value_column);
            ++appended;
        }

        auto* offsets = map_column->offsets_column_raw_ptr();
        offsets->append(offsets->immutable_data().back() + appended);
        map_column->check_or_die();
    }

    const bool _is_min;
};

} // namespace starrocks
