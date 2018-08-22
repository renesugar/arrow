// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package arrow_test

import (
	"fmt"

	"github.com/apache/arrow/go/arrow"
	"github.com/apache/arrow/go/arrow/array"
	"github.com/apache/arrow/go/arrow/memory"
)

// This example demonstrates how to build an array of int64 values using a builder and Append.
// Whilst convenient for small arrays,
func Example_minimal() {
	// Create an allocator.
	pool := memory.NewGoAllocator()

	// Create an int64 array builder.
	builder := array.NewInt64Builder(pool)
	defer builder.Release()

	builder.Append(1)
	builder.Append(2)
	builder.Append(3)
	builder.AppendNull()
	builder.Append(5)
	builder.Append(6)
	builder.Append(7)
	builder.Append(8)

	// Finish building the int64 array and reset the builder.
	ints := builder.NewInt64Array()
	defer ints.Release()

	// Enumerate the values.
	for i, v := range ints.Int64Values() {
		fmt.Printf("ints[%d] = ", i)
		if ints.IsNull(i) {
			fmt.Println("(null)")
		} else {
			fmt.Println(v)
		}
	}

	// Output:
	// ints[0] = 1
	// ints[1] = 2
	// ints[2] = 3
	// ints[3] = (null)
	// ints[4] = 5
	// ints[5] = 6
	// ints[6] = 7
	// ints[7] = 8
}

// This example demonstrates creating an array, sourcing the values and
// null bitmaps directly from byte slices. The null count is set to
// UnknownNullCount, instructing the array to calculate the
// null count from the bitmap when NullN is called.
func Example_fromMemory() {
	// create LSB packed bits with the following pattern:
	// 01010011 11000101
	data := memory.NewBufferBytes([]byte{0xca, 0xa3})

	// create LSB packed validity (null) bitmap, where every 4th element is null:
	// 11101110 11101110
	nullBitmap := memory.NewBufferBytes([]byte{0x77, 0x77})

	// Create a boolean array and lazily determine NullN using UnknownNullCount
	bools := array.NewBoolean(16, data, nullBitmap, array.UnknownNullCount)
	defer bools.Release()

	// Show the null count
	fmt.Printf("NullN()  = %d\n", bools.NullN())

	// Enumerate the values.
	n := bools.Len()
	for i := 0; i < n; i++ {
		fmt.Printf("bools[%d] = ", i)
		if bools.IsNull(i) {
			fmt.Println("(null)")
		} else {
			fmt.Printf("%t\n", bools.Value(i))
		}
	}

	// Output:
	// NullN()  = 4
	// bools[0] = false
	// bools[1] = true
	// bools[2] = false
	// bools[3] = (null)
	// bools[4] = false
	// bools[5] = false
	// bools[6] = true
	// bools[7] = (null)
	// bools[8] = true
	// bools[9] = true
	// bools[10] = false
	// bools[11] = (null)
	// bools[12] = false
	// bools[13] = true
	// bools[14] = false
	// bools[15] = (null)
}

// This example shows how to create a List array.
// The resulting array should be:
//  [[0, 1, 2], [], [3], [4, 5], [6, 7, 8], [], [9]]
func Example_listArray() {
	pool := memory.NewGoAllocator()

	lb := array.NewListBuilder(pool, arrow.PrimitiveTypes.Int64)
	defer lb.Release()

	vb := lb.ValueBuilder().(*array.Int64Builder)
	defer vb.Release()

	vb.Reserve(10)

	lb.Append(true)
	vb.Append(0)
	vb.Append(1)
	vb.Append(2)

	lb.AppendNull()

	lb.Append(true)
	vb.Append(3)

	lb.Append(true)
	vb.Append(4)
	vb.Append(5)

	lb.Append(true)
	vb.Append(6)
	vb.Append(7)
	vb.Append(8)

	lb.AppendNull()

	lb.Append(true)
	vb.Append(9)

	arr := lb.NewArray().(*array.List)
	defer arr.Release()

	fmt.Printf("NullN()   = %d\n", arr.NullN())
	fmt.Printf("Len()     = %d\n", arr.Len())
	fmt.Printf("Offsets() = %v\n", arr.Offsets())

	offsets := arr.Offsets()[1:]

	varr := arr.ListValues().(*array.Int64)

	pos := 0
	for i := 0; i < arr.Len(); i++ {
		if !arr.IsValid(i) {
			fmt.Printf("List[%d]   = (null)\n", i)
			continue
		}
		fmt.Printf("List[%d]   = [", i)
		for j := pos; j < int(offsets[i]); j++ {
			if j != pos {
				fmt.Printf(", ")
			}
			fmt.Printf("%v", varr.Value(j))
		}
		pos = int(offsets[i])
		fmt.Printf("]\n")
	}

	// Output:
	// NullN()   = 2
	// Len()     = 7
	// Offsets() = [0 3 3 4 6 9 9 10]
	// List[0]   = [0, 1, 2]
	// List[1]   = (null)
	// List[2]   = [3]
	// List[3]   = [4, 5]
	// List[4]   = [6, 7, 8]
	// List[5]   = (null)
	// List[6]   = [9]
}

