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
#include <cctz/time_zone.h>

#include "column/vectorized_fwd.h"
#include "formats/parquet/variant.h"
#include "formats/parquet/column_reader.h"
#include "formats/parquet/variant_builder.h"
#include "types/logical_type.h"
#include "types/variant_value.h"
#include "velocypack/vpack.h"
#include <memory>
#include <unordered_map>

namespace starrocks {
struct VariantUtil {
    static std::string type_to_string(VariantType type);

    static Status variant_to_json(std::string_view metadata, std::string_view value, std::stringstream& json_str,
                                  cctz::time_zone timezone = cctz::local_time_zone());
    static uint32_t readLittleEndianUnsigned(const void* from, uint8_t size);

    static std::string decimal4_to_string(DecimalValue<int32_t> decimal);
    static std::string decimal8_to_string(DecimalValue<int64_t> decimal);
    static std::string decimal16_to_string(DecimalValue<int128_t> decimal);

    static uint8_t primitiveHeader(VariantPrimitiveType primitive);

    struct VariantSchema {

        size_t metadata_column_index = -1;
        size_t value_column_index = -1;
        size_t typed_value_column_index = -1;

        bool metadata_column_read = false;
        bool value_column_read = false;
        bool typed_value_column_read = false;

        parquet::ColumnReader* metadata_reader = nullptr;
        parquet::ColumnReader* value_reader = nullptr;
        parquet::ColumnReader* typed_value_reader = nullptr;

        int num_fields = 0;

        struct ScalarSchema;
        struct ArraySchema;
        struct ObjectSchema;

        std::unique_ptr<ScalarSchema> scalar_schema;
        std::unique_ptr<ArraySchema> array_schema;
        std::unique_ptr<ObjectSchema> object_schema;

        struct ScalarSchema {
            LogicalType type;
        };

        struct ArraySchema {
            std::unique_ptr<VariantSchema> element_schema;

            std::unique_ptr<ArraySchema> clone() const {
                auto cloned = std::make_unique<ArraySchema>();
                if (element_schema) cloned->element_schema = element_schema->clone();
                return cloned;
            }
        };

        struct ObjectSchema {
            struct FieldSchema {
                std::string field_name;
                std::unique_ptr<VariantSchema> schema;
            };
            std::vector<FieldSchema> fields;
            std::unordered_map<std::string, int> field_map;

            int length() const { return static_cast<int>(fields.size()); }

            std::unique_ptr<ObjectSchema> clone() const {
                auto cloned = std::make_unique<ObjectSchema>();
                cloned->fields.reserve(fields.size());
                for (const auto& f : fields) {
                    cloned->fields.push_back({
                        f.field_name,
                        f.schema ? f.schema->clone() : nullptr
                    });
                }

                cloned->field_map.clear();
                for (int i = 0; i < static_cast<int>(cloned->fields.size()); ++i) {
                    cloned->field_map[cloned->fields[i].field_name] = i;
                }
                return cloned;
            }
        };

        std::unique_ptr<VariantSchema> clone() const {
            auto cloned = std::make_unique<VariantSchema>();

            cloned->metadata_column_index = metadata_column_index;
            cloned->value_column_index = value_column_index;
            cloned->typed_value_column_index = typed_value_column_index;

            cloned->metadata_reader = metadata_reader;
            cloned->value_reader = value_reader;
            cloned->typed_value_reader = typed_value_reader;

            cloned->num_fields = num_fields;

            if (scalar_schema) cloned->scalar_schema = std::make_unique<ScalarSchema>(*scalar_schema);
            if (array_schema) cloned->array_schema = array_schema->clone();
            if (object_schema) cloned->object_schema = object_schema->clone();

            return cloned;
        }

        static std::unique_ptr<ScalarSchema> createScalar(LogicalType type);
        static std::unique_ptr<ArraySchema> createArray(std::unique_ptr<VariantSchema> element_schema);
        static std::unique_ptr<ObjectSchema> createObject(std::vector<ObjectSchema::FieldSchema> fields);
    };

    static StatusOr<VariantValue> assembleVariant(
        size_t row,
        const StructColumn* variant_column,
        const VariantSchema& schema);

private:
    static Status rebuild(
        size_t row,
        std::string_view metadata,
        const StructColumn& column,
        const VariantSchema& schema,
        parquet::VariantBuilder& builder);
};

} // namespace starrocks