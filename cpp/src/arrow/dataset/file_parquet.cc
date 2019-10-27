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

#include "arrow/dataset/file_parquet.h"

#include <memory>
#include <utility>
#include <vector>

#include "arrow/dataset/filter.h"
#include "arrow/dataset/scanner.h"
#include "arrow/table.h"
#include "arrow/util/iterator.h"
#include "arrow/util/range.h"
#include "arrow/util/stl.h"
#include "parquet/arrow/reader.h"
#include "parquet/file_reader.h"

namespace arrow {
namespace dataset {

/// \brief A ScanTask backed by a parquet file and a subset of RowGroups.
class ParquetScanTask : public ScanTask {
 public:
  ParquetScanTask(std::vector<int> row_groups, std::vector<int> columns_projection,
                  std::shared_ptr<parquet::arrow::FileReader> reader,
                  std::shared_ptr<ScanOptions> options,
                  std::shared_ptr<ScanContext> context)
      : row_groups_(std::move(row_groups)),
        columns_projection_(std::move(columns_projection)),
        reader_(reader),
        options_(std::move(options)),
        context_(std::move(context)) {}

  RecordBatchIterator Scan() {
    // The construction of parquet's RecordBatchReader is deferred here to
    // control the memory usage of consumers who materialize all ScanTasks
    // before dispatching them, e.g. for scheduling purposes.
    //
    // Thus the memory incurred by the RecordBatchReader is allocated when
    // Scan is called.
    std::unique_ptr<RecordBatchReader> record_batch_reader;
    auto status = reader_->GetRecordBatchReader(row_groups_, columns_projection_,
                                                &record_batch_reader);
    // Propagate the previous error as an error iterator.
    if (!status.ok()) {
      return MakeErrorIterator<std::shared_ptr<RecordBatch>>(std::move(status));
    }

    return MakePointerIterator(std::move(record_batch_reader));
  }

 private:
  // Subset of RowGroups and columns bound to this task.
  std::vector<int> row_groups_;
  std::vector<int> columns_projection_;
  // The ScanTask _must_ hold a reference to reader_ because there's no
  // guarantee the producing ParquetScanTaskIterator is still alive. This is a
  // contract required by record_batch_reader_
  std::shared_ptr<parquet::arrow::FileReader> reader_;

  std::shared_ptr<ScanOptions> options_;
  std::shared_ptr<ScanContext> context_;
};

constexpr int64_t kDefaultRowCountPerPartition = 1U << 16;

// A class that clusters RowGroups of a Parquet file until the cluster has a specified
// total row count. This doesn't guarantee exact row counts; it may exceed the target.
class ParquetRowGroupPartitioner {
 public:
  ParquetRowGroupPartitioner(std::shared_ptr<parquet::FileMetaData> metadata,
                             int64_t row_count = kDefaultRowCountPerPartition)
      : metadata_(std::move(metadata)), row_count_(row_count), row_group_idx_(0) {
    num_row_groups_ = metadata_->num_row_groups();
  }

  std::vector<int> Next() {
    int64_t partition_size = 0;
    std::vector<int> partitions;

    while (row_group_idx_ < num_row_groups_ && partition_size < row_count_) {
      partition_size += metadata_->RowGroup(row_group_idx_)->num_rows();
      partitions.push_back(row_group_idx_++);
    }

    return partitions;
  }

 private:
  std::shared_ptr<parquet::FileMetaData> metadata_;
  int64_t row_count_;
  int row_group_idx_;
  int num_row_groups_;
};

class ParquetScanTaskIterator {
 public:
  static Status Make(std::shared_ptr<ScanOptions> options,
                     std::shared_ptr<ScanContext> context,
                     std::unique_ptr<parquet::ParquetFileReader> reader,
                     ScanTaskIterator* out) {
    auto metadata = reader->metadata();

    std::vector<int> columns_projection;
    RETURN_NOT_OK(InferColumnProjection(*metadata, options, &columns_projection));

    std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
    RETURN_NOT_OK(parquet::arrow::FileReader::Make(context->pool, std::move(reader),
                                                   &arrow_reader));

    *out = ScanTaskIterator(
        ParquetScanTaskIterator(std::move(options), std::move(context),
                                columns_projection, metadata, std::move(arrow_reader)));
    return Status::OK();
  }

  Status Next(std::unique_ptr<ScanTask>* task) {
    auto partition = partitioner_.Next();

    // Iteration is done.
    if (partition.size() == 0) {
      task->reset(nullptr);
      return Status::OK();
    }

    task->reset(new ParquetScanTask(std::move(partition), columns_projection_, reader_,
                                    options_, context_));

    return Status::OK();
  }

 private:
  // Compute the column projection out of an optional arrow::Schema
  static Status InferColumnProjection(const parquet::FileMetaData& metadata,
                                      const std::shared_ptr<ScanOptions>& options,
                                      std::vector<int>* out) {
    // TODO(fsaintjacques): Compute intersection _and_ validity
    *out = internal::Iota(metadata.num_columns());

    return Status::OK();
  }

  ParquetScanTaskIterator(std::shared_ptr<ScanOptions> options,
                          std::shared_ptr<ScanContext> context,
                          std::vector<int> columns_projection,
                          std::shared_ptr<parquet::FileMetaData> metadata,
                          std::unique_ptr<parquet::arrow::FileReader> reader)
      : options_(std::move(options)),
        context_(std::move(context)),
        columns_projection_(columns_projection),
        partitioner_(std::move(metadata)),
        reader_(std::move(reader)) {}

  std::shared_ptr<ScanOptions> options_;
  std::shared_ptr<ScanContext> context_;
  std::vector<int> columns_projection_;
  ParquetRowGroupPartitioner partitioner_;
  std::shared_ptr<parquet::arrow::FileReader> reader_;
};

Status ParquetFileFormat::Inspect(const FileSource& source,
                                  std::shared_ptr<Schema>* out) const {
  auto pool = default_memory_pool();

  std::unique_ptr<parquet::ParquetFileReader> reader;
  RETURN_NOT_OK(OpenReader(source, pool, &reader));

  std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
  RETURN_NOT_OK(parquet::arrow::FileReader::Make(pool, std::move(reader), &arrow_reader));

  return arrow_reader->GetSchema(out);
}

Status ParquetFileFormat::ScanFile(const FileSource& source,
                                   std::shared_ptr<ScanOptions> scan_options,
                                   std::shared_ptr<ScanContext> scan_context,
                                   ScanTaskIterator* out) const {
  std::unique_ptr<parquet::ParquetFileReader> reader;
  RETURN_NOT_OK(OpenReader(source, scan_context->pool, &reader));

  return ParquetScanTaskIterator::Make(scan_options, scan_context, std::move(reader),
                                       out);
}

Status ParquetFileFormat::MakeFragment(const FileSource& source,
                                       std::shared_ptr<ScanOptions> opts,
                                       std::unique_ptr<DataFragment>* out) {
  // TODO(bkietz) check location.path() against IsKnownExtension etc
  *out = internal::make_unique<ParquetFragment>(source, opts);
  return Status::OK();
}

Status ParquetFileFormat::OpenReader(
    const FileSource& source, MemoryPool* pool,
    std::unique_ptr<parquet::ParquetFileReader>* out) const {
  std::shared_ptr<io::RandomAccessFile> input;
  RETURN_NOT_OK(source.Open(&input));

  *out = parquet::ParquetFileReader::Open(input);
  return Status::OK();
}

}  // namespace dataset
}  // namespace arrow
