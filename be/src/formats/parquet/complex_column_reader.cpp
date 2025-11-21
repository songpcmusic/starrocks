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

#include "formats/parquet/complex_column_reader.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/map_column.h"
#include "column/struct_column.h"
#include "column/variant_column.h"
#include "formats/parquet/schema.h"
#include "gutil/casts.h"
#include "gutil/strings/substitute.h"
#include "types/variant_value.h"
#include "util/json.h"
#include "util/slice.h"
#include "util/variant_util.h"

namespace starrocks::parquet {

template <typename TOffset, typename TIsNull>
static void def_rep_to_offset(const LevelInfo& level_info, const level_t* def_levels, const level_t* rep_levels,
                              size_t num_levels, TOffset* offsets, TIsNull* is_nulls, size_t* num_offsets,
                              bool* has_null) {
    size_t offset_pos = 0;
    for (int i = 0; i < num_levels; ++i) {
        // when def_level is less than immediate_repeated_ancestor_def_level, it means that level
        // will affect its ancestor.
        // when rep_level is greater than max_rep_level, this means that level affects its
        // descendants.
        // So we can skip this levels
        if (def_levels[i] < level_info.immediate_repeated_ancestor_def_level ||
            rep_levels[i] > level_info.max_rep_level) {
            continue;
        }
        if (rep_levels[i] == level_info.max_rep_level) {
            offsets[offset_pos]++;
            continue;
        }

        // Start for a new row
        offset_pos++;
        offsets[offset_pos] = offsets[offset_pos - 1];
        if (def_levels[i] >= level_info.max_def_level) {
            offsets[offset_pos]++;
        }

        // when def_level equals with max_def_level, this is a non null element or a required element
        // when def_level equals with (max_def_level - 1), this indicates an empty array
        // when def_level less than (max_def_level - 1) it means this array is null
        if (def_levels[i] >= level_info.max_def_level - 1) {
            is_nulls[offset_pos - 1] = 0;
        } else {
            is_nulls[offset_pos - 1] = 1;
            *has_null = true;
        }
    }
    *num_offsets = offset_pos;
}

Status ListColumnReader::read_range(const Range<uint64_t>& range, const Filter* filter, Column* dst) {
    NullableColumn* nullable_column = nullptr;
    ArrayColumn* array_column = nullptr;
    if (dst->is_nullable()) {
        nullable_column = down_cast<NullableColumn*>(dst);
        DCHECK(nullable_column->mutable_data_column()->is_array());
        array_column = down_cast<ArrayColumn*>(nullable_column->mutable_data_column());
    } else {
        DCHECK(dst->is_array());
        DCHECK(!get_column_parquet_field()->is_nullable);
        array_column = down_cast<ArrayColumn*>(dst);
    }
    auto* child_column = array_column->elements_column().get();
    RETURN_IF_ERROR(_element_reader->read_range(range, filter, child_column));

    level_t* def_levels = nullptr;
    level_t* rep_levels = nullptr;
    size_t num_levels = 0;
    _element_reader->get_levels(&def_levels, &rep_levels, &num_levels);

    auto& offsets = array_column->offsets_column()->get_data();
    offsets.resize(num_levels + 1);
    NullColumn null_column(num_levels);
    auto& is_nulls = null_column.get_data();
    size_t num_offsets = 0;
    bool has_null = false;
    def_rep_to_offset(get_column_parquet_field()->level_info, def_levels, rep_levels, num_levels, &offsets[0],
                      &is_nulls[0], &num_offsets, &has_null);
    offsets.resize(num_offsets + 1);
    is_nulls.resize(num_offsets);

    if (dst->is_nullable()) {
        DCHECK(nullable_column != nullptr);
        nullable_column->mutable_null_column()->swap_column(null_column);
        nullable_column->set_has_null(has_null);
    }

    return Status::OK();
}

Status MapColumnReader::read_range(const Range<uint64_t>& range, const Filter* filter, Column* dst) {
    NullableColumn* nullable_column = nullptr;
    MapColumn* map_column = nullptr;
    if (dst->is_nullable()) {
        nullable_column = down_cast<NullableColumn*>(dst);
        DCHECK(nullable_column->mutable_data_column()->is_map());
        map_column = down_cast<MapColumn*>(nullable_column->mutable_data_column());
    } else {
        DCHECK(dst->is_map());
        DCHECK(!get_column_parquet_field()->is_nullable);
        map_column = down_cast<MapColumn*>(dst);
    }
    auto* key_column = map_column->keys_column().get();
    auto* value_column = map_column->values_column().get();
    if (_key_reader != nullptr) {
        RETURN_IF_ERROR(_key_reader->read_range(range, filter, key_column));
    }

    if (_value_reader != nullptr) {
        RETURN_IF_ERROR(_value_reader->read_range(range, filter, value_column));
    }

    // if neither key_reader not value_reader is nullptr , check the value_column size is the same with key_column
    DCHECK((_key_reader == nullptr) || (_value_reader == nullptr) || (value_column->size() == key_column->size()));

    level_t* def_levels = nullptr;
    level_t* rep_levels = nullptr;
    size_t num_levels = 0;

    if (_key_reader != nullptr) {
        _key_reader->get_levels(&def_levels, &rep_levels, &num_levels);
    } else if (_value_reader != nullptr) {
        _value_reader->get_levels(&def_levels, &rep_levels, &num_levels);
    } else {
        DCHECK(false) << "Unreachable!";
    }

    auto& offsets = map_column->offsets_column()->get_data();
    offsets.resize(num_levels + 1);
    NullColumn null_column(num_levels);
    auto& is_nulls = null_column.get_data();
    size_t num_offsets = 0;
    bool has_null = false;

    // ParquetFiled Map -> Map<Struct<key,value>>
    def_rep_to_offset(get_column_parquet_field()->level_info, def_levels, rep_levels, num_levels, &offsets[0],
                      &is_nulls[0], &num_offsets, &has_null);
    offsets.resize(num_offsets + 1);
    is_nulls.resize(num_offsets);

    // fill with default
    if (_key_reader == nullptr) {
        key_column->append_default(offsets.back());
    }
    if (_value_reader == nullptr) {
        value_column->append_default(offsets.back());
    }

    if (dst->is_nullable()) {
        DCHECK(nullable_column != nullptr);
        nullable_column->mutable_null_column()->swap_column(null_column);
        nullable_column->set_has_null(has_null);
    }

    return Status::OK();
}

Status StructColumnReader::read_range(const Range<uint64_t>& range, const Filter* filter, Column* dst) {
    NullableColumn* nullable_column = nullptr;
    StructColumn* struct_column = nullptr;
    if (dst->is_nullable()) {
        nullable_column = down_cast<NullableColumn*>(dst);
        DCHECK(nullable_column->mutable_data_column()->is_struct());
        struct_column = down_cast<StructColumn*>(nullable_column->mutable_data_column());
    } else {
        DCHECK(dst->is_struct());
        DCHECK(!get_column_parquet_field()->is_nullable);
        struct_column = down_cast<StructColumn*>(dst);
    }

    const auto& field_names = struct_column->field_names();

    DCHECK_EQ(field_names.size(), _child_readers.size());

    // Fill data for subfield column reader
    size_t real_read = 0;
    bool first_read = true;
    for (size_t i = 0; i < field_names.size(); i++) {
        const auto& field_name = field_names[i];
        if (LIKELY(_child_readers.find(field_name) != _child_readers.end())) {
            if (_child_readers[field_name] != nullptr) {
                Column* child_column = struct_column->field_column(field_name).get();
                RETURN_IF_ERROR(_child_readers[field_name]->read_range(range, filter, child_column));
                real_read = child_column->size();
                first_read = false;
            } else {
                LOG(WARNING) << "[Variant] StructColumnReader: field_name='" << field_name << "' has NULL reader";
            }
        } else {
            LOG(ERROR) << "[Variant] StructColumnReader: field_name='" << field_name << "' NOT FOUND in _child_readers";
            return Status::InternalError(strings::Substitute("there is no match subfield reader for $0", field_name));
        }
    }

    if (UNLIKELY(first_read)) {
        return Status::InternalError(strings::Substitute("All used subfield of struct type $0 is not exist",
                                                         get_column_parquet_field()->name));
    }

    for (size_t i = 0; i < field_names.size(); i++) {
        const auto& field_name = field_names[i];
        if (_child_readers[field_name] == nullptr) {
            Column* child_column = struct_column->field_column(field_name).get();
            child_column->append_default(real_read);
        }
    }

    if (dst->is_nullable()) {
        DCHECK(nullable_column != nullptr);
        size_t row_nums = struct_column->fields_column()[0]->size();
        NullColumn null_column(row_nums, 0);
        auto& is_nulls = null_column.get_data();
        bool has_null = false;
        _handle_null_rows(is_nulls.data(), &has_null, row_nums);

        nullable_column->mutable_null_column()->swap_column(null_column);
        nullable_column->set_has_null(has_null);
    }
    return Status::OK();
}

bool StructColumnReader::try_to_use_dict_filter(ExprContext* ctx, bool is_decode_needed, const SlotId slotId,
                                                const std::vector<std::string>& sub_field_path, const size_t& layer) {
    if (sub_field_path.size() <= layer) {
        return false;
    }
    const std::string& sub_field = sub_field_path[layer];
    if (_child_readers.find(sub_field) == _child_readers.end()) {
        return false;
    }

    if (_child_readers[sub_field] == nullptr) {
        return false;
    }
    return _child_readers[sub_field]->try_to_use_dict_filter(ctx, is_decode_needed, slotId, sub_field_path, layer + 1);
}

Status StructColumnReader::filter_dict_column(const ColumnPtr& column, Filter* filter,
                                              const std::vector<std::string>& sub_field_path, const size_t& layer) {
    const std::string& sub_field = sub_field_path[layer];
    StructColumn* struct_column = nullptr;
    if (column->is_nullable()) {
        NullableColumn* nullable_column = down_cast<NullableColumn*>(column.get());
        DCHECK(nullable_column->mutable_data_column()->is_struct());
        struct_column = down_cast<StructColumn*>(nullable_column->mutable_data_column());
    } else {
        DCHECK(column->is_struct());
        DCHECK(!get_column_parquet_field()->is_nullable);
        struct_column = down_cast<StructColumn*>(column.get());
    }
    return _child_readers[sub_field]->filter_dict_column(struct_column->field_column(sub_field), filter, sub_field_path,
                                                         layer + 1);
}

Status StructColumnReader::fill_dst_column(ColumnPtr& dst, const ColumnPtr& src) {
    StructColumn* struct_column_src = nullptr;
    StructColumn* struct_column_dst = nullptr;
    if (src->is_nullable()) {
        NullableColumn* nullable_column_src = down_cast<NullableColumn*>(src.get());
        DCHECK(nullable_column_src->mutable_data_column()->is_struct());
        struct_column_src = down_cast<StructColumn*>(nullable_column_src->mutable_data_column());
        NullColumn* null_column_src = nullable_column_src->mutable_null_column();
        NullableColumn* nullable_column_dst = down_cast<NullableColumn*>(dst.get());
        DCHECK(nullable_column_dst->mutable_data_column()->is_struct());
        struct_column_dst = down_cast<StructColumn*>(nullable_column_dst->mutable_data_column());
        NullColumn* null_column_dst = nullable_column_dst->mutable_null_column();
        null_column_dst->swap_column(*null_column_src);
        nullable_column_src->update_has_null();
        nullable_column_dst->update_has_null();
    } else {
        DCHECK(src->is_struct());
        DCHECK(dst->is_struct());
        DCHECK(!get_column_parquet_field()->is_nullable);
        struct_column_src = down_cast<StructColumn*>(src.get());
        struct_column_dst = down_cast<StructColumn*>(dst.get());
    }
    const auto& field_names = struct_column_dst->field_names();
    for (size_t i = 0; i < field_names.size(); i++) {
        const auto& field_name = field_names[i];
        if (LIKELY(_child_readers.find(field_name) != _child_readers.end())) {
            if (_child_readers[field_name] == nullptr) {
                struct_column_dst->field_column(field_name)
                        ->swap_column(*(struct_column_src->field_column(field_name)));
            } else {
                RETURN_IF_ERROR(_child_readers[field_name]->fill_dst_column(
                        struct_column_dst->field_column(field_name), struct_column_src->field_column(field_name)));
            }
        } else {
            return Status::InternalError(strings::Substitute("there is no match subfield reader for $1", field_name));
        }
    }
    return Status::OK();
}

void StructColumnReader::_handle_null_rows(uint8_t* is_nulls, bool* has_null, size_t num_rows) {
    level_t* def_levels = nullptr;
    level_t* rep_levels = nullptr;
    size_t num_levels = 0;
    (*_def_rep_level_child_reader)->get_levels(&def_levels, &rep_levels, &num_levels);

    if (def_levels == nullptr) {
        // If subfields are required, def_levels is nullptr
        *has_null = false;
        return;
    }

    LevelInfo level_info = get_column_parquet_field()->level_info;

    if (rep_levels != nullptr) {
        // It's a RepeatedStoredColumnReader
        size_t rows = 0;
        for (size_t i = 0; i < num_levels; i++) {
            if (def_levels[i] < level_info.immediate_repeated_ancestor_def_level ||
                rep_levels[i] > level_info.max_rep_level) {
                continue;
            }

            // Start for a new row
            if (def_levels[i] >= level_info.max_def_level) {
                is_nulls[rows] = 0;
            } else {
                is_nulls[rows] = 1;
                *has_null = true;
            }
            rows++;
        }
        DCHECK_EQ(num_rows, rows);
    } else {
        // For OptionalStoredColumnReader, num_levels is equal to num_rows
        DCHECK(num_rows == num_levels);
        for (size_t i = 0; i < num_levels; i++) {
            if (def_levels[i] >= level_info.max_def_level) {
                is_nulls[i] = 0;
            } else {
                is_nulls[i] = 1;
                *has_null = true;
            }
        }
    }
}

// VariantColumnReader

Status VariantColumnReader::read_range(const Range<uint64_t>& range, const Filter* filter, Column* dst) {
    VariantColumn* variant_column = nullptr;
    NullableColumn* nullable_column = nullptr;
    if (dst->is_nullable()) {
        nullable_column = down_cast<NullableColumn*>(dst);
        DCHECK(nullable_column->mutable_data_column()->is_variant());
        variant_column = down_cast<VariantColumn*>(nullable_column->mutable_data_column());
    } else {
        DCHECK(dst->is_variant());
        DCHECK(!get_column_parquet_field()->is_nullable);
        variant_column = down_cast<VariantColumn*>(dst);
    }

    // Read metadata and value columns (both are always present)
    ColumnPtr metadata_col = NullableColumn::create(BinaryColumn::create(), NullColumn::create());
    ColumnPtr value_col = NullableColumn::create(BinaryColumn::create(), NullColumn::create());
    RETURN_IF_ERROR(_metadata_reader->read_range(range, filter, metadata_col.get()));
    RETURN_IF_ERROR(_value_reader->read_range(range, filter, value_col.get()));

    auto* metadata_nullable = down_cast<NullableColumn*>(metadata_col.get());
    auto* value_nullable = down_cast<NullableColumn*>(value_col.get());
    const auto* metadata_column = down_cast<const BinaryColumn*>(metadata_nullable->data_column().get());
    const auto* value_column = down_cast<const BinaryColumn*>(value_nullable->data_column().get());

    // Read typed_value column if this is a Shredding Variant
    ColumnPtr typed_value_col;
    if (_typed_value_reader != nullptr) {
        // Create appropriate column based on typed_value type
        if (_typed_value_type == TYPE_STRUCT) {
            // For STRUCT typed_value (Object Shredding), create StructColumn with field definitions
            DCHECK_EQ(_struct_field_names.size(), _struct_field_types.size())
                << "Field names and types size mismatch";

            // Create child columns for each field
            Columns field_columns;
            field_columns.reserve(_struct_field_names.size());
            for (size_t i = 0; i < _struct_field_names.size(); ++i) {
                TypeDescriptor field_type_desc(_struct_field_types[i]);
                field_columns.push_back(ColumnHelper::create_column(field_type_desc, true));
            }

            // Create StructColumn with predefined fields
            auto struct_data_col = StructColumn::create(std::move(field_columns), _struct_field_names);
            typed_value_col = NullableColumn::create(std::move(struct_data_col), NullColumn::create());
        } else {
            typed_value_col = ColumnHelper::create_column(TypeDescriptor(_typed_value_type), true);
        }
        RETURN_IF_ERROR(_typed_value_reader->read_range(range, filter, typed_value_col.get()));
    }

    // Get definition levels to determine which variant groups are null
    level_t* def_levels = nullptr;
    level_t* rep_levels = nullptr;
    size_t num_levels = 0;
    _value_reader->get_levels(&def_levels, &rep_levels, &num_levels);
    const LevelInfo level_info = get_column_parquet_field()->level_info;

    DCHECK_EQ(metadata_column->size(), value_column->size());

    const size_t expected_size = range.span_size();
    const size_t actual_rows = metadata_column->size();

    variant_column->reserve(expected_size);

    // Lambda for appending VariantValue (Standard or Shredded)
    auto append_variant_column = [&](const size_t idx) {
        const Slice metadata_slice = metadata_column->get_slice(idx);
        const Slice value_slice = value_column->get_slice(idx);

        if (_typed_value_reader != nullptr) {
            // Shredding Variant
            if (_typed_value_type == TYPE_STRUCT) {
                // Object Shredding: typed_value is a STRUCT
                // Extract StructColumn from typed_value_col (handle nullable wrapper)
                StructColumn* struct_col = nullptr;
                NullColumn* null_col = nullptr;
                if (typed_value_col->is_nullable()) {
                    auto* nullable = down_cast<NullableColumn*>(typed_value_col.get());
                    struct_col = down_cast<StructColumn*>(nullable->mutable_data_column());
                    null_col = nullable->mutable_null_column();
                } else {
                    struct_col = down_cast<StructColumn*>(typed_value_col.get());
                }

                // Use relative index within the range (typed_value_col only contains current range data)
                size_t relative_idx = idx - range.begin();

                // Check if this row's typed_value is null
                bool typed_value_is_null = (null_col != nullptr && null_col->get_data()[relative_idx] != 0);

                if (typed_value_is_null) {
                    // typed_value is null, only use remain_value
                    variant_column->append(VariantValue(std::string(metadata_slice.data, metadata_slice.size),
                                                        std::string(value_slice.data, value_slice.size)));
                } else {
                    // Build JSON object from STRUCT fields
                    const auto& field_names = struct_col->field_names();
                    const auto& field_columns = struct_col->fields_column();

                    DCHECK_EQ(field_names.size(), _struct_field_types.size())
                        << "Field names and types size mismatch";

                    // Collect fields into a map for build_variant_object
                    std::map<std::string, std::pair<Datum, LogicalType>> typed_fields;

                    for (size_t field_idx = 0; field_idx < field_names.size(); ++field_idx) {
                        const auto& field_name = field_names[field_idx];
                        const auto& field_column = field_columns[field_idx];
                        const LogicalType field_type = _struct_field_types[field_idx];

                        // StructColumn fields are always NullableColumn
                        // We need to check null_column and access data_column directly
                        auto* nullable_col = down_cast<NullableColumn*>(field_column.get());
                        DCHECK(nullable_col != nullptr);

                        // Check if this field is null
                        bool is_null = nullable_col->is_null(relative_idx);

                        // Skip null fields
                        if (is_null) {
                            continue;
                        }

                        // Get field value from data_column (not using get() which checks null again)
                        Datum field_datum = nullable_col->data_column()->get(relative_idx);

                        typed_fields[field_name] = std::make_pair(field_datum, field_type);
                    }

                    // Handle empty typed_fields (all fields are null)
                    if (typed_fields.empty()) {
                        if (value_slice.size == 0) {
                            // Both typed_value and remain_value are empty, use null
                            variant_column->append(VariantValue::of_null());
                        } else {
                            // Use remain_value only
                            variant_column->append(VariantValue(std::string(metadata_slice.data, metadata_slice.size),
                                                              std::string(value_slice.data, value_slice.size)));
                        }
                        return;
                    }
                    // Build Variant Object from typed_value fields using shared metadata
                    std::string_view shared_metadata(metadata_slice.data, metadata_slice.size);
                    std::string typed_value;

                    auto build_status = VariantUtil::build_variant_object_with_metadata(typed_fields, shared_metadata, typed_value);
                    if (!build_status.ok()) {
                        LOG(WARNING) << "[Variant] Failed to build Variant Object: " << build_status;
                        variant_column->append(VariantValue::of_null());
                        return;
                    }

                    if (value_slice.size == 0) {
                        // No remain_value, use typed_value only with shared metadata
                        variant_column->append(VariantValue(std::string(shared_metadata), typed_value));
                    } else {
                        // Have remain_value, merge typed_value with remain_value (using same metadata)
                        std::string_view remain_value(value_slice.data, value_slice.size);

                        std::string merged_value;
                        auto merge_status = VariantUtil::merge_variant_objects_same_metadata(
                            std::string_view(typed_value),
                            remain_value,
                            shared_metadata,
                            merged_value);

                        if (!merge_status.ok()) {
                            LOG(WARNING) << "[Variant] Failed to merge objects: " << merge_status << ", using typed_value only";
                            variant_column->append(VariantValue(std::string(shared_metadata), typed_value));
                        } else {
                            variant_column->append(VariantValue(std::string(shared_metadata), merged_value));
                        }
                    }
                }
            } else {
                // Primitive Shredding: use create_shredded()
                Datum typed_value_datum = typed_value_col->get(idx);
                variant_column->append(VariantValue::create_shredded(
                        std::string(metadata_slice.data, metadata_slice.size),
                        std::move(typed_value_datum),
                        _typed_value_type,
                        std::string(value_slice.data, value_slice.size)));
            }
        } else {
            // Standard Variant: use normal constructor
            variant_column->append(VariantValue(std::string(metadata_slice.data, metadata_slice.size),
                                                std::string(value_slice.data, value_slice.size)));
        }
    };

    if (def_levels != nullptr && num_levels > 0) {
        size_t data_idx = 0;
        for (size_t i = 0; i < expected_size && i < num_levels; ++i) {
            if (def_levels[i] >= level_info.max_def_level) {
                if (data_idx < actual_rows) {
                    append_variant_column(data_idx);
                    data_idx++;
                } else {
                    variant_column->append(VariantValue::of_null());
                }
            } else {
                variant_column->append(VariantValue::of_null());
            }
        }

        // If we still have fewer rows than expected, fill with nulls
        if (size_t current_size = variant_column->size(); current_size < expected_size) {
            size_t to_fill = expected_size - current_size;
            variant_column->append_nulls(to_fill);
        }
    } else {
        // Variant group is required, so all rows are non-null
        for (size_t i = 0; i < actual_rows; ++i) {
            append_variant_column(i);
        }

        DCHECK_EQ(actual_rows, expected_size);
        if (actual_rows < expected_size) {
            size_t to_fill = expected_size - actual_rows;
            variant_column->append_nulls(to_fill);
        }
    }

    // Handle nullable column null flags
    if (dst->is_nullable()) {
        DCHECK(nullable_column != nullptr);
        if (def_levels != nullptr && num_levels > 0) {
            NullColumn null_column(expected_size);
            auto& is_nulls = null_column.get_data();
            bool has_null = false;
            for (size_t i = 0; i < expected_size && i < num_levels; ++i) {
                if (def_levels[i] >= level_info.max_def_level) {
                    is_nulls[i] = 0; // Variant group exists
                } else {
                    is_nulls[i] = 1; // Variant group is null
                    has_null = true;
                }
            }

            for (size_t i = num_levels; i < expected_size; ++i) {
                is_nulls[i] = 1;
                has_null = true;
            }

            nullable_column->mutable_null_column()->swap_column(null_column);
            nullable_column->set_has_null(has_null);
        } else {
            NullColumn null_column(expected_size, 0);
            nullable_column->mutable_null_column()->swap_column(null_column);
            nullable_column->set_has_null(false);
        }
    }

    return Status::OK();
}

} // namespace starrocks::parquet
