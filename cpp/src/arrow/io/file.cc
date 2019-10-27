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

#include "arrow/util/windows_compatibility.h"  // IWYU pragma: keep

// sys/mman.h not present in Visual Studio or Cygwin
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "arrow/io/mman.h"
#undef Realloc
#undef Free
#else
#include <sys/mman.h>
#include <unistd.h>  // IWYU pragma: keep
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

// ----------------------------------------------------------------------
// Other Arrow includes

#include "arrow/io/file.h"
#include "arrow/io/interfaces.h"
#include "arrow/io/util_internal.h"

#include "arrow/buffer.h"
#include "arrow/memory_pool.h"
#include "arrow/status.h"
#include "arrow/util/io_util.h"
#include "arrow/util/logging.h"

namespace arrow {
namespace io {

class OSFile {
 public:
  OSFile() : fd_(-1), is_open_(false), size_(-1), need_seeking_(false) {}

  ~OSFile() {}

  // Note: only one of the Open* methods below may be called on a given instance

  Status OpenWritable(const std::string& path, bool truncate, bool append,
                      bool write_only) {
    RETURN_NOT_OK(SetFileName(path));

    RETURN_NOT_OK(::arrow::internal::FileOpenWritable(file_name_, write_only, truncate,
                                                      append, &fd_));
    is_open_ = true;
    mode_ = write_only ? FileMode::WRITE : FileMode::READWRITE;

    if (!truncate) {
      RETURN_NOT_OK(::arrow::internal::FileGetSize(fd_, &size_));
    } else {
      size_ = 0;
    }
    return Status::OK();
  }

  // This is different from OpenWritable(string, ...) in that it doesn't
  // truncate nor mandate a seekable file
  Status OpenWritable(int fd) {
    if (!::arrow::internal::FileGetSize(fd, &size_).ok()) {
      // Non-seekable file
      size_ = -1;
    }
    RETURN_NOT_OK(SetFileName(fd));
    is_open_ = true;
    mode_ = FileMode::WRITE;
    fd_ = fd;
    return Status::OK();
  }

  Status OpenReadable(const std::string& path) {
    RETURN_NOT_OK(SetFileName(path));

    RETURN_NOT_OK(::arrow::internal::FileOpenReadable(file_name_, &fd_));
    RETURN_NOT_OK(::arrow::internal::FileGetSize(fd_, &size_));

    is_open_ = true;
    mode_ = FileMode::READ;
    return Status::OK();
  }

  Status OpenReadable(int fd) {
    RETURN_NOT_OK(::arrow::internal::FileGetSize(fd, &size_));
    RETURN_NOT_OK(SetFileName(fd));
    is_open_ = true;
    mode_ = FileMode::READ;
    fd_ = fd;
    return Status::OK();
  }

  Status CheckClosed() const {
    if (!is_open_) {
      return Status::Invalid("Invalid operation on closed file");
    }
    return Status::OK();
  }

  Status Close() {
    if (is_open_) {
      // Even if closing fails, the fd will likely be closed (perhaps it's
      // already closed).
      is_open_ = false;
      int fd = fd_;
      fd_ = -1;
      RETURN_NOT_OK(::arrow::internal::FileClose(fd));
    }
    return Status::OK();
  }

  Status Read(int64_t nbytes, int64_t* bytes_read, void* out) {
    RETURN_NOT_OK(CheckClosed());
    RETURN_NOT_OK(CheckPositioned());
    return ::arrow::internal::FileRead(fd_, reinterpret_cast<uint8_t*>(out), nbytes,
                                       bytes_read);
  }

  Status ReadAt(int64_t position, int64_t nbytes, int64_t* bytes_read, void* out) {
    RETURN_NOT_OK(CheckClosed());
    // ReadAt() leaves the file position undefined, so require that we seek
    // before calling Read() or Write().
    need_seeking_.store(true);
    return ::arrow::internal::FileReadAt(fd_, reinterpret_cast<uint8_t*>(out), position,
                                         nbytes, bytes_read);
  }

