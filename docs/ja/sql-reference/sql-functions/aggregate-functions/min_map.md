---
displayed_sidebar: docs
description: "複数の MAP 値についてキーごとの最小値を計算します。"
---

# min_map

## 説明

複数行の MAP に対して、キーごとに非 NULL value の最小値を計算します。ある行に存在しないキーは計算に
寄与せず、デフォルト値としても扱われません。

BE のカラムデータでは、重複する物理キーエントリを個別に処理します。ただし、`map_from_arrays` など一部の
SQL MAP コンストラクターは集約前に重複キーを正規化し、最後の value を保持します。

## 構文

```SQL
min_map(map_expr)
```

## パラメータ

`map_expr` は MAP である必要があります。key は BOOLEAN、各整数型、FLOAT、DOUBLE、DECIMALV2、
DECIMAL32/64/128、CHAR、VARCHAR、DATE、DATETIME をサポートします。

value は BOOLEAN、各整数型、FLOAT、DOUBLE、DECIMALV2、DECIMAL32/64/128、CHAR、VARCHAR、DATE、
DATETIME をサポートします。

## 戻り値

入力と同じ key 型と value 型の `MAP<K,V>` を返します。Decimal の precision と scale は保持されます。
MAP エントリの順序は保証されません。テキスト表示の順序に依存せず、map の添字で value を取得してください。

浮動小数点 value では、すべての非 NaN 値を NaN より小さいものとして扱います。そのため NaN と非 NaN が
混在する場合は最小の非 NaN 値を返し、すべて NaN のキーは NaN を返します。Infinity は通常の数値順序に
従います。

## NULL の処理

- 入力 MAP 全体が `NULL` の場合はスキップします。すべての入力 MAP が `NULL` なら結果も `NULL` です。
- 型なしの `NULL` 引数を受け入れ、`NULL` を返します。
- `NULL` key は一つの独立した key として保持し、集約します。
- `NULL` value は最小値の計算には含めませんが、key は保持します。ある key の value がすべて `NULL`
  の場合、結果の value も `NULL` です。
- 空の MAP はエントリを追加しません。非 NULL 入力がすべて空なら結果は `{}` です。

## 例

```SQL
SELECT min_map(m)
FROM (
    SELECT map{'latency': 20, 'errors': 3} AS m
    UNION ALL
    SELECT map{'latency': 10}
) t;

-- {'errors':3,'latency':10}
```

存在しない key と NULL value はデフォルト値を追加しません。

```SQL
SELECT min_map(m)
FROM (
    SELECT map{1: 10, 2: NULL} AS m
    UNION ALL
    SELECT map{1: 5, 2: NULL, 3: 30}
) t;

-- {1:5,2:NULL,3:30}
```

## キーワード

MIN_MAP, MAP, AGGREGATE
