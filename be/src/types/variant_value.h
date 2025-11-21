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

#include <optional>
#include <string_view>

#include "column/datum.h"
#include "common/statusor.h"
#include "fmt/format.h"
#include "types/logical_type.h"
#include "util/slice.h"

namespace starrocks {

class VariantValue {
public:
    VariantValue(const std::string_view metadata, const std::string_view value) : _metadata(metadata), _value(value) {}
    VariantValue(std::string metadata, std::string value) : _metadata(std::move(metadata)), _value(std::move(value)) {}
    VariantValue() = default;

    /**
     * Static factory method to create a VariantValue from a Slice.
     * @param slice The Slice must contain the full variant binary including size header.
     * The first 4 bytes of the Slice are expected to be the size of the variant.
     * The memory layout is: [total size (4 bytes)][metadata][value].
     * @return The created VariantValue or an error status.
     */
    static StatusOr<VariantValue> create(const Slice& slice);

    /**
     * Static factory method to create a Shredded VariantValue.
     * This is used for Parquet Variant Shredding support.
     * The actual value reconstruction will be deferred until first access (lazy loading).
     *
     * @param metadata Variant metadata
     * @param typed_value The typed value (primitive type from Parquet typed_value column)
     * @param typed_value_type Logical type of the typed_value
     * @param remain_value The remain value binary (from Parquet value column)
     * @return The created Shredded VariantValue
     */
    static VariantValue create_shredded(std::string metadata, Datum typed_value, LogicalType typed_value_type,
                                       std::string remain_value);

    VariantValue(const VariantValue& rhs) = default;

    VariantValue(VariantValue&& rhs) noexcept = default;

    static Status validate_metadata(const std::string_view metadata);

    VariantValue& operator=(const VariantValue& rhs) = default;

    VariantValue& operator=(VariantValue&& rhs) noexcept = default;

    static VariantValue of_null();

    // Load metadata from the variant binary.
    // will slice the variant binary to extract metadata
    static StatusOr<std::string_view> load_metadata(std::string_view variant);

    // Serialize the VariantValue to a byte array.
    // return the number of bytes written
    size_t serialize(uint8_t* dst) const;

    // Calculate the size of the serialized VariantValue.
    // 4 bytes for value size + metadata size + value size
    uint32_t serialize_size() const;

    uint64_t mem_usage() const { return serialize_size(); }

    // Convert to a JSON string
    StatusOr<std::string> to_json(cctz::time_zone timezone = cctz::local_time_zone());
    std::string to_string();

    std::string get_metadata() const { return _metadata; }

    // Get the value, reconstructing from shredded data if necessary (lazy loading)
    std::string get_value();

    // Check if this is a shredded variant
    bool is_shredded() const { return _shredded_data.has_value(); }

    // Variant value has a maximum size limit of 16MB to prevent excessive memory usage.
    static constexpr uint32_t kMaxVariantSize = 16 * 1024 * 1024;

    // Build Variant value binary from primitive typed_value
    // This is a public utility method used by VariantUtil to construct Object binary
    static Status build_variant_value_from_primitive(const Datum& typed_value, LogicalType type, std::string& result);

private:
    // Internal structure for storing shredded variant data (for lazy reconstruction)
    struct ShreddedData {
        Datum typed_value;              // The typed value from Parquet typed_value column
        LogicalType typed_value_type;   // Logical type of the typed_value
        std::string remain_value;       // The remain value from Parquet value column
        bool is_reconstructed = false;  // Whether value has been reconstructed
    };

    static constexpr uint8_t kVersionMask = 0b1111;
    static constexpr uint8_t kSortedStrings = 0b10000;
    static constexpr uint8_t kOffsetSizeMask = 0b11000000;
    static constexpr uint8_t kOffsetSizeShift = 6;
    static constexpr uint8_t kHeaderSize = 1;
    static constexpr size_t kMinMetadataSize = 3;

    // Ensure value is reconstructed from shredded data (if needed)
    Status ensure_value_reconstructed();

    // Now directly store strings instead of string_views
    std::string _metadata;
    mutable std::string _value;

    // Optional shredded data for lazy reconstruction
    mutable std::optional<ShreddedData> _shredded_data;
};

// append json string to the stream
std::ostream& operator<<(std::ostream& os, VariantValue& json);

} // namespace starrocks

// fmt::format
template <>
struct fmt::formatter<starrocks::VariantValue> : formatter<std::string> {
    template <typename FormatContext>
    auto format(starrocks::VariantValue& p, FormatContext& ctx) -> decltype(ctx.out()) {
        return formatter<std::string>::format(p.to_string(), ctx);
    }
}; // namespace fmt