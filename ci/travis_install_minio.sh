#!/usr/bin/env bash

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

set -e
set -x

if [ "$ARROW_TRAVIS_S3" == "1" ]; then
    # Download the Minio S3 server into PATH
    if [ $TRAVIS_OS_NAME = "osx" ]; then
        MINIO_URL=https://dl.min.io/server/minio/release/darwin-amd64/minio
    else
        MINIO_URL=https://dl.min.io/server/minio/release/linux-amd64/minio
    fi

    S3FS_DIR=~/.local/bin/
    mkdir -p $S3FS_DIR
    wget --quiet --directory-prefix $S3FS_DIR $MINIO_URL
    chmod +x $S3FS_DIR/minio
fi
