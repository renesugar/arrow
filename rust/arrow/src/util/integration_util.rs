// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

//! Utils for JSON integration testing
//!
//! These utilities define structs that read the integration JSON format for integration testing purposes.

use serde_derive::Deserialize;
use serde_json::Value;

use crate::array::*;
use crate::datatypes::*;
use crate::record_batch::RecordBatch;

/// A struct that represents an Arrow file with a schema and record batches
#[derive(Deserialize)]
struct ArrowJson {
    schema: ArrowJsonSchema,
    batches: Vec<ArrowJsonBatch>,
}

/// A struct that partially reads the Arrow JSON schema.
///
/// Fields are left as JSON `Value` as they vary by `DataType`
#[derive(Deserialize)]
struct ArrowJsonSchema {
    fields: Vec<Value>,
}

/// A struct that partially reads the Arrow JSON record batch
#[derive(Deserialize)]
struct ArrowJsonBatch {
    count: usize,
    columns: Vec<ArrowJsonColumn>,
}

/// A struct that partially reads the Arrow JSON column/array
#[derive(Deserialize, Clone, Debug)]
struct ArrowJsonColumn {
    name: String,
    count: usize,
    #[serde(rename = "VALIDITY")]
    validity: Vec<u8>,
    #[serde(rename = "DATA")]
    data: Option<Vec<Value>>,
    #[serde(rename = "OFFSET")]
    offset: Option<Vec<Value>>, // leaving as Value as 64-bit offsets are strings
    children: Option<Vec<ArrowJsonColumn>>,
}

impl ArrowJsonSchema {
    /// Compare the Arrow JSON schema with the Arrow `Schema`
    fn equals_schema(&self, schema: &Schema) -> bool {
        let field_len = self.fields.len();
        if field_len != schema.fields().len() {
            return false;
        }
        for i in 0..field_len {
            let json_field = &self.fields[i];
            let field = schema.field(i);
            assert_eq!(json_field, &field.to_json());
        }
        true
    }
}

