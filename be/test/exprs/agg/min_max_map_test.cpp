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

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/map_column.h"
#include "exprs/agg/base_aggregate_test.h"
#include "runtime/mem_pool.h"
#include "types/decimalv2_value.h"

namespace starrocks {

class MinMaxMapTest : public testing::Test {
public:
    void SetUp() override {
        _allocator = std::make_unique<CountingAllocatorWithHook>();
        tls_agg_state_allocator = _allocator.get();
    }

    void TearDown() override {
        tls_agg_state_allocator = nullptr;
        _allocator.reset();
    }

protected:
    static TypeDescriptor map_type(const TypeDescriptor& key_type, const TypeDescriptor& value_type) {
        TypeDescriptor type(TYPE_MAP);
        type.children = {key_type, value_type};
        return type;
    }

    static MapColumn::MutablePtr int_map(const std::vector<std::optional<int32_t>>& keys,
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
            map->offsets_column_raw_ptr()->append(row_end);
        }
        map->check_or_die();
        return map;
    }

    static MapColumn::MutablePtr string_map(const std::vector<std::optional<std::string>>& keys,
                                            const std::vector<std::optional<std::string>>& values) {
        DCHECK_EQ(keys.size(), values.size());
        auto key_data = BinaryColumn::create();
        auto key_nulls = NullColumn::create();
        auto value_data = BinaryColumn::create();
        auto value_nulls = NullColumn::create();
        for (size_t i = 0; i < keys.size(); ++i) {
            const std::string& key = keys[i].value_or("");
            const std::string& value = values[i].value_or("");
            key_data->append(Slice(key.data(), key.size()));
            key_nulls->append(keys[i].has_value() ? 0 : 1);
            value_data->append(Slice(value.data(), value.size()));
            value_nulls->append(values[i].has_value() ? 0 : 1);
        }

        auto map = MapColumn::create(NullableColumn::create(std::move(key_data), std::move(key_nulls)),
                                     NullableColumn::create(std::move(value_data), std::move(value_nulls)),
                                     UInt32Column::create());
        map->offsets_column_raw_ptr()->append(keys.size());
        map->check_or_die();
        return map;
    }

    static MapColumn::MutablePtr int_double_map(const std::vector<int32_t>& keys,
                                                const std::vector<std::optional<double>>& values) {
        DCHECK_EQ(keys.size(), values.size());
        auto key_data = Int32Column::create();
        auto value_data = DoubleColumn::create();
        auto value_nulls = NullColumn::create();
        for (size_t i = 0; i < keys.size(); ++i) {
            key_data->append(keys[i]);
            value_data->append(values[i].value_or(0));
            value_nulls->append(values[i].has_value() ? 0 : 1);
        }

        auto map = MapColumn::create(NullableColumn::create(std::move(key_data), NullColumn::create(keys.size(), 0)),
                                     NullableColumn::create(std::move(value_data), std::move(value_nulls)),
                                     UInt32Column::create());
        map->offsets_column_raw_ptr()->append(keys.size());
        map->check_or_die();
        return map;
    }

    static void assert_int_result(const Column& result, const std::vector<std::optional<int32_t>>& keys,
                                  const std::vector<std::optional<int32_t>>& values) {
        const auto* map = down_cast<const MapColumn*>(&result);
        ASSERT_EQ(1, map->size());
        ASSERT_EQ(keys.size(), map->get_map_size(0));
        ColumnViewer<TYPE_INT> key_viewer(map->keys_column());
        ColumnViewer<TYPE_INT> value_viewer(map->values_column());
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
                EXPECT_EQ(*values[i], value_viewer.value(actual_index));
            }
        }
    }

    static void assert_int_double_result(const Column& result, const std::vector<int32_t>& keys,
                                         const std::vector<double>& values) {
        const auto* map = down_cast<const MapColumn*>(&result);
        ASSERT_EQ(1, map->size());
        ASSERT_EQ(keys.size(), map->get_map_size(0));
        ColumnViewer<TYPE_INT> key_viewer(map->keys_column());
        ColumnViewer<TYPE_DOUBLE> value_viewer(map->values_column());
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t actual_index = keys.size();
            for (size_t j = 0; j < keys.size(); ++j) {
                if (!key_viewer.is_null(j) && keys[i] == key_viewer.value(j)) {
                    actual_index = j;
                    break;
                }
            }
            ASSERT_LT(actual_index, keys.size());
            ASSERT_FALSE(value_viewer.is_null(actual_index));
            if (std::isnan(values[i])) {
                EXPECT_TRUE(std::isnan(value_viewer.value(actual_index)));
            } else {
                EXPECT_EQ(values[i], value_viewer.value(actual_index));
            }
        }
    }

    std::unique_ptr<CountingAllocatorWithHook> _allocator;
};

