/* Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/fluid/framework/ir/fusion_group/elementwise_group_detector.h"
#include <string>
#include <unordered_set>
#include <vector>
#include "paddle/fluid/framework/ir/fusion_group/operation.h"
#include "paddle/fluid/framework/ir/subgraph_detector.h"

namespace paddle {
namespace framework {
namespace ir {
namespace fusion_group {

static std::unordered_set<std::string> elementwise_op_types;

static std::unordered_set<std::string>& GetElementwiseOpTypes() {
  if (elementwise_op_types.empty()) {
    elementwise_op_types = OperationMap::Instance().Find(/* type= */ 0);
  }
  return elementwise_op_types;
}

static bool IsSpecifiedOp(const std::unordered_set<std::string>& op_types,
                          const Node* n) {
  if (n && n->IsOp() && n->Op() && n->outputs.size() > 0U) {
    auto iter = op_types.find(n->Op()->Type());
    if (iter != op_types.end()) {
      return true;
    }
  }
  return false;
}

static bool IsGradOp(const Node* n) {
  PADDLE_ENFORCE_EQ(n && n->IsOp() && n->Op(), true,
                    platform::errors::InvalidArgument(
                        "Expected node %p to be an operator node.", n));
  std::string suffix = "_grad";
  std::string op_type = n->Op()->Type();
  size_t pos = op_type.rfind(suffix);
  return pos != std::string::npos &&
         pos == (op_type.length() - suffix.length());
}

static bool IsEqualAndNotEmpty(const std::vector<int64_t>& l,
                               const std::vector<int64_t>& r) {
  return l.size() != 0U && r.size() != 0U && l == r;
}

bool GroupDetector::IsFusionGroupOp(const Node* n) {
  if (!(n && n->IsOp() && n->Op())) return false;
  bool is_first = true;
  proto::VarType::Type i_data_type = proto::VarType::FP32;
  proto::VarType::Type o_data_type = proto::VarType::FP32;

  for (auto* i_node : n->inputs) {
    if (!i_node->Var()) return false;
    if (i_node->Var()->GetType() != proto::VarType::LOD_TENSOR) {
      return false;
    }
    if (is_first) {
      i_data_type = i_node->Var()->GetDataType();
      is_first = false;
    } else {
      if (i_data_type != i_node->Var()->GetDataType()) return false;
    }
  }

  is_first = true;
  for (auto* o_node : n->outputs) {
    if (!o_node->Var()) return false;
    if (o_node->Var()->GetType() != proto::VarType::LOD_TENSOR) {
      return false;
    }
    if (is_first) {
      o_data_type = o_node->Var()->GetDataType();
      is_first = false;
    } else {
      if (o_data_type != o_node->Var()->GetDataType()) return false;
    }
  }

  if (!(i_data_type == proto::VarType::FP32 ||
        i_data_type == proto::VarType::FP64 ||
        i_data_type == proto::VarType::FP16) ||
      !(o_data_type == proto::VarType::FP32 ||
        o_data_type == proto::VarType::FP64 ||
        o_data_type == proto::VarType::FP16))
    return false;

  return true;
}

bool ElementwiseGroupDetector::IsElementwiseOp(const Node* n) {
  if (IsSpecifiedOp(GetElementwiseOpTypes(), n)) {
    std::vector<int64_t> shape_0;
    for (size_t i = 0; i < n->inputs.size(); ++i) {
      auto* in_i = n->inputs[i];
      if (!(in_i && in_i->IsVar() && in_i->Var())) {
        return false;
      }

      std::vector<int64_t> shape_i = in_i->Var()->GetShape();
      if (i == 0U) {
        shape_0 = shape_i;
      } else {
        if (!IsEqualAndNotEmpty(shape_0, shape_i)) {
          return false;
        }
      }
    }
    return true;
  }
  return false;
}

std::vector<std::vector<Node*>> ElementwiseGroupDetector::operator()(
    Graph* graph) {
  auto teller = [&](const Node* n) -> bool {
    return IsFusionGroupOp(n) && IsElementwiseOp(n);
  };

  return SubgraphDetector(graph, teller)();
}

}  // namespace fusion_group
}  // namespace ir
}  // namespace framework
}  // namespace paddle
