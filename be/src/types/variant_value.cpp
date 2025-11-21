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

#include "column/fixed_length_column.h"
#include "formats/parquet/variant.h"
#include "gutil/strings/substitute.h"
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



VariantValue VariantValue::create_shredded(std::string metadata, Datum typed_value, LogicalType typed_value_type,
                                          std::string remain_value) {
    VariantValue result;
    result._metadata = std::move(metadata);
    result._value = "";  // Empty initially, will be reconstructed on first access
    result._shredded_data = ShreddedData{
        .typed_value = std::move(typed_value),
        .typed_value_type = typed_value_type,
        .remain_value = std::move(remain_value),
        .is_reconstructed = false
    };
    return result;
}

std::string VariantValue::get_value() {
    if (_shredded_data.has_value() && !_shredded_data->is_reconstructed) {
        auto status = ensure_value_reconstructed();
        if (!status.ok()) {
            LOG(WARNING) << "Failed to reconstruct shredded variant value: " << status.to_string();
            // Fallback to empty or remain_value
            return _shredded_data->remain_value;
        }
    }
    return _value;
}

StatusOr<std::string> VariantValue::to_json(cctz::time_zone timezone) {
    // Ensure value is reconstructed before converting to JSON
    if (_shredded_data.has_value() && !_shredded_data->is_reconstructed) {
        RETURN_IF_ERROR(ensure_value_reconstructed());
    }

    std::stringstream json_str;
    auto status = VariantUtil::variant_to_json(_metadata, _value, json_str, timezone);
    if (!status.ok()) {
        return status;
    }

    return json_str.str();
}

std::string VariantValue::to_string() {
    auto json_result = to_json();
    if (!json_result.ok()) {
        return "";
    }

    return json_result.value();
}

Status VariantValue::ensure_value_reconstructed() {
    if (!_shredded_data.has_value() || _shredded_data->is_reconstructed) {
        return Status::OK();
    }

    const auto& shredded = *_shredded_data;

    // Handle null cases according to Variant Shredding spec
    bool typed_value_is_null = shredded.typed_value.is_null();
    bool remain_value_is_null = shredded.remain_value.empty();

    if (!typed_value_is_null && !remain_value_is_null) {
        // Both non-null: partially shredded object (not supported yet)
        return Status::NotSupported(
                "Partially shredded objects (both typed_value and remain_value are non-null) are not supported");
    }

    if (typed_value_is_null && remain_value_is_null) {
        // Both null: value is missing, use Variant null
        uint8_t null_header = static_cast<uint8_t>(VariantPrimitiveType::NULL_TYPE) << 2;
        _value = std::string(reinterpret_cast<const char*>(&null_header), 1);
    } else if (!typed_value_is_null) {
        // typed_value is non-null, remain_value is null: reconstruct from typed_value
        RETURN_IF_ERROR(build_variant_value_from_primitive(shredded.typed_value, shredded.typed_value_type, _value));
    } else {
        // typed_value is null, remain_value is non-null: use remain_value directly
        _value = shredded.remain_value;
    }

    _shredded_data->is_reconstructed = true;
    return Status::OK();
}

Status VariantValue::build_variant_value_from_primitive(const Datum& typed_value, LogicalType type,
                                                        std::string& result) {
    // Helper to write little-endian values
    auto write_little_endian = [](std::string& buffer, auto value) {
        using T = decltype(value);
        T le_value = arrow::bit_util::ToLittleEndian(value);
        buffer.append(reinterpret_cast<const char*>(&le_value), sizeof(T));
    };

    // Reject non-primitive types
    if (type == TYPE_ARRAY || type == TYPE_STRUCT || type == TYPE_MAP) {
        return Status::NotSupported(strings::Substitute(
                "Complex typed_value (type: $0) is not supported. "
                "Only primitive types (INT, FLOAT, STRING, etc.) are supported.",
                type_to_string(type)));
    }

    // Build Variant binary for primitive types
    switch (type) {
    case TYPE_BOOLEAN: {
        bool val = typed_value.get_int8() != 0;
        uint8_t header = static_cast<uint8_t>(val ? VariantPrimitiveType::BOOLEAN_TRUE
                                                   : VariantPrimitiveType::BOOLEAN_FALSE)
                         << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        return Status::OK();
    }
    case TYPE_TINYINT: {
        int8_t val = typed_value.get_int8();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::INT8) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        result.append(reinterpret_cast<const char*>(&val), sizeof(int8_t));
        return Status::OK();
    }
    case TYPE_SMALLINT: {
        int16_t val = typed_value.get_int16();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::INT16) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, val);
        return Status::OK();
    }
    case TYPE_INT: {
        int32_t val = typed_value.get_int32();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::INT32) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, val);
        return Status::OK();
    }
    case TYPE_BIGINT: {
        int64_t val = typed_value.get_int64();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::INT64) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, val);
        return Status::OK();
    }
    case TYPE_FLOAT: {
        float val = typed_value.get_float();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::FLOAT) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, val);
        return Status::OK();
    }
    case TYPE_DOUBLE: {
        double val = typed_value.get_double();
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::DOUBLE) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, val);
        return Status::OK();
    }
    case TYPE_VARCHAR:
    case TYPE_CHAR: {
        const Slice& str = typed_value.get_slice();

        // Use short string optimization if possible (< 64 bytes)
        if (str.size < 64) {
            uint8_t header = (1 << 0) | (static_cast<uint8_t>(str.size) << 2); // basic_type=1 (short string)
            result = std::string(reinterpret_cast<const char*>(&header), 1);
            result.append(str.data, str.size);
        } else {
            uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::STRING) << 2;
            result = std::string(reinterpret_cast<const char*>(&header), 1);
            write_little_endian(result, static_cast<uint32_t>(str.size));
            result.append(str.data, str.size);
        }
        return Status::OK();
    }
    case TYPE_DATE: {
        DateValue date_val = typed_value.get_date();
        int32_t days = date_val.julian() - date::UNIX_EPOCH_JULIAN;
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::DATE) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, days);
        return Status::OK();
    }
    case TYPE_DATETIME: {
        TimestampValue ts_val = typed_value.get_timestamp();
        int hour, minute, second, usec;
        ts_val.to_time(&hour, &minute, &second, &usec);
        int64_t micros = ts_val.to_unix_second() * 1000000LL + usec;
        uint8_t header = static_cast<uint8_t>(VariantPrimitiveType::TIMESTAMP_NTZ) << 2;
        result = std::string(reinterpret_cast<const char*>(&header), 1);
        write_little_endian(result, micros);
        return Status::OK();
    }
    default:
        return Status::NotSupported(
                strings::Substitute("Unsupported type for Variant primitive: $0. "
                                    "Supported types: BOOLEAN, INT8/16/32/64, FLOAT, DOUBLE, STRING, DATE, DATETIME",
                                    type_to_string(type)));
    }
}

std::ostream& operator<<(std::ostream& os, VariantValue& value) {
    return os << value.to_string();
}

} // namespace starrocks