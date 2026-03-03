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

#include "variant_value.h"

#include <arrow/util/endian.h>

#include <boost/uuid/uuid_io.hpp>
#include <cstring>

#include "util/url_coding.h"
#include "util/variant_util.h"

namespace starrocks {

StatusOr<VariantValue> VariantValue::create(const Slice& slice) {
    // Validate slice first
    if (slice.get_data() == nullptr) {
        return Status::InvalidArgument("Invalid variant slice: null data pointer");
    }

    if (slice.get_size() < sizeof(uint32_t)) {
        return Status::InvalidArgument("Invalid variant slice: too small to contain size header");
    }

    const char* variant_raw = slice.get_data();
    // The first 4 bytes are the size of the variant
    uint32_t variant_size;
    std::memcpy(&variant_size, variant_raw, sizeof(uint32_t));
    // Check variant size limit (16MB)
    if (variant_size > kMaxVariantSize) {
        return Status::InvalidArgument("Variant size exceeds maximum limit: " + std::to_string(variant_size) + " > " +
                                       std::to_string(kMaxVariantSize));
    }

    if (variant_size > slice.get_size() - sizeof(uint32_t)) {
        return Status::InvalidArgument(
                "Invalid variant size: " + std::to_string(variant_size) +
                " exceeds available data: " + std::to_string(slice.get_size() - sizeof(uint32_t)));
    }

    const auto variant = std::string_view(variant_raw + sizeof(uint32_t), variant_size);

    auto metadata_status = load_metadata(variant);
    if (!metadata_status.ok()) {
        return metadata_status.status();
    }

    const auto& metadata_view = metadata_status.value();
    if (metadata_view.size() > variant_size) {
        return Status::InvalidArgument("Metadata size exceeds variant size");
    }

    std::string metadata(metadata_view);
    RETURN_IF_ERROR(validate_metadata(metadata));
    std::string value(variant_raw + sizeof(uint32_t) + metadata_view.size(), variant_size - metadata_view.size());

    return VariantValue(std::move(metadata), std::move(value));
}

Status VariantValue::validate_metadata(const std::string_view metadata) {
    // metadata at least 3 bytes: version, dictionarySize and at least one offset.
    if (metadata.size() < kMinMetadataSize) {
        return Status::InternalError("Variant metadata is too short");
    }

    const uint8_t header = static_cast<uint8_t>(metadata[0]);
    if (const uint8_t version = header & kVersionMask; version != 1) {
        return Status::NotSupported("Unsupported variant version: " + std::to_string(version));
    }

    return Status::OK();
}

VariantValue VariantValue::of_variant(const Variant& variant) {
    const std::string_view metadata = variant.metadata().value();
    const std::string_view value = variant.value();

    std::string value_str(value);
    std::string metadata_str(metadata);

    return VariantValue(std::move(metadata_str), std::move(value_str));
}

VariantValue VariantValue::of_null() {
    static constexpr uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::NULL_TYPE) << 2;
    static constexpr uint8_t null_chars[] = {header};
    return VariantValue(VariantMetadata::kEmptyMetadata,
                        std::string_view{reinterpret_cast<const char*>(null_chars), 1});
}

