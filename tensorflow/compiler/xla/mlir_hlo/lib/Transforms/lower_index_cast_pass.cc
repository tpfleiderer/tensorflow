/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// This file contains the patterns to convert arith.index_cast on tensors to
// tensor ops and index_cast on scalars.

#include <utility>

#include "mlir-hlo/Transforms/passes.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {

#define GEN_PASS_DEF_LOWERINDEXCASTPASS
#include "mlir-hlo/Transforms/passes.h.inc"

namespace {

// index_cast is not defined on tensors, so lower it to a tensor.generate.
struct IndexCastConverter : public OpRewritePattern<arith::IndexCastOp> {
 public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(arith::IndexCastOp op,
                                PatternRewriter &rewriter) const final {
    auto resultTy = op.getType().dyn_cast<RankedTensorType>();
    if (!resultTy) return failure();

    SmallVector<Value> dynamicExtents;
    for (auto dim : llvm::seq<int64_t>(0, resultTy.getRank())) {
      if (resultTy.isDynamicDim(dim)) {
        dynamicExtents.push_back(
            rewriter.create<tensor::DimOp>(op.getLoc(), op.getIn(), dim));
      }
    }

    rewriter.replaceOpWithNewOp<tensor::GenerateOp>(
        op, op.getType(), dynamicExtents,
        [&](OpBuilder &b, Location loc, ValueRange args) {
          Value extent = b.create<tensor::ExtractOp>(loc, op.getIn(), args);
          Value casted = b.create<arith::IndexCastOp>(
              loc, resultTy.getElementType(), extent);
          b.create<tensor::YieldOp>(loc, casted);
        });
    return success();
  }
};

struct LowerIndexCastPass
    : public impl::LowerIndexCastPassBase<LowerIndexCastPass> {
  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    patterns.add<IndexCastConverter>(patterns.getContext());
    if (failed(
            applyPatternsAndFoldGreedily(getOperation(), std::move(patterns))))
      return signalPassFailure();
  }
};

}  // namespace

std::unique_ptr<OperationPass<func::FuncOp>> createLowerIndexCastPass() {
  return std::make_unique<LowerIndexCastPass>();
}

}  // namespace mlir
