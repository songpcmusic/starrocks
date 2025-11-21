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

#include "formats/parquet/variant.h"
#include "types/variant_value.h"

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

    // Build Variant Object from field map
    // Returns metadata and value binary for a Variant Object
    static Status build_variant_object(const std::map<std::string, std::pair<Datum, LogicalType>>& fields,
                                       std::string& metadata_out, std::string& value_out);

    // Merge two Variant Objects (typed_value fields + remain_value fields)
    // Returns merged metadata and value
    static Status merge_variant_objects(std::string_view typed_metadata, std::string_view typed_value,
                                        std::string_view remain_metadata, std::string_view remain_value,
                                        std::string& merged_metadata_out, std::string& merged_value_out);

    // Build Variant Object value from field map using existing metadata
    // Returns only value binary (metadata is provided separately)
    static Status build_variant_object_with_metadata(
            const std::map<std::string, std::pair<Datum, LogicalType>>& fields,
            std::string_view existing_metadata, std::string& value_out);

    // Merge two Variant Object values that share the same metadata
    // Returns merged value only (metadata remains the same)
    static Status merge_variant_objects_same_metadata(std::string_view typed_value, std::string_view remain_value,
                                                       std::string_view metadata, std::string& merged_value_out);
};

} // namespace starrocks
