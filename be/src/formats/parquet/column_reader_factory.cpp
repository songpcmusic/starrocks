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

#include "formats/parquet/column_reader_factory.h"

#include "formats/parquet/complex_column_reader.h"
#include "formats/parquet/scalar_column_reader.h"
#include "formats/parquet/schema.h"
#include "formats/utils.h"

namespace starrocks::parquet {

StatusOr<ColumnReaderPtr> ColumnReaderFactory::create(const ColumnReaderOptions& opts, const ParquetField* field,
                                                      const TypeDescriptor& col_type) {
    // We will only set a complex type in ParquetField
    if ((field->is_complex_type() || col_type.is_complex_type()) && !field->has_same_complex_type(col_type)) {
        return Status::InternalError(
                strings::Substitute("ParquetField '$0' file's type $1 is different from table's type $2", field->name,
                                    column_type_to_string(field->type), logical_type_to_string(col_type.type)));
    }
    if (field->type == ColumnType::ARRAY) {
        ASSIGN_OR_RETURN(ColumnReaderPtr child_reader,
                         ColumnReaderFactory::create(opts, &field->children[0], col_type.children[0]));
        if (child_reader != nullptr) {
            return std::make_unique<ListColumnReader>(field, std::move(child_reader));
        } else {
            return nullptr;
        }
    } else if (field->type == ColumnType::MAP) {
        std::unique_ptr<ColumnReader> key_reader = nullptr;
        std::unique_ptr<ColumnReader> value_reader = nullptr;

        if (!col_type.children[0].is_unknown_type()) {
            ASSIGN_OR_RETURN(key_reader,
                             ColumnReaderFactory::create(opts, &(field->children[0]), col_type.children[0]));
        }
        if (!col_type.children[1].is_unknown_type()) {
            ASSIGN_OR_RETURN(value_reader,
                             ColumnReaderFactory::create(opts, &field->children[1], col_type.children[1]));
        }

        if (key_reader != nullptr || value_reader != nullptr) {
            return std::make_unique<MapColumnReader>(field, std::move(key_reader), std::move(value_reader));
        } else {
            return nullptr;
        }
    } else if (field->type == ColumnType::STRUCT) {
        if (col_type.type == LogicalType::TYPE_VARIANT) {
            return create_variant_column_reader(opts, field);
        }

        std::vector<int32_t> subfield_pos(col_type.children.size());
        get_subfield_pos_with_pruned_type(*field, col_type, opts.case_sensitive, subfield_pos);

        std::map<std::string, ColumnReaderPtr> children_readers;
        for (size_t i = 0; i < col_type.children.size(); i++) {
            if (subfield_pos[i] == -1) {
                // -1 means subfield not existed; we need to emplace nullptr
                children_readers.emplace(col_type.field_names[i], nullptr);
                continue;
            }
            ASSIGN_OR_RETURN(
                    ColumnReaderPtr child_reader,
                    ColumnReaderFactory::create(opts, &field->children[subfield_pos[i]], col_type.children[i]));
            children_readers.emplace(col_type.field_names[i], std::move(child_reader));
        }

        // maybe struct subfield ColumnReader is null
        if (_has_valid_subfield_column_reader(children_readers)) {
            return std::make_unique<StructColumnReader>(field, std::move(children_readers));
        } else {
            return nullptr;
        }
    } else {
        return std::make_unique<ScalarColumnReader>(field, &opts.row_group_meta->columns[field->physical_column_index],
                                                    &col_type, opts);
    }
}

StatusOr<ColumnReaderPtr> ColumnReaderFactory::create(const ColumnReaderOptions& opts, const ParquetField* field,
                                                      const TypeDescriptor& col_type,
                                                      const TIcebergSchemaField* lake_schema_field) {
    // We will only set a complex type in ParquetField
    if ((field->is_complex_type() || col_type.is_complex_type()) && !field->has_same_complex_type(col_type)) {
        return Status::InternalError(
                strings::Substitute("ParquetField '$0' file's type $1 is different from table's type $2", field->name,
                                    column_type_to_string(field->type), logical_type_to_string(col_type.type)));
    }
    DCHECK(lake_schema_field != nullptr);
    if (field->type == ColumnType::ARRAY) {
        const TIcebergSchemaField* element_schema = &lake_schema_field->children[0];
        ASSIGN_OR_RETURN(ColumnReaderPtr child_reader,
                         ColumnReaderFactory::create(opts, &field->children[0], col_type.children[0], element_schema));
        if (child_reader != nullptr) {
            return std::make_unique<ListColumnReader>(field, std::move(child_reader));
        } else {
            return nullptr;
        }
    } else if (field->type == ColumnType::MAP) {
        std::unique_ptr<ColumnReader> key_reader = nullptr;
        std::unique_ptr<ColumnReader> value_reader = nullptr;

        const TIcebergSchemaField* key_lake_schema = &lake_schema_field->children[0];
        const TIcebergSchemaField* value_lake_schema = &lake_schema_field->children[1];

        if (!col_type.children[0].is_unknown_type()) {
            ASSIGN_OR_RETURN(key_reader, ColumnReaderFactory::create(opts, &(field->children[0]), col_type.children[0],
                                                                     key_lake_schema));
        }
        if (!col_type.children[1].is_unknown_type()) {
            ASSIGN_OR_RETURN(value_reader, ColumnReaderFactory::create(opts, &(field->children[1]),
                                                                       col_type.children[1], value_lake_schema));
        }

        if (key_reader != nullptr || value_reader != nullptr) {
            return std::make_unique<MapColumnReader>(field, std::move(key_reader), std::move(value_reader));
        } else {
            return nullptr;
        }
    } else if (field->type == ColumnType::STRUCT) {
        if (col_type.type == LogicalType::TYPE_VARIANT) {
            return create_variant_column_reader(opts, field);
        }

        std::vector<int32_t> subfield_pos(col_type.children.size());
        std::vector<const TIcebergSchemaField*> lake_schema_subfield(col_type.children.size());
        get_subfield_pos_with_pruned_type(*field, col_type, opts.case_sensitive, lake_schema_field, subfield_pos,
                                          lake_schema_subfield);

        std::map<std::string, std::unique_ptr<ColumnReader>> children_readers;
        for (size_t i = 0; i < col_type.children.size(); i++) {
            if (subfield_pos[i] == -1) {
                // -1 means subfield not existed; we need to emplace nullptr
                children_readers.emplace(col_type.field_names[i], nullptr);
                continue;
            }

            ASSIGN_OR_RETURN(ColumnReaderPtr child_reader,
                             ColumnReaderFactory::create(opts, &field->children[subfield_pos[i]], col_type.children[i],
                                                         lake_schema_subfield[i]));
            children_readers.emplace(col_type.field_names[i], std::move(child_reader));
        }

        // maybe struct subfield ColumnReader is null
        if (_has_valid_subfield_column_reader(children_readers)) {
            return std::make_unique<StructColumnReader>(field, std::move(children_readers));
        } else {
            return nullptr;
        }
    } else {
        return std::make_unique<ScalarColumnReader>(field, &opts.row_group_meta->columns[field->physical_column_index],
                                                    &col_type, opts);
    }
}

StatusOr<ColumnReaderPtr> ColumnReaderFactory::create_variant_column_reader(const ColumnReaderOptions& opts,
                                                                            const ParquetField* variant_field) {
    DCHECK(opts.row_group_meta != nullptr);
    DCHECK(variant_field->type == ColumnType::STRUCT);
    DCHECK(variant_field->children.size() >= 2);

    int metadata_index = -1;
    int value_index = -1;
    int typed_value_index = -1;

    for (size_t i = 0; i < variant_field->children.size(); ++i) {
        const auto& child = variant_field->children[i];
        if (child.name == "metadata") {
            metadata_index = i;
        } else if (child.name == "value") {
            value_index = i;
        } else if (child.name == "typed_value") {
            typed_value_index = i;
        }
    }

    if (metadata_index == -1 || value_index == -1) {
        return Status::InvalidArgument("Variant type must have 'metadata' and 'value' fields");
    }

    const tparquet::ColumnChunk* column_chunks = opts.row_group_meta->columns.data();
    const ParquetField* metadata_field = &variant_field->children[metadata_index];
    const ParquetField* value_field = &variant_field->children[value_index];

    auto metadata_reader = std::make_unique<ScalarColumnReader>(
            metadata_field, &(column_chunks[metadata_field->physical_column_index]), &TYPE_VARBINARY_DESC, opts);
    auto value_reader = std::make_unique<ScalarColumnReader>(
            value_field, &(column_chunks[value_field->physical_column_index]), &TYPE_VARBINARY_DESC, opts);

    // Check if this is a Shredding Variant (has typed_value field)
    if (typed_value_index != -1) {
        const ParquetField* typed_value_field = &variant_field->children[typed_value_index];
        LOG(INFO) << "[Variant] Detected Shredding Variant with typed_value field";

        // Check if typed_value is a STRUCT (Object Shredding)
        if (typed_value_field->type == ColumnType::STRUCT) {
            LOG(INFO) << "[Variant] Object Shredding detected, typed_value is STRUCT with "
                      << typed_value_field->children.size() << " fields";
            // For Object Shredding, create a StructColumnReader for typed_value
            // Build TypeDescriptor for the STRUCT
            std::vector<std::string> field_names;
            std::vector<TypeDescriptor> field_types;

            for (const auto& child : typed_value_field->children) {
                field_names.push_back(child.name);

                // In Object Shredding, each field (age, city) is itself a STRUCT with:
                // - value (optional binary): remain_value
                // - typed_value: the actual typed value
                // We need to get the type from the typed_value child
                LogicalType child_type = TYPE_UNKNOWN;

                if (child.type == ColumnType::STRUCT && !child.children.empty()) {
                    // Find the 'typed_value' child
                    const ParquetField* typed_value_child = nullptr;
                    for (const auto& grandchild : child.children) {
                        if (grandchild.name == "typed_value") {
                            typed_value_child = &grandchild;
                            break;
                        }
                    }

                    if (typed_value_child != nullptr) {
                        auto type_result = _map_parquet_type_to_logical_type(typed_value_child);
                        if (type_result.ok()) {
                            child_type = type_result.value();
                        } else {
                            LOG(WARNING) << "[Variant] Failed to map child field '" << child.name
                                        << "'.typed_value type: " << type_result.status().message();
                            continue;
                        }
                    } else {
                        LOG(WARNING) << "[Variant] STRUCT field '" << child.name
                                    << "' has no typed_value child";
                        continue;
                    }
                } else {
                    LOG(WARNING) << "[Variant] Field '" << child.name
                                << "' is not a STRUCT, unexpected in Object Shredding";
                    continue;
                }

                field_types.emplace_back(child_type);
            }

            if (field_names.empty()) {
                LOG(WARNING) << "[Variant] STRUCT typed_value has no valid fields, falling back to Standard Variant";
                return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                             std::move(value_reader));
            }

            LOG(INFO) << "[Variant] Object Shredding: created STRUCT with fields: "
                      << boost::algorithm::join(field_names, ", ");

            // Create TypeDescriptor for the STRUCT
            TypeDescriptor struct_type_desc;
            struct_type_desc.type = TYPE_STRUCT;
            struct_type_desc.field_names = field_names;
            struct_type_desc.children = field_types;

            // Create ColumnReaders for each field
            // In Object Shredding, each field (age, city) is itself a STRUCT with:
            // - value: remain_value
            // - typed_value: the actual value we need to read
            std::map<std::string, ColumnReaderPtr> child_readers;
            for (size_t i = 0; i < typed_value_field->children.size(); ++i) {
                const auto& child_field = typed_value_field->children[i];

                // Find the 'typed_value' child within this field
                const ParquetField* typed_value_child = nullptr;
                for (const auto& grandchild : child_field.children) {
                    if (grandchild.name == "typed_value") {
                        typed_value_child = &grandchild;
                        break;
                    }
                }

                if (typed_value_child == nullptr) {
                    LOG(WARNING) << "[Variant] Field '" << field_names[i] << "' has no typed_value child, skipping";
                    continue;
                }

                // Create reader for the typed_value field (not the wrapper STRUCT)
                ASSIGN_OR_RETURN(ColumnReaderPtr child_reader,
                                ColumnReaderFactory::create(opts, typed_value_child, field_types[i]));

                if (child_reader != nullptr) {
                    child_readers.emplace(field_names[i], std::move(child_reader));
                } else {
                    LOG(WARNING) << "[Variant] Failed to create reader for STRUCT field '" << field_names[i] << "'";
                }
            }

            if (child_readers.empty()) {
                LOG(WARNING) << "[Variant] No valid child readers created, falling back to Standard Variant";
                return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                             std::move(value_reader));
            }

            LOG(INFO) << "[Variant] Object Shredding: created " << child_readers.size() << " child readers";

            // Create StructColumnReader
            auto struct_reader = std::make_unique<StructColumnReader>(typed_value_field, std::move(child_readers));

            // Extract LogicalType from TypeDescriptor
            std::vector<LogicalType> field_logical_types;
            field_logical_types.reserve(field_types.size());
            for (const auto& type_desc : field_types) {
                field_logical_types.push_back(type_desc.type);
            }

            // Return Shredding Variant reader with STRUCT typed_value
            LOG(INFO) << "[Variant] Successfully created Object Shredding VariantColumnReader";
            return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                         std::move(struct_reader), TYPE_STRUCT,
                                                         std::move(value_reader),
                                                         field_names, field_logical_types);
        }

        // For ARRAY/MAP typed_value, fall back to Standard Variant (not yet supported)
        if (typed_value_field->type == ColumnType::ARRAY || typed_value_field->type == ColumnType::MAP) {
            LOG(INFO) << "[Variant] ARRAY/MAP Shredding not supported yet, falling back to Standard Variant";
            return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                         std::move(value_reader));
        }

        // Map Parquet primitive type to StarRocks LogicalType
        LogicalType typed_value_type = TYPE_UNKNOWN;
        ASSIGN_OR_RETURN(typed_value_type, _map_parquet_type_to_logical_type(typed_value_field));

        // Create TypeDescriptor for typed_value
        TypeDescriptor typed_value_col_type(typed_value_type);

        // Create reader for typed_value column (primitive only)
        ASSIGN_OR_RETURN(ColumnReaderPtr typed_value_reader,
                         ColumnReaderFactory::create(opts, typed_value_field, typed_value_col_type));

        if (typed_value_reader == nullptr) {
            LOG(WARNING) << "[Variant] Failed to create typed_value reader";
            return Status::InternalError("Failed to create typed_value reader for Shredding Variant");
        }

        // Return Shredding Variant reader (3-column, primitive typed_value)
        LOG(INFO) << "[Variant] Successfully created Primitive Shredding VariantColumnReader with type: "
                  << logical_type_to_string(typed_value_type);
        return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                     std::move(typed_value_reader), typed_value_type,
                                                     std::move(value_reader));
    } else {
        // Return Standard Variant reader (2-column)
        LOG(INFO) << "[Variant] Creating Standard Variant reader (no typed_value field)";
        return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader),
                                                     std::move(value_reader));
    }
}

