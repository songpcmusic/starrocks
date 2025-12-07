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

#include "exprs/variant_functions.h"

#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "column/type_traits.h"
#include "exprs/variant_path.h"
#include "formats/parquet/variant.h"
#include "types/logical_type.h"
#include "util/variant_converter.h"

namespace starrocks {

struct VariantPathState {
    VariantPath variant_path;
};

static StatusOr<VariantPath*> get_prepared_or_parse(FunctionContext* context, Slice slice, VariantPath* out) {
    auto* prepared = reinterpret_cast<VariantPathState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (prepared != nullptr && !prepared->variant_path.is_empty()) {
        return &prepared->variant_path;
    }
    auto res = VariantPath::parse(slice);
    RETURN_IF(!res.ok(), res.status());
    out->reset(std::move(res.value()));
    return out;
}

Status VariantFunctions::variant_path_prepare(FunctionContext* context,
                                              FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    if (context->is_notnull_constant_column(1)) {
        auto path_column = context->get_constant_column(1);
        Slice path_value = ColumnHelper::get_const_value<TYPE_VARCHAR>(path_column);
        auto variant_path = VariantPath::parse(path_value);
        RETURN_IF(!variant_path.ok(), variant_path.status());

        auto* state = new VariantPathState();
        state->variant_path.reset(std::move(variant_path.value()));
        context->set_function_state(scope, state);
        VLOG(10) << "prepare variant path: " << path_value;
    } else {
        auto* state = new VariantPathState();
        context->set_function_state(scope, state);
    }
    return Status::OK();
}

Status VariantFunctions::variant_path_close(FunctionContext* context,
                                           FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<VariantPathState*>(context->get_function_state(scope));
        delete state;
    }
    return Status::OK();
}

template <LogicalType ResultType>
StatusOr<ColumnPtr> VariantFunctions::_variant_query_impl(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    auto num_rows = columns[0]->size();
    auto variant_viewer = ColumnViewer<TYPE_VARIANT>(columns[0]);
    auto path_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);

    auto* state =
            reinterpret_cast<VariantPathState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    ColumnBuilder<ResultType> result(num_rows);

    VariantPath stored_path;
    for (int row = 0; row < num_rows; ++row) {
        if (variant_viewer.is_null(row) || path_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        VariantValue* variant_value = variant_viewer.value(row);
        auto path_value = path_viewer.value(row);

        auto variant_path = get_prepared_or_parse(context, path_value, &stored_path);
        if (!variant_path.ok()) {
            VLOG(2) << "parse variant path failed: " << path_value;
            result.append_null();
            continue;
        }

        Variant variant(variant_value->get_metadata(), variant_value->get_value());
        auto extract_result = VariantPath::extract(variant, *variant_path.value());
        if (!extract_result.ok()) {
            result.append_null();
            continue;
        }

        Status st = cast_variant_to<ResultType, false>(extract_result.value(), result);
        if (!st.ok()) {
            result.append_null();
            continue;
        }

        if constexpr (ResultType == TYPE_VARIANT) {
            auto* variant_col = down_cast<VariantColumn*>(result.data_column().get());
            if (variant_col->size() > 0) {
                auto* last_variant = variant_col->get_object(variant_col->size() - 1);
                LOG(INFO) << "variant_query result at row " << row
                          << ": metadata_size=" << last_variant->get_metadata().size()
                          << ", value_size=" << last_variant->get_value().size()
                          << ", to_string=" << last_variant->to_string();
            }
        }
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> VariantFunctions::get_native_variant_bool(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_BOOLEAN>(context, columns);
}

StatusOr<ColumnPtr> VariantFunctions::get_native_variant_int(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_INT>(context, columns);
}

StatusOr<ColumnPtr> VariantFunctions::get_native_variant_bigint(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_BIGINT>(context, columns);
}

StatusOr<ColumnPtr> VariantFunctions::get_native_variant_double(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_DOUBLE>(context, columns);
}

StatusOr<ColumnPtr> VariantFunctions::get_native_variant_string(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_VARCHAR>(context, columns);
}

StatusOr<ColumnPtr> VariantFunctions::variant_query(FunctionContext* context, const Columns& columns) {
    return _variant_query_impl<TYPE_VARIANT>(context, columns);
}

} // namespace starrocks
