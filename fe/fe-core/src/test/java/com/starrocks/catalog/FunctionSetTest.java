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

package com.starrocks.catalog;

import com.google.common.collect.Lists;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.type.AnyArrayType;
import com.starrocks.type.AnyElementType;
import com.starrocks.type.ArrayType;
import com.starrocks.type.BitmapType;
import com.starrocks.type.BooleanType;
import com.starrocks.type.CharType;
import com.starrocks.type.DateType;
import com.starrocks.type.DecimalType;
import com.starrocks.type.FloatType;
import com.starrocks.type.HLLType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.InvalidType;
import com.starrocks.type.JsonType;
import com.starrocks.type.MapType;
import com.starrocks.type.NullType;
import com.starrocks.type.Type;
import com.starrocks.type.VarbinaryType;
import com.starrocks.type.VarcharType;
import com.starrocks.type.VariantType;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class FunctionSetTest {

    private FunctionSet functionSet;

    private static final Type VARCHAR_ARRAY = new ArrayType(VarcharType.VARCHAR);
    private static final Type TINYINT_ARRAY = new ArrayType(IntegerType.TINYINT);
    private static final Type INT_ARRAY = new ArrayType(IntegerType.INT);
    private static final Type DOUBLE_ARRAY = new ArrayType(FloatType.DOUBLE);
    private static final Type INT_ARRAY_ARRAY = new ArrayType(INT_ARRAY);
    private static final Type TINYINT_ARRAY_ARRAY = new ArrayType(TINYINT_ARRAY);
    private static final Type VARCHAR_ARRAY_ARRAY = new ArrayType(VARCHAR_ARRAY);

    @BeforeEach
    public void setUp() {
        functionSet = new FunctionSet();
        functionSet.init();
    }

    @Test
    public void testGetLagFunction() {
        Type[] argTypes1 = {DecimalType.DECIMALV2, IntegerType.TINYINT, IntegerType.TINYINT};
        Function lagDesc1 = new Function(new FunctionName(FunctionSet.LAG), argTypes1, InvalidType.INVALID, false);
        Function newFunction = functionSet.getFunction(lagDesc1, Function.CompareMode.IS_SUPERTYPE_OF);
        Type[] newArgTypes = newFunction.getArgs();
        Assertions.assertTrue(newArgTypes[0].matchesType(newArgTypes[2]));
        Assertions.assertTrue(newArgTypes[0].matchesType(DecimalType.DECIMALV2));

        Type[] argTypes2 = {VarcharType.VARCHAR, IntegerType.TINYINT, IntegerType.TINYINT};
        Function lagDesc2 = new Function(new FunctionName(FunctionSet.LAG), argTypes2, InvalidType.INVALID, false);
        newFunction = functionSet.getFunction(lagDesc2, Function.CompareMode.IS_SUPERTYPE_OF);
        newArgTypes = newFunction.getArgs();
        Assertions.assertTrue(newArgTypes[0].matchesType(newArgTypes[2]));
        Assertions.assertTrue(newArgTypes[0].matchesType(VarcharType.VARCHAR));
    }

    @Test
    public void testPolymorphicFunction() {
        // array_append(ARRAY<INT>, INT)
        Type[] argTypes = {INT_ARRAY, IntegerType.INT};
        Function desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        Function fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(IntegerType.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, TINYINT)
        argTypes = new Type[] {INT_ARRAY, IntegerType.TINYINT};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(IntegerType.INT, fn.getArgs()[1]);

        // array_append(ARRAY<TINYINT>, INT)
        argTypes = new Type[] {TINYINT_ARRAY, IntegerType.INT};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(IntegerType.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, DOUBLE)
        argTypes = new Type[] {INT_ARRAY, FloatType.DOUBLE};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(DOUBLE_ARRAY, fn.getReturnType());
        Assertions.assertEquals(DOUBLE_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(FloatType.DOUBLE, fn.getArgs()[1]);

        // array_append(NULL, INT)
        argTypes = new Type[] {NullType.NULL, IntegerType.INT};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(IntegerType.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, NULL)
        argTypes = new Type[] {INT_ARRAY, NullType.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(IntegerType.INT, fn.getArgs()[1]);

        // array_append(ARRAY<TINYINT>, VARCHAR)
        argTypes = new Type[] {TINYINT_ARRAY, VarcharType.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(VarcharType.VARCHAR, fn.getArgs()[1]);

        // array_append(NULL, NULL)
        argTypes = new Type[] {NullType.NULL, NullType.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(new ArrayType(BooleanType.BOOLEAN), fn.getReturnType());
        Assertions.assertEquals(new ArrayType(BooleanType.BOOLEAN), fn.getArgs()[0]);

        // array_append(ARRAY<ARRAY<INT>>, ARRAY<INT>)
        argTypes = new Type[] {INT_ARRAY_ARRAY, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<INT>>, ARRAY<TINYINT>)
        argTypes = new Type[] {INT_ARRAY_ARRAY, TINYINT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, ARRAY<INT>)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, NULL)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, NullType.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(TINYINT_ARRAY_ARRAY, fn.getReturnType());
        Assertions.assertEquals(TINYINT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(TINYINT_ARRAY, fn.getArgs()[1]);

        // array_append(NULL, ARRAY<INT>)
        argTypes = new Type[] {NullType.NULL, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, TINYINT)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, IntegerType.TINYINT};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNull(fn);

        // array_append(ARRAY<ARRAY<TINYINT>>, ARRAY<VARCHAR>)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, VARCHAR_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(new ArrayType(VARCHAR_ARRAY), fn.getReturnType());
        Assertions.assertEquals(new ArrayType(VARCHAR_ARRAY), fn.getArgs()[0]);
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<VARCHAR>, VARCHAR)
        argTypes = new Type[] {VARCHAR_ARRAY, VarcharType.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(VarcharType.VARCHAR, fn.getArgs()[1]);

        // array_append(ARRAY<VARCHAR>, CHAR)
        argTypes = new Type[] {VARCHAR_ARRAY, CharType.CHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assertions.assertEquals(VarcharType.VARCHAR, fn.getArgs()[1]);

        // array_append(VARCHAR, VARCHAR)
        argTypes = new Type[] {VarcharType.VARCHAR, VarcharType.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNull(fn);

        // array_append(INT, VARCHAR)
        argTypes = new Type[] {IntegerType.INT, VarcharType.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNull(fn);

        // array_length(INT)
        argTypes = new Type[] {IntegerType.INT};
        desc = new Function(new FunctionName("array_length"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNull(fn);

        // array_length(INT)
        argTypes = new Type[] {IntegerType.INT};
        desc = new Function(new FunctionName("array_length"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNull(fn);

        // array_length(ARRAY<INT>)
        argTypes = new Type[] {INT_ARRAY};
        desc = new Function(new FunctionName("array_length"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(IntegerType.INT, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);

        // array_length(ARRAY<ARRAY<INT>>)
        argTypes = new Type[] {INT_ARRAY_ARRAY};
        desc = new Function(new FunctionName("array_length"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(IntegerType.INT, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);

        // array_length(NULL)
        argTypes = new Type[] {NullType.NULL};
        desc = new Function(new FunctionName("array_length"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(IntegerType.INT, fn.getReturnType());
        Assertions.assertEquals(new ArrayType(BooleanType.BOOLEAN), fn.getArgs()[0]);

        // array_generate(SmallInt,Int,BigInt)
        argTypes = new Type[] {IntegerType.SMALLINT, IntegerType.INT, IntegerType.BIGINT};
        desc = new Function(new FunctionName("array_generate"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(ArrayType.ARRAY_BIGINT, fn.getReturnType());
        Assertions.assertEquals(IntegerType.BIGINT, fn.getArgs()[0]);

        // arrays_overlap
        argTypes = new Type[] {ArrayType.ARRAY_BIGINT, ArrayType.ARRAY_TINYINT};
        desc = new Function(new FunctionName("arrays_overlap"), argTypes, BooleanType.BOOLEAN, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(fn.functionId, 150216L);

        // array_flatten(ARRAY<ARRAY<TINYINT>>)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY};
        desc = new Function(new FunctionName("array_flatten"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(TINYINT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(TINYINT_ARRAY_ARRAY, fn.getArgs()[0]);

        // array_flatten(ARRAY<ARRAY<INT>>)
        argTypes = new Type[] {INT_ARRAY_ARRAY};
        desc = new Function(new FunctionName("array_flatten"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(INT_ARRAY, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);

        // array_flatten(ARRAY<ARRAY<INT>>)
        argTypes = new Type[] {VARCHAR_ARRAY_ARRAY};
        desc = new Function(new FunctionName("array_flatten"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assertions.assertEquals(VARCHAR_ARRAY_ARRAY, fn.getArgs()[0]);

        // null_or_empty(null)
        argTypes = new Type[] {NullType.NULL};
        desc = new Function(new FunctionName("null_or_empty"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(BooleanType.BOOLEAN, fn.getReturnType());
        Assertions.assertEquals(VarcharType.VARCHAR, fn.getArgs()[0]);

        // null_or_empty(ARRAY<INT>)
        argTypes = new Type[] {INT_ARRAY};
        desc = new Function(new FunctionName("null_or_empty"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(BooleanType.BOOLEAN, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY, fn.getArgs()[0]);

        // null_or_empty(ARRAY<ARRAY<INT>>)
        argTypes = new Type[] {INT_ARRAY_ARRAY};
        desc = new Function(new FunctionName("null_or_empty"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(fn);
        Assertions.assertEquals(BooleanType.BOOLEAN, fn.getReturnType());
        Assertions.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);

        // coalesce
        argTypes = new Type[] {INT_ARRAY_ARRAY, DOUBLE_ARRAY};
        desc = new Function(new FunctionName("coalesce"), argTypes, InvalidType.INVALID, false);
        try {
            functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assertions.fail();
        } catch (Exception e) {
            Assertions.assertTrue(e instanceof SemanticException);
            Assertions.assertTrue(e.getMessage().contains("in the function [coalesce]"));
        }
    }

    @Test
    public void testPolymorphicTVF() {
        // First two columns of the result are polymorphic (derived from argument type), but the last argument is of a
        // "concrete" type BIGINT, which is retained in the resolved function.
        TableFunction polymorphicTVF =
                new TableFunction(new FunctionName("three_column_tvf"), Lists.newArrayList("a", "b", "c"),
                        Lists.newArrayList(AnyArrayType.ANY_ARRAY),
                        Lists.newArrayList(AnyElementType.ANY_ELEMENT, AnyElementType.ANY_ELEMENT, IntegerType.BIGINT));

        functionSet.addBuiltin(polymorphicTVF);

        Type[] argTypes = new Type[] {VARCHAR_ARRAY};
        Function desc = new Function(new FunctionName("three_column_tvf"), argTypes, InvalidType.INVALID, false);
        Function fn = functionSet.getFunction(desc, Function.CompareMode.IS_IDENTICAL);
        Assertions.assertNotNull(fn);
        Assertions.assertTrue(fn instanceof TableFunction);
        TableFunction tableFunction = (TableFunction) fn;
        Assertions.assertEquals(3, tableFunction.getTableFnReturnTypes().size());
        Assertions.assertEquals(VarcharType.VARCHAR, tableFunction.getTableFnReturnTypes().get(0));
        Assertions.assertEquals(VarcharType.VARCHAR, tableFunction.getTableFnReturnTypes().get(1));
        Assertions.assertEquals(IntegerType.BIGINT, tableFunction.getTableFnReturnTypes().get(2));

        // Same but for two column TVF.
        TableFunction twoColumnTVF =
                new TableFunction(new FunctionName("two_column_tvf"), Lists.newArrayList("a", "b"),
                        Lists.newArrayList(AnyArrayType.ANY_ARRAY),
                        Lists.newArrayList(IntegerType.BIGINT, AnyElementType.ANY_ELEMENT));
        functionSet.addBuiltin(twoColumnTVF);

        argTypes = new Type[] {VARCHAR_ARRAY};
        desc = new Function(new FunctionName("two_column_tvf"), argTypes, InvalidType.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_IDENTICAL);
        Assertions.assertNotNull(fn);
        Assertions.assertTrue(fn instanceof TableFunction);
        tableFunction = (TableFunction) fn;
        Assertions.assertEquals(2, tableFunction.getTableFnReturnTypes().size());
        Assertions.assertEquals(IntegerType.BIGINT, tableFunction.getTableFnReturnTypes().get(0));
        Assertions.assertEquals(VarcharType.VARCHAR, tableFunction.getTableFnReturnTypes().get(1));
    }

    @Test
    public void testAvgMapPolymorphicTypes() {
        MapType intMap = new MapType(IntegerType.INT, IntegerType.INT);
        Function desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {intMap},
                InvalidType.INVALID, false);
        Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(function);
        Assertions.assertEquals(intMap, function.getArgs()[0]);
        Assertions.assertEquals(new MapType(IntegerType.INT, FloatType.DOUBLE), function.getReturnType());
        Assertions.assertEquals(VarbinaryType.VARBINARY, ((AggregateFunction) function).getIntermediateType());

        MapType decimalMap = new MapType(VarcharType.VARCHAR, DecimalType.DEFAULT_DECIMAL128);
        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {decimalMap},
                InvalidType.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(function);
        Assertions.assertEquals(decimalMap, function.getArgs()[0]);
        Assertions.assertEquals(new MapType(VarcharType.VARCHAR, FloatType.DOUBLE), function.getReturnType());

        MapType decimalV2Map = new MapType(IntegerType.INT, DecimalType.DECIMALV2);
        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {decimalV2Map},
                InvalidType.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(function);
        Assertions.assertEquals(decimalV2Map, function.getArgs()[0]);
        Assertions.assertEquals(new MapType(IntegerType.INT, FloatType.DOUBLE), function.getReturnType());

        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {NullType.NULL},
                InvalidType.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assertions.assertNotNull(function);
        Assertions.assertEquals(new MapType(BooleanType.BOOLEAN, BooleanType.BOOLEAN), function.getArgs()[0]);
        Assertions.assertEquals(new MapType(BooleanType.BOOLEAN, FloatType.DOUBLE), function.getReturnType());

        MapType stringValueMap = new MapType(IntegerType.INT, VarcharType.VARCHAR);
        Function invalidStringValue = new Function(new FunctionName(FunctionSet.AVG_MAP),
                new Type[] {stringValueMap}, InvalidType.INVALID, false);
        SemanticException stringError = Assertions.assertThrows(SemanticException.class,
                () -> functionSet.getFunction(invalidStringValue, Function.CompareMode.IS_SUPERTYPE_OF));
        Assertions.assertTrue(stringError.getMessage().contains("avg_map unsupported value type"));

        MapType decimal256Map = new MapType(IntegerType.INT, DecimalType.DEFAULT_DECIMAL256);
        Function invalidDecimal256 = new Function(new FunctionName(FunctionSet.AVG_MAP),
                new Type[] {decimal256Map}, InvalidType.INVALID, false);
        SemanticException decimalError = Assertions.assertThrows(SemanticException.class,
                () -> functionSet.getFunction(invalidDecimal256, Function.CompareMode.IS_SUPERTYPE_OF));
        Assertions.assertTrue(decimalError.getMessage().contains("avg_map unsupported value type"));
    }

    @Test
    public void testMinMaxMapPolymorphicIntegerType() {
        MapType intMap = new MapType(IntegerType.INT, IntegerType.INT);
        for (String functionName : new String[] {"min_map", "max_map"}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {intMap},
                    InvalidType.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assertions.assertNotNull(function, functionName);
            Assertions.assertEquals(intMap, function.getArgs()[0]);
            Assertions.assertEquals(intMap, function.getReturnType());
            Assertions.assertEquals(intMap, ((AggregateFunction) function).getIntermediateTypeOrReturnType());
        }
    }

    @Test
    public void testMinMaxMapUntypedNull() {
        MapType booleanMap = new MapType(BooleanType.BOOLEAN, BooleanType.BOOLEAN);
        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {NullType.NULL},
                    InvalidType.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assertions.assertNotNull(function, functionName);
            Assertions.assertEquals(booleanMap, function.getArgs()[0]);
            Assertions.assertEquals(booleanMap, function.getReturnType());
            Assertions.assertEquals(booleanMap, ((AggregateFunction) function).getIntermediateTypeOrReturnType());
        }
    }

    @Test
    public void testMinMaxMapStringType() {
        MapType stringMap = new MapType(VarcharType.VARCHAR, VarcharType.VARCHAR);
        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {stringMap},
                    InvalidType.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assertions.assertNotNull(function, functionName);
            Assertions.assertEquals(stringMap, function.getArgs()[0]);
            Assertions.assertEquals(stringMap, function.getReturnType());
            Assertions.assertEquals(stringMap, ((AggregateFunction) function).getIntermediateTypeOrReturnType());
        }
    }

    @Test
    public void testMinMaxMapOrderableTypeMatrix() {
        Type[] supportedKeys = {BooleanType.BOOLEAN, IntegerType.TINYINT, IntegerType.SMALLINT, IntegerType.INT,
                IntegerType.BIGINT, IntegerType.LARGEINT, FloatType.FLOAT, FloatType.DOUBLE, DecimalType.DECIMALV2,
                DecimalType.DEFAULT_DECIMAL32, DecimalType.DEFAULT_DECIMAL64, DecimalType.DEFAULT_DECIMAL128,
                CharType.CHAR, VarcharType.VARCHAR, DateType.DATE, DateType.DATETIME};
        Type[] supportedValues = {BooleanType.BOOLEAN, IntegerType.TINYINT, IntegerType.SMALLINT, IntegerType.INT,
                IntegerType.BIGINT, IntegerType.LARGEINT, FloatType.FLOAT, FloatType.DOUBLE, DecimalType.DECIMALV2,
                DecimalType.DEFAULT_DECIMAL32, DecimalType.DEFAULT_DECIMAL64, DecimalType.DEFAULT_DECIMAL128,
                DecimalType.DEFAULT_DECIMAL256, CharType.CHAR, VarcharType.VARCHAR, DateType.DATE, DateType.DATETIME};

        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            for (Type keyType : supportedKeys) {
                MapType map = new MapType(keyType, IntegerType.INT);
                Function desc = new Function(new FunctionName(functionName), new Type[] {map},
                        InvalidType.INVALID, false);
                Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
                Assertions.assertNotNull(function, functionName + " key " + keyType);
                Assertions.assertEquals(map, function.getReturnType());
            }
            for (Type valueType : supportedValues) {
                MapType map = new MapType(IntegerType.INT, valueType);
                Function desc = new Function(new FunctionName(functionName), new Type[] {map},
                        InvalidType.INVALID, false);
                Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
                Assertions.assertNotNull(function, functionName + " value " + valueType);
                Assertions.assertEquals(map, function.getReturnType());
            }

            for (MapType invalidMap : new MapType[] {
                    new MapType(DecimalType.DEFAULT_DECIMAL256, IntegerType.INT),
                    new MapType(new ArrayType(IntegerType.INT), IntegerType.INT),
                    new MapType(IntegerType.INT, JsonType.JSON),
                    new MapType(IntegerType.INT, VariantType.VARIANT),
                    new MapType(IntegerType.INT, HLLType.HLL),
                    new MapType(IntegerType.INT, BitmapType.BITMAP),
                    new MapType(IntegerType.INT, VarbinaryType.VARBINARY),
                    new MapType(IntegerType.INT, new ArrayType(IntegerType.INT)),
                    new MapType(IntegerType.INT, new MapType(IntegerType.INT, IntegerType.INT))}) {
                Function invalid = new Function(new FunctionName(functionName), new Type[] {invalidMap},
                        InvalidType.INVALID, false);
                Assertions.assertThrows(SemanticException.class,
                        () -> functionSet.getFunction(invalid, Function.CompareMode.IS_SUPERTYPE_OF),
                        functionName + " should reject " + invalidMap);
            }

            for (Type[] invalidArgs : new Type[][] {
                    {}, {IntegerType.INT}, {new MapType(IntegerType.INT, IntegerType.INT),
                            new MapType(IntegerType.INT, IntegerType.INT)}}) {
                Function invalid = new Function(new FunctionName(functionName), invalidArgs,
                        InvalidType.INVALID, false);
                Assertions.assertNull(functionSet.getFunction(invalid, Function.CompareMode.IS_SUPERTYPE_OF),
                        functionName + " should reject arity/types " + java.util.Arrays.toString(invalidArgs));
            }
        }
    }
}