StatusOr<std::string_view> VariantValue::load_metadata(const std::string_view variant) {
    if (variant.empty()) {
        return Status::InvalidArgument("Variant is empty");
    }

    // Check variant size limit (16MB)
    if (variant.size() > kMaxVariantSize) {
        return Status::InvalidArgument("Variant size exceeds maximum limit: " + std::to_string(variant.size()) + " > " +
                                       std::to_string(kMaxVariantSize));
    }

    const uint8_t header = static_cast<uint8_t>(variant[0]);
    if (const uint8_t version = header & kVersionMask; version != 1) {
        return Status::NotSupported("Unsupported variant version: " + std::to_string(version));
    }

    const uint8_t offset_size = 1 + ((header & kOffsetSizeMask) >> kOffsetSizeShift);
    if (offset_size < 1 || offset_size > 4) {
        return Status::InvalidArgument("Invalid offset size in variant metadata: " + std::to_string(offset_size) +
                                       ", expected 1, 2, 3 or 4 bytes");
    }

    if (variant.size() < kHeaderSize + offset_size) {
        return Status::InvalidArgument("Variant too short to contain dict_size");
    }

    uint32_t dict_size = VariantUtil::readLittleEndianUnsigned(variant.data() + 1, offset_size);
    uint32_t offset_list_offset = kHeaderSize + offset_size;

    // Check for potential overflow in offset list size calculation
    if (dict_size > (kMaxVariantSize - offset_list_offset) / offset_size - 1) {
        return Status::InvalidArgument("Dict size too large: " + std::to_string(dict_size));
    }

    uint32_t required_offset_list_size = (1 + dict_size) * offset_size;
    uint32_t data_offset = offset_list_offset + required_offset_list_size;
    uint32_t last_offset_pos = offset_list_offset + dict_size * offset_size;
    if (last_offset_pos + offset_size > variant.size()) {
        return Status::InvalidArgument("Variant too short to contain all offsets");
    }

    uint32_t last_data_size = VariantUtil::readLittleEndianUnsigned(variant.data() + last_offset_pos, offset_size);
    uint32_t end_offset = data_offset + last_data_size;

    if (end_offset > variant.size()) {
        return Status::CapacityLimitExceed("Variant metadata end offset exceeds variant size: " +
                                           std::to_string(end_offset) + " > " + std::to_string(variant.size()));
    }

    return std::string_view(variant.data(), end_offset);
}