StatusOr<LogicalType> ColumnReaderFactory::_map_parquet_type_to_logical_type(const ParquetField* field) {
    // Map Parquet physical type to StarRocks LogicalType
    switch (field->physical_type) {
    case tparquet::Type::BOOLEAN:
        return TYPE_BOOLEAN;
    case tparquet::Type::INT32:
        // Check logical type for more precise mapping
        if (field->type_length > 0) {
            return TYPE_INT;
        }
        return TYPE_INT;
    case tparquet::Type::INT64:
        return TYPE_BIGINT;
    case tparquet::Type::FLOAT:
        return TYPE_FLOAT;
    case tparquet::Type::DOUBLE:
        return TYPE_DOUBLE;
    case tparquet::Type::BYTE_ARRAY:
    case tparquet::Type::FIXED_LEN_BYTE_ARRAY:
        // Check if it's a string or binary
        if (field->schema_element.__isset.converted_type) {
            if (field->schema_element.converted_type == tparquet::ConvertedType::UTF8) {
                return TYPE_VARCHAR;
            }
        }
        return TYPE_VARBINARY;
    case tparquet::Type::INT96:
        // INT96 is typically used for timestamps
        return TYPE_DATETIME;
    default:
        return Status::NotSupported(
                strings::Substitute("Unsupported Parquet type for typed_value: $0", field->physical_type));
    }
}

