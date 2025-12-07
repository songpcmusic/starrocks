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

#include "formats/parquet/variant_builder.h"

#include <algorithm>
#include <cstring>

#include "formats/parquet/variant.h"

namespace starrocks::parquet {

const int MAX_SHORT_STR_SIZE = 63;

void VariantBuilder::appendString(const std::string& str) {
    std::vector<uint8_t> text(str.begin(), str.end());
    bool longStr = text.size() > MAX_SHORT_STR_SIZE;
    
    _checkCapacity((longStr ? 1 + 4 : 1) + text.size());
    
    if (longStr) {
        _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::STRING));
        _writeLittleEndian(text.size(), 4);
    } else {
        _writeBuffer[_writePos++] = (text.size() << 2) | static_cast<uint8_t>(BasicType::SHORT_STRING);
    }
    
    std::copy(text.begin(), text.end(), _writeBuffer.begin() + _writePos);
    _writePos += text.size();
}

void VariantBuilder::appendNull() {
    _checkCapacity(1);
    _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::NULL_TYPE));
}

void VariantBuilder::appendBoolean(bool b) {
    _checkCapacity(1);
    _writeBuffer[_writePos++] = _primitiveHeader(b ? 
        static_cast<int>(VariantPrimitiveType::BOOLEAN_TRUE) : 
        static_cast<int>(VariantPrimitiveType::BOOLEAN_FALSE));
}

void VariantBuilder::appendLong(int64_t l) {
    _checkCapacity(1 + 8);
    if (l == static_cast<int8_t>(l)) {
        _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::INT8));
        _writeLittleEndian(l, 1);
    } else if (l == static_cast<int16_t>(l)) {
        _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::INT16));
        _writeLittleEndian(l, 2);
    } else if (l == static_cast<int32_t>(l)) {
        _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::INT32));
        _writeLittleEndian(l, 4);
    } else {
        _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::INT64));
        _writeLittleEndian(l, 8);
    }
}

void VariantBuilder::appendDouble(double d) {
    _checkCapacity(1 + 8);
    _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::DOUBLE));
    uint64_t bits = *reinterpret_cast<uint64_t*>(&d);
    _writeLittleEndian(bits, 8);
}

void VariantBuilder::appendFloat(float f) {
    _checkCapacity(1 + 4);
    _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::FLOAT));
    uint32_t bits = *reinterpret_cast<uint32_t*>(&f);
    _writeLittleEndian(bits, 4);
}

void VariantBuilder::appendBinary(const std::vector<uint8_t>& binary) {
    _checkCapacity(1 + 4 + binary.size());
    _writeBuffer[_writePos++] = _primitiveHeader(static_cast<int>(VariantPrimitiveType::BINARY));
    _writeLittleEndian(binary.size(), 4);
    std::copy(binary.begin(), binary.end(), _writeBuffer.begin() + _writePos);
    _writePos += binary.size();
}

int VariantBuilder::addKey(const std::string& key) {
    auto it = _dictionary.find(key);
    if (it != _dictionary.end()) {
        return it->second;
    }

    int keyId = _dictionaryKeys.size();
    _dictionary[key] = keyId;
    _dictionaryKeys.push_back(key);
    return keyId;
}

int VariantBuilder::getWritePos() const {
    return _writePos;
}