// This example shows how to create a Struct array.
// The resulting array should be:
//  [{‘joe’, 1}, {null, 2}, null, {‘mark’, 4}]
func Example_structArray() {
	pool := memory.NewGoAllocator()

	dtype := arrow.StructOf([]arrow.Field{
		{Name: "f1", Type: arrow.ListOf(arrow.PrimitiveTypes.Uint8)},
		{Name: "f2", Type: arrow.PrimitiveTypes.Int32},
	}...)

	sb := array.NewStructBuilder(pool, dtype)
	defer sb.Release()

	f1b := sb.FieldBuilder(0).(*array.ListBuilder)
	defer f1b.Release()
	f1vb := f1b.ValueBuilder().(*array.Uint8Builder)
	defer f1vb.Release()

	f2b := sb.FieldBuilder(1).(*array.Int32Builder)
	defer f2b.Release()

	sb.Reserve(4)
	f1vb.Reserve(7)
	f2b.Reserve(3)

	sb.Append(true)
	f1b.Append(true)
	f1vb.AppendValues([]byte("joe"), nil)
	f2b.Append(1)

	sb.Append(true)
	f1b.AppendNull()
	f2b.Append(2)

	sb.AppendNull()

	sb.Append(true)
	f1b.Append(true)
	f1vb.AppendValues([]byte("mark"), nil)
	f2b.Append(4)

	arr := sb.NewArray().(*array.Struct)
	defer arr.Release()

	fmt.Printf("NullN() = %d\n", arr.NullN())
	fmt.Printf("Len()   = %d\n", arr.Len())

	list := arr.Field(0).(*array.List)
	defer list.Release()

	offsets := list.Offsets()

	varr := list.ListValues().(*array.Uint8)
	defer varr.Release()

	ints := arr.Field(1).(*array.Int32)
	defer ints.Release()

	for i := 0; i < arr.Len(); i++ {
		if !arr.IsValid(i) {
			fmt.Printf("Struct[%d] = (null)\n", i)
			continue
		}
		fmt.Printf("Struct[%d] = [", i)
		pos := int(offsets[i])
		switch {
		case list.IsValid(pos):
			fmt.Printf("[")
			for j := offsets[i]; j < offsets[i+1]; j++ {
				if j != offsets[i] {
					fmt.Printf(", ")
				}
				fmt.Printf("%v", string(varr.Value(int(j))))
			}
			fmt.Printf("], ")
		default:
			fmt.Printf("(null), ")
		}
		fmt.Printf("%d]\n", ints.Value(i))
	}

	// Output:
	// NullN() = 1
	// Len()   = 4
	// Struct[0] = [[j, o, e], 1]
	// Struct[1] = [[], 2]
	// Struct[2] = (null)
	// Struct[3] = [[m, a, r, k], 4]
}

// This example shows how one can slice an array.
// The initial (float64) array is:
//  [1, 2, 3, (null), 4, 5]
//
// and the sub-slice is:
//  [3, (null), 4]
func Example_float64Slice() {
	pool := memory.NewGoAllocator()

	b := array.NewFloat64Builder(pool)
	defer b.Release()

	b.AppendValues(
		[]float64{1, 2, 3, -1, 4, 5},
		[]bool{true, true, true, false, true, true},
	)

	arr := b.NewFloat64Array()
	defer arr.Release()

	fmt.Printf("array = %v\n", arr.Float64Values())

	sli := array.NewSlice(arr, 2, 5).(*array.Float64)
	defer sli.Release()

	fmt.Printf("slice = %v\n", sli.Float64Values())

	// Output:
	// array = [1 2 3 -1 4 5]
	// slice = [3 -1 4]
}
