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

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/map_column.h"
#include "exprs/agg/base_aggregate_test.h"
#include "exprs/anyval_util.h"
#include "runtime/decimalv2_value.h"
#include "runtime/mem_pool.h"

namespace starrocks {

class AvgMapTest : public testing::Test {
protected:
    static TypeDescriptor map_type(const TypeDescriptor& key_type, const TypeDescriptor& value_type) {
        TypeDescriptor type(TYPE_MAP);
        type.children = {key_type, value_type};
        return type;
    }

    static MapColumn::Ptr int_map(const std::vector<std::optional<int32_t>>& keys,
                                  const std::vector<std::optional<int32_t>>& values,
                                  const std::vector<uint32_t>& row_ends) {
        DCHECK_EQ(keys.size(), values.size());
        auto key_data = Int32Column::create();
        auto key_nulls = NullColumn::create();
        auto value_data = Int32Column::create();
        auto value_nulls = NullColumn::create();
        for (size_t i = 0; i < keys.size(); ++i) {
            key_data->append(keys[i].value_or(0));
            key_nulls->append(keys[i].has_value() ? 0 : 1);
            value_data->append(values[i].value_or(0));
            value_nulls->append(values[i].has_value() ? 0 : 1);
        }

        auto map = MapColumn::create(NullableColumn::create(std::move(key_data), std::move(key_nulls)),
                                     NullableColumn::create(std::move(value_data), std::move(value_nulls)),
                                     UInt32Column::create());
        for (uint32_t row_end : row_ends) {
            map->offsets_column()->append(row_end);
        }
        map->check_or_die();
        return map;
    }

    static MapColumn::Ptr string_int_map(const std::vector<std::optional<std::string>>& keys,
                                         const std::vector<std::optional<int32_t>>& values) {
        DCHECK_EQ(keys.size(), values.size());
        auto key_data = BinaryColumn::create();
        auto key_nulls = NullColumn::create();
        auto value_data = Int32Column::create();
        auto value_nulls = NullColumn::create();
        for (size_t i = 0; i < keys.size(); ++i) {
            const std::string& key = keys[i].value_or("");
            key_data->append(Slice(key.data(), key.size()));
            key_nulls->append(keys[i].has_value() ? 0 : 1);
            value_data->append(values[i].value_or(0));
            value_nulls->append(values[i].has_value() ? 0 : 1);
        }

        auto map = MapColumn::create(NullableColumn::create(std::move(key_data), std::move(key_nulls)),
                                     NullableColumn::create(std::move(value_data), std::move(value_nulls)),
                                     UInt32Column::create());
        map->offsets_column()->append(keys.size());
        map->check_or_die();
        return map;
    }

    static const AggregateFunction* resolve(const TypeDescriptor& argument_type,
                                            const TypeDescriptor& /*return_type*/) {
        return get_aggregate_function("avg_map", argument_type.children[0].type, argument_type.children[1].type, false,
                                      TFunctionBinaryType::BUILTIN);
    }

    static std::unique_ptr<FunctionContext> test_context(const TypeDescriptor& argument_type,
                                                         const TypeDescriptor& return_type) {
        std::vector<FunctionContext::TypeDesc> argument_types = {AnyValUtil::column_type_to_type_desc(argument_type)};
        return std::unique_ptr<FunctionContext>(FunctionContext::create_test_context(
                std::move(argument_types), AnyValUtil::column_type_to_type_desc(return_type)));
    }

    static std::unique_ptr<FunctionContext> context_with_pool(MemPool* mem_pool, const TypeDescriptor& argument_type,
                                                              const TypeDescriptor& return_type) {
        std::vector<FunctionContext::TypeDesc> argument_types = {AnyValUtil::column_type_to_type_desc(argument_type)};
        return std::unique_ptr<FunctionContext>(FunctionContext::create_context(
                nullptr, mem_pool, AnyValUtil::column_type_to_type_desc(return_type), argument_types));
    }