TEST_F(MinMaxMapTest, updateAndFinalizeIntegerMaps) {
    TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
    auto input = int_map({2, 1, 3, 1, std::nullopt, 1, 2, 3, std::nullopt},
                         {20, 0, std::nullopt, 10, 4, 20, 40, std::nullopt, 8}, {5, 9});

    for (const auto& test_case : {std::pair{"min_map", std::vector<std::optional<int32_t>>{4, 0, 20, std::nullopt}},
                                  std::pair{"max_map", std::vector<std::optional<int32_t>>{8, 20, 40, std::nullopt}}}) {
        const AggregateFunction* function =
                get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << test_case.first;
        auto state = ManagedAggrState::create(context.get(), function);
        const Column* input_column = input.get();
        function->update_batch_single_state(context.get(), input->size(), &input_column, state->state());

        MutableColumnPtr result = ColumnHelper::create_column(map, false);
        function->finalize_to_column(context.get(), state->state(), result.get());
        assert_int_result(*result, {std::nullopt, 1, 2, 3}, test_case.second);
    }
}

TEST_F(MinMaxMapTest, partialStatesMergeAcrossBothSerializationPaths) {
    TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
    auto first_partition = int_map({1, 2}, {5, 100}, {2});
    auto second_partition = int_map({1, 2}, {3, 200}, {2});
    auto streaming_input = int_map({1, 1, 2, std::nullopt, 3, 1, 2, std::nullopt, 3},
                                   {5, 3, std::nullopt, 8, std::nullopt, 7, 10, 4, std::nullopt}, {5, 9});

    for (const auto& test_case : {std::pair{"min_map", std::vector<std::optional<int32_t>>{3, 100}},
                                  std::pair{"max_map", std::vector<std::optional<int32_t>>{5, 200}}}) {
        const AggregateFunction* function =
                get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << test_case.first;

        auto first_state = ManagedAggrState::create(context.get(), function);
        auto merged_state = ManagedAggrState::create(context.get(), function);
        const Column* input_column = first_partition.get();
        function->update(context.get(), &input_column, first_state->state(), 0);
        input_column = second_partition.get();
        function->update(context.get(), &input_column, merged_state->state(), 0);

        MutableColumnPtr serialized = ColumnHelper::create_column(map, false);
        function->serialize_to_column(context.get(), first_state->state(), serialized.get());
        function->merge(context.get(), serialized.get(), merged_state->state(), 0);
        MutableColumnPtr merged_result = ColumnHelper::create_column(map, false);
        function->finalize_to_column(context.get(), merged_state->state(), merged_result.get());
        assert_int_result(*merged_result, {1, 2}, test_case.second);

        Columns source{streaming_input};
        MutableColumnPtr streaming_serialized = ColumnHelper::create_column(map, false);
        function->convert_to_serialize_format(context.get(), source, streaming_input->size(), streaming_serialized);
        auto streaming_state = ManagedAggrState::create(context.get(), function);
        function->merge(context.get(), streaming_serialized.get(), streaming_state->state(), 0);
        function->merge(context.get(), streaming_serialized.get(), streaming_state->state(), 1);
        MutableColumnPtr streaming_result = ColumnHelper::create_column(map, false);
        function->finalize_to_column(context.get(), streaming_state->state(), streaming_result.get());
        const bool is_min = std::string(test_case.first) == "min_map";
        assert_int_result(*streaming_result, {std::nullopt, 1, 2, 3},
                          is_min ? std::vector<std::optional<int32_t>>{4, 3, 10, std::nullopt}
                                 : std::vector<std::optional<int32_t>>{8, 7, 10, std::nullopt});
    }
}