void ColumnReaderFactory::get_subfield_pos_with_pruned_type(const ParquetField& field, const TypeDescriptor& col_type,
                                                            bool case_sensitive, std::vector<int32_t>& pos) {
    DCHECK(field.type == ColumnType::STRUCT);
    if (!col_type.field_ids.empty()) {
        std::unordered_map<int32_t, size_t> field_id_2_pos;
        for (size_t i = 0; i < field.children.size(); i++) {
            field_id_2_pos.emplace(field.children[i].field_id, i);
        }

        for (size_t i = 0; i < col_type.children.size(); i++) {
            auto it = field_id_2_pos.find(col_type.field_ids[i]);
            if (it == field_id_2_pos.end()) {
                pos[i] = -1;
                continue;
            }
            pos[i] = it->second;
        }
    } else {
        std::unordered_map<std::string, size_t> field_name_2_pos;
        for (size_t i = 0; i < field.children.size(); i++) {
            const std::string& format_field_name = Utils::format_name(field.children[i].name, case_sensitive);
            field_name_2_pos.emplace(format_field_name, i);
        }

        if (!col_type.field_physical_names.empty()) {
            for (size_t i = 0; i < col_type.children.size(); i++) {
                const std::string& formatted_physical_name =
                        Utils::format_name(col_type.field_physical_names[i], case_sensitive);

                auto it = field_name_2_pos.find(formatted_physical_name);
                if (it == field_name_2_pos.end()) {
                    pos[i] = -1;
                    continue;
                }
                pos[i] = it->second;
            }
        } else {
            for (size_t i = 0; i < col_type.children.size(); i++) {
                const std::string formatted_subfield_name = Utils::format_name(col_type.field_names[i], case_sensitive);

                auto it = field_name_2_pos.find(formatted_subfield_name);
                if (it == field_name_2_pos.end()) {
                    pos[i] = -1;
                    continue;
                }
                pos[i] = it->second;
            }
        }
    }
}