    static void assert_int_double_result(const Column& result, const std::vector<std::optional<int32_t>>& keys,
                                         const std::vector<std::optional<double>>& values) {
        const auto* map = down_cast<const MapColumn*>(&result);
        ASSERT_EQ(1, map->size());
        ASSERT_EQ(keys.size(), map->get_map_size(0));
        ColumnViewer<TYPE_INT> key_viewer(map->keys_column());
        ColumnViewer<TYPE_DOUBLE> value_viewer(map->values_column());
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t actual_index = keys.size();
            for (size_t j = 0; j < keys.size(); ++j) {
                if ((!keys[i].has_value() && key_viewer.is_null(j)) ||
                    (keys[i].has_value() && !key_viewer.is_null(j) && *keys[i] == key_viewer.value(j))) {
                    actual_index = j;
                    break;
                }
            }
            ASSERT_LT(actual_index, keys.size());
            ASSERT_EQ(!values[i].has_value(), value_viewer.is_null(actual_index));
            if (values[i].has_value()) {
                EXPECT_DOUBLE_EQ(*values[i], value_viewer.value(actual_index));
            }
        }
    }
};

TEST_F(AvgMapTest, updatePreservesMissingDuplicateAndNullEntries) {
    TypeDescriptor argument_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    TypeDescriptor return_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context = test_context(argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    // Rows: {2:20, 1:0, 3:NULL, 1:10, NULL:4}, {1:20, 2:40, 3:NULL, NULL:8}.
    // A missing key contributes neither a zero nor a count; duplicate keys are independent observations.
    auto input = int_map({2, 1, 3, 1, std::nullopt, 1, 2, 3, std::nullopt},
                         {20, 0, std::nullopt, 10, 4, 20, 40, std::nullopt, 8}, {5, 9});
    auto state = ManagedAggrState::create(context.get(), function);
    const Column* input_column = input.get();
    function->update_batch_single_state(context.get(), input->size(), &input_column, state->state());

    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), state->state(), result.get());
    assert_int_double_result(*result, {std::nullopt, 1, 2, 3}, {6.0, 10.0, 30.0, std::nullopt});
}

TEST_F(AvgMapTest, mergeUsesWeightedPartialState) {
    TypeDescriptor argument_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    TypeDescriptor return_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context = test_context(argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    auto first_partition = int_map({1}, {0}, {1});
    auto second_partition = int_map({1, 1, 1}, {10, 20, 30}, {3});
    auto first_state = ManagedAggrState::create(context.get(), function);
    auto second_state = ManagedAggrState::create(context.get(), function);
    const Column* input_column = first_partition.get();
    function->update(context.get(), &input_column, first_state->state(), 0);
    input_column = second_partition.get();
    function->update(context.get(), &input_column, second_state->state(), 0);

    ColumnPtr serialized = BinaryColumn::create();
    function->serialize_to_column(context.get(), first_state->state(), serialized.get());
    function->merge(context.get(), serialized.get(), second_state->state(), 0);

    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), second_state->state(), result.get());
    // Correct answer is (0 + 10 + 20 + 30) / 4, not AVG(0, AVG(10,20,30)).
    assert_int_double_result(*result, {1}, {15.0});
}

TEST_F(AvgMapTest, convertToSerializeFormatRetainsPerKeyCounts) {
    TypeDescriptor argument_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    TypeDescriptor return_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context = test_context(argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    auto input = int_map({1, 2, 1, 3}, {10, 100, 20, std::nullopt}, {2, 4});
    ColumnPtr input_column(std::move(input));
    Columns source{input_column};
    ColumnPtr serialized = BinaryColumn::create();
    function->convert_to_serialize_format(context.get(), source, input_column->size(), &serialized);

    auto state = ManagedAggrState::create(context.get(), function);
    function->merge(context.get(), serialized.get(), state->state(), 0);
    function->merge(context.get(), serialized.get(), state->state(), 1);
    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), state->state(), result.get());
    assert_int_double_result(*result, {1, 2, 3}, {15.0, 100.0, std::nullopt});
}