TEST_F(MinMaxMapTest, booleanSignatureIsRegisteredForUntypedNull) {
    TypeDescriptor map = map_type(TypeDescriptor(TYPE_BOOLEAN), TypeDescriptor(TYPE_BOOLEAN));
    for (const char* function_name : {"min_map", "max_map"}) {
        const AggregateFunction* function =
                get_aggregate_function(function_name, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << function_name;
    }
}

TEST_F(MinMaxMapTest, allSupportedKeyAndValueTypesAreRegistered) {
    const std::vector<LogicalType> supported_key_types = {
            TYPE_BOOLEAN, TYPE_TINYINT, TYPE_SMALLINT,  TYPE_INT,       TYPE_BIGINT,    TYPE_LARGEINT,
            TYPE_FLOAT,   TYPE_DOUBLE,  TYPE_DECIMALV2, TYPE_DECIMAL32, TYPE_DECIMAL64, TYPE_DECIMAL128,
            TYPE_CHAR,    TYPE_VARCHAR, TYPE_DATE,      TYPE_DATETIME};
    const std::vector<LogicalType> supported_value_types = {
            TYPE_BOOLEAN,    TYPE_TINYINT, TYPE_SMALLINT,  TYPE_INT,       TYPE_BIGINT,    TYPE_LARGEINT,
            TYPE_FLOAT,      TYPE_DOUBLE,  TYPE_DECIMALV2, TYPE_DECIMAL32, TYPE_DECIMAL64, TYPE_DECIMAL128,
            TYPE_DECIMAL256, TYPE_CHAR,    TYPE_VARCHAR,   TYPE_DATE,      TYPE_DATETIME};

    for (const char* function_name : {"min_map", "max_map"}) {
        for (LogicalType key_type : supported_key_types) {
            TypeDescriptor map = map_type(TypeDescriptor(key_type), TypeDescriptor(TYPE_INT));
            EXPECT_NE(nullptr, get_aggregate_function(function_name, map, {map}, false, TFunctionBinaryType::BUILTIN))
                    << function_name << " key type " << key_type;
        }
        for (LogicalType value_type : supported_value_types) {
            TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(value_type));
            EXPECT_NE(nullptr, get_aggregate_function(function_name, map, {map}, false, TFunctionBinaryType::BUILTIN))
                    << function_name << " value type " << value_type;
        }

        TypeDescriptor decimal256_key = map_type(TypeDescriptor(TYPE_DECIMAL256), TypeDescriptor(TYPE_INT));
        EXPECT_EQ(nullptr, get_aggregate_function(function_name, decimal256_key, {decimal256_key}, false,
                                                  TFunctionBinaryType::BUILTIN));
    }
}

TEST_F(MinMaxMapTest, floatingPointValuesFollowClickHouseNaNRulesAcrossMergeOrders) {
    TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    auto first_partition = int_double_map({1, 2, 3, 4}, {nan, nan, infinity, -infinity});
    auto second_partition = int_double_map({1, 2, 3, 4}, {6.0, nan, 3.0, -3.0});

    for (const auto& test_case : {std::pair{"min_map", std::vector<double>{6.0, nan, 3.0, -infinity}},
                                  std::pair{"max_map", std::vector<double>{6.0, nan, infinity, -3.0}}}) {
        const AggregateFunction* function =
                get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << test_case.first;

        auto finalize_merged = [&](const Column* serialized_input, const Column* direct_input) {
            auto serialized_state = ManagedAggrState::create(context.get(), function);
            function->update(context.get(), &serialized_input, serialized_state->state(), 0);
            MutableColumnPtr serialized = ColumnHelper::create_column(map, false);
            function->serialize_to_column(context.get(), serialized_state->state(), serialized.get());

            auto merged_state = ManagedAggrState::create(context.get(), function);
            function->update(context.get(), &direct_input, merged_state->state(), 0);
            function->merge(context.get(), serialized.get(), merged_state->state(), 0);
            MutableColumnPtr result = ColumnHelper::create_column(map, false);
            function->finalize_to_column(context.get(), merged_state->state(), result.get());
            assert_int_double_result(*result, {1, 2, 3, 4}, test_case.second);
        };

        finalize_merged(first_partition.get(), second_partition.get());
        finalize_merged(second_partition.get(), first_partition.get());
    }
}