void ColumnReaderFactory::get_subfield_pos_with_pruned_type(
        const ParquetField& field, const TypeDescriptor& col_type, bool case_sensitive,
        const TIcebergSchemaField* lake_schema_field, std::vector<int32_t>& pos,
        std::vector<const TIcebergSchemaField*>& lake_schema_subfield) {
    // For Struct type with schema change, we need to consider a subfield not existed situation.
    // When Iceberg adds a new struct subfield, the original parquet file does not contain the newly added subfield.
    std::unordered_map<std::string, const TIcebergSchemaField*> subfield_name_2_field_schema{};
    for (const auto& each : lake_schema_field->children) {
        std::string format_subfield_name = case_sensitive ? each.name : boost::algorithm::to_lower_copy(each.name);
        subfield_name_2_field_schema.emplace(format_subfield_name, &each);
    }

    std::unordered_map<int32_t, size_t> field_id_2_pos{};
    for (size_t i = 0; i < field.children.size(); i++) {
        field_id_2_pos.emplace(field.children[i].field_id, i);
    }
    for (size_t i = 0; i < col_type.children.size(); i++) {
        const auto& format_subfield_name =
                case_sensitive ? col_type.field_names[i] : boost::algorithm::to_lower_copy(col_type.field_names[i]);

        auto iceberg_it = subfield_name_2_field_schema.find(format_subfield_name);
        if (iceberg_it == subfield_name_2_field_schema.end()) {
            // This situation should not be happened, means table's struct subfield not existed in iceberg schema
            // Below code is defensive
            DCHECK(false) << "Struct subfield name: " + format_subfield_name + " not found in iceberg schema.";
            pos[i] = -1;
            lake_schema_subfield[i] = nullptr;
            continue;
        }

        int32_t field_id = iceberg_it->second->field_id;

        auto parquet_field_it = field_id_2_pos.find(field_id);
        if (parquet_field_it == field_id_2_pos.end()) {
            // Means newly added struct subfield not existed in an original parquet file, we put nullptr
            // column reader in children_reader, we will append the default value for this subfield later.
            pos[i] = -1;
            lake_schema_subfield[i] = nullptr;
            continue;
        }

        pos[i] = parquet_field_it->second;
        lake_schema_subfield[i] = iceberg_it->second;
    }
}

bool ColumnReaderFactory::_has_valid_subfield_column_reader(
        const std::map<std::string, std::unique_ptr<ColumnReader>>& children_readers) {
    for (const auto& pair : children_readers) {
        if (pair.second != nullptr) {
            return true;
        }
    }
    return false;
}

} // namespace starrocks::parquet