impl ArrowJsonBatch {
    /// Comapre the Arrow JSON record batch with a `RecordBatch`
    fn equals_batch(&self, batch: &RecordBatch) -> bool {
        if self.count != batch.num_rows() {
            return false;
        }
        let num_columns = self.columns.len();
        if num_columns != batch.num_columns() {
            return false;
        }
        let schema = batch.schema();
        self.columns
            .iter()
            .zip(batch.columns())
            .zip(schema.fields())
            .all(|((col, arr), field)| {
                // compare each column based on its type
                if &col.name != field.name() {
                    return false;
                }
                let json_array: Vec<Value> = json_from_col(&col, field.data_type());
                match field.data_type() {
                    DataType::Boolean => {
                        let arr = arr.as_any().downcast_ref::<BooleanArray>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Int8 => {
                        let arr = arr.as_any().downcast_ref::<Int8Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Int16 => {
                        let arr = arr.as_any().downcast_ref::<Int16Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Int32 | DataType::Date32(_) | DataType::Time32(_) => {
                        let arr = Int32Array::from(arr.data());
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Int64
                    | DataType::Date64(_)
                    | DataType::Time64(_)
                    | DataType::Timestamp(_) => {
                        let arr = Int64Array::from(arr.data());
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::UInt8 => {
                        let arr = arr.as_any().downcast_ref::<UInt8Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::UInt16 => {
                        let arr = arr.as_any().downcast_ref::<UInt16Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::UInt32 => {
                        let arr = arr.as_any().downcast_ref::<UInt32Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::UInt64 => {
                        let arr = arr.as_any().downcast_ref::<UInt64Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Float32 => {
                        let arr = arr.as_any().downcast_ref::<Float32Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Float64 => {
                        let arr = arr.as_any().downcast_ref::<Float64Array>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Utf8 => {
                        let arr = arr.as_any().downcast_ref::<BinaryArray>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::List(_) => {
                        let arr = arr.as_any().downcast_ref::<ListArray>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    DataType::Struct(_) => {
                        let arr = arr.as_any().downcast_ref::<StructArray>().unwrap();
                        arr.equals_json(&json_array.iter().collect::<Vec<&Value>>()[..])
                    }
                    t @ _ => panic!("Unsupported comparison for {:?}", t),
                }
            })
    }
}

/// Convert an Arrow JSON column/array into a vector of `Value`
fn json_from_col(col: &ArrowJsonColumn, data_type: &DataType) -> Vec<Value> {
    match data_type {
        DataType::List(dt) => json_from_list_col(col, &**dt),
        DataType::Struct(fields) => json_from_struct_col(col, fields),
        _ => merge_json_array(&col.validity, &col.data.clone().unwrap()),
    }
}

/// Merge VALIDITY and DATA vectors from a primitive data type into a `Value` vector with nulls
fn merge_json_array(validity: &Vec<u8>, data: &Vec<Value>) -> Vec<Value> {
    validity
        .iter()
        .zip(data)
        .map(|(v, d)| match v {
            0 => Value::Null,
            1 => d.clone(),
            _ => panic!("Validity data should be 0 or 1"),
        })
        .collect()
}

/// Convert an Arrow JSON column/array of a `DataType::Struct` into a vector of `Value`
fn json_from_struct_col(col: &ArrowJsonColumn, fields: &Vec<Field>) -> Vec<Value> {
    let mut values = Vec::with_capacity(col.count);

    let children: Vec<Vec<Value>> = col
        .children
        .clone()
        .unwrap()
        .iter()
        .zip(fields)
        .map(|(child, field)| json_from_col(child, field.data_type()))
        .collect();

    // create a struct from children
    for j in 0..col.count {
        let mut map = serde_json::map::Map::new();
        for i in 0..children.len() {
            map.insert(fields[i].name().to_string(), children[i][j].clone());
        }
        values.push(Value::Object(map));
    }

    values
}

/// Convert an Arrow JSON column/array of a `DataType::List` into a vector of `Value`
fn json_from_list_col(col: &ArrowJsonColumn, data_type: &DataType) -> Vec<Value> {
    let mut values = Vec::with_capacity(col.count);

    // get the inner array
    let child = &col.children.clone().expect("list type must have children")[0];
    let offsets: Vec<usize> = col
        .offset
        .clone()
        .unwrap()
        .iter()
        .map(|o| match o {
            Value::String(s) => *&s.parse::<usize>().unwrap(),
            Value::Number(n) => n.as_u64().unwrap() as usize,
            _ => panic!(
                "Offsets should be numbers or strings that are convertible to numbers"
            ),
        })
        .collect();
    let inner = match data_type {
        DataType::List(ref dt) => json_from_col(child, &**dt),
        DataType::Struct(fields) => json_from_struct_col(col, fields),
        _ => merge_json_array(&child.validity, &child.data.clone().unwrap()),
    };

    for i in 0..col.count {
        match col.validity[i] {
            0 => values.push(Value::Null),
            1 => values.push(Value::Array(inner[offsets[i]..offsets[i + 1]].to_vec())),
            _ => panic!("Validity data should be 0 or 1"),
        }
    }

    values
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryFrom;
    use std::fs::File;
    use std::io::Read;
    use std::sync::Arc;

    use crate::buffer::Buffer;

    #[test]
    fn test_schema_equality() {
        let json = r#"
        {
            "fields": [
                {
                    "name": "c1",
                    "type": {"name": "int", "isSigned": true, "bitWidth": 32},
                    "nullable": true,
                    "children": []
                },
                {
                    "name": "c2",
                    "type": {"name": "floatingpoint", "precision": "DOUBLE"},
                    "nullable": true,
                    "children": []
                },
                {
                    "name": "c3",
                    "type": {"name": "utf8"},
                    "nullable": true,
                    "children": []
                },
                {
                    "name": "c4",
                    "type": {
                        "name": "list"
                    },
                    "nullable": true,
                    "children": [
                        {
                            "name": "item",
                            "type": {
                                "name": "int",
                                "isSigned": true,
                                "bitWidth": 32
                            },
                            "nullable": true,
                            "children": []
                        }
                    ]
                }
            ]
        }"#;
        let json_schema: ArrowJsonSchema = serde_json::from_str(json).unwrap();
        let schema = Schema::new(vec![
            Field::new("c1", DataType::Int32, true),
            Field::new("c2", DataType::Float64, true),
            Field::new("c3", DataType::Utf8, true),
            Field::new("c4", DataType::List(Box::new(DataType::Int32)), true),
        ]);
        assert!(json_schema.equals_schema(&schema));
    }

    #[test]
    fn test_arrow_data_equality() {
        let schema = Schema::new(vec![
            Field::new("bools", DataType::Boolean, true),
            Field::new("int8s", DataType::Int8, true),
            Field::new("int16s", DataType::Int16, true),
            Field::new("int32s", DataType::Int32, true),
            Field::new("int64s", DataType::Int64, true),
            Field::new("uint8s", DataType::UInt8, true),
            Field::new("uint16s", DataType::UInt16, true),
            Field::new("uint32s", DataType::UInt32, true),
            Field::new("uint64s", DataType::UInt64, true),
            Field::new("float32s", DataType::Float32, true),
            Field::new("float64s", DataType::Float64, true),
            Field::new("date_days", DataType::Date32(DateUnit::Day), true),
            Field::new("date_millis", DataType::Date64(DateUnit::Millisecond), true),
            Field::new("time_secs", DataType::Time32(TimeUnit::Second), true),
            Field::new("time_millis", DataType::Time32(TimeUnit::Millisecond), true),
            Field::new("time_micros", DataType::Time64(TimeUnit::Microsecond), true),
            Field::new("time_nanos", DataType::Time64(TimeUnit::Nanosecond), true),
            Field::new("ts_secs", DataType::Timestamp(TimeUnit::Second), true),
            Field::new(
                "ts_millis",
                DataType::Timestamp(TimeUnit::Millisecond),
                true,
            ),
            Field::new(
                "ts_micros",
                DataType::Timestamp(TimeUnit::Microsecond),
                true,
            ),
            Field::new("ts_nanos", DataType::Timestamp(TimeUnit::Nanosecond), true),
            Field::new("utf8s", DataType::Utf8, true),
            Field::new("lists", DataType::List(Box::new(DataType::Int32)), true),
            Field::new(
                "structs",
                DataType::Struct(vec![
                    Field::new("int32s", DataType::Int32, true),
                    Field::new("utf8s", DataType::Utf8, true),
                ]),
                true,
            ),
        ]);

        let bools = BooleanArray::from(vec![Some(true), None, Some(false)]);
        let int8s = Int8Array::from(vec![Some(1), None, Some(3)]);
        let int16s = Int16Array::from(vec![Some(1), None, Some(3)]);
        let int32s = Int32Array::from(vec![Some(1), None, Some(3)]);
        let int64s = Int64Array::from(vec![Some(1), None, Some(3)]);
        let uint8s = UInt8Array::from(vec![Some(1), None, Some(3)]);
        let uint16s = UInt16Array::from(vec![Some(1), None, Some(3)]);
        let uint32s = UInt32Array::from(vec![Some(1), None, Some(3)]);
        let uint64s = UInt64Array::from(vec![Some(1), None, Some(3)]);
        let float32s = Float32Array::from(vec![Some(1.0), None, Some(3.0)]);
        let float64s = Float64Array::from(vec![Some(1.0), None, Some(3.0)]);
        let date_days = Date32Array::from(vec![Some(1196848), None, None]);
        let date_millis = Date64Array::from(vec![
            Some(167903550396207),
            Some(29923997007884),
            Some(30612271819236),
        ]);
        let time_secs =
            Time32SecondArray::from(vec![Some(27974), Some(78592), Some(43207)]);
        let time_millis = Time32MillisecondArray::from(vec![
            Some(6613125),
            Some(74667230),
            Some(52260079),
        ]);
        let time_micros =
            Time64MicrosecondArray::from(vec![Some(62522958593), None, None]);
        let time_nanos = Time64NanosecondArray::from(vec![
            Some(73380123595985),
            None,
            Some(16584393546415),
        ]);
        let ts_secs = TimestampSecondArray::from(vec![None, Some(193438817552), None]);
        let ts_millis = TimestampMillisecondArray::from(vec![
            None,
            Some(38606916383008),
            Some(58113709376587),
        ]);
        let ts_micros = TimestampMicrosecondArray::from(vec![None, None, None]);
        let ts_nanos =
            TimestampNanosecondArray::from(vec![None, None, Some(-6473623571954960143)]);
        let utf8s = BinaryArray::try_from(vec![Some("aa"), None, Some("bbb")]).unwrap();

        let value_data = Int32Array::from(vec![None, Some(2), None, None]);
        let value_offsets = Buffer::from(&[0, 3, 4, 4].to_byte_slice());
        let list_data_type = DataType::List(Box::new(DataType::Int32));
        let list_data = ArrayData::builder(list_data_type)
            .len(3)
            .add_buffer(value_offsets)
            .add_child_data(value_data.data())
            .build();
        let lists = ListArray::from(list_data);

        let structs_int32s = Int32Array::from(vec![None, Some(-2), None]);
        let structs_utf8s =
            BinaryArray::try_from(vec![None, None, Some("aaaaaa")]).unwrap();
        let structs = StructArray::from(vec![
            (
                Field::new("int32s", DataType::Int32, true),
                Arc::new(structs_int32s) as ArrayRef,
            ),
            (
                Field::new("utf8s", DataType::Utf8, true),
                Arc::new(structs_utf8s) as ArrayRef,
            ),
        ]);

        let record_batch = RecordBatch::try_new(
            Arc::new(schema.clone()),
            vec![
                Arc::new(bools),
                Arc::new(int8s),
                Arc::new(int16s),
                Arc::new(int32s),
                Arc::new(int64s),
                Arc::new(uint8s),
                Arc::new(uint16s),
                Arc::new(uint32s),
                Arc::new(uint64s),
                Arc::new(float32s),
                Arc::new(float64s),
                Arc::new(date_days),
                Arc::new(date_millis),
                Arc::new(time_secs),
                Arc::new(time_millis),
                Arc::new(time_micros),
                Arc::new(time_nanos),
                Arc::new(ts_secs),
                Arc::new(ts_millis),
                Arc::new(ts_micros),
                Arc::new(ts_nanos),
                Arc::new(utf8s),
                Arc::new(lists),
                Arc::new(structs),
            ],
        )
        .unwrap();
        let mut file = File::open("test/data/integration.json").unwrap();
        let mut json = String::new();
        file.read_to_string(&mut json).unwrap();
        let arrow_json: ArrowJson = serde_json::from_str(&json).unwrap();
        // test schemas
        assert!(arrow_json.schema.equals_schema(&schema));
        // test record batch
        assert!(arrow_json.batches[0].equals_batch(&record_batch));
    }
}
