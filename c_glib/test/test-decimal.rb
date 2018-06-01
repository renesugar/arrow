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

class TestDecimal128 < Test::Unit::TestCase
  def test_to_string_scale
    integer_data = 23423445
    string_data = "234.23445"
    decimal = Arrow::Decimal128.new(integer_data)
    assert_equal(string_data, decimal.to_string_scale(5))
  end

  def test_to_string
    string_data = "99999999999999999999999999999999999999"
    decimal = Arrow::Decimal128.new(string_data)
    assert_equal(string_data, decimal.to_s)
  end

  def test_abs
    absolute_value = "23049223942343532412"
    negative_value = "-23049223942343532412"
    decimal = Arrow::Decimal128.new(negative_value)
    decimal.abs
    assert_equal(absolute_value, decimal.to_s)
  end

  def test_negate
    positive_value = "23049223942343532412"
    negative_value = "-23049223942343532412"
    decimal = Arrow::Decimal128.new(positive_value)
    decimal.negate
    assert_equal(negative_value, decimal.to_s)
    decimal.negate
    assert_equal(positive_value, decimal.to_s)
  end
end
