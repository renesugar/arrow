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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arrow/dataset/test_util.h"
#include "arrow/filesystem/test_util.h"

namespace arrow {
namespace dataset {

class FileSystemDataSourceDiscoveryTest : public TestFileSystemBasedDataSource {
 public:
  void MakeDiscovery(const std::vector<fs::FileStats>& files) {
    MakeFileSystem(files);
    ASSERT_OK(
        FileSystemDataSourceDiscovery::Make(fs_.get(), files, format_, &discovery_));
  }

  void MakeDiscovery(const std::vector<fs::FileStats>& files, fs::Selector selector) {
    MakeFileSystem(files);
    ASSERT_OK(
        FileSystemDataSourceDiscovery::Make(fs_.get(), selector, format_, &discovery_));
  }

 protected:
  std::shared_ptr<DataSourceDiscovery> discovery_;
  std::shared_ptr<FileFormat> format_ = std::make_shared<DummyFileFormat>();
};

TEST_F(FileSystemDataSourceDiscoveryTest, Basic) {
  MakeDiscovery({fs::File("a"), fs::File("b")});

  ASSERT_OK(discovery_->Finish(&source_));
  AssertFragmentsAreFromPath(source_->GetFragments(options_), {"a", "b"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, Selector) {
  // This test ensure that the Selector is enforced.
  fs::Selector selector;
  selector.base_dir = "A";
  MakeDiscovery({fs::File("0"), fs::File("A/a")}, selector);

  ASSERT_OK(discovery_->Finish(&source_));
  AssertFragmentsAreFromPath(source_->GetFragments(options_), {"A/a"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, Inspect) {
  auto s = schema({field("f64", float64())});
  format_ = std::make_shared<DummyFileFormat>(s);

  MakeDiscovery({});
  std::shared_ptr<Schema> actual;

  // No files
  ASSERT_OK(discovery_->Inspect(&actual));
  EXPECT_EQ(actual, nullptr);

  MakeDiscovery({fs::File("test")});
  ASSERT_OK(discovery_->Inspect(&actual));
  EXPECT_EQ(actual, s);
}

}  // namespace dataset
}  // namespace arrow
