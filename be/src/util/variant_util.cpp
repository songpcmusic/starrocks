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

#include "variant_util.h"

#include <arrow/util/endian.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "exprs/cast_expr.h"
#include "formats/parquet/variant.h"
#include "runtime/decimalv3.h"
#include "types/timestamp_value.h"
#include "url_coding.h"

namespace starrocks {

uint32_t VariantUtil::readLittleEndianUnsigned(const void* from, uint8_t size) {
    uint32_t result = 0;
    memcpy(&result, from, size);
    return arrow::bit_util::FromLittleEndian(result);
}

std::string VariantUtil::type_to_string(VariantType type) {
    switch (type) {
    case VariantType::OBJECT:
        return "Object";
    case VariantType::ARRAY:
        return "Array";
    case VariantType::NULL_TYPE:
        return "Null";
    case VariantType::BOOLEAN:
        return "Boolean";
    case VariantType::INT8:
        return "Int8";
    case VariantType::INT16:
        return "Int16";
    case VariantType::INT32:
        return "Int32";
    case VariantType::INT64:
        return "Int64";
    case VariantType::DOUBLE:
        return "Double";
    case VariantType::DECIMAL4:
        return "Decimal4";
    case VariantType::DECIMAL8:
        return "Decimal8";
    case VariantType::DECIMAL16:
        return "Decimal16";
    case VariantType::DATE:
        return "Date";
    case VariantType::TIMESTAMP_TZ:
        return "TimestampTZ";
    case VariantType::TIMESTAMP_NTZ:
        return "TimestampNTZ";
    case VariantType::FLOAT:
        return "Float";
    case VariantType::BINARY:
        return "Binary";
    case VariantType::STRING:
        return "String";
    case VariantType::TIME_NTZ:
        return "TimeNTZ";
    case VariantType::TIMESTAMP_TZ_NANOS:
        return "TimestampTZNanos";
    case VariantType::TIMESTAMP_NTZ_NANOS:
        return "TimestampNTZNanos";
    case VariantType::UUID:
        return "UUID";
    default:
        return "Unknown";
    }
}

std::string epoch_day_to_date(int32_t epoch_days) {
    std::time_t raw_time = epoch_days * 86400; // to seconds
    std::tm* ptm = std::gmtime(&raw_time);     // to UTC
    char buffer[11];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", ptm);
    return buffer;
}

std::string VariantUtil::decimal4_to_string(DecimalValue<int32_t> decimal) {
    return DecimalV3Cast::to_string<int32_t>(decimal.value, decimal_precision_limit<int32_t>, decimal.scale);
}

std::string VariantUtil::decimal8_to_string(DecimalValue<int64_t> decimal) {
    return DecimalV3Cast::to_string<int64_t>(decimal.value, decimal_precision_limit<int64_t>, decimal.scale);
}

std::string VariantUtil::decimal16_to_string(DecimalValue<int128_t> decimal) {
    return DecimalV3Cast::to_string<int128_t>(decimal.value, decimal_precision_limit<int128_t>, decimal.scale);
}

void append_quoted_string(std::stringstream& ss, const std::string& str) {
    ss << '"' << str << '"';
}

Status VariantUtil::variant_to_json(std::string_view metadata, std::string_view value, std::stringstream& json_str,
                                    cctz::time_zone timezone) {
    Variant variant{metadata, value};
    switch (variant.type()) {
    case VariantType::NULL_TYPE:
        json_str << "null";
        break;
    case VariantType::BOOLEAN: {
        bool res = *variant.get_bool();
        json_str << (res ? "true" : "false");
        break;
    }
    case VariantType::INT8:
        json_str << std::to_string(*variant.get_int8());
        break;
    case VariantType::INT16:
        json_str << std::to_string(*variant.get_int16());
        break;
    case VariantType::INT32:
        json_str << std::to_string(*variant.get_int32());
        break;
    case VariantType::INT64:
        json_str << std::to_string(*variant.get_int64());
        break;
    case VariantType::FLOAT: {
        const float f = *variant.get_float();
        if (std::isfinite(f)) {
            json_str << std::to_string(f);
        } else {
            append_quoted_string(json_str, std::to_string(f));
        }
        break;
    }
    case VariantType::DOUBLE: {
        const double d = *variant.get_double();
        if (std::isfinite(d)) {
            json_str << std::to_string(d);
        } else {
            append_quoted_string(json_str, std::to_string(d));
        }
        break;
    }
    case VariantType::DECIMAL4: {
        DecimalValue<int32_t> decimal = *variant.get_decimal4();
        json_str << decimal4_to_string(decimal);
        break;
    }
    case VariantType::DECIMAL8: {
        DecimalValue<int64_t> decimal = *variant.get_decimal8();
        json_str << decimal8_to_string(decimal);
        break;
    }
    case VariantType::DECIMAL16: {
        DecimalValue<int128_t> decimal = *variant.get_decimal16();
        json_str << decimal16_to_string(decimal);
        break;
    }
    case VariantType::STRING: {
        append_quoted_string(json_str, std::string(*variant.get_string()));
        break;
    }
    case VariantType::BINARY: {
        const std::string_view binary = *variant.get_binary();
        const std::string binary_str(binary.data(), binary.size());
        std::string encoded;
        base64_encode(binary_str, &encoded);
        append_quoted_string(json_str, encoded);
        break;
    }
    case VariantType::UUID: {
        const auto uuid_arr = *variant.get_uuid();
        boost::uuids::uuid uuid{};
        for (size_t i = 0; i < uuid.size(); ++i) {
            uuid.data[i] = uuid_arr[i];
        }
        append_quoted_string(json_str, boost::uuids::to_string(uuid));
        break;
    }
    case VariantType::DATE: {
        int32_t date = *variant.get_date();
        std::string date_str = epoch_day_to_date(date);
        append_quoted_string(json_str, date_str);
        break;
    }
    case VariantType::TIMESTAMP_TZ: {
        const int64_t timestamp_micros = *variant.get_timestamp_micros();
        TimestampValue tsv{};
        tsv.from_unix_second(timestamp_micros / 1000000, timestamp_micros % 1000000);
        std::string timestamp_str = timestamp::to_string_with_timezone<false, false>(tsv.timestamp(), timezone);
        append_quoted_string(json_str, timestamp_str);
        break;
    }
    case VariantType::TIMESTAMP_NTZ: {
        const int64_t timestamp_micros = *variant.get_timestamp_micros_ntz();
        TimestampValue tsv{};
        tsv.from_unix_second(timestamp_micros / 1000000, timestamp_micros % 1000000);
        std::string timestamp_str = tsv.to_string(false);
        append_quoted_string(json_str, timestamp_str);
        break;
    }
    case VariantType::OBJECT: {
        auto info = get_object_info(value);
        if (!info.ok()) {
            return info.status();
        }
        const auto& [num_elements, id_start_offset, id_size, offset_start_offset, offset_size, data_start_offset] =
                info.value();
        json_str << "{";
        for (size_t i = 0; i < num_elements; ++i) {
            if (i > 0) {
                json_str << ",";
            }

            uint32_t id = readLittleEndianUnsigned(value.data() + id_start_offset + i * id_size, id_size);
            uint32_t offset =
                    readLittleEndianUnsigned(value.data() + offset_start_offset + i * offset_size, offset_size);
            auto key = variant.metadata().get_key(id);
            if (!key.ok()) {
                return key.status();
            }

            append_quoted_string(json_str, std::string(*key));
            json_str << ":";

            if (uint32_t next_pos = data_start_offset + offset; next_pos < value.size()) {
                std::string_view next_value = value.substr(next_pos, value.size() - next_pos);
                // Recursively convert the next value to JSON
                auto status = variant_to_json(metadata, next_value, json_str, timezone);
                if (!status.ok()) {
                    return status;
                }
            } else {
                return Status::InternalError("Invalid offset in object: " + std::to_string(offset));
            }
        }
        json_str << "}";
        break;
    }
    case VariantType::ARRAY: {
        auto info = get_array_info(value);
        if (!info.ok()) {
            return info.status();
        }

        const auto& [num_elements, offset_size, offset_start_offset, data_start_offset] = info.value();
        json_str << "[";
        for (size_t i = 0; i < num_elements; ++i) {
            if (i > 0) {
                json_str << ",";
            }

            uint32_t offset =
                    readLittleEndianUnsigned(value.data() + offset_start_offset + i * offset_size, offset_size);
            if (uint32_t next_pos = data_start_offset + offset; next_pos < value.size()) {
                std::string_view next_value = value.substr(next_pos, value.size() - next_pos);
                // Recursively convert the next value to JSON
                auto status = variant_to_json(metadata, next_value, json_str, timezone);
                if (!status.ok()) {
                    return status;
                }
            } else {
                return Status::InternalError("Invalid offset in array: " + std::to_string(offset));
            }
        }
        json_str << "]";
        break;
    }
    default:
        return Status::NotSupported("Unsupported variant type: " + type_to_string(variant.type()));
    }

    return Status::OK();
}

uint8_t VariantUtil::primitiveHeader(VariantPrimitiveType primitive) {
    return static_cast<uint8_t>(primitive) << 2;
}

// Helper function to write little-endian values
template <typename T>
static void write_little_endian(std::string& buffer, T value) {
    T le_value = arrow::bit_util::ToLittleEndian(value);
    buffer.append(reinterpret_cast<const char*>(&le_value), sizeof(T));
}

// Helper function to determine the minimum bytes needed to represent a value
// Note: Only 1, 2, and 4 bytes are valid (no 3-byte encoding in Variant format)
static uint8_t get_min_bytes_for_value(uint32_t value) {
    if (value <= 0xFF) return 1;
    if (value <= 0xFFFF) return 2;
    return 4;  // Skip 3 bytes, use 4 directly
}

// Helper function to convert byte size to encoding code
// 1 byte -> 0, 2 bytes -> 1, 4 bytes -> 2
static uint8_t size_to_code(uint8_t size) {
    if (size == 1) return 0;
    if (size == 2) return 1;
    return 2;  // size == 4
}

Status VariantUtil::build_variant_object(const std::map<std::string, std::pair<Datum, LogicalType>>& fields,
                                         std::string& metadata_out, std::string& value_out) {
    if (fields.empty()) {
        return Status::InvalidArgument("Cannot build empty Variant Object");
    }

    // Step 1: Build Metadata (dictionary of field names)
    // Metadata format:
    //   Header (1 byte): version(4 bits) | sorted(1 bit) | reserved(1 bit) | offset_size(2 bits)
    //   Dictionary size (1 or 4 bytes)
    //   Offset list (offset_size * dict_size bytes)
    //   String data

    std::vector<std::string> field_names;
    field_names.reserve(fields.size());
    for (const auto& [name, _] : fields) {
        field_names.push_back(name);
    }

    // Calculate total string data size
    uint32_t total_string_size = 0;
    for (const auto& name : field_names) {
        total_string_size += name.size();
    }

    // Determine offset_size (1, 2, or 4 bytes)
    uint8_t offset_size_bits = get_min_bytes_for_value(total_string_size);
    uint8_t offset_size_code = size_to_code(offset_size_bits);

    // Build metadata
    metadata_out.clear();

    // Header: version=1, sorted=1 (fields are sorted in map), offset_size
    uint8_t metadata_header = 0x01 | (1 << 4) | (offset_size_code << 6);
    metadata_out.push_back(metadata_header);

    // Dictionary size
    uint32_t dict_size = field_names.size();
    if (dict_size <= 0xFF) {
        metadata_out.push_back(static_cast<uint8_t>(dict_size));
    } else {
        write_little_endian(metadata_out, dict_size);
    }

    // Offset list + string data
    uint32_t current_offset = 0;
    std::string string_data;
    for (const auto& name : field_names) {
        // Write offset
        if (offset_size_bits == 1) {
            metadata_out.push_back(static_cast<uint8_t>(current_offset));
        } else if (offset_size_bits == 2) {
            write_little_endian(metadata_out, static_cast<uint16_t>(current_offset));
        } else {
            write_little_endian(metadata_out, current_offset);
        }

        // Append string data
        string_data.append(name);
        current_offset += name.size();
    }
    metadata_out.append(string_data);

    // Step 2: Build field values
    std::vector<std::string> field_values;
    field_values.reserve(fields.size());
    uint32_t total_data_size = 0;

    for (const auto& [name, field_data] : fields) {
        const auto& [datum, type] = field_data;
        std::string field_value;
        RETURN_IF_ERROR(VariantValue::build_variant_value_from_primitive(datum, type, field_value));
        total_data_size += field_value.size();
        field_values.push_back(std::move(field_value));
    }

    // Step 3: Build Object Value
    // Object format:
    //   Header (1 byte): basic_type(2 bits) | field_id_size-1(2 bits) | field_offset_size-1(2 bits) | is_large(1 bit)
    //   Num elements (1 or 4 bytes)
    //   Field IDs (field_id_size * num_elements)
    //   Field offsets (field_offset_size * (num_elements + 1))
    //   Field data

    uint32_t num_elements = fields.size();

    // Determine field_id_size and field_offset_size
    uint8_t field_id_size = get_min_bytes_for_value(num_elements - 1);  // IDs are 0-based
    uint8_t field_offset_size = get_min_bytes_for_value(total_data_size);

    // Determine if is_large (num_elements needs 4 bytes)
    bool is_large = (num_elements > 0xFF);

    // Build Object header
    uint8_t basic_type = static_cast<uint8_t>(BasicType::OBJECT);  // 0b10
    uint8_t field_id_size_code = size_to_code(field_id_size);
    uint8_t field_offset_size_code = size_to_code(field_offset_size);
    uint8_t object_header = basic_type |
                           (field_id_size_code << 2) |
                           (field_offset_size_code << 4) |
                           (is_large ? (1 << 6) : 0);

    value_out.clear();
    value_out.push_back(object_header);

    // Write num_elements
    if (is_large) {
        write_little_endian(value_out, num_elements);
    } else {
        value_out.push_back(static_cast<uint8_t>(num_elements));
    }

    // Write field IDs (0, 1, 2, ...)
    for (uint32_t i = 0; i < num_elements; ++i) {
        if (field_id_size == 1) {
            value_out.push_back(static_cast<uint8_t>(i));
        } else if (field_id_size == 2) {
            write_little_endian(value_out, static_cast<uint16_t>(i));
        } else {
            write_little_endian(value_out, i);
        }
    }

    // Write field offsets
    uint32_t current_data_offset = 0;
    for (size_t i = 0; i <= num_elements; ++i) {  // Note: num_elements + 1 offsets
        if (field_offset_size == 1) {
            value_out.push_back(static_cast<uint8_t>(current_data_offset));
        } else if (field_offset_size == 2) {
            write_little_endian(value_out, static_cast<uint16_t>(current_data_offset));
        } else {
            write_little_endian(value_out, current_data_offset);
        }

        if (i < num_elements) {
            current_data_offset += field_values[i].size();
        }
    }

    // Write field data
    for (const auto& field_value : field_values) {
        value_out.append(field_value);
    }

    return Status::OK();
}

Status VariantUtil::merge_variant_objects(std::string_view typed_metadata, std::string_view typed_value,
                                          std::string_view remain_metadata, std::string_view remain_value,
                                          std::string& merged_metadata_out, std::string& merged_value_out) {
    // Parse both objects
    Variant typed_variant(typed_metadata, typed_value);
    Variant remain_variant(remain_metadata, remain_value);

    if (typed_variant.type() != VariantType::OBJECT || remain_variant.type() != VariantType::OBJECT) {
        return Status::InvalidArgument("Both inputs must be Variant Objects");
    }

    // Get object info
    auto typed_info_result = get_object_info(typed_value);
    auto remain_info_result = get_object_info(remain_value);

    if (!typed_info_result.ok()) return typed_info_result.status();
    if (!remain_info_result.ok()) return remain_info_result.status();

    const auto& typed_info = typed_info_result.value();
    const auto& remain_info = remain_info_result.value();

    // Collect all fields (typed_value fields take precedence)
    std::map<std::string, std::string> merged_fields;  // field_name -> field_value_binary

    // Add typed_value fields
    for (uint32_t i = 0; i < typed_info.num_elements; ++i) {
        uint32_t field_id = readLittleEndianUnsigned(
            typed_value.data() + typed_info.id_start_offset + i * typed_info.id_size, typed_info.id_size);
        uint32_t current_offset = readLittleEndianUnsigned(
            typed_value.data() + typed_info.offset_start_offset + i * typed_info.offset_size, typed_info.offset_size);

        auto key_result = typed_variant.metadata().get_key(field_id);
        if (!key_result.ok()) return key_result.status();

        std::string field_name(key_result.value());
        uint32_t data_pos = typed_info.data_start_offset + current_offset;

        // Calculate field length
        uint32_t field_length;
        if (i == typed_info.num_elements - 1) {
            // Last field: use total size - current offset
            field_length = typed_value.size() - data_pos;
        } else {
            // Not last field: use next_offset - current_offset
            uint32_t next_offset = readLittleEndianUnsigned(
                typed_value.data() + typed_info.offset_start_offset + (i + 1) * typed_info.offset_size, typed_info.offset_size);
            field_length = next_offset - current_offset;
        }

        std::string_view field_value_binary = typed_value.substr(data_pos, field_length);
        merged_fields[field_name] = std::string(field_value_binary.data(), field_value_binary.size());
    }

    // Add remain_value fields (only if not already present)
    for (uint32_t i = 0; i < remain_info.num_elements; ++i) {
        uint32_t field_id = readLittleEndianUnsigned(
            remain_value.data() + remain_info.id_start_offset + i * remain_info.id_size, remain_info.id_size);
        uint32_t current_offset = readLittleEndianUnsigned(
            remain_value.data() + remain_info.offset_start_offset + i * remain_info.offset_size, remain_info.offset_size);

        auto key_result = remain_variant.metadata().get_key(field_id);
        if (!key_result.ok()) return key_result.status();

        std::string field_name(key_result.value());

        // Only add if not already in typed_value
        if (merged_fields.find(field_name) == merged_fields.end()) {
            uint32_t data_pos = remain_info.data_start_offset + current_offset;

            // Calculate field length
            uint32_t field_length;
            if (i == remain_info.num_elements - 1) {
                // Last field: use total size - current offset
                field_length = remain_value.size() - data_pos;
            } else {
                // Not last field: use next_offset - current_offset
                uint32_t next_offset = readLittleEndianUnsigned(
                    remain_value.data() + remain_info.offset_start_offset + (i + 1) * remain_info.offset_size, remain_info.offset_size);
                field_length = next_offset - current_offset;
            }

            std::string_view field_value_binary = remain_value.substr(data_pos, field_length);
            merged_fields[field_name] = std::string(field_value_binary.data(), field_value_binary.size());
        }
    }

    // Build merged metadata
    std::vector<std::string> field_names;
    field_names.reserve(merged_fields.size());
    for (const auto& [name, _] : merged_fields) {
        field_names.push_back(name);
    }

    // Calculate string data size
    uint32_t total_string_size = 0;
    for (const auto& name : field_names) {
        total_string_size += name.size();
    }

    uint8_t offset_size_bits = get_min_bytes_for_value(total_string_size);
    uint8_t offset_size_code = size_to_code(offset_size_bits);

    merged_metadata_out.clear();
    uint8_t metadata_header = 0x01 | (1 << 4) | (offset_size_code << 6);
    merged_metadata_out.push_back(metadata_header);

    uint32_t dict_size = field_names.size();
    if (dict_size <= 0xFF) {
        merged_metadata_out.push_back(static_cast<uint8_t>(dict_size));
    } else {
        write_little_endian(merged_metadata_out, dict_size);
    }

    uint32_t current_offset = 0;
    std::string string_data;
    for (const auto& name : field_names) {
        if (offset_size_bits == 1) {
            merged_metadata_out.push_back(static_cast<uint8_t>(current_offset));
        } else if (offset_size_bits == 2) {
            write_little_endian(merged_metadata_out, static_cast<uint16_t>(current_offset));
        } else {
            write_little_endian(merged_metadata_out, current_offset);
        }
        string_data.append(name);
        current_offset += name.size();
    }
    merged_metadata_out.append(string_data);

    // Build merged value
    uint32_t num_elements = merged_fields.size();
    uint32_t total_data_size = 0;
    for (const auto& [_, value] : merged_fields) {
        total_data_size += value.size();
    }

    uint8_t field_id_size = get_min_bytes_for_value(num_elements - 1);
    uint8_t field_offset_size = get_min_bytes_for_value(total_data_size);
    bool is_large = (num_elements > 0xFF);

    uint8_t basic_type = static_cast<uint8_t>(BasicType::OBJECT);
    uint8_t field_id_size_code = size_to_code(field_id_size);
    uint8_t field_offset_size_code = size_to_code(field_offset_size);
    uint8_t object_header = basic_type |
                           (field_id_size_code << 2) |
                           (field_offset_size_code << 4) |
                           (is_large ? (1 << 6) : 0);

    merged_value_out.clear();
    merged_value_out.push_back(object_header);

    if (is_large) {
        write_little_endian(merged_value_out, num_elements);
    } else {
        merged_value_out.push_back(static_cast<uint8_t>(num_elements));
    }

    // Write field IDs
    for (uint32_t i = 0; i < num_elements; ++i) {
        if (field_id_size == 1) {
            merged_value_out.push_back(static_cast<uint8_t>(i));
        } else if (field_id_size == 2) {
            write_little_endian(merged_value_out, static_cast<uint16_t>(i));
        } else {
            write_little_endian(merged_value_out, i);
        }
    }

    // Write field offsets
    uint32_t current_data_offset = 0;
    size_t field_idx = 0;
    for (const auto& [_, value] : merged_fields) {
        if (field_offset_size == 1) {
            merged_value_out.push_back(static_cast<uint8_t>(current_data_offset));
        } else if (field_offset_size == 2) {
            write_little_endian(merged_value_out, static_cast<uint16_t>(current_data_offset));
        } else {
            write_little_endian(merged_value_out, current_data_offset);
        }
        current_data_offset += value.size();
        field_idx++;
    }
    // Write final offset
    if (field_offset_size == 1) {
        merged_value_out.push_back(static_cast<uint8_t>(current_data_offset));
    } else if (field_offset_size == 2) {
        write_little_endian(merged_value_out, static_cast<uint16_t>(current_data_offset));
    } else {
        write_little_endian(merged_value_out, current_data_offset);
    }

    // Write field data
    for (const auto& [_, value] : merged_fields) {
        merged_value_out.append(value);
    }

    return Status::OK();
}

Status VariantUtil::build_variant_object_with_metadata(
        const std::map<std::string, std::pair<Datum, LogicalType>>& fields,
        std::string_view existing_metadata, std::string& value_out) {

    if (fields.empty()) {
        return Status::InvalidArgument("Cannot build Variant Object with empty fields");
    }

    // Parse existing metadata to get field name -> field ID mapping
    VariantMetadata metadata(existing_metadata);

    // Build field_id -> field_value_binary mapping
    std::map<uint32_t, std::string> id_to_value;

    for (const auto& [field_name, field_data] : fields) {
        const auto& [datum, type] = field_data;

        // Get field_id from metadata
        uint32_t field_id = metadata.get_index(field_name);
        if (field_id == static_cast<uint32_t>(-1)) {
            return Status::InvalidArgument(fmt::format("Field '{}' not found in metadata", field_name));
        }

        // Build field value binary
        std::string field_value_binary;
        RETURN_IF_ERROR(VariantValue::build_variant_value_from_primitive(datum, type, field_value_binary));

        id_to_value[field_id] = std::move(field_value_binary);
    }

    // Build Object value using sorted field IDs
    uint32_t num_elements = id_to_value.size();
    uint32_t max_field_id = id_to_value.rbegin()->first;  // Largest field_id

    // Calculate total data size
    uint32_t total_data_size = 0;
    for (const auto& [_, value] : id_to_value) {
        total_data_size += value.size();
    }

    // Determine field_id_size and field_offset_size
    uint8_t field_id_size = get_min_bytes_for_value(max_field_id);
    uint8_t field_offset_size = get_min_bytes_for_value(total_data_size);

    // Determine if is_large
    bool is_large = (num_elements > 0xFF);

    // Build Object header
    uint8_t basic_type = static_cast<uint8_t>(BasicType::OBJECT);
    uint8_t field_id_size_code = size_to_code(field_id_size);
    uint8_t field_offset_size_code = size_to_code(field_offset_size);
    uint8_t object_header = basic_type |
                           (field_id_size_code << 2) |
                           (field_offset_size_code << 4) |
                           (is_large ? (1 << 6) : 0);

    value_out.clear();
    value_out.push_back(object_header);

    // Write num_elements
    if (is_large) {
        write_little_endian(value_out, num_elements);
    } else {
        value_out.push_back(static_cast<uint8_t>(num_elements));
    }

    // Write field IDs (sorted)
    for (const auto& [field_id, _] : id_to_value) {
        if (field_id_size == 1) {
            value_out.push_back(static_cast<uint8_t>(field_id));
        } else if (field_id_size == 2) {
            write_little_endian(value_out, static_cast<uint16_t>(field_id));
        } else {
            write_little_endian(value_out, field_id);
        }
    }

    // Write field offsets
    uint32_t current_data_offset = 0;
    for (const auto& [_, value] : id_to_value) {
        if (field_offset_size == 1) {
            value_out.push_back(static_cast<uint8_t>(current_data_offset));
        } else if (field_offset_size == 2) {
            write_little_endian(value_out, static_cast<uint16_t>(current_data_offset));
        } else {
            write_little_endian(value_out, current_data_offset);
        }
        current_data_offset += value.size();
    }
    // Write final offset
    if (field_offset_size == 1) {
        value_out.push_back(static_cast<uint8_t>(current_data_offset));
    } else if (field_offset_size == 2) {
        write_little_endian(value_out, static_cast<uint16_t>(current_data_offset));
    } else {
        write_little_endian(value_out, current_data_offset);
    }

    // Write field data (in sorted field_id order)
    for (const auto& [_, value] : id_to_value) {
        value_out.append(value);
    }

    return Status::OK();
}

Status VariantUtil::merge_variant_objects_same_metadata(std::string_view typed_value, std::string_view remain_value,
                                                         std::string_view metadata, std::string& merged_value_out) {
    // Parse both values
    auto typed_info_result = get_object_info(typed_value);
    auto remain_info_result = get_object_info(remain_value);

    if (!typed_info_result.ok()) return typed_info_result.status();
    if (!remain_info_result.ok()) return remain_info_result.status();

    const auto& typed_info = typed_info_result.value();
    const auto& remain_info = remain_info_result.value();

    VariantMetadata variant_metadata(metadata);

    // Collect all fields: field_id -> field_value_binary
    std::map<uint32_t, std::string> merged_fields;

    // Add typed_value fields
    for (uint32_t i = 0; i < typed_info.num_elements; ++i) {
        uint32_t field_id = readLittleEndianUnsigned(
            typed_value.data() + typed_info.id_start_offset + i * typed_info.id_size, typed_info.id_size);
        uint32_t current_offset = readLittleEndianUnsigned(
            typed_value.data() + typed_info.offset_start_offset + i * typed_info.offset_size, typed_info.offset_size);

        uint32_t data_pos = typed_info.data_start_offset + current_offset;

        // Calculate field length
        uint32_t field_length;
        if (i == typed_info.num_elements - 1) {
            field_length = typed_value.size() - data_pos;
        } else {
            uint32_t next_offset = readLittleEndianUnsigned(
                typed_value.data() + typed_info.offset_start_offset + (i + 1) * typed_info.offset_size, typed_info.offset_size);
            field_length = next_offset - current_offset;
        }

        std::string_view field_value_binary = typed_value.substr(data_pos, field_length);
        merged_fields[field_id] = std::string(field_value_binary);
    }

    // Add remain_value fields (only if not already present)
    for (uint32_t i = 0; i < remain_info.num_elements; ++i) {
        uint32_t field_id = readLittleEndianUnsigned(
            remain_value.data() + remain_info.id_start_offset + i * remain_info.id_size, remain_info.id_size);

        // Only add if not already in typed_value
        if (merged_fields.find(field_id) == merged_fields.end()) {
            uint32_t current_offset = readLittleEndianUnsigned(
                remain_value.data() + remain_info.offset_start_offset + i * remain_info.offset_size, remain_info.offset_size);

            uint32_t data_pos = remain_info.data_start_offset + current_offset;

            // Calculate field length
            uint32_t field_length;
            if (i == remain_info.num_elements - 1) {
                field_length = remain_value.size() - data_pos;
            } else {
                uint32_t next_offset = readLittleEndianUnsigned(
                    remain_value.data() + remain_info.offset_start_offset + (i + 1) * remain_info.offset_size, remain_info.offset_size);
                field_length = next_offset - current_offset;
            }

            std::string_view field_value_binary = remain_value.substr(data_pos, field_length);
            merged_fields[field_id] = std::string(field_value_binary);
        }
    }

    // Build merged value
    uint32_t num_elements = merged_fields.size();
    uint32_t max_field_id = merged_fields.rbegin()->first;

    uint32_t total_data_size = 0;
    for (const auto& [_, value] : merged_fields) {
        total_data_size += value.size();
    }

    uint8_t field_id_size = get_min_bytes_for_value(max_field_id);
    uint8_t field_offset_size = get_min_bytes_for_value(total_data_size);
    bool is_large = (num_elements > 0xFF);

    uint8_t basic_type = static_cast<uint8_t>(BasicType::OBJECT);
    uint8_t field_id_size_code = size_to_code(field_id_size);
    uint8_t field_offset_size_code = size_to_code(field_offset_size);
    uint8_t object_header = basic_type |
                           (field_id_size_code << 2) |
                           (field_offset_size_code << 4) |
                           (is_large ? (1 << 6) : 0);

    merged_value_out.clear();
    merged_value_out.push_back(object_header);

    if (is_large) {
        write_little_endian(merged_value_out, num_elements);
    } else {
        merged_value_out.push_back(static_cast<uint8_t>(num_elements));
    }

    // Write field IDs (sorted)
    for (const auto& [field_id, _] : merged_fields) {
        if (field_id_size == 1) {
            merged_value_out.push_back(static_cast<uint8_t>(field_id));
        } else if (field_id_size == 2) {
            write_little_endian(merged_value_out, static_cast<uint16_t>(field_id));
        } else {
            write_little_endian(merged_value_out, field_id);
        }
    }

    // Write field offsets
    uint32_t current_data_offset = 0;
    for (const auto& [_, value] : merged_fields) {
        if (field_offset_size == 1) {
            merged_value_out.push_back(static_cast<uint8_t>(current_data_offset));
        } else if (field_offset_size == 2) {
            write_little_endian(merged_value_out, static_cast<uint16_t>(current_data_offset));
        } else {
            write_little_endian(merged_value_out, current_data_offset);
        }
        current_data_offset += value.size();
    }
    // Write final offset
    if (field_offset_size == 1) {
        merged_value_out.push_back(static_cast<uint8_t>(current_data_offset));
    } else if (field_offset_size == 2) {
        write_little_endian(merged_value_out, static_cast<uint16_t>(current_data_offset));
    } else {
        write_little_endian(merged_value_out, current_data_offset);
    }

    // Write field data (in sorted field_id order)
    for (const auto& [_, value] : merged_fields) {
        merged_value_out.append(value);
    }

    return Status::OK();
}

} // namespace starrocks
