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

    if (typed_value_index != -1) {

        ASSIGN_OR_RETURN(TypeDescriptor type_desc, _infer_type(variant_field));

        auto type_desc_ptr = std::make_unique<TypeDescriptor>(std::move(type_desc));

        std::unique_ptr<ColumnReader> metadata_reader;
        std::unique_ptr<ColumnReader> value_reader;
        std::vector<std::unique_ptr<ColumnReader>> typed_value_readers;

        auto variant_schema = _build_variant_schema(*variant_field, *type_desc_ptr, metadata_reader, value_reader, typed_value_readers, opts, true);
        if (!variant_schema.ok()) {
            return Status::InternalError(strings::Substitute("Failed to build variant schema: $0",
                                                            variant_schema.status().message()));
        }

        auto schema_ptr = std::make_unique<VariantUtil::VariantSchema>(std::move(variant_schema.value()));
        return std::make_unique<ShreddedVariantColumnReader>(
            variant_field,
            std::move(type_desc_ptr),
            std::move(metadata_reader),
            std::move(value_reader),
            std::move(typed_value_readers),
            std::move(schema_ptr),
            nullptr  // column_access_path
        );
    } else {
        const ParquetField* metadata_field = &variant_field->children[metadata_index];
        const ParquetField* value_field = &variant_field->children[value_index];

        auto metadata_reader = std::make_unique<ScalarColumnReader>(
                metadata_field, &(column_chunks[metadata_field->physical_column_index]), &TYPE_VARBINARY_DESC, opts);
        auto value_reader = std::make_unique<ScalarColumnReader>(
                value_field, &(column_chunks[value_field->physical_column_index]), &TYPE_VARBINARY_DESC, opts);

        return std::make_unique<VariantColumnReader>(variant_field, std::move(metadata_reader), std::move(value_reader));
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

StatusOr<VariantUtil::VariantSchema> ColumnReaderFactory::_build_variant_schema(
    const ParquetField& field,
    const TypeDescriptor& type_desc,
    std::unique_ptr<ColumnReader>& top_metadata_reader,
    std::unique_ptr<ColumnReader>& top_value_reader,
    std::vector<std::unique_ptr<ColumnReader>>& typed_value_readers,
    const ColumnReaderOptions& opts,
    bool top_level) {

    size_t metadata_column_index = -1;
    size_t value_column_index = -1;
    size_t typed_value_column_index = -1;

    ColumnReader* metadata_reader = nullptr;
    ColumnReader* value_reader = nullptr;
    ColumnReader* typed_value_reader = nullptr;

    std::unique_ptr<VariantUtil::VariantSchema> variant_schema;

    for (size_t i = 0; i < field.children.size(); ++i) {
        const auto child = field.children[i];
        if (child.name == "typed_value") {
            typed_value_column_index = i;
            switch (child.type) {
                case STRUCT: {
                    std::vector<VariantUtil::VariantSchema::ObjectSchema::FieldSchema> fields;
                    fields.reserve(child.children.size());
                    for (size_t j = 0; j < child.children.size(); ++j) {
                        const auto& sub_field = child.children[j];
                        switch (sub_field.type) {
                            case STRUCT: {
                                auto sub_variant_schema = _build_variant_schema(field.children[i].children[j], type_desc.children[i].children[j], top_metadata_reader, top_value_reader, typed_value_readers, opts, false);
                                if (!sub_variant_schema.ok()) {
                                    return sub_variant_schema.status();
                                }

                                fields.emplace_back();
                                auto& field_schema = fields.back();
                                field_schema.field_name = sub_field.name;
                                field_schema.schema = std::make_unique<VariantUtil::VariantSchema>(std::move(sub_variant_schema.value()));
                                break;
                            }
                            default:
                                return Status::InvalidArgument("Unsupported sub-field type in STRUCT typed_value");
                        }
                    }

                    variant_schema = VariantUtil::VariantSchema::createObject(std::move(fields));
                    break;
                }
                case SCALAR: {
                    auto scalar_reader = std::make_unique<ScalarColumnReader>(
                        &field.children[i],
                        &opts.row_group_meta->columns[field.children[i].physical_column_index],
                        &type_desc.children[i],
                        opts
                    );

                    typed_value_reader = scalar_reader.get();
                    typed_value_readers.emplace_back(std::move(scalar_reader));

                    variant_schema = VariantUtil::VariantSchema::createScalar(type_desc.children[i].type);
                    break;
                }
                case ARRAY: {
                    return Status::NotSupported("ARRAY typed_value not yet implemented");
                }
                default:
                    return Status::InvalidArgument("Unsupported typed_value type");
            }
        } else if (child.name == "value") {
            value_column_index = i;
            if (child.physical_type != tparquet::Type::BYTE_ARRAY) {
                return Status::InvalidArgument("value field must be BINARY type");
            }

            auto scalar_reader = std::make_unique<ScalarColumnReader>(
                &field.children[i],
                &opts.row_group_meta->columns[field.children[i].physical_column_index],
                &type_desc.children[i],
                opts
            );

            value_reader = scalar_reader.get();
            if (top_level) {
                top_value_reader = std::move(scalar_reader);
            } else {
                typed_value_readers.emplace_back(std::move(scalar_reader));
            }
        } else if (child.name == "metadata") {
            if (!top_level) {
                return Status::InvalidArgument("metadata field can only exist at top level");
            }

            if (child.physical_type != tparquet::Type::BYTE_ARRAY) {
                return Status::InvalidArgument("metadata field must be BINARY type");
            }

            metadata_column_index = i;

            auto scalar_reader = std::make_unique<ScalarColumnReader>(
                &field.children[i],
                &opts.row_group_meta->columns[field.children[i].physical_column_index],
                &type_desc.children[i],
                opts
            );

            metadata_reader = scalar_reader.get();
            top_metadata_reader = std::move(scalar_reader);
        } else {
            return Status::InvalidArgument(strings::Substitute("Unknown variant field: $0", child.name));
        }
    }

    if (!variant_schema) {
        return Status::InvalidArgument("No valid typed_value field found in variant schema");
    }

    variant_schema->metadata_column_index = metadata_column_index;
    variant_schema->value_column_index = value_column_index;
    variant_schema->typed_value_column_index = typed_value_column_index;
    variant_schema->metadata_reader = metadata_reader;
    variant_schema->value_reader = value_reader;
    variant_schema->typed_value_reader = typed_value_reader;

    return std::move(*variant_schema);
}

StatusOr<TypeDescriptor> ColumnReaderFactory::_infer_type(const ParquetField* field) {
    if (field == nullptr) {
        return Status::InvalidArgument("typed_value_field cannot be null");
    }

    switch (field->type) {
        case SCALAR:
            return _infer_primitive_type(field);

        case ARRAY:
            return _infer_array_type(field);

        case STRUCT:
            return _infer_object_type(field);

        default:
            return Status::NotSupported(strings::Substitute("Unsupported typed_value field type: $0",
                                                           column_type_to_string(field->type)));
    }
}

StatusOr<TypeDescriptor> ColumnReaderFactory::_infer_primitive_type(const ParquetField* field) {
    DCHECK(field->type == ColumnType::SCALAR);

    const auto& schema_element = field->schema_element;

    auto physical_type = field->physical_type;

    switch (physical_type) {
    case tparquet::Type::BOOLEAN:
        return TypeDescriptor(TYPE_BOOLEAN);
        break;
    case tparquet::Type::FLOAT:
        return TypeDescriptor(TYPE_FLOAT);
        break;
    case tparquet::Type::DOUBLE:
        return TypeDescriptor(TYPE_DOUBLE);
        break;
    case tparquet::Type::INT32:
        if (schema_element.logicalType.__isset.INTEGER) {
            return TypeDescriptor(TYPE_INT);
        } else if (schema_element.logicalType.__isset.DATE) {
            return TypeDescriptor(TYPE_DATE);
        } else if (schema_element.logicalType.__isset.TIME) {
            return TypeDescriptor(TYPE_TIME);
        } else if (schema_element.logicalType.__isset.DECIMAL) {
            const auto& decimal = schema_element.logicalType.DECIMAL;
            return TypeDescriptor::promote_decimal_type(decimal.precision, decimal.scale);
        } else {
            return TypeDescriptor(TYPE_INT);
        }
        break;
    case tparquet::Type::INT64:
        if (schema_element.logicalType.__isset.INTEGER) {
            return TypeDescriptor(TYPE_BIGINT);
        } else if (schema_element.logicalType.__isset.TIME) {
            return TypeDescriptor(TYPE_TIME);
        } else if (schema_element.logicalType.__isset.TIMESTAMP) {
            return TypeDescriptor(TYPE_DATETIME);
        } else if (schema_element.logicalType.__isset.DECIMAL) {
            const auto& decimal = schema_element.logicalType.DECIMAL;
            return TypeDescriptor::promote_decimal_type(decimal.precision, decimal.scale);
        } else {
            return TypeDescriptor(TYPE_BIGINT);
        }
        break;
    case tparquet::Type::INT96:
        return TypeDescriptor(TYPE_DATETIME);
        break;
    case tparquet::Type::BYTE_ARRAY:
        if (schema_element.logicalType.__isset.STRING) {
            return TypeDescriptor::create_varchar_type(TypeDescriptor::MAX_VARCHAR_LENGTH);
        } else if (schema_element.logicalType.__isset.DECIMAL) {
            const auto& decimal = schema_element.logicalType.DECIMAL;
            return TypeDescriptor::promote_decimal_type(decimal.precision, decimal.scale);
        } else if (schema_element.logicalType.__isset.JSON) {
            return TypeDescriptor::create_json_type();
        } else {
            return TypeDescriptor::create_varbinary_type(TypeDescriptor::MAX_VARCHAR_LENGTH);
        }
        break;
    case tparquet::Type::FIXED_LEN_BYTE_ARRAY: {
        if (schema_element.logicalType.__isset.DECIMAL) {
            const auto& decimal = schema_element.logicalType.DECIMAL;
            return TypeDescriptor::promote_decimal_type(decimal.precision, decimal.scale);
        } else {
            return TypeDescriptor::create_varchar_type(TypeDescriptor::MAX_VARCHAR_LENGTH);
        }
        break;
    }
    default:
        // Treat unsupported types as varbinary type.
        return TypeDescriptor::create_varbinary_type(TypeDescriptor::MAX_VARCHAR_LENGTH);
    }
}

StatusOr<TypeDescriptor> ColumnReaderFactory::_infer_array_type(const ParquetField* field) {
    DCHECK(field->type == ColumnType::ARRAY);

    if (field->children.empty()) {
        return Status::InvalidArgument("Array field must have element type");
    }

    const ParquetField* element_field = &field->children[0];

    ASSIGN_OR_RETURN(TypeDescriptor element_type, _infer_type(element_field));

    return TypeDescriptor::create_array_type(element_type);
}

StatusOr<TypeDescriptor> ColumnReaderFactory::_infer_object_type(const ParquetField* field) {
    DCHECK(field->type == ColumnType::STRUCT);

    std::vector<std::string> field_names;
    std::vector<TypeDescriptor> field_types;

    for (const auto& child_field : field->children) {
        field_names.push_back(child_field.name);
        ASSIGN_OR_RETURN(TypeDescriptor field_type, _infer_type(&child_field));
        field_types.push_back(field_type);
    }

    return TypeDescriptor::create_struct_type(field_names, field_types);
}

} // namespace starrocks::parquet