void VariantBuilder::finishWritingObject(int start, std::vector<FieldEntry>& fields) {
    int fieldCount = fields.size();
    if (fieldCount == 0) {
        // Empty object
        uint8_t header = static_cast<uint8_t>(BasicType::OBJECT);
        _writeBuffer.insert(_writeBuffer.begin() + start, header);
        _writePos++;
        return;
    }

    std::sort(fields.begin(), fields.end());

    int maxId = 0;
    for (const auto& field : fields) {
        maxId = std::max(maxId, field.id);
    }

    int dataSize = _writePos - start;
    bool isLarge = fieldCount > 255;
    uint8_t sizeBytes = isLarge ? 4 : 1;
    uint8_t idSize = _getIntegerSize(maxId);
    uint8_t offsetSize = _getIntegerSize(dataSize);

    int headerSize = 1 + sizeBytes + fieldCount * idSize + (fieldCount + 1) * offsetSize;

    _writeBuffer.insert(_writeBuffer.begin() + start, headerSize, 0);
    _writePos += headerSize;

    uint8_t valueHeader = (isLarge ? 0x10 : 0) | ((idSize - 1) << 2) | (offsetSize - 1);
    uint8_t header = (valueHeader << 2) | static_cast<uint8_t>(BasicType::OBJECT);
    _writeBuffer[start] = header;

    int pos = start + 1;
    if (isLarge) {
        for (int i = 0; i < 4; ++i) {
            _writeBuffer[pos++] = static_cast<uint8_t>((fieldCount >> (i * 8)) & 0xFF);
        }
    } else {
        _writeBuffer[pos++] = static_cast<uint8_t>(fieldCount);
    }

    for (const auto& field : fields) {
        for (uint8_t i = 0; i < idSize; ++i) {
            _writeBuffer[pos++] = static_cast<uint8_t>((field.id >> (i * 8)) & 0xFF);
        }
    }

    for (const auto& field : fields) {
        for (uint8_t i = 0; i < offsetSize; ++i) {
            _writeBuffer[pos++] = static_cast<uint8_t>((field.offset >> (i * 8)) & 0xFF);
        }
    }

    for (uint8_t i = 0; i < offsetSize; ++i) {
        _writeBuffer[pos++] = static_cast<uint8_t>((dataSize >> (i * 8)) & 0xFF);
    }
}

void VariantBuilder::finishWritingArray(int start, std::vector<int>& offsets) {
    int elementCount = offsets.size();
    if (elementCount == 0) {
        uint8_t header = static_cast<uint8_t>(BasicType::ARRAY);
        _writeBuffer.insert(_writeBuffer.begin() + start, header);
        _writePos++;
        return;
    }

    int dataSize = _writePos - start;
    bool isLarge = elementCount > 255;
    uint8_t sizeBytes = isLarge ? 4 : 1;
    uint8_t offsetSize = _getIntegerSize(dataSize);

    int headerSize = 1 + sizeBytes + elementCount * offsetSize;

    _writeBuffer.insert(_writeBuffer.begin() + start, headerSize, 0);
    _writePos += headerSize;

    uint8_t valueHeader = (isLarge ? 0x10 : 0) | (offsetSize - 1);
    uint8_t header = (valueHeader << 2) | static_cast<uint8_t>(BasicType::ARRAY);
    _writeBuffer[start] = header;

    int pos = start + 1;
    if (isLarge) {
        for (int i = 0; i < 4; ++i) {
            _writeBuffer[pos++] = static_cast<uint8_t>((elementCount >> (i * 8)) & 0xFF);
        }
    } else {
        _writeBuffer[pos++] = static_cast<uint8_t>(elementCount);
    }

    for (int offset : offsets) {
        for (uint8_t i = 0; i < offsetSize; ++i) {
            _writeBuffer[pos++] = static_cast<uint8_t>((offset >> (i * 8)) & 0xFF);
        }
    }
}



void VariantBuilder::appendVariant(const Variant& variant) {
    VariantType type = variant.type();

    switch (type) {
        case VariantType::OBJECT:
            handleObject(variant);
            break;
        case VariantType::ARRAY:
            handleArray(variant);
            break;
        default:
            handlePrimitive(variant);
            break;
    }
}
void VariantBuilder::handleObject(const Variant& variant) {
    auto num_fields_result = variant.num_elements();
    if (!num_fields_result.ok()) {
        return;
    }

    uint32_t num_fields = num_fields_result.value();

    std::vector<FieldEntry> fields;
    fields.reserve(num_fields);
    int start = _writePos;

    for (uint32_t i = 0; i < num_fields; ++i) {
        auto field_result = variant.get_field_at_index(i);
        if (!field_result.ok()) {
            continue;
        }

        auto [field_key, field_variant] = field_result.value();
        std::string key(field_key);

        int id = addKey(key);
        int offset_before = _writePos - start;
        fields.emplace_back(key, id, offset_before);

        appendVariant(field_variant);
    }

    finishWritingObject(start, fields);
}
void VariantBuilder::handleArray(const Variant& variant) {
    auto num_elements_result = variant.num_elements();
    if (!num_elements_result.ok()) {
        return;
    }

    uint32_t num_elements = num_elements_result.value();
    std::vector<int> offsets;
    offsets.reserve(num_elements);
    int start = _writePos;

    for (uint32_t i = 0; i < num_elements; ++i) {
        auto element_result = variant.get_element_at_index(i);
        if (!element_result.ok()) {
            continue;
        }

        Variant element_variant = element_result.value();
        offsets.push_back(_writePos - start);
        appendVariant(element_variant);
    }

    finishWritingArray(start, offsets);
}