size_t VariantValue::serialize(uint8_t* dst) const {
    size_t offset = 0;

    // The first 4 bytes are the total size of the variant
    uint32_t total_size = static_cast<uint32_t>(_metadata.size() + _value.size());
    memcpy(dst + offset, &total_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // metadata
    memcpy(dst + offset, _metadata.data(), _metadata.size());
    offset += _metadata.size();

    // value
    memcpy(dst + offset, _value.data(), _value.size());
    offset += _value.size();

    return offset;
}

uint32_t VariantValue::serialize_size() const {
    return sizeof(uint32_t) + _metadata.size() + _value.size();
}

StatusOr<std::string> VariantValue::to_json(cctz::time_zone timezone) const {
    std::stringstream json_str;
    auto status = VariantUtil::variant_to_json(_metadata, _value, json_str, timezone);
    if (!status.ok()) {
        return status;
    }

    return json_str.str();
}

std::string VariantValue::to_string() const {
    auto json_result = to_json();
    if (!json_result.ok()) {
        return "";
    }

    return json_result.value();
}

std::ostream& operator<<(std::ostream& os, const VariantValue& value) {
    return os << value.to_string();
}

static bool is_numeric_type(VariantType type) {
    return type == VariantType::INT8 || type == VariantType::INT16 ||
           type == VariantType::INT32 || type == VariantType::INT64 ||
           type == VariantType::FLOAT || type == VariantType::DOUBLE ||
           type == VariantType::DECIMAL4 || type == VariantType::DECIMAL8 ||
           type == VariantType::DECIMAL16;
}

static bool is_integer_type(VariantType type) {
    return type == VariantType::INT8 || type == VariantType::INT16 ||
           type == VariantType::INT32 || type == VariantType::INT64;
}

static bool is_primitive_type(VariantType type) {
    return type != VariantType::OBJECT && type != VariantType::ARRAY;
}

static int compare_double(double left, double right) {
    if (std::isless(left, right)) {
        return -1;
    } else if (std::isgreater(left, right)) {
        return 1;
    }
    return 0;
}

static StatusOr<int64_t> get_int_value(const Variant& v, VariantType type) {
    switch (type) {
        case VariantType::INT8: {
            auto result = v.get_int8();
            if (!result.ok()) return result.status();
            return static_cast<int64_t>(result.value());
        }
        case VariantType::INT16: {
            auto result = v.get_int16();
            if (!result.ok()) return result.status();
            return static_cast<int64_t>(result.value());
        }
        case VariantType::INT32: {
            auto result = v.get_int32();
            if (!result.ok()) return result.status();
            return static_cast<int64_t>(result.value());
        }
        case VariantType::INT64:
            return v.get_int64();
        default:
            return Status::InternalError("Not an integer type");
    }
}

static StatusOr<double> get_double_value(const Variant& v, VariantType type) {
    switch (type) {
        case VariantType::INT8: {
            auto result = v.get_int8();
            if (!result.ok()) return result.status();
            return static_cast<double>(result.value());
        }
        case VariantType::INT16: {
            auto result = v.get_int16();
            if (!result.ok()) return result.status();
            return static_cast<double>(result.value());
        }
        case VariantType::INT32: {
            auto result = v.get_int32();
            if (!result.ok()) return result.status();
            return static_cast<double>(result.value());
        }
        case VariantType::INT64: {
            auto result = v.get_int64();
            if (!result.ok()) return result.status();
            return static_cast<double>(result.value());
        }
        case VariantType::FLOAT: {
            auto result = v.get_float();
            if (!result.ok()) return result.status();
            return static_cast<double>(result.value());
        }
        case VariantType::DECIMAL4: {
            auto result = v.get_decimal4();
            if (!result.ok()) return result.status();
            auto decimal = result.value();
            return static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
        }
        case VariantType::DECIMAL8: {
            auto result = v.get_decimal8();
            if (!result.ok()) return result.status();
            auto decimal = result.value();
            return static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
        }
        case VariantType::DECIMAL16: {
            auto result = v.get_decimal16();
            if (!result.ok()) return result.status();
            auto decimal = result.value();
            return static_cast<double>(decimal.value) / std::pow(10.0, decimal.scale);
        }
        case VariantType::DOUBLE:
            return v.get_double();
        default:
            return Status::InternalError("Not a numeric type");
    }
}

static int compare_same_primitive_type(const Variant& left, const Variant& right, VariantType type) {
    switch (type) {
        case VariantType::NULL_TYPE:
            return 0;

        case VariantType::BOOLEAN: {
            auto l_result = left.get_bool();
            auto r_result = right.get_bool();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            bool l = l_result.value();
            bool r = r_result.value();
            return l == r ? 0 : (l ? 1 : -1);
        }

        case VariantType::INT8: {
            auto l_result = left.get_int8();
            auto r_result = right.get_int8();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            int8_t l = l_result.value();
            int8_t r = r_result.value();
            return l == r ? 0 : (l < r ? -1 : 1);
        }

        case VariantType::INT16: {
            auto l_result = left.get_int16();
            auto r_result = right.get_int16();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            int16_t l = l_result.value();
            int16_t r = r_result.value();
            return l == r ? 0 : (l < r ? -1 : 1);
        }

        case VariantType::INT32: {
            auto l_result = left.get_int32();
            auto r_result = right.get_int32();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            int32_t l = l_result.value();
            int32_t r = r_result.value();
            return l == r ? 0 : (l < r ? -1 : 1);
        }

        case VariantType::INT64: {
            auto l_result = left.get_int64();
            auto r_result = right.get_int64();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            int64_t l = l_result.value();
            int64_t r = r_result.value();
            return l == r ? 0 : (l < r ? -1 : 1);
        }

        case VariantType::FLOAT: {
            auto l_result = left.get_float();
            auto r_result = right.get_float();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            return compare_double(l_result.value(), r_result.value());
        }

        case VariantType::DOUBLE: {
            auto l_result = left.get_double();
            auto r_result = right.get_double();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            return compare_double(l_result.value(), r_result.value());
        }

        case VariantType::DECIMAL4: {
            auto l_result = left.get_decimal4();
            auto r_result = right.get_decimal4();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            auto l = l_result.value();
            auto r = r_result.value();
            double l_double = static_cast<double>(l.value) / std::pow(10.0, l.scale);
            double r_double = static_cast<double>(r.value) / std::pow(10.0, r.scale);
            return compare_double(l_double, r_double);
        }

        case VariantType::DECIMAL8: {
            auto l_result = left.get_decimal8();
            auto r_result = right.get_decimal8();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            auto l = l_result.value();
            auto r = r_result.value();
            double l_double = static_cast<double>(l.value) / std::pow(10.0, l.scale);
            double r_double = static_cast<double>(r.value) / std::pow(10.0, r.scale);
            return compare_double(l_double, r_double);
        }

        case VariantType::DECIMAL16: {
            auto l_result = left.get_decimal16();
            auto r_result = right.get_decimal16();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            auto l = l_result.value();
            auto r = r_result.value();
            double l_double = static_cast<double>(l.value) / std::pow(10.0, l.scale);
            double r_double = static_cast<double>(r.value) / std::pow(10.0, r.scale);
            return compare_double(l_double, r_double);
        }

        case VariantType::STRING: {
            auto l_result = left.get_string();
            auto r_result = right.get_string();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            auto l = l_result.value();
            auto r = r_result.value();
            int result = l.compare(r);
            return result == 0 ? 0 : (result < 0 ? -1 : 1);
        }

        case VariantType::BINARY: {
            LOG(INFO) << "  → BINARY comparison";
            auto l_result = left.get_binary();
            auto r_result = right.get_binary();
            if (!l_result.ok() || !r_result.ok()) {
                int cmp = left.value().compare(right.value());
                return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
            }
            auto l = l_result.value();
            auto r = r_result.value();
            int cmp = l.compare(r);
            return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
        }

        default:
            int cmp = left.value().compare(right.value());
            return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
    }
}

static int compare_variants(const Variant& left, const Variant& right);

static int compare_primitive_values(const Variant& left, VariantType left_type,
                                   const Variant& right, VariantType right_type) {
    LOG(INFO) << "compare_primitive_values: left_type=" << static_cast<int>(left_type)
              << ", right_type=" << static_cast<int>(right_type);

    if (left_type == right_type) {
        LOG(INFO) << "  Types are the same, using compare_same_primitive_type";
        return compare_same_primitive_type(left, right, left_type);
    }

    bool left_is_int = is_integer_type(left_type);
    bool right_is_int = is_integer_type(right_type);

    if (left_is_int && right_is_int) {
        LOG(INFO) << "  Both are integers, converting to int64 for comparison";
        auto l_result = get_int_value(left, left_type);
        auto r_result = get_int_value(right, right_type);
        if (!l_result.ok() || !r_result.ok()) {
            if (!l_result.ok()) {
                LOG(INFO) << "  get_int_value failed for LEFT: " << l_result.status().to_string();
            }
            if (!r_result.ok()) {
                LOG(INFO) << "  get_int_value failed for RIGHT: " << r_result.status().to_string();
            }
            int cmp = left.value().compare(right.value());
            return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
        }
        int64_t l = l_result.value();
        int64_t r = r_result.value();
        return l == r ? 0 : (l < r ? -1 : 1);
    }

    bool left_is_numeric = is_numeric_type(left_type);
    bool right_is_numeric = is_numeric_type(right_type);

    if (left_is_numeric && right_is_numeric) {
        LOG(INFO) << "  Both are numeric, converting to double for comparison";
        auto l_result = get_double_value(left, left_type);
        auto r_result = get_double_value(right, right_type);
        if (!l_result.ok() || !r_result.ok()) {
            if (!l_result.ok()) {
                LOG(INFO) << "  get_double_value failed for LEFT: " << l_result.status().to_string();
            }
            if (!r_result.ok()) {
                LOG(INFO) << "  get_double_value failed for RIGHT: " << r_result.status().to_string();
            }
            int cmp = left.value().compare(right.value());
            return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
        }
        double l = l_result.value();
        double r = r_result.value();
        return compare_double(l, r);
    }

    LOG(INFO) << "  Types are different and not compatible, comparing by type enum value";
    return static_cast<int>(left_type) - static_cast<int>(right_type);
}

static int compare_objects(const Variant& left, const Variant& right) {
    LOG(INFO) << "compare_objects called";
    auto left_num_result = left.num_elements();
    auto right_num_result = right.num_elements();
    if (!left_num_result.ok() || !right_num_result.ok()) {
        int cmp = left.value().compare(right.value());
        return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
    }
    auto left_num = left_num_result.value();
    auto right_num = right_num_result.value();
    LOG(INFO) << "  left_num=" << left_num << ", right_num=" << right_num;

    for (uint32_t i = 0; i < left_num; i++) {
        auto left_field_result = left.get_field_at_index(i);
        if (!left_field_result.ok()) {
            int cmp = left.value().compare(right.value());
            return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
        }
        auto left_field = left_field_result.value();
        auto& left_key = left_field.first;
        auto& left_value = left_field.second;

        auto right_value_result = right.get_object_by_key(left_key);
        if (!right_value_result.ok()) {
            return 1;
        }

        int cmp = compare_variants(left_value, right_value_result.value());
        if (cmp != 0) {
            return cmp;
        }
    }

    return static_cast<int>(left_num) - static_cast<int>(right_num);
}

static int compare_arrays(const Variant& left, const Variant& right) {
    LOG(INFO) << "compare_arrays called";
    auto left_num_result = left.num_elements();
    auto right_num_result = right.num_elements();
    if (!left_num_result.ok() || !right_num_result.ok()) {
        int cmp = left.value().compare(right.value());
        return cmp == 0 ? 0 : (cmp < 0 ? -1 : 1);
    }
    auto left_num = left_num_result.value();
    auto right_num = right_num_result.value();
    LOG(INFO) << "  left_num=" << left_num << ", right_num=" << right_num;

    if (left_num != right_num) {
        return static_cast<int>(left_num) - static_cast<int>(right_num);
    }

    for (uint32_t i = 0; i < left_num; i++) {
        auto left_elem_result = left.get_element_at_index(i);
        auto right_elem_result = right.get_element_at_index(i);
        if (!left_elem_result.ok() || !right_elem_result.ok()) {
            return left.value().compare(right.value());
        }
        auto left_elem = left_elem_result.value();
        auto right_elem = right_elem_result.value();

        int cmp = compare_variants(left_elem, right_elem);
        if (cmp != 0) {
            return cmp;
        }
    }

    return 0;
}

static int compare_variants(const Variant& left, const Variant& right) {
    VariantType left_type = left.type();
    VariantType right_type = right.type();
    LOG(INFO) << "compare_variants: left_type=" << static_cast<int>(left_type)
              << ", right_type=" << static_cast<int>(right_type);

    if (left_type == VariantType::OBJECT && right_type == VariantType::OBJECT) {
        return compare_objects(left, right);
    }

    if (left_type == VariantType::ARRAY && right_type == VariantType::ARRAY) {
        return compare_arrays(left, right);
    }

    if (is_primitive_type(left_type) && is_primitive_type(right_type)) {
        return compare_primitive_values(left, left_type, right, right_type);
    }

    return static_cast<int>(left_type) - static_cast<int>(right_type);
}

int VariantValue::compare(const VariantValue& rhs) const {
    LOG(INFO) << "  LHS: metadata_size=" << _metadata.size() << ", value_size=" << _value.size();
    LOG(INFO) << "  RHS: metadata_size=" << rhs._metadata.size() << ", value_size=" << rhs._value.size();

    if (_metadata == rhs._metadata && _value == rhs._value) {
        LOG(INFO) << "  Fast path: metadata and value are identical, returning 0";
        return 0;
    }

    if (_metadata.empty() && _value.empty()) {
        if (rhs._metadata.empty() && rhs._value.empty()) {
            LOG(INFO) << "  Both are empty, returning 0";
            return 0;
        }
        LOG(INFO) << "  LHS is empty, returning -1";
        return -1;
    }
    if (rhs._metadata.empty() && rhs._value.empty()) {
        LOG(INFO) << "  RHS is empty, returning 1";
        return 1;
    }

    Variant left(_metadata, _value);
    Variant right(rhs._metadata, rhs._value);

    int result = compare_variants(left, right);
    LOG(INFO) << "  compare result=" << result;
    return result;
}

int VariantValue::compare(const Slice& lhs, const Slice& rhs) {
    LOG(INFO) << "VariantValue::compare(Slice, Slice) called, lhs.size=" << lhs.size
              << ", rhs.size=" << rhs.size;

    auto lhs_variant = VariantValue::create(lhs);
    auto rhs_variant = VariantValue::create(rhs);

    if (!lhs_variant.ok() || !rhs_variant.ok()) {
        LOG(WARNING) << "VariantValue::create failed, using binary compare. "
                     << "lhs_ok=" << lhs_variant.ok() << ", rhs_ok=" << rhs_variant.ok();
        int result = lhs.compare(rhs);
        LOG(INFO) << "  Binary compare result=" << result;
        return result;
    }

    LOG(INFO) << "  Both VariantValue created successfully, calling instance compare";
    return lhs_variant.value().compare(rhs_variant.value());
}

int64_t VariantValue::hash() const {
    std::hash<std::string> hasher;
    size_t h1 = hasher(_metadata);
    size_t h2 = hasher(_value);
    return static_cast<int64_t>(h1 ^ (h2 << 1));
}

} // namespace starrocks