  Status Seek(int64_t pos) {
    RETURN_NOT_OK(CheckClosed());
    if (pos < 0) {
      return Status::Invalid("Invalid position");
    }
    Status st = ::arrow::internal::FileSeek(fd_, pos);
    if (st.ok()) {
      need_seeking_.store(false);
    }
    return st;
  }

  Status Tell(int64_t* pos) const {
    RETURN_NOT_OK(CheckClosed());
    return ::arrow::internal::FileTell(fd_, pos);
  }

  Status Write(const void* data, int64_t length) {
    RETURN_NOT_OK(CheckClosed());

    std::lock_guard<std::mutex> guard(lock_);
    RETURN_NOT_OK(CheckPositioned());
    if (length < 0) {
      return Status::IOError("Length must be non-negative");
    }
    return ::arrow::internal::FileWrite(fd_, reinterpret_cast<const uint8_t*>(data),
                                        length);
  }

  int fd() const { return fd_; }

  bool is_open() const { return is_open_; }

  int64_t size() const { return size_; }

  FileMode::type mode() const { return mode_; }

  std::mutex& lock() { return lock_; }

 protected:
  Status SetFileName(const std::string& file_name) {
    return ::arrow::internal::FileNameFromString(file_name, &file_name_);
  }
  Status SetFileName(int fd) {
    std::stringstream ss;
    ss << "<fd " << fd << ">";
    return SetFileName(ss.str());
  }

  Status CheckPositioned() {
    if (need_seeking_.load()) {
      return Status::Invalid(
          "Need seeking after ReadAt() before "
          "calling implicitly-positioned operation");
    }
    return Status::OK();
  }

  ::arrow::internal::PlatformFilename file_name_;

  std::mutex lock_;

  // File descriptor
  int fd_;

  FileMode::type mode_;

  bool is_open_;
  int64_t size_;
  // Whether ReadAt made the file position non-deterministic.
  std::atomic<bool> need_seeking_;
};

// ----------------------------------------------------------------------
// ReadableFile implementation

class ReadableFile::ReadableFileImpl : public OSFile {
 public:
  explicit ReadableFileImpl(MemoryPool* pool) : OSFile(), pool_(pool) {}

  Status Open(const std::string& path) { return OpenReadable(path); }
  Status Open(int fd) { return OpenReadable(fd); }

  Status ReadBuffer(int64_t nbytes, std::shared_ptr<Buffer>* out) {
    std::shared_ptr<ResizableBuffer> buffer;
    RETURN_NOT_OK(AllocateResizableBuffer(pool_, nbytes, &buffer));

    int64_t bytes_read = 0;
    RETURN_NOT_OK(Read(nbytes, &bytes_read, buffer->mutable_data()));
    if (bytes_read < nbytes) {
      RETURN_NOT_OK(buffer->Resize(bytes_read));
      buffer->ZeroPadding();
    }
    *out = buffer;
    return Status::OK();
  }

  Status ReadBufferAt(int64_t position, int64_t nbytes, std::shared_ptr<Buffer>* out) {
    std::shared_ptr<ResizableBuffer> buffer;
    RETURN_NOT_OK(AllocateResizableBuffer(pool_, nbytes, &buffer));

    int64_t bytes_read = 0;
    RETURN_NOT_OK(ReadAt(position, nbytes, &bytes_read, buffer->mutable_data()));
    if (bytes_read < nbytes) {
      RETURN_NOT_OK(buffer->Resize(bytes_read));
      buffer->ZeroPadding();
    }
    *out = buffer;
    return Status::OK();
  }

