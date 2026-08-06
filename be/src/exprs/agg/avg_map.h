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

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/phmap/phmap.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "column/map_column.h"
#include "column/nullable_column.h"
#include "column/runtime_type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/avg.h"
#include "exprs/function_context.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include "types/decimalv3.h"
#include "types/logical_type.h"

namespace starrocks {

// avg_map must preserve both parts of AVG's algebraic state for every key. In particular,
// partial averages cannot be merged correctly without retaining their individual counts.
template <LogicalType KT, LogicalType VT, typename MyHashMap = std::map<int, size_t>>
struct AvgMapAggregateFunctionState : public AggregateFunctionEmptyState {
    // Current StarRocks decimal AVG widens Decimal32/64/128 numerators to
    // Decimal128. Preserve that behavior per key instead of overflowing in the
    // narrower input representation before finalization.
    static constexpr LogicalType ImmediateLT = (VT == TYPE_DECIMAL32 || VT == TYPE_DECIMAL64 || VT == TYPE_DECIMAL128)
                                                       ? TYPE_DECIMAL128
                                                       : ImmediateAvgResultLT<VT>;
    using ImmediateType = RunTimeCppType<ImmediateLT>;
    using ValueState = AvgAggregateState<ImmediateType>;

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
            auto key = key_viewer.value(index);
            auto [it, inserted] = hash_map.try_emplace(key, value_states.size());
            if (inserted) {
                value_states.emplace_back();
            }
            return value_states[it->second];
        }
    }

    template <typename KeyType>
    ValueState& get_or_create(MemPool* mem_pool, bool is_null_key, const KeyType& key) {
        if (is_null_key) {
            has_null_key = true;
            return null_key_state;
        }

        if constexpr (lt_is_string<KT>) {
            SliceWithHash probe(key);
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
            auto [it, inserted] = hash_map.try_emplace(key, value_states.size());
            if (inserted) {
                value_states.emplace_back();
            }
            return value_states[it->second];
        }
    }

    template <typename KeyViewer, typename ValueViewer>
    void update(MemPool* mem_pool, const KeyViewer& key_viewer, const ValueViewer& value_viewer, size_t offset,
                size_t count) {
        for (size_t i = offset; i < offset + count; ++i) {
            // Deliberately create the key state before checking the value. This matches
            // ClickHouse avgMap: a key whose values are all NULL is retained as {key: NULL}.
            ValueState& value_state = get_or_create(mem_pool, key_viewer, i);
            if (!value_viewer.is_null(i)) {
                value_state.sum += value_viewer.value(i);
                ++value_state.count;
            }
        }
    }
};

