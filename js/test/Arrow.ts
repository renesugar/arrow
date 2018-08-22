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

/* tslint:disable */
// Dynamically load an Arrow target build based on command line arguments

(<any> global).window = (<any> global).window || global;

// Fix for Jest in node v10.x
Object.defineProperty(ArrayBuffer, Symbol.hasInstance, {
    writable: true,
    configurable: true,
    value(inst: any) {
        return inst && inst.constructor && inst.constructor.name === 'ArrayBuffer';
    }
});

const path = require('path');
const target = process.env.TEST_TARGET!;
const format = process.env.TEST_MODULE!;
const useSrc = process.env.TEST_TS_SOURCE === `true`;

// these are duplicated in the gulpfile :<
const targets = [`es5`, `es2015`, `esnext`];
const formats = [`cjs`, `esm`, `cls`, `umd`];

function throwInvalidImportError(name: string, value: string, values: string[]) {
    throw new Error('Unrecognized ' + name + ' \'' + value + '\'. Please run tests with \'--' + name + ' <any of ' + values.join(', ') + '>\'');
}

let modulePath = ``;

if (useSrc) modulePath = '../src';
else if (target === `ts` || target === `apache-arrow`) modulePath = target;
else if (!~targets.indexOf(target)) throwInvalidImportError('target', target, targets);
else if (!~formats.indexOf(format)) throwInvalidImportError('module', format, formats);
else modulePath = path.join(target, format);

import { read, readAsync } from '../src/Arrow';
export { read, readAsync };
import { View,  VectorLike } from '../src/Arrow';
export { View,  VectorLike };
import { Table, Field, Schema, RecordBatch, Type, vector } from '../src/Arrow';
export { Table, Field, Schema, RecordBatch, Type, vector };

import { TypedArray, TypedArrayConstructor, IntBitWidth, TimeBitWidth } from '../src/Arrow';
export { TypedArray, TypedArrayConstructor, IntBitWidth, TimeBitWidth };

import * as Arrow_ from '../src/Arrow';
export let Arrow = require(path.resolve(`./targets`, modulePath, `Arrow`)) as typeof Arrow_;
export default Arrow;
