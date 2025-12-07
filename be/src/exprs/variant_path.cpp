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

#include "exprs/variant_path.h"

#include <re2/re2.h>
#include <re2/stringpiece.h>

#include <boost/tokenizer.hpp>
#include <memory>

#include "common/compiler_util.h"
#include "common/status.h"
#include "exprs/function_helper.h"
#include "glog/logging.h"
#include "gutil/strings/split.h"
#include "gutil/strings/substitute.h"

namespace starrocks {

static const re2::RE2 VARIANT_PATH_PATTERN(R"(^([^\"\[\]]*)((?:\[(?:[0-9\*]+)\])*))", re2::RE2::Quiet);

static const re2::RE2 VAR_ARRAY_INDEX_PATTERN(R"(\[([0-9\*]+)\])");
static const re2::RE2 VAR_ARRAY_SINGLE_SELECTOR(R"(\d+)", re2::RE2::Quiet);
static const std::string VARIANT_PATH_ROOT = "$";

bool VariantArraySelectorSingle::match(const std::string& input) {
    return RE2::FullMatch(input, VAR_ARRAY_SINGLE_SELECTOR);
}

bool VariantArraySelectorWildcard::match(const std::string& input) {
    return input == "*";
}

Status VariantArraySelector::parse(const std::string& index, 
                                   std::unique_ptr<VariantArraySelector>* output) {
    if (index.empty()) {
        *output = std::make_unique<VariantArraySelectorNone>();
        return Status::OK();
    } else if (VariantArraySelectorSingle::match(index)) {
        StringParser::ParseResult result;
        int index_int = StringParser::string_to_int<int>(index.c_str(), index.length(), &result);
        if (result != StringParser::PARSE_SUCCESS) {
            return Status::InvalidArgument(strings::Substitute("Invalid variant path: $0", index));
        }
        *output = std::make_unique<VariantArraySelectorSingle>(index_int);
        return Status::OK();
    } else if (VariantArraySelectorWildcard::match(index)) {
        *output = std::make_unique<VariantArraySelectorWildcard>();
        return Status::OK();
    }

    return Status::InvalidArgument(strings::Substitute("Invalid variant path: $0", index));
}

Status VariantPathPiece::parse(const std::string& path_string, 
                              std::vector<VariantPathPiece>* parsed_paths) {
    if (path_string.empty()) {
        return Status::InvalidArgument("Empty variant path");
    }

    std::vector<std::string> path_exprs;
    try {
        boost::tokenizer<boost::escaped_list_separator<char>> tok(
            path_string, boost::escaped_list_separator<char>("\\", ".", "\""));
        path_exprs.assign(tok.begin(), tok.end());
    } catch (const boost::escaped_list_error& e) {
        return Status::InvalidArgument(
            strings::Substitute("Invalid variant path $0", e.what()));
    }

    for (size_t i = 0; i < path_exprs.size(); i++) {
        std::string variable;
        std::string array_pieces;
        auto& current = path_exprs[i];

        if (i == 0) {
            std::shared_ptr<VariantArraySelector> selector(new VariantArraySelectorNone());
            if (current != "$") {
                parsed_paths->emplace_back(VariantPathPiece("$", std::move(selector)));
            } else {
                parsed_paths->emplace_back(VariantPathPiece("$", std::move(selector)));
                continue;
            }
        }

        if (!RE2::FullMatch(current, VARIANT_PATH_PATTERN, &variable, &array_pieces)) {
            parsed_paths->emplace_back("", 
                std::unique_ptr<VariantArraySelector>(new VariantArraySelectorNone()));
            return Status::InvalidArgument(
                strings::Substitute("Invalid variant path: $0", path_exprs[i]));
        } else if (array_pieces.empty()) {
            // 没有数组选择器
            std::unique_ptr<VariantArraySelector> selector;
            RETURN_IF_ERROR(VariantArraySelector::parse(array_pieces, &selector));
            parsed_paths->emplace_back(VariantPathPiece(variable, std::move(selector)));
        } else {
            // 处理多个数组选择器
            re2::StringPiece array_piece(array_pieces);
            std::string single_piece;
            while (RE2::Consume(&array_piece, VAR_ARRAY_INDEX_PATTERN, &single_piece)) {
                std::unique_ptr<VariantArraySelector> selector;
                RETURN_IF_ERROR(VariantArraySelector::parse(single_piece, &selector));
                parsed_paths->emplace_back(VariantPathPiece(variable, std::move(selector)));
                variable = "";
            }
        }
    }

    return Status::OK();
}

StatusOr<Variant> VariantPathPiece::extract(
        const Variant& variant, 
        const std::vector<VariantPathPiece>& variant_path) {
    return extract(variant, variant_path, 1);
}

StatusOr<Variant> VariantPathPiece::extract(
        const Variant& root, 
        const std::vector<VariantPathPiece>& variant_path,
        size_t path_index) {
    Variant current_value = root;

    for (size_t i = path_index; i < variant_path.size(); i++) {
        auto& path_item = variant_path[i];
        auto item_key = path_item.key;
        auto& array_selector = path_item.array_selector;

        Variant next_item = current_value;
        
        if (item_key == VARIANT_PATH_ROOT) {
            next_item = root;
        } else if (!item_key.empty()) {
            if (current_value.basic_type() != BasicType::OBJECT) {
                return Status::NotFound("Path not found: not an object");
            }

            auto field_result = current_value.get_object_by_key(item_key);
            if (!field_result.ok()) {
                return field_result.status();
            }
            next_item = field_result.value();
        }

        switch (array_selector->type) {
        case VAR_INVALID:
            DCHECK(false);
            break;
        case VAR_NONE:
            break;
        case VAR_SINGLE: {
            if (next_item.basic_type() != BasicType::ARRAY) {
                return Status::NotFound("Path not found: not an array");
            }
            auto single_selector = static_cast<VariantArraySelectorSingle*>(array_selector.get());
            auto element_result = next_item.get_element_at_index(single_selector->index);
            if (!element_result.ok()) {
                return element_result.status();
            }
            next_item = element_result.value();
            break;
        }
        case VAR_WILDCARD: {
            return Status::NotSupported("Wildcard selector not supported yet");
        }
        }

        current_value = next_item;
    }

    return current_value;
}

void VariantPath::reset(const VariantPath& rhs) {
    paths = rhs.paths;
}

void VariantPath::reset(VariantPath&& rhs) {
    paths = std::move(rhs.paths);
}

StatusOr<VariantPath> VariantPath::parse(const Slice& path_string) {
    std::vector<VariantPathPiece> pieces;
    RETURN_IF_ERROR(VariantPathPiece::parse(path_string.to_string(), &pieces));
    return VariantPath(pieces);
}

StatusOr<Variant> VariantPath::extract(const Variant& variant, const VariantPath& variant_path) {
    return VariantPathPiece::extract(variant, variant_path.paths);
}

} // namespace starrocks
