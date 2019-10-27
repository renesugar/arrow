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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "arrow/buffer.h"
#include "arrow/memory_pool.h"
#include "arrow/scalar.h"
#include "arrow/status.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/type_traits.h"

namespace arrow {

TEST(TestNullScalar, Basics) {
  NullScalar scalar;
  ASSERT_FALSE(scalar.is_valid);
  ASSERT_TRUE(scalar.type->Equals(*null()));
}

template <typename T>
class TestNumericScalar : public ::testing::Test {
 public:
  TestNumericScalar() {}
};

TYPED_TEST_CASE(TestNumericScalar, NumericArrowTypes);

TYPED_TEST(TestNumericScalar, Basics) {
  using T = typename TypeParam::c_type;
  using ScalarType = typename TypeTraits<TypeParam>::ScalarType;

  T value = static_cast<T>(1);

  auto scalar_val = std::make_shared<ScalarType>(value);
  ASSERT_EQ(value, scalar_val->value);
  ASSERT_TRUE(scalar_val->is_valid);

  auto expected_type = TypeTraits<TypeParam>::type_singleton();
  ASSERT_TRUE(scalar_val->type->Equals(*expected_type));

  T other_value = static_cast<T>(2);
  auto scalar_other = std::make_shared<ScalarType>(other_value);
  ASSERT_FALSE(scalar_other->Equals(scalar_val));

  scalar_val->value = other_value;
  ASSERT_EQ(other_value, scalar_val->value);
  ASSERT_TRUE(scalar_other->Equals(scalar_val));

  ScalarType stack_val = ScalarType(0, false);
  ASSERT_FALSE(stack_val.is_valid);
}

TYPED_TEST(TestNumericScalar, MakeScalar) {
  using T = typename TypeParam::c_type;
  using ScalarType = typename TypeTraits<TypeParam>::ScalarType;

  std::shared_ptr<Scalar> three = MakeScalar(static_cast<T>(3));
  ASSERT_TRUE(ScalarType(3).Equals(three));

  ASSERT_OK(
      MakeScalar(TypeTraits<TypeParam>::type_singleton(), static_cast<T>(3), &three));
  ASSERT_TRUE(ScalarType(3).Equals(three));

  ASSERT_OK(Scalar::Parse(TypeTraits<TypeParam>::type_singleton(), "3", &three));
  ASSERT_TRUE(ScalarType(3).Equals(three));
}

TEST(TestBinaryScalar, Basics) {
  std::string data = "test data";
  auto buf = std::make_shared<Buffer>(data);

  BinaryScalar value(buf);
  ASSERT_TRUE(value.value->Equals(*buf));
  ASSERT_TRUE(value.is_valid);
  ASSERT_TRUE(value.type->Equals(*binary()));

  auto ref_count = buf.use_count();
  // Check that destructor doesn't fail to clean up a buffer
  std::shared_ptr<Scalar> base_ref = std::make_shared<BinaryScalar>(buf);
  base_ref = nullptr;
  ASSERT_EQ(ref_count, buf.use_count());

  BinaryScalar null_value(nullptr, false);
  ASSERT_FALSE(null_value.is_valid);

  StringScalar value2(buf);
  ASSERT_TRUE(value2.value->Equals(*buf));
  ASSERT_TRUE(value2.is_valid);
  ASSERT_TRUE(value2.type->Equals(*utf8()));

  // Same buffer, different type.
  ASSERT_FALSE(value2.Equals(value));

  StringScalar value3(buf);
  // Same buffer, same type.
  ASSERT_TRUE(value2.Equals(value3));

  StringScalar null_value2(nullptr, false);
  ASSERT_FALSE(null_value2.is_valid);
}

TEST(TestStringScalar, MakeScalar) {
  auto three = MakeScalar("three");
  ASSERT_TRUE(StringScalar("three").Equals(three));

  ASSERT_OK(MakeScalar(utf8(), Buffer::FromString("three"), &three));
  ASSERT_TRUE(StringScalar("three").Equals(three));

  ASSERT_OK(Scalar::Parse(utf8(), "three", &three));
  ASSERT_TRUE(StringScalar("three").Equals(three));
}

TEST(TestFixedSizeBinaryScalar, Basics) {
  std::string data = "test data";
  auto buf = std::make_shared<Buffer>(data);

  auto ex_type = fixed_size_binary(9);

  FixedSizeBinaryScalar value(buf, ex_type);
  ASSERT_TRUE(value.value->Equals(*buf));
  ASSERT_TRUE(value.is_valid);
  ASSERT_TRUE(value.type->Equals(*ex_type));
}

TEST(TestFixedSizeBinaryScalar, MakeScalar) {
  std::string data = "test data";
  auto buf = std::make_shared<Buffer>(data);
  auto type = fixed_size_binary(9);

  std::shared_ptr<Scalar> s;
  ASSERT_OK(MakeScalar(type, buf, &s));
  ASSERT_TRUE(FixedSizeBinaryScalar(buf, type).Equals(s));

  ASSERT_OK(Scalar::Parse(type, util::string_view(data), &s));
  ASSERT_TRUE(FixedSizeBinaryScalar(buf, type).Equals(s));

  // wrong length:
  ASSERT_RAISES(Invalid, MakeScalar(type, Buffer::FromString(data.substr(3)), &s));
  ASSERT_RAISES(Invalid, Scalar::Parse(type, util::string_view(data).substr(3), &s));
}

TEST(TestDateScalars, Basics) {
  int32_t i32_val = 1;
  Date32Scalar date32_val(i32_val);
  Date32Scalar date32_null(i32_val, false);
  ASSERT_EQ(i32_val, date32_val.value);
  ASSERT_TRUE(date32_val.type->Equals(*date32()));
  ASSERT_TRUE(date32_val.is_valid);
  ASSERT_FALSE(date32_null.is_valid);

  int64_t i64_val = 2;
  Date64Scalar date64_val(i64_val);
  Date64Scalar date64_null(i64_val, false);
  ASSERT_EQ(i64_val, date64_val.value);
  ASSERT_TRUE(date64_val.type->Equals(*date64()));
  ASSERT_TRUE(date64_val.is_valid);
  ASSERT_FALSE(date64_null.is_valid);
}

TEST(TestDateScalars, MakeScalar) {
  std::shared_ptr<Scalar> s;
  ASSERT_OK(MakeScalar(date32(), int32_t(1), &s));
  ASSERT_TRUE(Date32Scalar(1).Equals(s));

  ASSERT_OK(MakeScalar(date64(), int64_t(1), &s));
  ASSERT_TRUE(Date64Scalar(1).Equals(s));

  ASSERT_RAISES(NotImplemented, Scalar::Parse(date64(), "", &s));
}

TEST(TestTimeScalars, Basics) {
  auto type1 = time32(TimeUnit::MILLI);
  auto type2 = time32(TimeUnit::SECOND);
  auto type3 = time64(TimeUnit::MICRO);
  auto type4 = time64(TimeUnit::NANO);

  int32_t i32_val = 1;
  Time32Scalar time32_val(i32_val, type1);
  Time32Scalar time32_null(i32_val, type2, false);
  ASSERT_EQ(i32_val, time32_val.value);
  ASSERT_TRUE(time32_val.type->Equals(*type1));
  ASSERT_TRUE(time32_val.is_valid);
  ASSERT_FALSE(time32_null.is_valid);
  ASSERT_TRUE(time32_null.type->Equals(*type2));

  int64_t i64_val = 2;
  Time64Scalar time64_val(i64_val, type3);
  Time64Scalar time64_null(i64_val, type4, false);
  ASSERT_EQ(i64_val, time64_val.value);
  ASSERT_TRUE(time64_val.type->Equals(*type3));
  ASSERT_TRUE(time64_val.is_valid);
  ASSERT_FALSE(time64_null.is_valid);
  ASSERT_TRUE(time64_null.type->Equals(*type4));
}

TEST(TestTimeScalars, MakeScalar) {
  auto type1 = time32(TimeUnit::MILLI);
  auto type2 = time32(TimeUnit::SECOND);
  auto type3 = time64(TimeUnit::MICRO);
  auto type4 = time64(TimeUnit::NANO);

  std::shared_ptr<Scalar> s;

  ASSERT_OK(MakeScalar(type1, int32_t(1), &s));
  ASSERT_TRUE(Time32Scalar(1, type1).Equals(s));

  ASSERT_OK(MakeScalar(type2, int32_t(1), &s));
  ASSERT_TRUE(Time32Scalar(1, type2).Equals(s));

  ASSERT_OK(MakeScalar(type3, int64_t(1), &s));
  ASSERT_TRUE(Time64Scalar(1, type3).Equals(s));

  ASSERT_OK(MakeScalar(type4, int64_t(1), &s));
  ASSERT_TRUE(Time64Scalar(1, type4).Equals(s));

  ASSERT_RAISES(NotImplemented, Scalar::Parse(type4, "", &s));
}

TEST(TestTimestampScalars, Basics) {
  auto type1 = timestamp(TimeUnit::MILLI);
  auto type2 = timestamp(TimeUnit::SECOND);

  int64_t val1 = 1;
  int64_t val2 = 2;
  TimestampScalar ts_val1(val1, type1);
  TimestampScalar ts_val2(val2, type2);
  TimestampScalar ts_null(val2, type1, false);
  ASSERT_EQ(val1, ts_val1.value);
  ASSERT_EQ(val2, ts_null.value);

  ASSERT_TRUE(ts_val1.type->Equals(*type1));
  ASSERT_TRUE(ts_val2.type->Equals(*type2));
  ASSERT_TRUE(ts_val1.is_valid);
  ASSERT_FALSE(ts_null.is_valid);
  ASSERT_TRUE(ts_null.type->Equals(*type1));

  ASSERT_FALSE(ts_val1.Equals(ts_val2));
  ASSERT_FALSE(ts_val1.Equals(ts_null));
  ASSERT_FALSE(ts_val2.Equals(ts_null));
}

TEST(TestTimestampScalars, MakeScalar) {
  auto type1 = timestamp(TimeUnit::MILLI);
  auto type2 = timestamp(TimeUnit::SECOND);
  auto type3 = timestamp(TimeUnit::MICRO);
  auto type4 = timestamp(TimeUnit::NANO);

  std::shared_ptr<Scalar> s;

  util::string_view epoch_plus_1s = "1970-01-01 00:00:01";

  ASSERT_OK(MakeScalar(type1, int64_t(1), &s));
  ASSERT_TRUE(TimestampScalar(1, type1).Equals(s));
  ASSERT_OK(Scalar::Parse(type1, epoch_plus_1s, &s));
  ASSERT_TRUE(TimestampScalar(1000, type1).Equals(s));

  ASSERT_OK(MakeScalar(type2, int64_t(1), &s));
  ASSERT_TRUE(TimestampScalar(1, type2).Equals(s));
  ASSERT_OK(Scalar::Parse(type2, epoch_plus_1s, &s));
  ASSERT_TRUE(TimestampScalar(1, type2).Equals(s));

  ASSERT_OK(MakeScalar(type3, int64_t(1), &s));
  ASSERT_TRUE(TimestampScalar(1, type3).Equals(s));
  ASSERT_OK(Scalar::Parse(type3, epoch_plus_1s, &s));
  ASSERT_TRUE(TimestampScalar(1000 * 1000, type3).Equals(s));

  ASSERT_OK(MakeScalar(type4, int64_t(1), &s));
  ASSERT_TRUE(TimestampScalar(1, type4).Equals(s));
  ASSERT_OK(Scalar::Parse(type4, epoch_plus_1s, &s));
  ASSERT_TRUE(TimestampScalar(1000 * 1000 * 1000, type4).Equals(s));
}

TEST(TestDurationScalars, Basics) {
  auto type1 = duration(TimeUnit::MILLI);
  auto type2 = duration(TimeUnit::SECOND);

  int64_t val1 = 1;
  int64_t val2 = 2;
  DurationScalar ts_val1(val1, type1);
  DurationScalar ts_val2(val2, type2);
  DurationScalar ts_null(val2, type1, false);
  ASSERT_EQ(val1, ts_val1.value);
  ASSERT_EQ(val2, ts_null.value);

  ASSERT_TRUE(ts_val1.type->Equals(*type1));
  ASSERT_TRUE(ts_val2.type->Equals(*type2));
  ASSERT_TRUE(ts_val1.is_valid);
  ASSERT_FALSE(ts_null.is_valid);
  ASSERT_TRUE(ts_null.type->Equals(*type1));

  ASSERT_FALSE(ts_val1.Equals(ts_val2));
  ASSERT_FALSE(ts_val1.Equals(ts_null));
  ASSERT_FALSE(ts_val2.Equals(ts_null));
}

TEST(TestMonthIntervalScalars, Basics) {
  auto type = month_interval();

  int32_t val1 = 1;
  int32_t val2 = 2;
  MonthIntervalScalar ts_val1(val1);
  MonthIntervalScalar ts_val2(val2);
  MonthIntervalScalar ts_null(val2, false);
  ASSERT_EQ(val1, ts_val1.value);
  ASSERT_EQ(val2, ts_null.value);

  ASSERT_TRUE(ts_val1.type->Equals(*type));
  ASSERT_TRUE(ts_val2.type->Equals(*type));
  ASSERT_TRUE(ts_val1.is_valid);
  ASSERT_FALSE(ts_null.is_valid);
  ASSERT_TRUE(ts_null.type->Equals(*type));

  ASSERT_FALSE(ts_val1.Equals(ts_val2));
  ASSERT_FALSE(ts_val1.Equals(ts_null));
  ASSERT_FALSE(ts_val2.Equals(ts_null));
}

TEST(TestDayTimeIntervalScalars, Basics) {
  auto type = day_time_interval();

  DayTimeIntervalType::DayMilliseconds val1 = {1, 1};
  DayTimeIntervalType::DayMilliseconds val2 = {2, 2};
  DayTimeIntervalScalar ts_val1(val1);
  DayTimeIntervalScalar ts_val2(val2);
  DayTimeIntervalScalar ts_null(val2, false);
  ASSERT_EQ(val1, ts_val1.value);
  ASSERT_EQ(val2, ts_null.value);

  ASSERT_TRUE(ts_val1.type->Equals(*type));
  ASSERT_TRUE(ts_val2.type->Equals(*type));
  ASSERT_TRUE(ts_val1.is_valid);
  ASSERT_FALSE(ts_null.is_valid);
  ASSERT_TRUE(ts_null.type->Equals(*type));

  ASSERT_FALSE(ts_val1.Equals(ts_val2));
  ASSERT_FALSE(ts_val1.Equals(ts_null));
  ASSERT_FALSE(ts_val2.Equals(ts_null));
}

// TODO test HalfFloatScalar

}  // namespace arrow