template <LogicalType KT, LogicalType VT, typename MyHashMap = std::map<int, size_t>>
class AvgMapAggregateFunction final
        : public AggregateFunctionBatchHelper<AvgMapAggregateFunctionState<KT, VT, MyHashMap>,
                                              AvgMapAggregateFunction<KT, VT, MyHashMap>> {
public:
    using State = AvgMapAggregateFunctionState<KT, VT, MyHashMap>;
    using KeyColumnType = RunTimeColumnType<KT>;
    using KeyCppType = RunTimeCppType<KT>;
    using ImmediateType = typename State::ImmediateType;
    using ValueState = typename State::ValueState;
    using HashKeyType = typename MyHashMap::key_type;
    using ResultColumnType = RunTimeColumnType<TYPE_DOUBLE>;

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).reset();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        DCHECK(!columns[0]->is_nullable());
        const auto* map_column = down_cast<const MapColumn*>(ColumnHelper::get_data_column(columns[0]));
        const auto& offsets = map_column->offsets().immutable_data();
        if (offsets[row_num + 1] == offsets[row_num]) {
            return;
        }

        ColumnViewer<KT> key_viewer(map_column->keys_column());
        ColumnViewer<VT> value_viewer(map_column->values_column());
        this->data(state).update(ctx->mem_pool(), key_viewer, value_viewer, offsets[row_num],
                                 offsets[row_num + 1] - offsets[row_num]);
    }

    AggStateTableKind agg_state_table_kind(bool is_append_only) const override {
        return AggStateTableKind::INTERMEDIATE;
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        DCHECK(column->is_binary());
        Slice serialized = column->get(row_num).get_slice();
        merge_serialized(ctx->mem_pool(), serialized, &this->data(state));
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        DCHECK(to->is_binary());
        std::string serialized;
        serialize_state(this->data(state), &serialized);
        down_cast<BinaryColumn*>(to)->append(Slice(serialized));
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     MutableColumnPtr& dst) const override {
        DCHECK(!src[0]->is_nullable());
        DCHECK(dst->is_binary());

        const auto* map_column = down_cast<const MapColumn*>(ColumnHelper::get_data_column(src[0]));
        const auto& offsets = map_column->offsets().immutable_data();
        ColumnViewer<KT> key_viewer(map_column->keys_column());
        ColumnViewer<VT> value_viewer(map_column->values_column());
        auto* dst_column = down_cast<BinaryColumn*>(dst.get());
        for (size_t row = 0; row < chunk_size; ++row) {
            std::string serialized;
            append_pod(&serialized, kSerializationVersion);
            uint64_t entry_count = offsets[row + 1] - offsets[row];
            append_pod(&serialized, entry_count);
            for (size_t i = offsets[row]; i < offsets[row + 1]; ++i) {
                append_input_entry(key_viewer, value_viewer, i, &serialized);
            }
            dst_column->append(Slice(serialized));
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        DCHECK(!to->is_nullable());
        const State& state_impl = this->data(state);
        auto* map_column = down_cast<MapColumn*>(ColumnHelper::get_data_column(to));
        Column* key_column = map_column->keys_column_raw_ptr();
        Column* value_column = map_column->values_column_raw_ptr();
        size_t appended = 0;

        if (state_impl.has_null_key) {
            key_column->append_nulls(1);
            append_average(ctx, state_impl.null_key_state, value_column);
            ++appended;
        }

        for (const auto& entry : state_impl.hash_map) {
            append_key(entry.first, key_column);
            append_average(ctx, state_impl.value_states[entry.second], value_column);
            ++appended;
        }

        auto* offsets = map_column->offsets_column_raw_ptr();
        offsets->append(offsets->immutable_data().back() + appended);
        map_column->check_or_die();
    }

    std::string get_name() const override { return "avg_map"; }

private:
    static constexpr uint8_t kSerializationVersion = 1;
    template <typename T>
    static void append_pod(std::string* output, const T& value) {
        output->append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    static T read_pod(Slice input, size_t* offset) {
        DCHECK_LE(*offset + sizeof(T), input.size);
        T value;
        memcpy(&value, input.data + *offset, sizeof(T));
        *offset += sizeof(T);
        return value;
    }

    template <typename KeyViewer, typename ValueViewer>
    static void append_input_entry(const KeyViewer& key_viewer, const ValueViewer& value_viewer, size_t index,
                                   std::string* output) {
        bool is_null_key = key_viewer.is_null(index);
        append_pod(output, is_null_key);
        if (!is_null_key) {
            append_key_to_serialized(key_viewer.value(index), output);
        }

        ValueState value_state;
        if (!value_viewer.is_null(index)) {
            value_state.sum += value_viewer.value(index);
            value_state.count = 1;
        }
        append_pod(output, value_state.sum);
        append_pod(output, value_state.count);
    }

    template <typename Key>
    static void append_key_to_serialized(const Key& key, std::string* output) {
        if constexpr (lt_is_string<KT>) {
            uint64_t size = key.size;
            append_pod(output, size);
            output->append(reinterpret_cast<const char*>(key.data), key.size);
        } else {
            append_pod(output, key);
        }
    }

    static void append_state_entry(bool is_null_key, const HashKeyType* key, const ValueState& value_state,
                                   std::string* output) {
        append_pod(output, is_null_key);
        if (!is_null_key) {
            append_key_to_serialized(*key, output);
        }
        append_pod(output, value_state.sum);
        append_pod(output, value_state.count);
    }

    static void serialize_state(const State& state, std::string* output) {
        append_pod(output, kSerializationVersion);
        uint64_t entry_count = state.hash_map.size() + (state.has_null_key ? 1 : 0);
        append_pod(output, entry_count);
        if (state.has_null_key) {
            append_state_entry(true, nullptr, state.null_key_state, output);
        }
        for (const auto& entry : state.hash_map) {
            append_state_entry(false, &entry.first, state.value_states[entry.second], output);
        }
    }

    static void merge_serialized(MemPool* mem_pool, Slice input, State* state) {
        size_t offset = 0;
        uint8_t version = read_pod<uint8_t>(input, &offset);
        DCHECK_EQ(version, kSerializationVersion);
        uint64_t entry_count = read_pod<uint64_t>(input, &offset);
        for (uint64_t i = 0; i < entry_count; ++i) {
            bool is_null_key = read_pod<bool>(input, &offset);
            ValueState* destination;
            if constexpr (lt_is_string<KT>) {
                Slice key;
                if (!is_null_key) {
                    uint64_t key_size = read_pod<uint64_t>(input, &offset);
                    DCHECK_LE(offset + key_size, input.size);
                    key = Slice(input.data + offset, key_size);
                    offset += key_size;
                }
                destination = &state->get_or_create(mem_pool, is_null_key, key);
            } else {
                KeyCppType key{};
                if (!is_null_key) {
                    key = read_pod<KeyCppType>(input, &offset);
                }
                destination = &state->get_or_create(mem_pool, is_null_key, key);
            }

            ImmediateType sum = read_pod<ImmediateType>(input, &offset);
            int64_t count = read_pod<int64_t>(input, &offset);
            destination->sum += sum;
            destination->count += count;
        }
        DCHECK_EQ(offset, input.size);
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

    static void append_average(FunctionContext* ctx, const ValueState& value_state, Column* output) {
        DCHECK(output->is_nullable());
        if (value_state.count == 0) {
            output->append_nulls(1);
            return;
        }

        double average;
        if constexpr (lt_is_decimalv2<VT>) {
            average = static_cast<double>(value_state.sum) / value_state.count;
        } else if constexpr (lt_is_decimal<VT>) {
            const auto* argument_type = ctx->get_arg_type(0);
            DCHECK(argument_type != nullptr);
            DCHECK_EQ(argument_type->children.size(), 2);
            int scale = argument_type->children[1].scale;
            double sum_as_double;
            DecimalV3Cast::to_float<ImmediateType, double>(value_state.sum, get_scale_factor<ImmediateType>(scale),
                                                           &sum_as_double);
            average = sum_as_double / value_state.count;
        } else {
            average = static_cast<double>(value_state.sum) / value_state.count;
        }

        auto* nullable = down_cast<NullableColumn*>(output);
        auto* data = down_cast<ResultColumnType*>(nullable->data_column()->as_mutable_raw_ptr());
        data->append(average);
        nullable->null_column_data().push_back(0);
    }
};

} // namespace starrocks
