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

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

#include "types/variant_value.h"

namespace starrocks::parquet {

struct FieldEntry {
    std::string key;
    int id;
    int offset;

    FieldEntry(std::string k, int i, int o) : key(std::move(k)), id(i), offset(o) {}

    bool operator<(const FieldEntry& other) const {
        return key < other.key;
    }
};

class VariantBuilder {
public:
    VariantBuilder() = default;
    ~VariantBuilder() = default;

    void appendString(const std::string& str);
    void appendNull();
    void appendBoolean(bool b);
    void appendLong(int64_t l);
    void appendDouble(double d);
    void appendFloat(float f);
    void appendBinary(const std::vector<uint8_t>& binary);

    void appendDecimal(int32_t unscaled_value, int precision, int scale);
    void appendDecimal(int64_t unscaled_value, int precision, int scale);
    void appendDecimal(int128_t unscaled_value, int precision, int scale);

    int addKey(const std::string& key);
    int getWritePos() const;
    void finishWritingObject(int start, std::vector<FieldEntry>& fields);
    void finishWritingArray(int start, std::vector<int>& offsets);

    void appendVariant(const Variant& variant);

    VariantValue result();
    std::vector<uint8_t> valueWithoutMetadata();

private:
    void handleObject(const Variant& variant);
    void handleArray(const Variant& variant);
    void handlePrimitive(const Variant& variant);

    static constexpr uint8_t kMetadataVersion = 1;          // Variant format version
    static constexpr uint8_t kSortedFlag = 1;               // Dictionary is sorted
    static constexpr uint8_t kSortedFlagShift = 5;          // Sorted flag at bit 5
    static constexpr uint8_t kOffsetSizeShift = 6;          // Offset size at bits 6-7

    std::vector<uint8_t> _writeBuffer;
    int _writePos = 0;
    std::unordered_map<std::string, int> _dictionary;
    std::vector<std::string> _dictionaryKeys;

    void _checkCapacity(int additional);
    void _writeLittleEndian(uint64_t value, int bytes);
    uint8_t _primitiveHeader(int primitiveType) const;
    uint8_t _getIntegerSize(int value) const;
    std::string _buildMetadata() const;
};

} // namespace starrocks::parquet
