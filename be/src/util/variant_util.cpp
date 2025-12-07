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

#include "column/column.h"
#include "exprs/cast_expr.h"
#include "formats/parquet/variant.h"
#include "formats/parquet/variant_builder.h"
#include "glog/logging.h"
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
        json_str << *variant.get_string();
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

            json_str << *key << ":";

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

std::unique_ptr<VariantUtil::VariantSchema> VariantUtil::VariantSchema::createScalar(LogicalType type) {
    auto schema = std::make_unique<VariantSchema>();
    schema->num_fields = 1;

    schema->scalar_schema = std::make_unique<ScalarSchema>();
    schema->scalar_schema->type = type;

    return schema;
}

std::unique_ptr<VariantUtil::VariantSchema> VariantUtil::VariantSchema::createArray(std::unique_ptr<VariantSchema> element_schema) {
    auto schema = std::make_unique<VariantSchema>();
    schema->num_fields = element_schema->num_fields;

    schema->array_schema = std::make_unique<ArraySchema>();
    schema->array_schema->element_schema = std::move(element_schema);

    return schema;
}

std::unique_ptr<VariantUtil::VariantSchema> VariantUtil::VariantSchema::createObject(std::vector<ObjectSchema::FieldSchema> fields) {
    auto schema = std::make_unique<VariantSchema>();
    schema->num_fields = fields.size();

    schema->object_schema = std::make_unique<ObjectSchema>();
    schema->object_schema->fields = std::move(fields);

    for (size_t i = 0; i < schema->object_schema->fields.size(); i++) {
        const auto& field = schema->object_schema->fields[i];
        schema->object_schema->field_map[field.field_name] = static_cast<int>(i);
    }

    return schema;
}

StatusOr<VariantValue> VariantUtil::assembleVariant(
    size_t row,
    const StructColumn* variant_column,
    const VariantSchema& schema) {

    auto metadata_column = variant_column->field_column("metadata");
    if (!metadata_column || row >= metadata_column->size()) {
        return Status::InvalidArgument("Invalid metadata column or row index");
    }

    auto* metadata_nullable_col = down_cast<const NullableColumn*>(metadata_column.get());

    if (metadata_nullable_col->is_null(row)) {
        return Status::InvalidArgument("Metadata is null at row " + std::to_string(row));
    }

    auto* metadata_binary_col = down_cast<const BinaryColumn*>(metadata_nullable_col->data_column().get());
    Slice metadata_slice = metadata_binary_col->get_slice(row);
    std::string_view metadata(metadata_slice.data, metadata_slice.size);

    parquet::VariantBuilder builder;

    RETURN_IF_ERROR(rebuild(row, metadata, *variant_column, schema, builder));

    return builder.result();
}

