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

#include "exprs/agg/aggregate_factory.h"
#include "runtime/mem_pool.h"

namespace starrocks {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _function->destroy(_context, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* context, const AggregateFunction* function) {
        return std::make_unique<ManagedAggrState>(context, function);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext* context, const AggregateFunction* function)
            : _context(context), _function(function) {
        _state = _mem_pool.allocate_aligned(function->size(), function->alignof_size());
        _function->create(_context, _state);
    }

    FunctionContext* _context;
    const AggregateFunction* _function;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace starrocks
