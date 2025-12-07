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

#include <memory>
#include <string>
#include <vector>

#include "common/status.h"
#include "formats/parquet/variant.h"
#include "util/slice.h"

namespace starrocks {

enum VariantArraySelectorType {
    VAR_INVALID,
    VAR_NONE,
    VAR_SINGLE,
    VAR_WILDCARD,
};

struct VariantArraySelector {
    VariantArraySelectorType type = VAR_INVALID;

    VariantArraySelector() = default;
    virtual ~VariantArraySelector() = default;

    static Status parse(const std::string& str, std::unique_ptr<VariantArraySelector>* output);

    virtual bool match(const VariantArraySelector& other) const { 
        return type == other.type; 
    }
};

struct VariantArraySelectorNone final : public VariantArraySelector {
    VariantArraySelectorNone() { type = VAR_NONE; }
};

struct VariantArraySelectorSingle final : public VariantArraySelector {
    int index;

    explicit VariantArraySelectorSingle(int idx) : index(idx) { type = VAR_SINGLE; }

    static bool match(const std::string& input);

    bool match(const VariantArraySelector& other) const override {
        if (type != other.type) return false;
        return index == static_cast<const VariantArraySelectorSingle*>(&other)->index;
    }
};

struct VariantArraySelectorWildcard final : public VariantArraySelector {
    VariantArraySelectorWildcard() { type = VAR_WILDCARD; }

    static bool match(const std::string& input);
};

struct VariantPathPiece {
    std::string key;
    std::shared_ptr<VariantArraySelector> array_selector;

    VariantPathPiece(std::string k, std::shared_ptr<VariantArraySelector> selector)
            : key(std::move(k)), array_selector(std::move(selector)) {}

    VariantPathPiece(std::string k, VariantArraySelector* selector) 
            : key(std::move(k)), array_selector(selector) {}

    static Status parse(const std::string& path_string, 
                       std::vector<VariantPathPiece>* parsed_path);

    static StatusOr<Variant> extract(const Variant& variant, 
                                     const std::vector<VariantPathPiece>& variant_path);
    
    static StatusOr<Variant> extract(const Variant& root, 
                                     const std::vector<VariantPathPiece>& variant_path,
                                     size_t path_index);
};

struct VariantPath {
    std::vector<VariantPathPiece> paths;

    explicit VariantPath(std::vector<VariantPathPiece> value) : paths(std::move(value)) {}
    VariantPath() = default;
    VariantPath(VariantPath&&) = default;
    VariantPath(const VariantPath&) = default;
    VariantPath& operator=(const VariantPath&) = default;
    VariantPath& operator=(VariantPath&&) = default;
    ~VariantPath() = default;

    void reset(const VariantPath& rhs);
    void reset(VariantPath&& rhs);

    bool is_empty() const { return paths.empty(); }

    static StatusOr<VariantPath> parse(const Slice& path_string);

    static StatusOr<Variant> extract(const Variant& variant, const VariantPath& variant_path);
};

} // namespace starrocks
