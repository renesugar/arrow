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

#include "arrow/io/interfaces.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <typeinfo>
#include <utility>

#include "arrow/buffer.h"
#include "arrow/io/concurrency.h"
#include "arrow/io/util_internal.h"
#include "arrow/status.h"
#include "arrow/util/iterator.h"
#include "arrow/util/logging.h"
#include "arrow/util/string_view.h"

namespace arrow {
namespace io {

FileInterface::~FileInterface() = default;

Status FileInterface::Abort() { return Close(); }

class InputStreamBlockIterator {
 public:
  InputStreamBlockIterator(std::shared_ptr<InputStream> stream, int64_t block_size)
      : stream_(stream), block_size_(block_size) {}

  Status Next(std::shared_ptr<Buffer>* out) {
    if (done_) {
      out->reset();
      return Status::OK();
    }
    RETURN_NOT_OK(stream_->Read(block_size_, out));
    if ((*out)->size() == 0) {
      done_ = true;
      stream_.reset();
      out->reset();
    }
    return Status::OK();
  }

 protected:
  std::shared_ptr<InputStream> stream_;
  int64_t block_size_;
  bool done_ = false;
};

Status InputStream::Advance(int64_t nbytes) {
  std::shared_ptr<Buffer> temp;
  return Read(nbytes, &temp);
}

Status InputStream::Peek(int64_t ARROW_ARG_UNUSED(nbytes),
                         util::string_view* ARROW_ARG_UNUSED(out)) {
  return Status::NotImplemented("Peek not implemented");
}

bool InputStream::supports_zero_copy() const { return false; }

Status MakeInputStreamIterator(std::shared_ptr<InputStream> stream, int64_t block_size,
                               Iterator<std::shared_ptr<Buffer>>* out) {
  if (stream->closed()) {
    return Status::Invalid("Cannot take iterator on closed stream");
  }
  DCHECK_GT(block_size, 0);
  *out = Iterator<std::shared_ptr<Buffer>>(InputStreamBlockIterator(stream, block_size));
  return Status::OK();
}

struct RandomAccessFile::RandomAccessFileImpl {
  std::mutex lock_;
};

RandomAccessFile::~RandomAccessFile() = default;

RandomAccessFile::RandomAccessFile()
    : interface_impl_(new RandomAccessFile::RandomAccessFileImpl()) {}

Status RandomAccessFile::ReadAt(int64_t position, int64_t nbytes, int64_t* bytes_read,
                                void* out) {
  std::lock_guard<std::mutex> lock(interface_impl_->lock_);
  RETURN_NOT_OK(Seek(position));
  return Read(nbytes, bytes_read, out);
}

Status RandomAccessFile::ReadAt(int64_t position, int64_t nbytes,
                                std::shared_ptr<Buffer>* out) {
  std::lock_guard<std::mutex> lock(interface_impl_->lock_);
  RETURN_NOT_OK(Seek(position));
  return Read(nbytes, out);
}

Status Writable::Write(const std::string& data) {
  return Write(data.c_str(), static_cast<int64_t>(data.size()));
}

Status Writable::Write(const std::shared_ptr<Buffer>& data) {
  return Write(data->data(), data->size());
}

Status Writable::Flush() { return Status::OK(); }

class FileSegmentReader
    : public internal::InputStreamConcurrencyWrapper<FileSegmentReader> {
 public:
  FileSegmentReader(std::shared_ptr<RandomAccessFile> file, int64_t file_offset,
                    int64_t nbytes)
      : file_(std::move(file)),
        closed_(false),
        position_(0),
        file_offset_(file_offset),
        nbytes_(nbytes) {
    FileInterface::set_mode(FileMode::READ);
  }

  Status CheckOpen() const {
    if (closed_) {
      return Status::IOError("Stream is closed");
    }
    return Status::OK();
  }

  Status DoClose() {
    closed_ = true;
    return Status::OK();
  }

  Status DoTell(int64_t* position) const {
    RETURN_NOT_OK(CheckOpen());
    *position = position_;
    return Status::OK();
  }

  bool closed() const override { return closed_; }

  Status DoRead(int64_t nbytes, int64_t* bytes_read, void* out) {
    RETURN_NOT_OK(CheckOpen());
    int64_t bytes_to_read = std::min(nbytes, nbytes_ - position_);
    RETURN_NOT_OK(
        file_->ReadAt(file_offset_ + position_, bytes_to_read, bytes_read, out));
    position_ += *bytes_read;
    return Status::OK();
  }

  Status DoRead(int64_t nbytes, std::shared_ptr<Buffer>* out) {
    RETURN_NOT_OK(CheckOpen());
    int64_t bytes_to_read = std::min(nbytes, nbytes_ - position_);
    RETURN_NOT_OK(file_->ReadAt(file_offset_ + position_, bytes_to_read, out));
    position_ += (*out)->size();
    return Status::OK();
  }

 private:
  std::shared_ptr<RandomAccessFile> file_;
  bool closed_;
  int64_t position_;
  int64_t file_offset_;
  int64_t nbytes_;
};

std::shared_ptr<InputStream> RandomAccessFile::GetStream(
    std::shared_ptr<RandomAccessFile> file, int64_t file_offset, int64_t nbytes) {
  return std::make_shared<FileSegmentReader>(std::move(file), file_offset, nbytes);
}

//////////////////////////////////////////////////////////////////////////
// Implement utilities exported from concurrency.h and util_internal.h

namespace internal {

void CloseFromDestructor(FileInterface* file) {
  Status st = file->Close();
  if (!st.ok()) {
    auto file_type = typeid(*file).name();
#ifdef NDEBUG
    ARROW_LOG(ERROR) << "Error ignored when destroying file of type " << file_type << ": "
                     << st;
#else
    std::stringstream ss;
    ss << "When destroying file of type " << file_type << ": " << st.message();
    ARROW_LOG(FATAL) << st.WithMessage(ss.str());
#endif
  }
}

#ifndef NDEBUG

// Debug mode concurrency checking

struct SharedExclusiveChecker::Impl {
  std::mutex mutex;
  int64_t n_shared = 0;
  int64_t n_exclusive = 0;
};

SharedExclusiveChecker::SharedExclusiveChecker() : impl_(new Impl) {}

void SharedExclusiveChecker::LockShared() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  // XXX The error message doesn't really describe the actual situation
  // (e.g. ReadAt() called while Read() call in progress)
  ARROW_CHECK_EQ(impl_->n_exclusive, 0)
      << "Attempted to take shared lock while locked exclusive";
  ++impl_->n_shared;
}

void SharedExclusiveChecker::UnlockShared() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ARROW_CHECK_GT(impl_->n_shared, 0);
  --impl_->n_shared;
}

void SharedExclusiveChecker::LockExclusive() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ARROW_CHECK_EQ(impl_->n_shared, 0)
      << "Attempted to take exclusive lock while locked shared";
  ARROW_CHECK_EQ(impl_->n_exclusive, 0)
      << "Attempted to take exclusive lock while already locked exclusive";
  ++impl_->n_exclusive;
}

void SharedExclusiveChecker::UnlockExclusive() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ARROW_CHECK_EQ(impl_->n_exclusive, 1);
  --impl_->n_exclusive;
}

#else

// Release mode no-op concurrency checking

struct SharedExclusiveChecker::Impl {};

SharedExclusiveChecker::SharedExclusiveChecker() {}

void SharedExclusiveChecker::LockShared() {}
void SharedExclusiveChecker::UnlockShared() {}
void SharedExclusiveChecker::LockExclusive() {}
void SharedExclusiveChecker::UnlockExclusive() {}

#endif

}  // namespace internal
}  // namespace io
}  // namespace arrow
