//   Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "paddle/phi/common/backend.h"
#include "paddle/phi/common/data_type.h"
#include "paddle/phi/common/layout.h"
#include "paddle/phi/core/compat/convert_utils.h"
#include "paddle/phi/core/enforce.h"
#include "paddle/phi/core/type_defs.h"
#include "paddle/utils/flat_hash_map.h"
#include "paddle/utils/small_vector.h"

namespace phi {

using DataType = paddle::experimental::DataType;
using DataLayout = paddle::experimental::DataLayout;

/**
 * [ Naming considerations ]
 *
 * The tensor operation library contains many kernels, and the computation
 * in each specific scenario is represented by an kernel.
 *
 * We directly named it `Kernel` instead of `Kernel`, the tensor operation
 * library here and fluid are independent, avoiding developers from
 * misunderstanding the relationship between the two concepts.
 */

class KernelContext;

class KernelKey {
 public:
  KernelKey() = default;

  KernelKey(Backend backend, DataLayout layout, DataType dtype)
      : backend_(backend), layout_(layout), dtype_(dtype) {}

  Backend backend() const { return backend_; }
  DataLayout layout() const { return layout_; }
  DataType dtype() const { return dtype_; }

  struct Hash {
    // Note: Now the number of bits we need does not exceed 32 bits, so there is
    // no need to use 64 bits. If needed in the future, it can be expanded,
    // but now we don’t over-design.
    uint32_t operator()(const KernelKey& key) const;
  };

  uint32_t hash_value() const { return Hash()(*this); }

  bool operator<(const KernelKey& key) const {
    return hash_value() < key.hash_value();
  }

  bool operator==(const KernelKey& key) const {
    return hash_value() == key.hash_value();
  }

  bool operator!=(const KernelKey& key) const {
    return hash_value() != key.hash_value();
  }

 private:
  // In total should be smaller than 32.
  constexpr static int kBackendBitLength = 8;
  constexpr static int kDataLayoutBitLength = 4;
  constexpr static int kDataTypeBitLength = 8;

  Backend backend_{Backend::UNDEFINED};
  DataLayout layout_{DataLayout::UNDEFINED};
  DataType dtype_{DataType::UNDEFINED};
};

// TODO(chenweihang): how deal with vector<Param>?
struct TensorArgDef {
  Backend backend;
  DataLayout layout;
  DataType dtype;
  std::type_index type_index;

  TensorArgDef(Backend in_backend,
               DataLayout in_layout,
               DataType in_dtype,
               std::type_index in_type_index)
      : backend(in_backend),
        layout(in_layout),
        dtype(in_dtype),
        type_index(in_type_index) {}

  TensorArgDef& SetBackend(Backend in_backend) {
    backend = in_backend;
    return *this;
  }

  TensorArgDef& SetDataLayout(DataLayout in_layout) {
    layout = in_layout;
    return *this;
  }

  TensorArgDef& SetDataType(DataType in_dtype) {
    dtype = in_dtype;
    return *this;
  }
};

// Align the original fluid Attribute type with lower overhead
enum class AttributeType {
  UNDEFINED = 0,
  BOOL,
  INT32,
  INT64,
  FLOAT32,
  FLOAT64,
  STRING,
  BOOLS,
  INT32S,
  INT64S,
  FLOAT32S,
  FLOAT64S,
  STRINGS,
  SCALAR,
  SCALARS,
  INT_ARRAY,
  DATA_TYPE,
  DATA_LAYOUT,
  PLACE,
};

struct AttributeArgDef {
  AttributeType type_index;

  explicit AttributeArgDef(AttributeType type_index) : type_index(type_index) {}
};

class KernelArgsDef {
 public:
  KernelArgsDef() = default;

  void AppendInput(Backend backend,
                   DataLayout layout,
                   DataType dtype,
                   std::type_index type_index) {
    input_defs_.emplace_back(TensorArgDef(backend, layout, dtype, type_index));
  }

  void AppendOutput(Backend backend,
                    DataLayout layout,
                    DataType dtype,
                    std::type_index type_index) {
    output_defs_.emplace_back(TensorArgDef(backend, layout, dtype, type_index));
  }

  void AppendAttribute(AttributeType type_index) {
    attribute_defs_.emplace_back(AttributeArgDef(type_index));
  }

