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

#include "column/column_builder.h"
#include "column/type_traits.h"
#include "common/statusor.h"
#include "formats/parquet/variant.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType ResultType>
static StatusOr<RunTimeCppType<ResultType>> cast_variant_to_arithmetic(const Variant& variant) {
    constexpr auto min = RunTimeTypeLimits<ResultType>::min_value();
    constexpr auto max = RunTimeTypeLimits<ResultType>::max_value();

    auto variant_type = variant.type();

    if constexpr (lt_is_integer<ResultType>) {
        switch (variant_type) {
        case VariantType::INT8: {
            auto result = variant.get_int8();
            if (result.ok()) {
                auto v = result.value();
                if constexpr (ResultType != TYPE_BIGINT && ResultType != TYPE_LARGEINT) {
                    if (v < min || v > max) {
                        return Status::InvalidArgument("cast number overflow");
                    }
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::INT16: {
            auto result = variant.get_int16();
            if (result.ok()) {
                auto v = result.value();
                if constexpr (ResultType != TYPE_BIGINT && ResultType != TYPE_LARGEINT) {
                    if (v < min || v > max) {
                        return Status::InvalidArgument("cast number overflow");
                    }
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::INT32: {
            auto result = variant.get_int32();
            if (result.ok()) {
                auto v = result.value();
                if constexpr (ResultType != TYPE_BIGINT && ResultType != TYPE_LARGEINT) {
                    if (v < static_cast<int64_t>(min) || v > static_cast<int64_t>(max)) {
                        return Status::InvalidArgument("cast number overflow");
                    }
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::INT64: {
            auto result = variant.get_int64();
            if (result.ok()) {
                auto v = result.value();
                if constexpr (ResultType != TYPE_LARGEINT) {
                    if (v < static_cast<int64_t>(min) || v > static_cast<int64_t>(max)) {
                        return Status::InvalidArgument("cast number overflow");
                    }
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::DECIMAL4: {
            auto result = variant.get_decimal4();
            if (result.ok()) {
                auto decimal = result.value();
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                if (decimal_double < static_cast<double>(min) || decimal_double > static_cast<double>(max)) {
                    return Status::InvalidArgument("cast number overflow");
                }
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::DECIMAL8: {
            auto result = variant.get_decimal8();
            if (result.ok()) {
                auto decimal = result.value();
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                if (decimal_double < static_cast<double>(min) || decimal_double > static_cast<double>(max)) {
                    return Status::InvalidArgument("cast number overflow");
                }
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::DECIMAL16: {
            auto result = variant.get_decimal16();
            if (result.ok()) {
                auto decimal = result.value();
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                if (decimal_double < static_cast<double>(min) || decimal_double > static_cast<double>(max)) {
                    return Status::InvalidArgument("cast number overflow");
                }
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::DOUBLE: {
            auto result = variant.get_double();
            if (result.ok()) {
                auto v = result.value();
                if (v < static_cast<double>(min) || v > static_cast<double>(max)) {
                    return Status::InvalidArgument("cast number overflow");
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::FLOAT: {
            auto result = variant.get_float();
            if (result.ok()) {
                auto v = result.value();
                if (v < static_cast<float>(min) || v > static_cast<float>(max)) {
                    return Status::InvalidArgument("cast number overflow");
                }
                return static_cast<RunTimeCppType<ResultType>>(v);
            }
            break;
        }
        case VariantType::STRING: {
            // 从字符串解析
            auto result = variant.get_string();
            if (result.ok()) {
                auto str_view = result.value();
                StringParser::ParseResult parseResult;
                auto r = StringParser::string_to_int<RunTimeCppType<ResultType>>(
                    str_view.data(), str_view.length(), &parseResult);
                if (parseResult != StringParser::PARSE_SUCCESS) {
                    return Status::InvalidArgument("cast number from string failed");
                }
                return r;
            }
            break;
        }
        default:
            break;
        }
    } else if constexpr (lt_is_float<ResultType>) {
        // 处理浮点类型
        switch (variant_type) {
        case VariantType::FLOAT: {
            auto result = variant.get_float();
            if (result.ok()) {
                return static_cast<RunTimeCppType<ResultType>>(result.value());
            }
            break;
        }
        case VariantType::DOUBLE: {
            auto result = variant.get_double();
            if (result.ok()) {
                return static_cast<RunTimeCppType<ResultType>>(result.value());
            }
            break;
        }
        case VariantType::INT8:
        case VariantType::INT16:
        case VariantType::INT32:
        case VariantType::INT64: {
            auto result = variant.get_int64();
            if (result.ok()) {
                return static_cast<RunTimeCppType<ResultType>>(result.value());
            }
            break;
        }
        case VariantType::DECIMAL4: {
            auto result = variant.get_decimal4();
            if (result.ok()) {
                auto decimal = result.value();
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::DECIMAL8: {
            auto result = variant.get_decimal8();
            if (result.ok()) {
                auto decimal = result.value();
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::DECIMAL16: {
            auto result = variant.get_decimal16();
            if (result.ok()) {
                auto decimal = result.value();
                // Note: int128_t to double may lose precision
                double decimal_double = static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
                return static_cast<RunTimeCppType<ResultType>>(decimal_double);
            }
            break;
        }
        case VariantType::STRING: {
            auto result = variant.get_string();
            if (result.ok()) {
                auto str_view = result.value();
                StringParser::ParseResult parseResult;
                auto r = StringParser::string_to_float<RunTimeCppType<ResultType>>(
                    str_view.data(), str_view.length(), &parseResult);
                if (parseResult != StringParser::PARSE_SUCCESS || std::isnan(r) || std::isinf(r)) {
                    return Status::InvalidArgument("cast float from string failed");
                }
                return r;
            }
            break;
        }
        default:
            break;
        }
    }

    return Status::InvalidArgument("not a number or unsupported conversion");
}

template <LogicalType ResultType, bool AllowThrowException>
static Status cast_variant_to(const Variant& variant, ColumnBuilder<ResultType>& result) {
    if constexpr (!lt_is_arithmetic<ResultType> && !lt_is_string<ResultType> && ResultType != TYPE_VARIANT) {
        if constexpr (AllowThrowException) {
            return Status::NotSupported(fmt::format("not supported type {}", type_to_string(ResultType)));
        }
        result.append_null();
        return Status::OK();
    }

    if constexpr (ResultType == TYPE_VARIANT) {
        VariantValue variant_value = VariantValue::of_variant(variant);
        result.append(std::move(variant_value));
        return Status::OK();
    }

    if (variant.type() == VariantType::NULL_TYPE) {
        result.append_null();
        return Status::OK();
    }

    try {
        if constexpr (ResultType == TYPE_BOOLEAN) {
            if (variant.type() == VariantType::BOOLEAN) {
                auto bool_result = variant.get_bool();
                if (bool_result.ok()) {
                    result.append(bool_result.value());
                    return Status::OK();
                }
            } else if (variant.type() == VariantType::STRING) {
                auto str_result = variant.get_string();
                if (str_result.ok()) {
                    auto str_view = str_result.value();
                    const char* str = str_view.data();
                    size_t len = str_view.size();

                    StringParser::ParseResult parseResult;
                    auto r = StringParser::string_to_int<int32_t>(str, len, &parseResult);

                    if (parseResult != StringParser::PARSE_SUCCESS || std::isnan(r) || std::isinf(r)) {
                        bool b = StringParser::string_to_bool(str, len, &parseResult);
                        if (parseResult != StringParser::PARSE_SUCCESS) {
                            if constexpr (AllowThrowException) {
                                return Status::InvalidArgument(
                                    fmt::format("cast from Variant string({}) to BOOLEAN failed",
                                              std::string(str, len)));
                            }
                            result.append_null();
                            return Status::OK();
                        }
                        result.append(b);
                    } else {
                        result.append(r != 0);
                    }
                }
            } else {
                auto num_result = cast_variant_to_arithmetic<TYPE_DOUBLE>(variant);
                if (num_result.ok()) {
                    result.append(num_result.value() != 0);
                    return Status::OK();
                }
            }
            
            if constexpr (AllowThrowException) {
                return Status::InvalidArgument("cast to BOOLEAN failed");
            }
            result.append_null();
            return Status::OK();
        }

        if constexpr (lt_is_arithmetic<ResultType>) {
            if (variant.type() == VariantType::BOOLEAN) {
                auto bool_result = variant.get_bool();
                if (bool_result.ok()) {
                    result.append(static_cast<RunTimeCppType<ResultType>>(bool_result.value()));
                    return Status::OK();
                }
            } else {
                auto arithmetic_result = cast_variant_to_arithmetic<ResultType>(variant);
                if (arithmetic_result.ok()) {
                    result.append(arithmetic_result.value());
                    return Status::OK();
                } else {
                    if constexpr (AllowThrowException) {
                        return arithmetic_result.status();
                    }
                    result.append_null();
                    return Status::OK();
                }
            }
        }

        if constexpr (lt_is_string<ResultType>) {
            if (variant.type() == VariantType::STRING) {
                auto str_result = variant.get_string();
                if (str_result.ok()) {
                    result.append(Slice(str_result.value().data(), str_result.value().size()));
                    return Status::OK();
                }
            } else {
                VariantValue variant_value = VariantValue::of_variant(variant);
                auto json_result = variant_value.to_json();
                if (json_result.ok()) {
                    result.append(Slice(json_result.value()));
                    return Status::OK();
                }
            }
            result.append_null();
            return Status::OK();
        }

    } catch (const std::exception& e) {
        if constexpr (AllowThrowException) {
            return Status::InvalidArgument(
                fmt::format("cast from Variant to {} failed", type_to_string(ResultType)));
        }
        result.append_null();
    }

    return Status::OK();
}

} // namespace starrocks
