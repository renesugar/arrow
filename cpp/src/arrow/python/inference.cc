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

#include "arrow/python/inference.h"
#include "arrow/python/numpy_interop.h"

#include <datetime.h>

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "arrow/status.h"
#include "arrow/util/decimal.h"
#include "arrow/util/logging.h"

#include "arrow/python/decimal.h"
#include "arrow/python/helpers.h"
#include "arrow/python/iterators.h"
#include "arrow/python/numpy_convert.h"
#include "arrow/python/util/datetime.h"

namespace arrow {
namespace py {

#define _NUMPY_UNIFY_NOOP(DTYPE) \
  case NPY_##DTYPE:              \
    return NOOP;

#define _NUMPY_UNIFY_PROMOTE(DTYPE) \
  case NPY_##DTYPE:                 \
    return PROMOTE;

// Form a consensus NumPy dtype to use for Arrow conversion for a collection of dtype
// objects observed one at a time
class NumPyDtypeUnifier {
 public:
  enum Action { NOOP, PROMOTE, INVALID };

  NumPyDtypeUnifier() : current_type_num_(-1), current_dtype_(NULLPTR) {}

  Status InvalidMix(int new_dtype) {
    std::stringstream ss;
    ss << "Cannot mix NumPy dtypes " << GetNumPyTypeName(current_type_num_) << " and "
       << GetNumPyTypeName(new_dtype);
    return Status::Invalid(ss.str());
  }

  int Observe_BOOL(PyArray_Descr* descr, int dtype) { return INVALID; }

