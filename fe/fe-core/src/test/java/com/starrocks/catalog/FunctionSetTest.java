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
import com.starrocks.analysis.FunctionName;
import com.starrocks.sql.analyzer.SemanticException;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

public class FunctionSetTest {

    private FunctionSet functionSet;

    private static final Type VARCHAR_ARRAY = new ArrayType(Type.VARCHAR);
    private static final Type TINYINT_ARRAY = new ArrayType(Type.TINYINT);
    private static final Type INT_ARRAY = new ArrayType(Type.INT);
    private static final Type DOUBLE_ARRAY = new ArrayType(Type.DOUBLE);
    private static final Type INT_ARRAY_ARRAY = new ArrayType(INT_ARRAY);
    private static final Type TINYINT_ARRAY_ARRAY = new ArrayType(TINYINT_ARRAY);

    @Before
    public void setUp() {
        functionSet = new FunctionSet();
        functionSet.init();
    }

    @Test
    public void testGetLagFunction() {
        Type[] argTypes1 = {ScalarType.DECIMALV2, ScalarType.TINYINT, ScalarType.TINYINT};
        Function lagDesc1 = new Function(new FunctionName(FunctionSet.LAG), argTypes1, ScalarType.INVALID, false);
        Function newFunction = functionSet.getFunction(lagDesc1, Function.CompareMode.IS_SUPERTYPE_OF);
        Type[] newArgTypes = newFunction.getArgs();
        Assert.assertTrue(newArgTypes[0].matchesType(newArgTypes[2]));
        Assert.assertTrue(newArgTypes[0].matchesType(ScalarType.DECIMALV2));

        Type[] argTypes2 = {ScalarType.VARCHAR, ScalarType.TINYINT, ScalarType.TINYINT};
        Function lagDesc2 = new Function(new FunctionName(FunctionSet.LAG), argTypes2, ScalarType.INVALID, false);
        newFunction = functionSet.getFunction(lagDesc2, Function.CompareMode.IS_SUPERTYPE_OF);
        newArgTypes = newFunction.getArgs();
        Assert.assertTrue(newArgTypes[0].matchesType(newArgTypes[2]));
        Assert.assertTrue(newArgTypes[0].matchesType(ScalarType.VARCHAR));
    }

