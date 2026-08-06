---
displayed_sidebar: docs
description: "複数の MAP 値についてキーごとの平均値を計算します。"
---

# avg_map

## 説明

複数行の MAP に対して、キーごとに独立した平均値を計算します。`avg_map` はキーごとに sum と count を
保持するため、分散実行の部分結果は実際の観測数で重み付けしてマージされます。

キーが存在し、かつ value が `NULL` でないエントリだけがそのキーの観測数に含まれます。存在しないキーは
ゼロとして数えません。同じ MAP 内に同一キーが複数ある場合、各エントリを個別の観測として扱います。

BE のカラムデータでは重複キーの各エントリを個別に処理します。ただし、`map_from_arrays` など一部の SQL
MAP コンストラクターは集約前に重複キーを正規化して最後の value を保持するため、破棄されたエントリは
`avg_map` から参照できません。

## 構文

```SQL
avg_map(map_expr)
```

## パラメータ

`map_expr` は MAP である必要があります。value は BOOLEAN、TINYINT、SMALLINT、INT、BIGINT、LARGEINT、
FLOAT、DOUBLE、DECIMALV2、DECIMAL32、DECIMAL64、DECIMAL128 をサポートします。DECIMAL256 は未対応です。

key は BOOLEAN、整数、浮動小数点数、DECIMALV2、DECIMAL32、DECIMAL64、DECIMAL128、CHAR、VARCHAR、
DATE、DATETIME をサポートします。

## 戻り値

入力と同じキー型 `K` を持つ `MAP<K, DOUBLE>` を返します。MAP エントリの順序は保証されません。
テキスト表示の順序に依存せず、map の添字を使用してキーごとに value を取得してください。

## NULL の処理

- 入力 MAP 全体が `NULL` の場合はスキップします。
- 型なしの `NULL` 引数を受け入れて `NULL` を返します。これは StarRocks の `sum_map` と同じ動作です。
- `NULL` キーは独立した一つのキーとして保持し、集約します。
- value が `NULL` の場合、sum と count は更新しませんがキーは保持します。あるキーの value がすべて
  `NULL` の場合、結果の value も `NULL` です。
- 空の MAP はエントリを追加しません。

## 例

```SQL
SELECT avg_map(m) AS averages
FROM (
    SELECT map{'clicks': 10, 'latency': 20} AS m
    UNION ALL
    SELECT map{'clicks': 30} AS m
) t;

-- {'clicks':20, 'latency':20}
```

2 行目に存在しない `latency` はゼロとして数えません。

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

`NULL` value はキーを保持しますが、観測には含まれません。

```SQL
SELECT avg_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 20, 2: NULL}
) t;

-- {1:15, 2:NULL}
```

## キーワード

AVG_MAP, MAP, AGGREGATE