Status VariantUtil::rebuild(
    size_t row,
    std::string_view metadata,
    const StructColumn& column,
    const VariantSchema& schema,
    parquet::VariantBuilder& builder) {

    const char* schema_type = schema.scalar_schema ? "SCALAR" :
                             schema.array_schema ? "ARRAY" :
                             schema.object_schema ? "OBJECT" : "UNKNOWN";
    LOG(INFO) << "[rebuild] ENTRY: row=" << row
              << ", column.size()=" << column.size()
              << ", schema=" << schema_type;

    ColumnPtr typed_value_column = column.fields()[schema.typed_value_column_index];
    ColumnPtr value_column =  column.fields()[schema.value_column_index];

    bool has_typed_value = !typed_value_column->is_null(row);
    bool has_value = !value_column->is_null(row);

    LOG(INFO) << "[rebuild] row=" << row
              << " | typed_value: " << typed_value_column->get_name()
              << "[" << typed_value_column->size() << "]"
              << ", has=" << has_typed_value
              << " | value: " << (value_column ? value_column->get_name() : "null")
              << "[" << (value_column ? value_column->size() : 0) << "]"
              << ", has=" << has_value;

    bool typed_value_is_null = typed_value_column->is_null(row);
    if (!typed_value_is_null) {

        if (schema.scalar_schema != nullptr) {
            const auto& scalar = *schema.scalar_schema;
            auto* nullable_column = down_cast<const NullableColumn*>(typed_value_column.get());

            LOG(INFO) << "[rebuild] SCALAR type: " << logical_type_to_string(scalar.type);

            switch (scalar.type) {
                case TYPE_VARCHAR:
                case TYPE_CHAR: {
                    auto* str_col = down_cast<const BinaryColumn*>(nullable_column->data_column().get());
                    Slice slice = str_col->get_slice(row);

                    LOG(INFO) << "[rebuild] VARCHAR: row=" << row
                              << ", slice.size=" << slice.size
                              << ", slice.data ptr=" << (void*)slice.data;

                    if (slice.size > 100 * 1024 * 1024) {
                        LOG(ERROR) << "[rebuild] VARCHAR size too large: " << slice.size
                                  << " bytes, this is abnormal!";
                        return Status::InvalidArgument("VARCHAR size exceeds 100MB limit");
                    }

                    std::string str_value(slice.data, slice.size);

                    std::string preview = str_value.length() > 100 ?
                        str_value.substr(0, 100) + "..." : str_value;
                    LOG(INFO) << "[rebuild] VARCHAR value preview: '" << preview << "'";

                    builder.appendString(str_value);
                    break;
                }
                case TYPE_TINYINT: {
                    auto* int_col = down_cast<const Int8Column*>(nullable_column->data_column().get());
                    builder.appendLong(static_cast<int64_t>(int_col->get_data()[row]));
                    break;
                }
                case TYPE_SMALLINT: {
                    auto* int_col = down_cast<const Int16Column*>(nullable_column->data_column().get());
                    builder.appendLong(static_cast<int64_t>(int_col->get_data()[row]));
                    break;
                }
                case TYPE_INT: {
                    auto* int_col = down_cast<const Int32Column*>(nullable_column->data_column().get());
                    int32_t int_value = int_col->get_data()[row];
                    LOG(INFO) << "[rebuild] INT value: " << int_value;
                    builder.appendLong(static_cast<int64_t>(int_value));
                    break;
                }
                case TYPE_BIGINT: {
                    auto* int_col = down_cast<const Int64Column*>(nullable_column->data_column().get());
                    int64_t bigint_value = int_col->get_data()[row];
                    LOG(INFO) << "[rebuild] BIGINT value: " << bigint_value;
                    builder.appendLong(bigint_value);
                    break;
                }
                case TYPE_FLOAT: {
                    auto* float_col = down_cast<const FloatColumn*>(nullable_column->data_column().get());
                    builder.appendFloat(float_col->get_data()[row]);
                    break;
                }
                case TYPE_DOUBLE: {
                    auto* double_col = down_cast<const DoubleColumn*>(nullable_column->data_column().get());
                    builder.appendDouble(double_col->get_data()[row]);
                    break;
                }
                case TYPE_BOOLEAN: {
                    auto* bool_col = down_cast<const BooleanColumn*>(nullable_column->data_column().get());
                    bool bool_value = bool_col->get_data()[row];
                    LOG(INFO) << "[rebuild] BOOLEAN value: " << (bool_value ? "true" : "false");
                    builder.appendBoolean(bool_value);
                    break;
                }
                case TYPE_BINARY: {
                    auto* binary_col = down_cast<const BinaryColumn*>(nullable_column->data_column().get());
                    Slice slice = binary_col->get_slice(row);
                    std::vector<uint8_t> binary_data(slice.data, slice.data + slice.size);
                    builder.appendBinary(binary_data);
                    break;
                }
                default:
                    LOG(WARNING) << "[VariantUtil::rebuild] Unsupported scalar type: "
                                << logical_type_to_string(scalar.type);
                    builder.appendNull();
                    break;
            }
        } else if (schema.array_schema != nullptr) {
            return Status::NotSupported("Array schema processing is not yet implemented");
        } else if (schema.object_schema != nullptr) {
            const auto& object_schema = *schema.object_schema;
            std::vector<parquet::FieldEntry> fields;
            int start = builder.getWritePos();

            LOG(INFO) << "[rebuild] OBJECT: row=" << row
                      << ", num_fields=" << object_schema.fields.size();

            NullableColumn* typed_nullable_column = down_cast<NullableColumn*>(typed_value_column.get());
            StructColumn* typed_struct_column = down_cast<StructColumn*>(typed_nullable_column->data_column().get());

            LOG(INFO) << "[rebuild] typed_struct: " << typed_struct_column->get_name()
                      << "[" << typed_struct_column->size() << "]"
                      << ", num_columns=" << typed_struct_column->fields_column().size();

            for (const auto& field_schema : object_schema.fields) {
                const std::string& field_name = field_schema.field_name;
                const auto& field_variant_schema = *field_schema.schema;

                NullableColumn* field_nullable_column = down_cast<NullableColumn*>(typed_struct_column->field_column(field_name).get());
                StructColumn* field_column = down_cast<StructColumn*>(field_nullable_column->data_column().get());

                LOG(INFO) << "[rebuild] field='" << field_name << "'"
                          << " | nullable: " << field_nullable_column->get_name()
                          << "[" << field_nullable_column->size() << "]"
                          << ", null=" << field_nullable_column->is_null(row)
                          << " | struct: " << field_column->get_name()
                          << "[" << field_column->size() << "]"
                          << ", fields=" << field_column->fields_column().size();

                bool has_typed_data = !field_column->fields()[field_variant_schema.typed_value_column_index]->is_null(row);
                bool has_value_data = !field_column->fields()[field_variant_schema.value_column_index]->is_null(row);

                if (has_typed_data || has_value_data) {
                    int id = builder.addKey(field_name);
                    fields.emplace_back(field_name, id, builder.getWritePos() - start);

                    Status status = rebuild(row, metadata, *field_column, field_variant_schema, builder);
                    if (status.is_cancelled()) {
                        fields.pop_back();
                        LOG(INFO) << "[rebuild] Remove field: '" << field_name << "' (child returned no content)";
                    } else if (!status.ok()) {
                        return status;
                    }
                } else {
                    LOG(INFO) << "[rebuild] SKIP field: '" << field_name << "' (no data)";
                }
            }

            if (schema.metadata_column_index != static_cast<size_t>(-1) && fields.size() == 0) {
                typed_value_is_null = true;
            } else {
                if (fields.size() == 0 && value_column->is_null(row)) {
                    LOG(INFO) << "[rebuild] Empty OBJECT (no fields), returning Cancelled";
                    return Status::Cancelled("Object has no fields");
                }

                if (!value_column->is_null(row)) {
                    LOG(INFO) << "[rebuild] Entered leftover fields processing";

                    auto* value_nullable_col = down_cast<const NullableColumn*>(value_column.get());
                    auto* value_binary_col = down_cast<const BinaryColumn*>(value_nullable_col->data_column().get());
                    Slice slice = value_binary_col->get_slice(row);
                    std::string value_data(slice.data, slice.size);

                    LOG(INFO) << "[rebuild] value_data.size()=" << value_data.size()
                              << ", preview=" << (value_data.size() > 50 ? value_data.substr(0, 50) : value_data);

                    try {
                        Variant v(metadata, value_data);
                        if (v.type() != VariantType::OBJECT) {
                            return Status::InvalidArgument("[VariantUtil::rebuild] Malformed variant: value must be OBJECT type");
                        }

                        ASSIGN_OR_RETURN(uint32_t num_elements, v.num_elements());
                        LOG(INFO) << "[rebuild] num_elements=" << num_elements;

                        for (uint32_t i = 0; i < num_elements; ++i) {
                            auto field_result = v.get_field_at_index(i);
                            if (!field_result.ok()) {
                                return Status::InvalidArgument(fmt::format(
                                    "[VariantUtil::rebuild] Failed to get field at index {}: {}", i, field_result.status().message()));
                            }

                            auto [field_key, field_variant] = field_result.value();
                            if (object_schema.field_map.find(std::string(field_key)) != object_schema.field_map.end()) {
                                return Status::InvalidArgument(fmt::format(
                                    "[VariantUtil::rebuild] Malformed variant: field '{}' should be in typed_value, not in value", field_key));
                            }

                            LOG(INFO) << "[rebuild] Field '" << field_key << "' is leftover, processing...";

                            int id = builder.addKey(std::string(field_key));
                            fields.emplace_back(std::string(field_key), id, builder.getWritePos() - start);
                            builder.appendVariant(field_variant);
                        }
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "[rebuild] Exception caught when processing leftover fields: " << e.what();
                    }
                }

                builder.finishWritingObject(start, fields);
            }
        }
        else {
            return Status::InvalidArgument("[VariantUtil::rebuild] Invalid schema: no scalar/array/object schema");
        }
    }

    if (typed_value_is_null) {
        if (!value_column->is_null(row)) {
            auto* value_nullable_col = down_cast<NullableColumn*>(value_column.get());
            auto* value_binary_col = down_cast<const BinaryColumn*>(value_nullable_col->data_column().get());
            Slice slice = value_binary_col->get_slice(row);
            Variant variant(metadata, std::string_view(slice.data, slice.size));
            builder.appendVariant(variant);
        } else {
            LOG(ERROR) << "[rebuild] Malformed variant detected:"
                       << "\n  row=" << row
                       << "\n  column.size()=" << column.size()
                       << "\n  schema=" << schema_type
                       << "\n  typed_value: " << typed_value_column->get_name()
                       << "[" << typed_value_column->size() << "], has=" << has_typed_value
                       << "\n  value: " << (value_column ? value_column->get_name() : "null")
                       << "[" << (value_column ? value_column->size() : 0) << "], has=" << has_value
                       << "\n  metadata.size()=" << metadata.size();

            return Status::InvalidArgument("[VariantUtil::rebuild] Malformed variant: both typed_value and value are null");
        }
    }

    return Status::OK();
}

} // namespace starrocks