    @Test
    public void testPolymorphicFunction() {
        // array_append(ARRAY<INT>, INT)
        Type[] argTypes = {INT_ARRAY, Type.INT};
        Function desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        Function fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, TINYINT)
        argTypes = new Type[] {INT_ARRAY, Type.TINYINT};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.INT, fn.getArgs()[1]);

        // array_append(ARRAY<TINYINT>, INT)
        argTypes = new Type[] {TINYINT_ARRAY, Type.INT};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, DOUBLE)
        argTypes = new Type[] {INT_ARRAY, Type.DOUBLE};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(DOUBLE_ARRAY, fn.getReturnType());
        Assert.assertEquals(DOUBLE_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.DOUBLE, fn.getArgs()[1]);

        // array_append(NULL, INT)
        argTypes = new Type[] {Type.NULL, Type.INT};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.INT, fn.getArgs()[1]);

        // array_append(ARRAY<INT>, NULL)
        argTypes = new Type[] {INT_ARRAY, Type.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.INT, fn.getArgs()[1]);

        // array_append(ARRAY<TINYINT>, VARCHAR)
        argTypes = new Type[] {TINYINT_ARRAY, Type.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assert.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.VARCHAR, fn.getArgs()[1]);

        // array_append(NULL, NULL)
        argTypes = new Type[] {Type.NULL, Type.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(new ArrayType(Type.BOOLEAN), fn.getReturnType());
        Assert.assertEquals(new ArrayType(Type.BOOLEAN), fn.getArgs()[0]);

        // array_append(ARRAY<ARRAY<INT>>, ARRAY<INT>)
        argTypes = new Type[] {INT_ARRAY_ARRAY, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<INT>>, ARRAY<TINYINT>)
        argTypes = new Type[] {INT_ARRAY_ARRAY, TINYINT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, ARRAY<INT>)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, NULL)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, Type.NULL};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(TINYINT_ARRAY_ARRAY, fn.getReturnType());
        Assert.assertEquals(TINYINT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(TINYINT_ARRAY, fn.getArgs()[1]);

        // array_append(NULL, ARRAY<INT>)
        argTypes = new Type[] {Type.NULL, INT_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<ARRAY<TINYINT>>, TINYINT)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, Type.TINYINT};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNull(fn);

        // array_append(ARRAY<ARRAY<TINYINT>>, ARRAY<VARCHAR>)
        argTypes = new Type[] {TINYINT_ARRAY_ARRAY, VARCHAR_ARRAY};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(new ArrayType(VARCHAR_ARRAY), fn.getReturnType());
        Assert.assertEquals(new ArrayType(VARCHAR_ARRAY), fn.getArgs()[0]);
        Assert.assertEquals(VARCHAR_ARRAY, fn.getArgs()[1]);

        // array_append(ARRAY<VARCHAR>, VARCHAR)
        argTypes = new Type[] {VARCHAR_ARRAY, Type.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assert.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.VARCHAR, fn.getArgs()[1]);

        // array_append(ARRAY<VARCHAR>, CHAR)
        argTypes = new Type[] {VARCHAR_ARRAY, Type.CHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(VARCHAR_ARRAY, fn.getReturnType());
        Assert.assertEquals(VARCHAR_ARRAY, fn.getArgs()[0]);
        Assert.assertEquals(Type.VARCHAR, fn.getArgs()[1]);

        // array_append(VARCHAR, VARCHAR)
        argTypes = new Type[] {Type.VARCHAR, Type.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNull(fn);

        // array_append(INT, VARCHAR)
        argTypes = new Type[] {Type.INT, Type.VARCHAR};
        desc = new Function(new FunctionName("array_append"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNull(fn);

        // array_length(INT)
        argTypes = new Type[] {Type.INT};
        desc = new Function(new FunctionName("array_length"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNull(fn);

        // array_length(INT)
        argTypes = new Type[] {Type.INT};
        desc = new Function(new FunctionName("array_length"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNull(fn);

        // array_length(ARRAY<INT>)
        argTypes = new Type[] {INT_ARRAY};
        desc = new Function(new FunctionName("array_length"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(Type.INT, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY, fn.getArgs()[0]);

        // array_length(ARRAY<ARRAY<INT>>)
        argTypes = new Type[] {INT_ARRAY_ARRAY};
        desc = new Function(new FunctionName("array_length"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(Type.INT, fn.getReturnType());
        Assert.assertEquals(INT_ARRAY_ARRAY, fn.getArgs()[0]);

        // array_length(NULL)
        argTypes = new Type[] {Type.NULL};
        desc = new Function(new FunctionName("array_length"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(Type.INT, fn.getReturnType());
        Assert.assertEquals(new ArrayType(Type.BOOLEAN), fn.getArgs()[0]);

        // array_generate(SmallInt,Int,BigInt)
        argTypes = new Type[] {Type.SMALLINT, Type.INT, Type.BIGINT};
        desc = new Function(new FunctionName("array_generate"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(Type.ARRAY_BIGINT, fn.getReturnType());
        Assert.assertEquals(Type.BIGINT, fn.getArgs()[0]);

        // arrays_overlap
        argTypes = new Type[] {Type.ARRAY_BIGINT, Type.ARRAY_TINYINT};
        desc = new Function(new FunctionName("arrays_overlap"), argTypes, Type.BOOLEAN, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(fn);
        Assert.assertEquals(fn.functionId, 150216L);
    }

    @Test
    public void testPolymorphicTVF() {
        // First two columns of the result are polymorphic (derived from argument type), but the last argument is of a
        // "concrete" type BIGINT, which is retained in the resolved function.
        TableFunction polymorphicTVF =
                new TableFunction(new FunctionName("three_column_tvf"), Lists.newArrayList("a", "b", "c"),
                        Lists.newArrayList(Type.ANY_ARRAY),
                        Lists.newArrayList(Type.ANY_ELEMENT, Type.ANY_ELEMENT, Type.BIGINT));

        functionSet.addBuiltin(polymorphicTVF);

        Type[] argTypes = new Type[] {VARCHAR_ARRAY};
        Function desc = new Function(new FunctionName("three_column_tvf"), argTypes, Type.INVALID, false);
        Function fn = functionSet.getFunction(desc, Function.CompareMode.IS_IDENTICAL);
        Assert.assertNotNull(fn);
        Assert.assertTrue(fn instanceof TableFunction);
        TableFunction tableFunction = (TableFunction) fn;
        Assert.assertEquals(3, tableFunction.getTableFnReturnTypes().size());
        Assert.assertEquals(Type.VARCHAR, tableFunction.getTableFnReturnTypes().get(0));
        Assert.assertEquals(Type.VARCHAR, tableFunction.getTableFnReturnTypes().get(1));
        Assert.assertEquals(Type.BIGINT, tableFunction.getTableFnReturnTypes().get(2));

        // Same but for two column TVF.
        TableFunction twoColumnTVF =
                new TableFunction(new FunctionName("two_column_tvf"), Lists.newArrayList("a", "b"),
                        Lists.newArrayList(Type.ANY_ARRAY),
                        Lists.newArrayList(Type.BIGINT, Type.ANY_ELEMENT));
        functionSet.addBuiltin(twoColumnTVF);

        argTypes = new Type[] {VARCHAR_ARRAY};
        desc = new Function(new FunctionName("two_column_tvf"), argTypes, Type.INVALID, false);
        fn = functionSet.getFunction(desc, Function.CompareMode.IS_IDENTICAL);
        Assert.assertNotNull(fn);
        Assert.assertTrue(fn instanceof TableFunction);
        tableFunction = (TableFunction) fn;
        Assert.assertEquals(2, tableFunction.getTableFnReturnTypes().size());
        Assert.assertEquals(Type.BIGINT, tableFunction.getTableFnReturnTypes().get(0));
        Assert.assertEquals(Type.VARCHAR, tableFunction.getTableFnReturnTypes().get(1));
    }

    @Test
    public void testAvgMapPolymorphicTypes() {
        MapType intMap = new MapType(Type.INT, Type.INT);
        Function desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {intMap},
                Type.INVALID, false);
        Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(function);
        Assert.assertEquals(intMap, function.getArgs()[0]);
        Assert.assertEquals(new MapType(Type.INT, Type.DOUBLE), function.getReturnType());
        Assert.assertEquals(Type.VARBINARY, ((AggregateFunction) function).getIntermediateType());

        MapType decimalMap = new MapType(Type.VARCHAR, Type.DEFAULT_DECIMAL128);
        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {decimalMap},
                Type.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(function);
        Assert.assertEquals(decimalMap, function.getArgs()[0]);
        Assert.assertEquals(new MapType(Type.VARCHAR, Type.DOUBLE), function.getReturnType());

        MapType decimalV2Map = new MapType(Type.INT, Type.DECIMALV2);
        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {decimalV2Map},
                Type.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(function);
        Assert.assertEquals(decimalV2Map, function.getArgs()[0]);
        Assert.assertEquals(new MapType(Type.INT, Type.DOUBLE), function.getReturnType());

        desc = new Function(new FunctionName(FunctionSet.AVG_MAP), new Type[] {Type.NULL}, Type.INVALID, false);
        function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
        Assert.assertNotNull(function);
        Assert.assertEquals(new MapType(Type.BOOLEAN, Type.BOOLEAN), function.getArgs()[0]);
        Assert.assertEquals(new MapType(Type.BOOLEAN, Type.DOUBLE), function.getReturnType());

        MapType stringValueMap = new MapType(Type.INT, Type.VARCHAR);
        Function invalidStringValue = new Function(new FunctionName(FunctionSet.AVG_MAP),
                new Type[] {stringValueMap}, Type.INVALID, false);
        SemanticException stringError = Assert.assertThrows(SemanticException.class,
                () -> functionSet.getFunction(invalidStringValue, Function.CompareMode.IS_SUPERTYPE_OF));
        Assert.assertTrue(stringError.getMessage().contains("avg_map unsupported value type"));
    }

    @Test
    public void testMinMaxMapPolymorphicIntegerType() {
        MapType intMap = new MapType(Type.INT, Type.INT);
        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {intMap},
                    Type.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assert.assertNotNull(functionName, function);
            Assert.assertEquals(intMap, function.getArgs()[0]);
            Assert.assertEquals(intMap, function.getReturnType());
            Assert.assertNull(((AggregateFunction) function).getIntermediateType());
        }
    }

    @Test
    public void testMinMaxMapUntypedNull() {
        MapType booleanMap = new MapType(Type.BOOLEAN, Type.BOOLEAN);
        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {Type.NULL}, Type.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assert.assertNotNull(functionName, function);
            Assert.assertEquals(booleanMap, function.getArgs()[0]);
            Assert.assertEquals(booleanMap, function.getReturnType());
        }
    }

    @Test
    public void testMinMaxMapStringType() {
        MapType stringMap = new MapType(Type.VARCHAR, Type.VARCHAR);
        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            Function desc = new Function(new FunctionName(functionName), new Type[] {stringMap},
                    Type.INVALID, false);
            Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
            Assert.assertNotNull(functionName, function);
            Assert.assertEquals(stringMap, function.getArgs()[0]);
            Assert.assertEquals(stringMap, function.getReturnType());
        }
    }

    @Test
    public void testMinMaxMapOrderableTypeMatrix() {
        Type[] supportedTypes = {Type.BOOLEAN, Type.TINYINT, Type.SMALLINT, Type.INT, Type.BIGINT, Type.LARGEINT,
                Type.FLOAT, Type.DOUBLE, Type.DECIMALV2, Type.DEFAULT_DECIMAL32, Type.DEFAULT_DECIMAL64,
                Type.DEFAULT_DECIMAL128, Type.CHAR, Type.VARCHAR, Type.DATE, Type.DATETIME};

        for (String functionName : new String[] {FunctionSet.MIN_MAP, FunctionSet.MAX_MAP}) {
            for (Type keyType : supportedTypes) {
                MapType map = new MapType(keyType, Type.INT);
                Function desc = new Function(new FunctionName(functionName), new Type[] {map},
                        Type.INVALID, false);
                Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
                Assert.assertNotNull(functionName + " key " + keyType, function);
                Assert.assertEquals(map, function.getReturnType());
            }
            for (Type valueType : supportedTypes) {
                MapType map = new MapType(Type.INT, valueType);
                Function desc = new Function(new FunctionName(functionName), new Type[] {map},
                        Type.INVALID, false);
                Function function = functionSet.getFunction(desc, Function.CompareMode.IS_SUPERTYPE_OF);
                Assert.assertNotNull(functionName + " value " + valueType, function);
                Assert.assertEquals(map, function.getReturnType());
            }

            for (MapType invalidMap : new MapType[] {
                    new MapType(new ArrayType(Type.INT), Type.INT),
                    new MapType(Type.INT, Type.JSON),
                    new MapType(Type.INT, Type.HLL),
                    new MapType(Type.INT, Type.BITMAP),
                    new MapType(Type.INT, Type.VARBINARY),
                    new MapType(Type.INT, new ArrayType(Type.INT)),
                    new MapType(Type.INT, new MapType(Type.INT, Type.INT))}) {
                Function invalid = new Function(new FunctionName(functionName), new Type[] {invalidMap},
                        Type.INVALID, false);
                Assert.assertThrows(functionName + " should reject " + invalidMap, SemanticException.class,
                        () -> functionSet.getFunction(invalid, Function.CompareMode.IS_SUPERTYPE_OF));
            }

            for (Type[] invalidArgs : new Type[][] {
                    {}, {Type.INT}, {new MapType(Type.INT, Type.INT), new MapType(Type.INT, Type.INT)}}) {
                Function invalid = new Function(new FunctionName(functionName), invalidArgs,
                        Type.INVALID, false);
                Assert.assertNull(functionName + " should reject arity/types " + java.util.Arrays.toString(invalidArgs),
                        functionSet.getFunction(invalid, Function.CompareMode.IS_SUPERTYPE_OF));
            }
        }
    }
}
