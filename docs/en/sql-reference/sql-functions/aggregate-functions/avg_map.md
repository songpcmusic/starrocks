---
displayed_sidebar: docs
description: "Calculates an average for each key across MAP values."
---

# avg_map

## Description

Calculates an average independently for each key across multiple MAP values. `avg_map` keeps a separate sum and count
for every key, so distributed partial results are weighted by their observation counts when they are merged.

A key is counted only in map entries where it is present and its value is not `NULL`. Missing keys do not contribute a
zero. If the same key occurs more than once in one map, every entry is an observation.

At the BE column level, repeated key entries are processed independently. Some SQL MAP constructors, including
`map_from_arrays`, canonicalize repeated keys before aggregation and keep the last value, so those discarded entries
are not visible to `avg_map`.

## Syntax

```SQL
avg_map(map_expr)
```

## Parameters

`map_expr` must be a MAP whose value type is one of BOOLEAN, TINYINT, SMALLINT, INT, BIGINT, LARGEINT, FLOAT, DOUBLE,
DECIMALV2, DECIMAL32, DECIMAL64, or DECIMAL128. DECIMAL256 is not supported.

Supported key types are BOOLEAN, integer, floating-point, DECIMALV2, DECIMAL32, DECIMAL64, DECIMAL128, CHAR, VARCHAR,
DATE, and DATETIME.

## Return value

Returns `MAP<K, DOUBLE>`, where `K` is the input key type. MAP entry order is not guaranteed. Use map subscripting to
read a value by key instead of relying on the textual entry order.

## NULL handling

- A `NULL` input map is skipped.
- An untyped `NULL` argument is accepted and returns `NULL`, consistently with `sum_map` in StarRocks.
- A `NULL` key is retained and aggregated like one distinct key.
- A `NULL` value does not update the sum or count, but its key is retained. A key whose observations are all `NULL`
  has a `NULL` result value.
- Empty maps contribute no entries.

## Examples

```SQL
SELECT avg_map(m) AS averages
FROM (
    SELECT map{'clicks': 10, 'latency': 20} AS m
    UNION ALL
    SELECT map{'clicks': 30} AS m
) t;

-- {'clicks':20, 'latency':20}
```

The missing `latency` key in the second row does not count as zero:

```SQL
SELECT avg_map(m)[1]
FROM (
    SELECT map{1: 0} AS m
    UNION ALL SELECT map{1: 10}
    UNION ALL SELECT map{1: 20}
    UNION ALL SELECT map{2: 100}
) t;

-- 10
```

`NULL` values retain the key but do not contribute an observation:

```SQL
SELECT avg_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 20, 2: NULL}
) t;

-- {1:15, 2:NULL}
```

## Keywords

AVG_MAP, MAP, AGGREGATE