void VariantBuilder::handlePrimitive(const Variant& variant) {
    std::string_view value_data = variant.value();
    size_t data_size = value_data.size();

    _checkCapacity(data_size);
    std::copy(value_data.begin(), value_data.end(), _writeBuffer.begin() + _writePos);
    _writePos += data_size;
}

VariantValue VariantBuilder::result() {
    std::string metadata = _buildMetadata();
    std::vector<uint8_t> value(_writeBuffer.begin(), _writeBuffer.begin() + _writePos);
    return VariantValue(std::move(metadata), std::string(value.begin(), value.end()));
}

std::vector<uint8_t> VariantBuilder::valueWithoutMetadata() {
    return std::vector<uint8_t>(_writeBuffer.begin(), _writeBuffer.begin() + _writePos);
}

void VariantBuilder::_checkCapacity(int additional) {
    if (_writePos + additional > _writeBuffer.size()) {
        _writeBuffer.resize(std::max(static_cast<size_t>(_writePos + additional), _writeBuffer.size() * 2));
    }
}

void VariantBuilder::_writeLittleEndian(uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        _writeBuffer[_writePos++] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

uint8_t VariantBuilder::_primitiveHeader(int primitiveType) const {
    return (static_cast<uint8_t>(primitiveType) << 2) | static_cast<uint8_t>(BasicType::PRIMITIVE);
}

uint8_t VariantBuilder::_getIntegerSize(int value) const {
    if (value <= 0xFF) return 1;
    if (value <= 0xFFFF) return 2;
    if (value <= 0xFFFFFF) return 3;
    return 4;
}

std::string VariantBuilder::_buildMetadata() const {
    if (_dictionaryKeys.empty()) {
        return std::string(VariantMetadata::kEmptyMetadata);
    }

    std::string metadata;

    int totalKeySize = 0;
    for (const std::string& key : _dictionaryKeys) {
        totalKeySize += key.length();
    }

    uint8_t offsetSize = _getIntegerSize(totalKeySize);

    // Header: version=1(4bits) + 0(1bit) + sorted=1(1bit) + offset_size(2bits)
    // Bit 0-3: version, Bit 5: sorted flag, Bit 6-7: offset_size-1
    // Using named constants for clarity and maintainability
    uint8_t header = kMetadataVersion |
                     (kSortedFlag << kSortedFlagShift) |
                     ((offsetSize - 1) << kOffsetSizeShift);
    metadata.push_back(header);

    int dictSize = _dictionaryKeys.size();
    for (uint8_t i = 0; i < offsetSize; ++i) {
        metadata.push_back(static_cast<char>((dictSize >> (i * 8)) & 0xFF));
    }

    std::vector<int> offsets;
    int currentOffset = 0;
    for (const std::string& key : _dictionaryKeys) {
        offsets.push_back(currentOffset);
        currentOffset += key.length();
    }
    offsets.push_back(currentOffset);

    for (int offset : offsets) {
        for (uint8_t i = 0; i < offsetSize; ++i) {
            metadata.push_back(static_cast<char>((offset >> (i * 8)) & 0xFF));
        }
    }

    for (const std::string& key : _dictionaryKeys) {
        metadata.append(key);
    }

    return metadata;
}

} // namespace starrocks::parquet