  int Observe_INT8(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_PROMOTE(INT16);
      _NUMPY_UNIFY_PROMOTE(INT32);
      _NUMPY_UNIFY_PROMOTE(INT64);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_INT16(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(INT8);
      _NUMPY_UNIFY_PROMOTE(INT32);
      _NUMPY_UNIFY_PROMOTE(INT64);
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_INT32(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(INT8);
      _NUMPY_UNIFY_NOOP(INT16);
      _NUMPY_UNIFY_PROMOTE(INT32);
      _NUMPY_UNIFY_PROMOTE(INT64);
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_INT64(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(INT8);
      _NUMPY_UNIFY_NOOP(INT16);
      _NUMPY_UNIFY_NOOP(INT32);
      _NUMPY_UNIFY_NOOP(INT64);
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_NOOP(UINT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_UINT8(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_PROMOTE(UINT16);
      _NUMPY_UNIFY_PROMOTE(UINT32);
      _NUMPY_UNIFY_PROMOTE(UINT64);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_UINT16(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_PROMOTE(UINT32);
      _NUMPY_UNIFY_PROMOTE(UINT64);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_UINT32(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_PROMOTE(UINT64);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_UINT64(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_NOOP(UINT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_FLOAT16(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_PROMOTE(FLOAT32);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_FLOAT32(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(INT8);
      _NUMPY_UNIFY_NOOP(INT16);
      _NUMPY_UNIFY_NOOP(INT32);
      _NUMPY_UNIFY_NOOP(INT64);
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_NOOP(UINT32);
      _NUMPY_UNIFY_NOOP(UINT64);
      _NUMPY_UNIFY_PROMOTE(FLOAT64);
      default:
        return INVALID;
    }
  }

  int Observe_FLOAT64(PyArray_Descr* descr, int dtype) {
    switch (dtype) {
      _NUMPY_UNIFY_NOOP(INT8);
      _NUMPY_UNIFY_NOOP(INT16);
      _NUMPY_UNIFY_NOOP(INT32);
      _NUMPY_UNIFY_NOOP(INT64);
      _NUMPY_UNIFY_NOOP(UINT8);
      _NUMPY_UNIFY_NOOP(UINT16);
      _NUMPY_UNIFY_NOOP(UINT32);
      _NUMPY_UNIFY_NOOP(UINT64);
      default:
        return INVALID;
    }
  }

  int Observe_DATETIME(PyArray_Descr* dtype_obj) {
    // TODO: check that units are all the same
    // current_dtype_ = dtype_obj->type_num;
    return NOOP;
  }

  Status Observe(PyArray_Descr* descr) {
    const int dtype = fix_numpy_type_num(descr->type_num);

    if (current_type_num_ == -1) {
      current_dtype_ = descr;
      current_type_num_ = dtype;
      return Status::OK();
    } else if (current_type_num_ == dtype) {
      return Status::OK();
    }

#define OBSERVE_CASE(DTYPE)                 \
  case NPY_##DTYPE:                         \
    action = Observe_##DTYPE(descr, dtype); \
    break;

    int action = NOOP;
    switch (current_type_num_) {
      OBSERVE_CASE(BOOL);
      OBSERVE_CASE(INT8);
      OBSERVE_CASE(INT16);
      OBSERVE_CASE(INT32);
      OBSERVE_CASE(INT64);
      OBSERVE_CASE(UINT8);
      OBSERVE_CASE(UINT16);
      OBSERVE_CASE(UINT32);
      OBSERVE_CASE(UINT64);
      OBSERVE_CASE(FLOAT16);
      OBSERVE_CASE(FLOAT32);
      OBSERVE_CASE(FLOAT64);
      case NPY_DATETIME:
        action = Observe_DATETIME(descr);
        break;
      default:
        std::stringstream ss;
        ss << "Unsupported numpy type " << GetNumPyTypeName(dtype) << std::endl;
        return Status::NotImplemented(ss.str());
    }

    if (action == INVALID) {
      return InvalidMix(dtype);
    } else if (action == PROMOTE) {
      current_type_num_ = dtype;
      current_dtype_ = descr;
    }
    return Status::OK();
  }

  bool dtype_was_observed() const { return current_type_num_ != -1; }

  PyArray_Descr* current_dtype() const { return current_dtype_; }

 private:
  int current_type_num_;
  PyArray_Descr* current_dtype_;
};

class TypeInferrer {
  // A type inference visitor for Python values
 public:
  // \param validate_interval the number of elements to observe before checking
  // whether the data is mixed type or has other problems. This helps avoid
  // excess computation for each element while also making sure we "bail out"
  // early with long sequences that may have problems up front
  // \param make_unions permit mixed-type data by creating union types (not yet
  // implemented)
  explicit TypeInferrer(int64_t validate_interval = 100, bool make_unions = false)
      : validate_interval_(validate_interval),
        make_unions_(make_unions),
        total_count_(0),
        none_count_(0),
        bool_count_(0),
        int_count_(0),
        date_count_(0),
        time_count_(0),
        timestamp_second_count_(0),
        timestamp_milli_count_(0),
        timestamp_micro_count_(0),
        timestamp_nano_count_(0),
        float_count_(0),
        binary_count_(0),
        unicode_count_(0),
        decimal_count_(0),
        list_count_(0),
        struct_count_(0),
        max_decimal_metadata_(std::numeric_limits<int32_t>::min(),
                              std::numeric_limits<int32_t>::min()),
        decimal_type_() {
    Status status = internal::ImportDecimalType(&decimal_type_);
    DCHECK_OK(status);
  }

  /// \param[in] obj a Python object in the sequence
  /// \param[out] keep_going if sufficient information has been gathered to
  /// attempt to begin converting the sequence, *keep_going will be set to true
  /// to signal to the calling visitor loop to terminate
  Status Visit(PyObject* obj, bool* keep_going) {
    ++total_count_;

    if (obj == Py_None || internal::PyFloat_IsNaN(obj)) {
      ++none_count_;
    } else if (PyBool_Check(obj)) {
      ++bool_count_;
      *keep_going = make_unions_;
    } else if (internal::PyFloatScalar_Check(obj)) {
      ++float_count_;
      *keep_going = make_unions_;
    } else if (internal::IsPyInteger(obj)) {
      ++int_count_;
    } else if (PyDateTime_Check(obj)) {
      ++timestamp_micro_count_;
      *keep_going = make_unions_;
    } else if (PyDate_Check(obj)) {
      ++date_count_;
      *keep_going = make_unions_;
    } else if (PyTime_Check(obj)) {
      ++time_count_;
      *keep_going = make_unions_;
    } else if (internal::IsPyBinary(obj)) {
      ++binary_count_;
      *keep_going = make_unions_;
    } else if (PyUnicode_Check(obj)) {
      ++unicode_count_;
      *keep_going = make_unions_;
    } else if (PyArray_CheckAnyScalarExact(obj)) {
      RETURN_NOT_OK(VisitDType(PyArray_DescrFromScalar(obj), keep_going));
    } else if (PyList_Check(obj)) {
      RETURN_NOT_OK(VisitList(obj, keep_going));
    } else if (PyArray_Check(obj)) {
      RETURN_NOT_OK(VisitNdarray(obj, keep_going));
    } else if (PyDict_Check(obj)) {
      RETURN_NOT_OK(VisitDict(obj));
    } else if (PyObject_IsInstance(obj, decimal_type_.obj())) {
      RETURN_NOT_OK(max_decimal_metadata_.Update(obj));
      ++decimal_count_;
    } else {
      return internal::InvalidValue(obj,
                                    "did not recognize Python value type when inferring "
                                    "an Arrow data type");
    }

    if (total_count_ % validate_interval_ == 0) {
      RETURN_NOT_OK(Validate());
    }

    return Status::OK();
  }

  // Infer value type from a sequence of values
  Status VisitSequence(PyObject* obj) {
    return internal::VisitSequence(obj, [this](PyObject* value, bool* keep_going) {
      return Visit(value, keep_going);
    });
  }

  Status GetType(std::shared_ptr<DataType>* out) const {
    // TODO(wesm): handling forming unions
    if (make_unions_) {
      return Status::NotImplemented("Creating union types not yet supported");
    }

    RETURN_NOT_OK(Validate());

    if (numpy_unifier_.current_dtype() != nullptr) {
      std::shared_ptr<DataType> type;
      RETURN_NOT_OK(NumPyDtypeToArrow(numpy_unifier_.current_dtype(), &type));
      *out = type;
    } else if (list_count_) {
      std::shared_ptr<DataType> value_type;
      RETURN_NOT_OK(list_inferrer_->GetType(&value_type));
      *out = list(value_type);
    } else if (struct_count_) {
      RETURN_NOT_OK(GetStructType(out));
    } else if (decimal_count_) {
      *out = decimal(max_decimal_metadata_.precision(), max_decimal_metadata_.scale());
    } else if (float_count_) {
      // Prioritize floats before integers
      *out = float64();
    } else if (int_count_) {
      *out = int64();
    } else if (date_count_) {
      *out = date32();
    } else if (time_count_) {
      *out = time64(TimeUnit::MICRO);
    } else if (timestamp_nano_count_) {
      *out = timestamp(TimeUnit::NANO);
    } else if (timestamp_micro_count_) {
      *out = timestamp(TimeUnit::MICRO);
    } else if (timestamp_milli_count_) {
      *out = timestamp(TimeUnit::MILLI);
    } else if (timestamp_second_count_) {
      *out = timestamp(TimeUnit::SECOND);
    } else if (bool_count_) {
      *out = boolean();
    } else if (binary_count_) {
      *out = binary();
    } else if (unicode_count_) {
      *out = utf8();
    } else {
      *out = null();
    }
    return Status::OK();
  }

  int64_t total_count() const { return total_count_; }

 protected:
  Status Validate() const {
    if (list_count_ > 0) {
      if (list_count_ + none_count_ != total_count_) {
        return Status::Invalid("cannot mix list and non-list, non-null values");
      }
      RETURN_NOT_OK(list_inferrer_->Validate());
    } else if (struct_count_ > 0) {
      if (struct_count_ + none_count_ != total_count_) {
        return Status::Invalid("cannot mix struct and non-struct, non-null values");
      }
      for (const auto& it : struct_inferrers_) {
        RETURN_NOT_OK(it.second.Validate());
      }
    }
    return Status::OK();
  }

  Status VisitDType(PyArray_Descr* dtype, bool* keep_going) {
    // Continue visiting dtypes for now.
    // TODO(wesm): devise approach for unions
    *keep_going = true;
    return numpy_unifier_.Observe(dtype);
  }

  Status VisitList(PyObject* obj, bool* keep_going /* unused */) {
    if (!list_inferrer_) {
      list_inferrer_.reset(new TypeInferrer(validate_interval_, make_unions_));
    }
    ++list_count_;
    return list_inferrer_->VisitSequence(obj);
  }

  Status VisitNdarray(PyObject* obj, bool* keep_going) {
    PyArray_Descr* dtype = PyArray_DESCR(reinterpret_cast<PyArrayObject*>(obj));
    if (dtype->type_num == NPY_OBJECT) {
      return VisitList(obj, keep_going);
    }
    // Not an object array: infer child Arrow type from dtype
    if (!list_inferrer_) {
      list_inferrer_.reset(new TypeInferrer(validate_interval_, make_unions_));
    }
    ++list_count_;
    return list_inferrer_->VisitDType(dtype, keep_going);
  }

  Status VisitDict(PyObject* obj) {
    PyObject* key_obj;
    PyObject* value_obj;
    Py_ssize_t pos = 0;

    while (PyDict_Next(obj, &pos, &key_obj, &value_obj)) {
      std::string key;
      if (PyUnicode_Check(key_obj)) {
        RETURN_NOT_OK(internal::PyUnicode_AsStdString(key_obj, &key));
      } else if (PyBytes_Check(key_obj)) {
        key = internal::PyBytes_AsStdString(key_obj);
      } else {
        std::stringstream ss;
        ss << "Expected dict key of type str or bytes, got '" << Py_TYPE(key_obj)->tp_name
           << "'";
        return Status::TypeError(ss.str());
      }
      // Get or create visitor for this key
      auto it = struct_inferrers_.find(key);
      if (it == struct_inferrers_.end()) {
        it = struct_inferrers_
                 .insert(
                     std::make_pair(key, TypeInferrer(validate_interval_, make_unions_)))
                 .first;
      }
      TypeInferrer* visitor = &it->second;

      // We ignore termination signals from child visitors for now
      //
      // TODO(wesm): keep track of whether type inference has terminated for
      // the child visitors to avoid doing unneeded work
      bool keep_going = true;
      RETURN_NOT_OK(visitor->Visit(value_obj, &keep_going));
    }

    // We do not terminate visiting dicts since we want the union of all
    // observed keys
    ++struct_count_;
    return Status::OK();
  }

  Status GetStructType(std::shared_ptr<DataType>* out) const {
    std::vector<std::shared_ptr<Field>> fields;
    for (const auto& it : struct_inferrers_) {
      std::shared_ptr<DataType> field_type;
      RETURN_NOT_OK(it.second.GetType(&field_type));
      fields.emplace_back(field(it.first, field_type));
    }
    *out = struct_(fields);
    return Status::OK();
  }

 private:
  int64_t validate_interval_;
  bool make_unions_;
  int64_t total_count_;
  int64_t none_count_;
  int64_t bool_count_;
  int64_t int_count_;
  int64_t date_count_;
  int64_t time_count_;
  int64_t timestamp_second_count_;
  int64_t timestamp_milli_count_;
  int64_t timestamp_micro_count_;
  int64_t timestamp_nano_count_;
  int64_t float_count_;
  int64_t binary_count_;
  int64_t unicode_count_;
  int64_t decimal_count_;
  int64_t list_count_;
  std::unique_ptr<TypeInferrer> list_inferrer_;
  int64_t struct_count_;
  std::map<std::string, TypeInferrer> struct_inferrers_;

  // If we observe a strongly-typed value in e.g. a NumPy array, we can store
  // it here to skip the type counting logic above
  NumPyDtypeUnifier numpy_unifier_;

  internal::DecimalMetadata max_decimal_metadata_;

  // Place to accumulate errors
  // std::vector<Status> errors_;
  OwnedRefNoGIL decimal_type_;
};

// Non-exhaustive type inference
Status InferArrowType(PyObject* obj, std::shared_ptr<DataType>* out_type) {
  PyDateTime_IMPORT;
  TypeInferrer inferrer;
  RETURN_NOT_OK(inferrer.VisitSequence(obj));
  RETURN_NOT_OK(inferrer.GetType(out_type));
  if (*out_type == nullptr) {
    return Status::TypeError("Unable to determine data type");
  }

  return Status::OK();
}

Status InferArrowTypeAndSize(PyObject* obj, int64_t* size,
                             std::shared_ptr<DataType>* out_type) {
  if (!PySequence_Check(obj)) {
    return Status::TypeError("Object is not a sequence");
  }
  *size = static_cast<int64_t>(PySequence_Size(obj));

  // For 0-length sequences, refuse to guess
  if (*size == 0) {
    *out_type = null();
    return Status::OK();
  }
  RETURN_NOT_OK(InferArrowType(obj, out_type));

  return Status::OK();
}

}  // namespace py
}  // namespace arrow
