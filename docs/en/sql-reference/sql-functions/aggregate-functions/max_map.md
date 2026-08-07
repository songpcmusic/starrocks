---
displayed_sidebar: docs
description: "Calculates the maximum value independently for each key across MAP values."
---

# max_map

## Description

Calculates the maximum non-NULL value independently for each key across multiple MAP values. A key that is absent
from a row contributes nothing; it does not contribute a default value.

At the BE column level, repeated physical key entries are processed independently. Some SQL MAP constructors,
including `map_from_arrays`, canonicalize repeated keys before aggregation and keep the last value.

## Syntax

```SQL
max_map(map_expr)
```

## Parameters

`map_expr` must be a MAP. Supported key types are BOOLEAN, integer types, FLOAT, DOUBLE, DECIMALV2,
DECIMAL32/64/128, CHAR, VARCHAR, DATE, and DATETIME.

Supported value types are BOOLEAN, integer types, FLOAT, DOUBLE, DECIMALV2, DECIMAL32/64/128, CHAR, VARCHAR,
DATE, and DATETIME.

## Return value

Returns `MAP<K,V>` with the same key and value types as the input. Decimal precision and scale are preserved.
MAP entry order is not guaranteed. Use map subscripting to read a value by key instead of relying on the textual entry
order.

For floating-point values, every non-NaN value is greater than NaN. Therefore a mixture of NaN and non-NaN values
returns the maximum non-NaN value, while an all-NaN key returns NaN. Infinity follows normal numeric ordering.

## NULL handling

- A `NULL` input MAP is skipped. If every input MAP is `NULL`, the result is `NULL`.
- An untyped `NULL` argument is accepted and returns `NULL`.
- A `NULL` key is retained and aggregated as one distinct key.
- A `NULL` value does not participate in the maximum, but its key is retained. A key whose values are all `NULL`
  has a `NULL` result value.
- Empty MAPs contribute no entries. If all non-NULL inputs are empty MAPs, the result is `{}`.

## Examples

```SQL
SELECT max_map(m)
FROM (
    SELECT map{'latency': 20, 'errors': 3} AS m
    UNION ALL
    SELECT map{'latency': 10}
) t;

-- {'errors':3,'latency':20}
```

Missing keys and NULL values do not supply defaults:

```SQL
SELECT max_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 5, 2: NULL, 3: 30}
) t;

-- {1:10,2:NULL,3:30}
```

## Keywords

MAX_MAP, MAP, AGGREGATE
