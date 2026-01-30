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
#include "column/column_access_path.h"
#include "formats/parquet/column_reader.h"
#include "util/variant_util.h"

namespace starrocks::parquet {

class ColumnReaderFactory {
public:
    // create a column reader
    static StatusOr<ColumnReaderPtr> create(const ColumnReaderOptions& opts, const ParquetField* field,
                                            const TypeDescriptor& col_type, const ColumnAccessPath* column_access_path);

    // Create a column reader with iceberg schema
    static StatusOr<ColumnReaderPtr> create(const ColumnReaderOptions& opts, const ParquetField* field,
                                            const TypeDescriptor& col_type,
                                            const TIcebergSchemaField* lake_schema_field,
                                            const ColumnAccessPath* column_access_path);

private:
    // for struct type without schema change
    static void get_subfield_pos_with_pruned_type(const ParquetField& field, const TypeDescriptor& col_type,
                                                  bool case_sensitive, std::vector<int32_t>& pos);

    // for schema changed
    static void get_subfield_pos_with_pruned_type(const ParquetField& field, const TypeDescriptor& col_type,
                                                  bool case_sensitive, const TIcebergSchemaField* lake_schema_field,
                                                  std::vector<int32_t>& pos,
                                                  std::vector<const TIcebergSchemaField*>& lake_schema_subfield);

    static bool _has_valid_subfield_column_reader(
            const std::map<std::string, std::unique_ptr<ColumnReader>>& children_readers);

    static StatusOr<ColumnReaderPtr> create_variant_column_reader(const ColumnReaderOptions& opts,
                                                                  const ParquetField* variant_field,
                                                                  const ColumnAccessPath* column_access_path);

    static StatusOr<VariantUtil::VariantSchema> _build_variant_schema(
        const ParquetField& field,
        const TypeDescriptor& type_desc,
        std::unique_ptr<ColumnReader>& top_metadata_reader,
        std::unique_ptr<ColumnReader>& top_value_reader,
        std::vector<std::unique_ptr<ColumnReader>>& typed_value_readers,
        const ColumnReaderOptions& opts,
        bool top_level);

    static StatusOr<TypeDescriptor> _infer_type(const ParquetField* field);
    static StatusOr<TypeDescriptor> _infer_primitive_type(const ParquetField* field);
    static StatusOr<TypeDescriptor> _infer_array_type(const ParquetField* field);
    static StatusOr<TypeDescriptor> _infer_object_type(const ParquetField* field);
};

} // namespace starrocks::parquet