  const paddle::small_vector<TensorArgDef, kInputSmallVectorSize>& input_defs()
      const {
    return input_defs_;
  }

  const paddle::small_vector<TensorArgDef, kOutputSmallVectorSize>&
  output_defs() const {
    return output_defs_;
  }

  const paddle::small_vector<AttributeArgDef, kAttrSmallVectorSize>&
  attribute_defs() const {
    return attribute_defs_;
  }

  paddle::small_vector<TensorArgDef, kInputSmallVectorSize>& input_defs() {
    return input_defs_;
  }

  paddle::small_vector<TensorArgDef, kOutputSmallVectorSize>& output_defs() {
    return output_defs_;
  }

  paddle::small_vector<AttributeArgDef, kAttrSmallVectorSize>&
  attribute_defs() {
    return attribute_defs_;
  }

 private:
  paddle::small_vector<TensorArgDef, kInputSmallVectorSize> input_defs_{{}};
  paddle::small_vector<TensorArgDef, kOutputSmallVectorSize> output_defs_{{}};
  paddle::small_vector<AttributeArgDef, kAttrSmallVectorSize> attribute_defs_{
      {}};
};

class Kernel {
 public:
  // for map element contruct
  Kernel() = default;

  explicit Kernel(KernelFn fn, void* variadic_fn)
      : fn_(fn), variadic_fn_(variadic_fn) {}

  void operator()(KernelContext* ctx) const { fn_(ctx); }

  template <typename Fn>
  Fn GetVariadicKernelFn() const {
    auto* func = reinterpret_cast<Fn>(variadic_fn_);
    return func;
  }

  KernelArgsDef* mutable_args_def() { return &args_def_; }

  const KernelArgsDef& args_def() const { return args_def_; }

  const TensorArgDef& InputAt(size_t idx) const {
    return args_def_.input_defs().at(idx);
  }

  TensorArgDef& InputAt(size_t idx) { return args_def_.input_defs().at(idx); }

  const TensorArgDef& OutputAt(size_t idx) const {
    return args_def_.output_defs().at(idx);
  }

  TensorArgDef& OutputAt(size_t idx) { return args_def_.output_defs().at(idx); }

  bool IsValid() const { return fn_ != nullptr; }

 private:
  KernelFn fn_{nullptr};
  void* variadic_fn_ = nullptr;
  KernelArgsDef args_def_;
};

using KernelKeyMap = paddle::flat_hash_map<KernelKey, Kernel, KernelKey::Hash>;

using KernelNameMap = paddle::flat_hash_map<std::string, KernelKeyMap>;

/**
 * Note: Each Computation need a basic kernel map that named by kernel_name.
 *       Such as for scale op, KernelMap contains a `scale` kernel map,
 *       if it still need other overload kernel, the op name can be
 *       `scale.***`.
 */
class KernelFactory {
 public:
  static KernelFactory& Instance();

  KernelNameMap& kernels() { return kernels_; }

  bool HasCompatiblePhiKernel(const std::string& op_type) const {
    return kernels_.find(TransToPhiKernelName(op_type)) != kernels_.end();
  }

  const Kernel& SelectKernelOrThrowError(const std::string& kernel_name,
                                         const KernelKey& kernel_key,
                                         bool use_gpudnn = false) const;

  const Kernel& SelectKernelOrThrowError(const std::string& kernel_name,
                                         Backend backend,
                                         DataLayout layout,
                                         DataType dtype) const;

  bool HasKernel(const std::string& kernel_name,
                 const KernelKey& kernel_key) const;

  const Kernel& SelectKernel(const std::string& kernel_name,
                             const KernelKey& kernel_key) const;

  KernelKeyMap SelectKernelMap(const std::string& kernel_name) const;

  const KernelArgsDef& GetFirstKernelArgsDef(
      const std::string& kernel_name) const;

 private:
  KernelFactory() = default;

  KernelNameMap kernels_;
};

inline std::ostream& operator<<(std::ostream& os, const KernelKey& kernel_key) {
  os << "(" << kernel_key.backend() << ", " << kernel_key.layout() << ", "
     << kernel_key.dtype() << ")";
  return os;
}

std::ostream& operator<<(std::ostream& os, AttributeType attr_type);

std::ostream& operator<<(std::ostream& os, const Kernel& kernel);

std::ostream& operator<<(std::ostream& os, KernelFactory& kernel_factory);

}  // namespace phi
