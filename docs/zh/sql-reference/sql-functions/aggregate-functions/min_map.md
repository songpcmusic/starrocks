---
displayed_sidebar: docs
description: "按 key 独立计算多个 MAP 值中的最小值。"
---

# min_map

## 功能

按 key 独立计算多行 MAP 中非 NULL value 的最小值。某行中缺失的 key 不参与计算，也不会贡献默认值。

在 BE 列数据层，重复的物理 key 条目会被逐项处理。但部分 SQL MAP 构造函数（包括 `map_from_arrays`）
会在聚合前规范化重复 key 并保留最后一个 value。

## 语法

```SQL
min_map(map_expr)
```

## 参数

`map_expr` 必须为 MAP。key 支持 BOOLEAN、各整数类型、FLOAT、DOUBLE、DECIMALV2、DECIMAL32/64/128、
CHAR、VARCHAR、DATE 和 DATETIME；不支持 DECIMAL256 key。

value 支持 BOOLEAN、各整数类型、FLOAT、DOUBLE、DECIMALV2、DECIMAL32/64/128/256、CHAR、VARCHAR、DATE 和
DATETIME。

## 返回值

返回与输入具有完全相同 key 和 value 类型的 `MAP<K,V>`，Decimal 的精度和小数位数保持不变。MAP 条目的
返回顺序不做保证；请使用下标按 key 读取 value，不要依赖 MAP 的文本展示顺序。

对于浮点 value，任何非 NaN 都小于 NaN。因此 NaN 与普通数混合时返回最小的非 NaN 值；某个 key 的
value 全为 NaN 时返回 NaN。Infinity 按普通数值顺序比较。

## NULL 处理

- 跳过整个值为 `NULL` 的 MAP；如果所有输入 MAP 都为 `NULL`，结果为 `NULL`。
- 接受无类型的 `NULL` 参数并返回 `NULL`。
- `NULL` key 作为一个独立 key 保留并参与聚合。
- `NULL` value 不参与最小值计算，但保留对应 key；如果某个 key 的所有 value 都为 `NULL`，结果中的
  value 也是 `NULL`。
- 空 MAP 不贡献任何条目；如果所有非 NULL 输入都是空 MAP，结果为 `{}`。

## 示例

```SQL
SELECT min_map(m)
FROM (
    SELECT map{'latency': 20, 'errors': 3} AS m
    UNION ALL
    SELECT map{'latency': 10}
) t;

-- {'errors':3,'latency':10}
```

缺失 key 和 NULL value 不会提供默认值：

```SQL
SELECT min_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 5, 2: NULL, 3: 30}
) t;

-- {1:5,2:NULL,3:30}
```

## 关键词

MIN_MAP, MAP, AGGREGATE
