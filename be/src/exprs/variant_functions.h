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

#include "column/vectorized_fwd.h"
#include "common/compiler_util.h"
#include "common/status.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"
#include "types/logical_type.h"

namespace starrocks {

class VariantFunctions {
public:

    /**
     * @param: [variant, tagged_value]
     * @paramType: [VariantColumn, BinaryColumn]
     * @return: type column
     */
    DEFINE_VECTORIZED_FN(get_native_variant_bool);
    DEFINE_VECTORIZED_FN(get_native_variant_int);
    DEFINE_VECTORIZED_FN(get_native_variant_bigint);
    DEFINE_VECTORIZED_FN(get_native_variant_double);
    DEFINE_VECTORIZED_FN(get_native_variant_string);
    DEFINE_VECTORIZED_FN(variant_query);

    [[nodiscard]] static Status variant_path_prepare(FunctionContext* context,
                                                     FunctionContext::FunctionStateScope scope);
    [[nodiscard]] static Status variant_path_close(FunctionContext* context,
                                                   FunctionContext::FunctionStateScope scope);

private:
    template <LogicalType ResultType>
    [[nodiscard]] static StatusOr<ColumnPtr> _variant_query_impl(FunctionContext* context, const Columns& columns);
};

} // namespace starrocks
