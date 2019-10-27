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

#include "arrow/dataset/discovery.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arrow/dataset/dataset.h"
#include "arrow/dataset/file_base.h"
#include "arrow/dataset/partition.h"
#include "arrow/dataset/type_fwd.h"
#include "arrow/filesystem/path_tree.h"
#include "arrow/status.h"

namespace arrow {
namespace dataset {

FileSystemDataSourceDiscovery::FileSystemDataSourceDiscovery(
    fs::FileSystem* filesystem, std::vector<fs::FileStats> files,
    std::shared_ptr<FileFormat> format)
    : fs_(filesystem), files_(std::move(files)), format_(std::move(format)) {}

Status FileSystemDataSourceDiscovery::Make(fs::FileSystem* filesystem,
                                           std::vector<fs::FileStats> files,
                                           std::shared_ptr<FileFormat> format,
                                           std::shared_ptr<DataSourceDiscovery>* out) {
  out->reset(new FileSystemDataSourceDiscovery(filesystem, files, format));
  return Status::OK();
}

Status FileSystemDataSourceDiscovery::Make(fs::FileSystem* filesystem,
                                           fs::Selector selector,
                                           std::shared_ptr<FileFormat> format,
                                           std::shared_ptr<DataSourceDiscovery>* out) {
  std::vector<fs::FileStats> files;
  RETURN_NOT_OK(filesystem->GetTargetStats(selector, &files));
  return Make(filesystem, files, format, out);
}

static inline Status InspectSchema(fs::FileSystem* fs,
                                   const std::vector<fs::FileStats> stats,
                                   const std::shared_ptr<FileFormat>& format,
                                   std::shared_ptr<Schema>* out) {
  std::vector<std::shared_ptr<Schema>> schemas;

  for (const auto& f : stats) {
    if (!f.IsFile()) continue;

    std::shared_ptr<Schema> schema;
    RETURN_NOT_OK(format->Inspect(FileSource(f.path(), fs), &schema));
    schemas.push_back(schema);
  }

  if (schemas.size() > 0) {
    // TODO merge schemas.
    *out = schemas[0];
  }

  return Status::OK();
}

Status FileSystemDataSourceDiscovery::Inspect(std::shared_ptr<Schema>* out) {
  return InspectSchema(fs_, files_, format_, out);
}

Status FileSystemDataSourceDiscovery::Finish(std::shared_ptr<DataSource>* out) {
  PathPartitions partitions;

  if (partition_scheme_ != nullptr) {
    RETURN_NOT_OK(ApplyPartitionScheme(*partition_scheme_, files_, &partitions));
  }

  return FileSystemBasedDataSource::Make(fs_, files_, root_partition(),
                                         std::move(partitions), format_, out);
}

}  // namespace dataset
}  // namespace arrow
