---
displayed_sidebar: docs
description: "按键计算多个 MAP 值中数值的平均值。"
---

# avg_map

## 功能

按 key 独立计算多行 MAP 的平均值。`avg_map` 为每个 key 保存各自的 sum 和 count，因此分布式合并中间结果时
会按实际样本数加权，不会对局部平均值再次取平均。

只有某个 key 实际出现且 value 不为 `NULL` 时才计入该 key 的样本数；缺失的 key 不会按零值参与计算。
如果同一个 MAP 中存在重复 key，每个条目都作为一次独立观测。

在 BE 列数据层，重复 key 条目会被逐项处理。但部分 SQL MAP 构造函数（包括 `map_from_arrays`）会在聚合前
规范化重复 key 并保留最后一个 value，因此被构造函数丢弃的条目对 `avg_map` 不可见。

## 语法

```SQL
avg_map(map_expr)
```

## 参数

`map_expr` 必须为 MAP，value 支持 BOOLEAN、TINYINT、SMALLINT、INT、BIGINT、LARGEINT、FLOAT、DOUBLE、
DECIMALV2、DECIMAL32、DECIMAL64 和 DECIMAL128，不支持 DECIMAL256。

key 支持 BOOLEAN、整数、浮点数、DECIMALV2、DECIMAL32、DECIMAL64、DECIMAL128、CHAR、VARCHAR、DATE 和
DATETIME。

## 返回值

返回 `MAP<K, DOUBLE>`，其中 `K` 与输入 key 类型相同。MAP 条目的返回顺序不做保证；请使用下标按 key
读取 value，不要依赖 MAP 的文本展示顺序。

## NULL 处理

- 跳过值为 `NULL` 的整个 MAP。
- 接受无类型的 `NULL` 参数并返回 `NULL`，与 StarRocks 的 `sum_map` 保持一致。
- `NULL` key 会作为一个独立 key 保留并参与聚合。
- value 为 `NULL` 时不更新 sum 和 count，但保留对应 key；如果某个 key 的所有 value 都为 `NULL`，
  结果中该 key 的 value 也是 `NULL`。
- 空 MAP 不贡献任何条目。

## 示例

```SQL
SELECT avg_map(m) AS averages
FROM (
    SELECT map{'clicks': 10, 'latency': 20} AS m
    UNION ALL
    SELECT map{'clicks': 30} AS m
) t;

-- {'clicks':20, 'latency':20}
```

第二行缺失的 `latency` 不会按零计数：

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

`NULL` value 会保留 key，但不计入观测：

```SQL
SELECT avg_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 20, 2: NULL}
) t;

-- {1:15, 2:NULL}
```

## 关键词

AVG_MAP, MAP, AGGREGATE