TEST_F(MinMaxMapTest, decimalValuesPreserveLegacyAndDecimal256Types) {
    {
        TypeDescriptor decimal_v2(TYPE_DECIMALV2);
        decimal_v2.precision = 18;
        decimal_v2.scale = 2;
        TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), decimal_v2);
        std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
        auto key_data = Int32Column::create();
        auto value_data = DecimalColumn::create();
        for (const char* value : {"1.25", "3.50", "-2.75"}) {
            key_data->append(1);
            value_data->append(DecimalV2Value(value));
        }
        auto input = MapColumn::create(NullableColumn::create(std::move(key_data), NullColumn::create(3, 0)),
                                       NullableColumn::create(std::move(value_data), NullColumn::create(3, 0)),
                                       UInt32Column::create());
        input->offsets_column_raw_ptr()->append(3);

        for (const auto& test_case :
             {std::pair{"min_map", DecimalV2Value("-2.75")}, std::pair{"max_map", DecimalV2Value("3.50")}}) {
            const AggregateFunction* function =
                    get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
            ASSERT_NE(nullptr, function) << test_case.first;
            auto state = ManagedAggrState::create(context.get(), function);
            const Column* input_column = input.get();
            function->update(context.get(), &input_column, state->state(), 0);
            MutableColumnPtr result = ColumnHelper::create_column(map, false);
            function->finalize_to_column(context.get(), state->state(), result.get());
            const auto* result_map = down_cast<const MapColumn*>(result.get());
            ColumnViewer<TYPE_DECIMALV2> viewer(result_map->values_column());
            ASSERT_FALSE(viewer.is_null(0));
            EXPECT_EQ(test_case.second, viewer.value(0));
        }
    }

    {
        TypeDescriptor decimal256 = TypeDescriptor::create_decimalv3_type(TYPE_DECIMAL256, 76, 2);
        TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), decimal256);
        std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
        auto key_data = Int32Column::create();
        auto value_data = Decimal256Column::create(76, 2);
        for (int256_t value : {int256_t(125), int256_t(350), int256_t(-275)}) {
            key_data->append(1);
            value_data->append(value);
        }
        auto input = MapColumn::create(NullableColumn::create(std::move(key_data), NullColumn::create(3, 0)),
                                       NullableColumn::create(std::move(value_data), NullColumn::create(3, 0)),
                                       UInt32Column::create());
        input->offsets_column_raw_ptr()->append(3);

        for (const auto& test_case : {std::pair{"min_map", int256_t(-275)}, std::pair{"max_map", int256_t(350)}}) {
            const AggregateFunction* function =
                    get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
            ASSERT_NE(nullptr, function) << test_case.first;
            auto state = ManagedAggrState::create(context.get(), function);
            const Column* input_column = input.get();
            function->update(context.get(), &input_column, state->state(), 0);
            MutableColumnPtr result = ColumnHelper::create_column(map, false);
            function->finalize_to_column(context.get(), state->state(), result.get());
            const auto* result_map = down_cast<const MapColumn*>(result.get());
            const auto* nullable = down_cast<const NullableColumn*>(result_map->values_column().get());
            const auto* decimal_column = down_cast<const Decimal256Column*>(nullable->data_column().get());
            ASSERT_EQ(1, decimal_column->size());
            EXPECT_EQ(76, decimal_column->precision());
            EXPECT_EQ(2, decimal_column->scale());
            EXPECT_TRUE(test_case.second == decimal_column->get_data()[0]);
        }
    }
}

TEST_F(MinMaxMapTest, emptyMapsProduceAnEmptyMap) {
    TypeDescriptor map = map_type(TypeDescriptor(TYPE_INT), TypeDescriptor(TYPE_INT));
    std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context({map}, map));
    auto input = int_map({}, {}, {0, 0});

    for (const char* function_name : {"min_map", "max_map"}) {
        const AggregateFunction* function =
                get_aggregate_function(function_name, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << function_name;
        auto state = ManagedAggrState::create(context.get(), function);
        const Column* input_column = input.get();
        function->update_batch_single_state(context.get(), input->size(), &input_column, state->state());
        MutableColumnPtr result = ColumnHelper::create_column(map, false);
        function->finalize_to_column(context.get(), state->state(), result.get());
        const auto* result_map = down_cast<const MapColumn*>(result.get());
        ASSERT_EQ(1, result_map->size());
        EXPECT_EQ(0, result_map->get_map_size(0));
    }
}

TEST_F(MinMaxMapTest, stringKeysAndSelectedValuesOutliveInput) {
    TypeDescriptor varchar = TypeDescriptor::create_varchar_type(20);
    TypeDescriptor map = map_type(varchar, varchar);
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> context(FunctionContext::create_context(nullptr, &mem_pool, map, {map}));

    for (const auto& test_case :
         {std::pair{"min_map", std::vector<std::optional<std::string>>{"c", "a", std::nullopt, "m"}},
          std::pair{"max_map", std::vector<std::optional<std::string>>{"q", "z", std::nullopt, "x"}}}) {
        const AggregateFunction* function =
                get_aggregate_function(test_case.first, map, {map}, false, TFunctionBinaryType::BUILTIN);
        ASSERT_NE(nullptr, function) << test_case.first;

        auto input = string_map({"z", "a", "b", "a", std::nullopt, "z", "a", std::nullopt},
                                {"m", "z", std::nullopt, "a", "q", "x", "b", "c"});
        auto state = ManagedAggrState::create(context.get(), function);
        const Column* input_column = input.get();
        function->update(context.get(), &input_column, state->state(), 0);
        input.reset();

        MutableColumnPtr result = ColumnHelper::create_column(map, false);
        function->finalize_to_column(context.get(), state->state(), result.get());
        const auto* result_map = down_cast<const MapColumn*>(result.get());
        ColumnViewer<TYPE_VARCHAR> key_viewer(result_map->keys_column());
        ColumnViewer<TYPE_VARCHAR> value_viewer(result_map->values_column());
        ASSERT_EQ(4, result_map->get_map_size(0));
        const std::vector<std::optional<std::string>> expected_keys{std::nullopt, "a", "b", "z"};
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
            ASSERT_EQ(!test_case.second[i].has_value(), value_viewer.is_null(actual_index));
            if (test_case.second[i].has_value()) {
                EXPECT_EQ(*test_case.second[i], value_viewer.value(actual_index).to_string());
            }
        }
    }
}

} // namespace starrocks