TEST_F(AvgMapTest, stringKeysAndValuesOutliveInput) {
    TypeDescriptor argument_type = map_type(TypeDescriptor::create_varchar_type(20), TypeDescriptor(TYPE_INT));
    TypeDescriptor return_type = map_type(TypeDescriptor::create_varchar_type(20), TypeDescriptor(TYPE_DOUBLE));
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> context = context_with_pool(&mem_pool, argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    auto input = string_int_map({"z", "a", std::nullopt, "a", "b"}, {4, 10, 6, 20, std::nullopt});
    auto state = ManagedAggrState::create(context.get(), function);
    const Column* input_column = input.get();
    function->update(context.get(), &input_column, state->state(), 0);
    input.reset();

    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), state->state(), result.get());
    const auto* map = down_cast<const MapColumn*>(result.get());
    ColumnViewer<TYPE_VARCHAR> key_viewer(map->keys_column());
    ColumnViewer<TYPE_DOUBLE> value_viewer(map->values_column());
    ASSERT_EQ(4, map->get_map_size(0));
    const std::vector<std::optional<std::string>> expected_keys{std::nullopt, "a", "b", "z"};
    const std::vector<std::optional<double>> expected_values{6.0, 15.0, std::nullopt, 4.0};
    for (size_t i = 0; i < expected_keys.size(); ++i) {
        size_t actual_index = expected_keys.size();
        for (size_t j = 0; j < expected_keys.size(); ++j) {
            if ((!expected_keys[i].has_value() && key_viewer.is_null(j)) ||
                (expected_keys[i].has_value() && !key_viewer.is_null(j) &&
                 *expected_keys[i] == key_viewer.value(j).to_string())) {
                actual_index = j;
                break;
            }
        }
        ASSERT_LT(actual_index, expected_keys.size());
        ASSERT_EQ(!expected_values[i].has_value(), value_viewer.is_null(actual_index));
        if (expected_values[i].has_value()) {
            EXPECT_DOUBLE_EQ(*expected_values[i], value_viewer.value(actual_index));
        }
    }
}

TEST_F(AvgMapTest, decimalValuesUseWideAccumulatorAndKeepScaleUntilFinalize) {
    TypeDescriptor decimal_type(TYPE_DECIMAL64);
    decimal_type.precision = 18;
    decimal_type.scale = 2;
    TypeDescriptor argument_type = map_type(TypeDescriptor(TYPE_INT), decimal_type);
    TypeDescriptor return_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context = test_context(argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    auto key_data = Int32Column::create();
    constexpr size_t kEntryCount = 20;
    auto value_data = Decimal64Column::create(18, 2);
    // Every value is 9000000000000000.00. Their encoded sum is 1.8e19, which
    // exceeds int64_t and proves that the per-key state uses Decimal128.
    for (size_t i = 0; i < kEntryCount; ++i) {
        key_data->append(1);
        value_data->append(900000000000000000LL);
    }
    auto input = MapColumn::create(NullableColumn::create(std::move(key_data), NullColumn::create(kEntryCount, 0)),
                                   NullableColumn::create(std::move(value_data), NullColumn::create(kEntryCount, 0)),
                                   UInt32Column::create());
    input->offsets_column()->append(kEntryCount);

    auto state = ManagedAggrState::create(context.get(), function);
    const Column* input_column = input.get();
    function->update(context.get(), &input_column, state->state(), 0);
    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), state->state(), result.get());
    assert_int_double_result(*result, {1}, {9000000000000000.0});
}

TEST_F(AvgMapTest, decimalV2ValuesAreRegisteredAndAveraged) {
    TypeDescriptor decimal_type(TYPE_DECIMALV2);
    decimal_type.precision = 18;
    decimal_type.scale = 2;
    TypeDescriptor argument_type = map_type(TypeDescriptor(TYPE_INT), decimal_type);
    TypeDescriptor return_type = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context = test_context(argument_type, return_type);
    const AggregateFunction* function = resolve(argument_type, return_type);
    ASSERT_NE(nullptr, function);

    auto key_data = Int32Column::create();
    auto value_data = DecimalColumn::create();
    for (const char* value : {"1.50", "2.50", "3.50"}) {
        key_data->append(1);
        value_data->append(DecimalV2Value(value));
    }
    auto input = MapColumn::create(NullableColumn::create(std::move(key_data), NullColumn::create(3, 0)),
                                   NullableColumn::create(std::move(value_data), NullColumn::create(3, 0)),
                                   UInt32Column::create());
    input->offsets_column()->append(3);
    input->check_or_die();

    auto state = ManagedAggrState::create(context.get(), function);
    const Column* input_column = input.get();
    function->update(context.get(), &input_column, state->state(), 0);
    ColumnPtr result = ColumnHelper::create_column(return_type, false);
    function->finalize_to_column(context.get(), state->state(), result.get());
    assert_int_double_result(*result, {1}, {2.5});
}

} // namespace starrocks