 private:
  MemoryPool* pool_;
};

ReadableFile::ReadableFile(MemoryPool* pool) { impl_.reset(new ReadableFileImpl(pool)); }

ReadableFile::~ReadableFile() { internal::CloseFromDestructor(this); }

Status ReadableFile::Open(const std::string& path, std::shared_ptr<ReadableFile>* file) {
  return Open(path, default_memory_pool(), file);
}

Status ReadableFile::Open(const std::string& path, MemoryPool* memory_pool,
                          std::shared_ptr<ReadableFile>* file) {
  *file = std::shared_ptr<ReadableFile>(new ReadableFile(memory_pool));
  return (*file)->impl_->Open(path);
}

Status ReadableFile::Open(int fd, MemoryPool* memory_pool,
                          std::shared_ptr<ReadableFile>* file) {
  *file = std::shared_ptr<ReadableFile>(new ReadableFile(memory_pool));
  return (*file)->impl_->Open(fd);
}

Status ReadableFile::Open(int fd, std::shared_ptr<ReadableFile>* file) {
  return Open(fd, default_memory_pool(), file);
}

Status ReadableFile::DoClose() { return impl_->Close(); }

bool ReadableFile::closed() const { return !impl_->is_open(); }

Status ReadableFile::DoTell(int64_t* pos) const { return impl_->Tell(pos); }

Status ReadableFile::DoRead(int64_t nbytes, int64_t* bytes_read, void* out) {
  return impl_->Read(nbytes, bytes_read, out);
}

Status ReadableFile::DoReadAt(int64_t position, int64_t nbytes, int64_t* bytes_read,
                              void* out) {
  return impl_->ReadAt(position, nbytes, bytes_read, out);
}

Status ReadableFile::DoReadAt(int64_t position, int64_t nbytes,
                              std::shared_ptr<Buffer>* out) {
  return impl_->ReadBufferAt(position, nbytes, out);
}

Status ReadableFile::DoRead(int64_t nbytes, std::shared_ptr<Buffer>* out) {
  return impl_->ReadBuffer(nbytes, out);
}

Status ReadableFile::DoGetSize(int64_t* size) {
  *size = impl_->size();
  return Status::OK();
}

Status ReadableFile::DoSeek(int64_t pos) { return impl_->Seek(pos); }

int ReadableFile::file_descriptor() const { return impl_->fd(); }

// ----------------------------------------------------------------------
// FileOutputStream

class FileOutputStream::FileOutputStreamImpl : public OSFile {
 public:
  Status Open(const std::string& path, bool append) {
    const bool truncate = !append;
    return OpenWritable(path, truncate, append, true /* write_only */);
  }
  Status Open(int fd) { return OpenWritable(fd); }
};

FileOutputStream::FileOutputStream() { impl_.reset(new FileOutputStreamImpl()); }

FileOutputStream::~FileOutputStream() { internal::CloseFromDestructor(this); }

Status FileOutputStream::Open(const std::string& path,
                              std::shared_ptr<OutputStream>* file) {
  return Open(path, false, file);
}

Status FileOutputStream::Open(const std::string& path, bool append,
                              std::shared_ptr<OutputStream>* out) {
  *out = std::shared_ptr<FileOutputStream>(new FileOutputStream());
  return std::static_pointer_cast<FileOutputStream>(*out)->impl_->Open(path, append);
}

Status FileOutputStream::Open(int fd, std::shared_ptr<OutputStream>* out) {
  *out = std::shared_ptr<FileOutputStream>(new FileOutputStream());
  return std::static_pointer_cast<FileOutputStream>(*out)->impl_->Open(fd);
}

Status FileOutputStream::Open(const std::string& path,
                              std::shared_ptr<FileOutputStream>* file) {
  return Open(path, false, file);
}

Status FileOutputStream::Open(const std::string& path, bool append,
                              std::shared_ptr<FileOutputStream>* file) {
  // private ctor
  *file = std::shared_ptr<FileOutputStream>(new FileOutputStream());
  return (*file)->impl_->Open(path, append);
}

Status FileOutputStream::Open(int fd, std::shared_ptr<FileOutputStream>* file) {
  *file = std::shared_ptr<FileOutputStream>(new FileOutputStream());
  return (*file)->impl_->Open(fd);
}

Status FileOutputStream::Close() { return impl_->Close(); }

bool FileOutputStream::closed() const { return !impl_->is_open(); }

Status FileOutputStream::Tell(int64_t* pos) const { return impl_->Tell(pos); }

Status FileOutputStream::Write(const void* data, int64_t length) {
  return impl_->Write(data, length);
}

int FileOutputStream::file_descriptor() const { return impl_->fd(); }

// ----------------------------------------------------------------------
// Implement MemoryMappedFile

class MemoryMappedFile::MemoryMap
    : public std::enable_shared_from_this<MemoryMappedFile::MemoryMap> {
 public:
  // An object representing the entire memory-mapped region.
  // It can be sliced in order to return individual subregions, which
  // will then keep the original region alive as long as necessary.
  class Region : public MutableBuffer {
   public:
    Region(std::shared_ptr<MemoryMappedFile::MemoryMap> memory_map, uint8_t* data,
           int64_t size)
        : MutableBuffer(data, size) {
      is_mutable_ = memory_map->writable();
      if (!is_mutable_) {
        mutable_data_ = nullptr;
      }
    }

    ~Region() {
      if (data_ != nullptr) {
        int result = munmap(data(), static_cast<size_t>(size_));
        ARROW_CHECK_EQ(result, 0) << "munmap failed";
      }
    }

    // For convenience
    uint8_t* data() { return const_cast<uint8_t*>(data_); }

    void Detach() { data_ = nullptr; }
  };

  MemoryMap() : file_size_(0), map_len_(0) {}

  ~MemoryMap() { ARROW_CHECK_OK(Close()); }

  Status Close() {
    if (file_->is_open()) {
      // Lose our reference to the MemoryMappedRegion, so that munmap()
      // is called as soon as all buffer exports are released.
      region_.reset();
      return file_->Close();
    } else {
      return Status::OK();
    }
  }

  bool closed() const { return !file_->is_open(); }

  Status Open(const std::string& path, FileMode::type mode, const int64_t offset = 0,
              const int64_t length = -1) {
    file_.reset(new OSFile());

    if (mode != FileMode::READ) {
      // Memory mapping has permission failures if PROT_READ not set
      prot_flags_ = PROT_READ | PROT_WRITE;
      map_mode_ = MAP_SHARED;
      constexpr bool append = false;
      constexpr bool truncate = false;
      constexpr bool write_only = false;
      RETURN_NOT_OK(file_->OpenWritable(path, truncate, append, write_only));
    } else {
      prot_flags_ = PROT_READ;
      map_mode_ = MAP_PRIVATE;  // Changes are not to be committed back to the file
      RETURN_NOT_OK(file_->OpenReadable(path));
    }
    map_len_ = offset_ = 0;

    // Memory mapping fails when file size is 0
    // delay it until the first resize
    if (file_->size() > 0) {
      RETURN_NOT_OK(InitMMap(file_->size(), false, offset, length));
    }

    position_ = 0;

    return Status::OK();
  }

  // Resize the mmap and file to the specified size.
  // Resize on memory mapped file region is not supported.
  Status Resize(const int64_t new_size) {
    if (!writable()) {
      return Status::IOError("Cannot resize a readonly memory map");
    }
    if (map_len_ != file_size_) {
      return Status::IOError("Cannot resize a partial memory map");
    }
    if (region_.use_count() > 1) {
      // There are buffer exports currently, the MemoryMapRemap() call
      // would make the buffers invalid
      return Status::IOError("Cannot resize memory map while there are active readers");
    }

    if (new_size == 0) {
      if (map_len_ > 0) {
        // Just unmap the mmap and truncate the file to 0 size
        region_.reset();
        RETURN_NOT_OK(::arrow::internal::FileTruncate(file_->fd(), 0));
        map_len_ = offset_ = file_size_ = 0;
      }
      position_ = 0;
      return Status::OK();
    }

    if (map_len_ > 0) {
      void* result;
      auto data = region_->data();
      RETURN_NOT_OK(::arrow::internal::MemoryMapRemap(data, map_len_, new_size,
                                                      file_->fd(), &result));
      region_->Detach();  // avoid munmap() on destruction
      region_ = std::make_shared<Region>(shared_from_this(),
                                         static_cast<uint8_t*>(result), new_size);
      map_len_ = file_size_ = new_size;
      offset_ = 0;
      if (position_ > map_len_) {
        position_ = map_len_;
      }
    } else {
      DCHECK_EQ(position_, 0);
      // the mmap is not yet initialized, resize the underlying
      // file, since it might have been 0-sized
      RETURN_NOT_OK(InitMMap(new_size, /*resize_file*/ true));
    }
    return Status::OK();
  }

  Status Seek(int64_t position) {
    if (position < 0) {
      return Status::Invalid("position is out of bounds");
    }
    position_ = position;
    return Status::OK();
  }

  Status Slice(int64_t offset, int64_t length, std::shared_ptr<Buffer>* out) {
    length = std::max<int64_t>(0, std::min(length, map_len_ - offset));

    if (length > 0) {
      DCHECK_NE(region_, nullptr);
      *out = SliceBuffer(region_, offset, length);
    } else {
      *out = std::make_shared<Buffer>(nullptr, 0);
    }
    return Status::OK();
  }

  // map_len_ == file_size_ if memory mapping on the whole file
  int64_t size() const { return map_len_; }

  int64_t position() { return position_; }

  void advance(int64_t nbytes) { position_ = position_ + nbytes; }

  uint8_t* head() { return data() + position_; }

  uint8_t* data() { return region_ ? region_->data() : nullptr; }

  bool writable() { return file_->mode() != FileMode::READ; }

  bool opened() { return file_->is_open(); }

  int fd() const { return file_->fd(); }

  std::mutex& write_lock() { return file_->lock(); }

  std::mutex& resize_lock() { return resize_lock_; }

 private:
  // Initialize the mmap and set size, capacity and the data pointers
  Status InitMMap(int64_t initial_size, bool resize_file = false,
                  const int64_t offset = 0, const int64_t length = -1) {
    DCHECK(!region_);

    if (resize_file) {
      RETURN_NOT_OK(::arrow::internal::FileTruncate(file_->fd(), initial_size));
    }

    size_t mmap_length = static_cast<size_t>(initial_size);
    if (length > initial_size) {
      return Status::Invalid("mapping length is beyond file size");
    }
    if (length >= 0 && length < initial_size) {
      // memory mapping a file region
      mmap_length = static_cast<size_t>(length);
    }

    void* result = mmap(nullptr, mmap_length, prot_flags_, map_mode_, file_->fd(),
                        static_cast<off_t>(offset));
    if (result == MAP_FAILED) {
      return Status::IOError("Memory mapping file failed: ", std::strerror(errno));
    }
    map_len_ = mmap_length;
    offset_ = offset;
    region_ = std::make_shared<Region>(shared_from_this(), static_cast<uint8_t*>(result),
                                       map_len_);
    file_size_ = initial_size;

    return Status::OK();
  }

  std::unique_ptr<OSFile> file_;
  int prot_flags_;
  int map_mode_;

  std::shared_ptr<Region> region_;
  int64_t file_size_;
  int64_t position_;
  int64_t offset_;
  int64_t map_len_;
  std::mutex resize_lock_;
};

MemoryMappedFile::MemoryMappedFile() {}

MemoryMappedFile::~MemoryMappedFile() { internal::CloseFromDestructor(this); }

Status MemoryMappedFile::Create(const std::string& path, int64_t size,
                                std::shared_ptr<MemoryMappedFile>* out) {
  std::shared_ptr<FileOutputStream> file;
  RETURN_NOT_OK(FileOutputStream::Open(path, &file));

  RETURN_NOT_OK(::arrow::internal::FileTruncate(file->file_descriptor(), size));

  RETURN_NOT_OK(file->Close());
  return MemoryMappedFile::Open(path, FileMode::READWRITE, out);
}

Status MemoryMappedFile::Open(const std::string& path, FileMode::type mode,
                              std::shared_ptr<MemoryMappedFile>* out) {
  std::shared_ptr<MemoryMappedFile> result(new MemoryMappedFile());

  result->memory_map_.reset(new MemoryMap());
  RETURN_NOT_OK(result->memory_map_->Open(path, mode));

  *out = result;
  return Status::OK();
}

Status MemoryMappedFile::Open(const std::string& path, FileMode::type mode,
                              const int64_t offset, const int64_t length,
                              std::shared_ptr<MemoryMappedFile>* out) {
  std::shared_ptr<MemoryMappedFile> result(new MemoryMappedFile());

  result->memory_map_.reset(new MemoryMap());
  RETURN_NOT_OK(result->memory_map_->Open(path, mode, offset, length));

  *out = result;
  return Status::OK();
}

Status MemoryMappedFile::GetSize(int64_t* size) const {
  *size = memory_map_->size();
  return Status::OK();
}

Status MemoryMappedFile::GetSize(int64_t* size) {
  return static_cast<const MemoryMappedFile*>(this)->GetSize(size);
}

Status MemoryMappedFile::Tell(int64_t* position) const {
  *position = memory_map_->position();
  return Status::OK();
}

Status MemoryMappedFile::Seek(int64_t position) { return memory_map_->Seek(position); }

Status MemoryMappedFile::Close() { return memory_map_->Close(); }

bool MemoryMappedFile::closed() const { return memory_map_->closed(); }

Status MemoryMappedFile::ReadAt(int64_t position, int64_t nbytes,
                                std::shared_ptr<Buffer>* out) {
  // if the file is writable, we acquire the lock before creating any slices
  // in case a resize is triggered concurrently, otherwise we wouldn't detect
  // a change in the use count
  auto guard_resize = memory_map_->writable()
                          ? std::unique_lock<std::mutex>(memory_map_->resize_lock())
                          : std::unique_lock<std::mutex>();
  return memory_map_->Slice(position, nbytes, out);
}

Status MemoryMappedFile::ReadAt(int64_t position, int64_t nbytes, int64_t* bytes_read,
                                void* out) {
  auto guard_resize = memory_map_->writable()
                          ? std::unique_lock<std::mutex>(memory_map_->resize_lock())
                          : std::unique_lock<std::mutex>();
  nbytes = std::max<int64_t>(0, std::min(nbytes, memory_map_->size() - position));
  if (nbytes > 0) {
    memcpy(out, memory_map_->data() + position, static_cast<size_t>(nbytes));
  }
  *bytes_read = nbytes;
  return Status::OK();
}

Status MemoryMappedFile::Read(int64_t nbytes, int64_t* bytes_read, void* out) {
  RETURN_NOT_OK(ReadAt(memory_map_->position(), nbytes, bytes_read, out));
  memory_map_->advance(*bytes_read);
  return Status::OK();
}

Status MemoryMappedFile::Read(int64_t nbytes, std::shared_ptr<Buffer>* out) {
  RETURN_NOT_OK(ReadAt(memory_map_->position(), nbytes, out));
  memory_map_->advance((*out)->size());
  return Status::OK();
}

bool MemoryMappedFile::supports_zero_copy() const { return true; }

Status MemoryMappedFile::WriteAt(int64_t position, const void* data, int64_t nbytes) {
  std::lock_guard<std::mutex> guard(memory_map_->write_lock());

  if (!memory_map_->opened() || !memory_map_->writable()) {
    return Status::IOError("Unable to write");
  }
  if (position + nbytes > memory_map_->size()) {
    return Status::Invalid("Cannot write past end of memory map");
  }

  RETURN_NOT_OK(memory_map_->Seek(position));
  if (nbytes + memory_map_->position() > memory_map_->size()) {
    return Status::Invalid("Cannot write past end of memory map");
  }

  return WriteInternal(data, nbytes);
}

Status MemoryMappedFile::Write(const void* data, int64_t nbytes) {
  std::lock_guard<std::mutex> guard(memory_map_->write_lock());

  if (!memory_map_->opened() || !memory_map_->writable()) {
    return Status::IOError("Unable to write");
  }
  if (nbytes + memory_map_->position() > memory_map_->size()) {
    return Status::Invalid("Cannot write past end of memory map");
  }

  return WriteInternal(data, nbytes);
}

Status MemoryMappedFile::WriteInternal(const void* data, int64_t nbytes) {
  memcpy(memory_map_->head(), data, static_cast<size_t>(nbytes));
  memory_map_->advance(nbytes);
  return Status::OK();
}

Status MemoryMappedFile::Resize(int64_t new_size) {
  std::unique_lock<std::mutex> write_guard(memory_map_->write_lock(), std::defer_lock);
  std::unique_lock<std::mutex> resize_guard(memory_map_->resize_lock(), std::defer_lock);
  std::lock(write_guard, resize_guard);
  RETURN_NOT_OK(memory_map_->Resize(new_size));
  return Status::OK();
}

int MemoryMappedFile::file_descriptor() const { return memory_map_->fd(); }

}  // namespace io
}  // namespace arrow
