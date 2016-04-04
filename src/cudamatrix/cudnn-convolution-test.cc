// cudamatrix/cudnn-convolution-test.cc

// Copyright 2016  Johns Hopkins University (author: Daniel Povey)
//           2016  Yiming Wang

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include <iostream>

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "cudamatrix/cu-matrix-lib.h"

using namespace kaldi;

#if HAVE_CUDA == 1 && HAVE_CUDNN == 1
#include "cudamatrix/cudnn-convolution.h"


namespace kaldi {

template<typename Real>
static void UnitTestCudnnConvolutionForward() {
  CuDnnConvolution<Real> cudnnConv;
  // input tensor
  size_t nbDimsX = 5;
  MatrixIndexT n_x = 50, c_x = 10, d_x = 50, h_x = 50, w_x = 50;
  MatrixIndexT dimX[] = {n_x, c_x, d_x, h_x, w_x};
  MatrixIndexT sizeX = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeX *= dimX[i];
  } 
  MatrixIndexT strideX[] = {c_x * d_x * h_x * w_x, d_x * h_x * w_x, h_x * w_x, w_x, 1};
  cudnnTensorDescriptor_t xDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimX, strideX, &xDesc);

  // filter
  size_t nbDimsW = 5;
  MatrixIndexT filterDimA[] = {1, 10, 10, 10, 10};
  MatrixIndexT sizeW = 1;
  for (int32 i = 0; i < nbDimsW; i++) {
    sizeW *= filterDimA[i];
  }
  cudnnFilterDescriptor_t wDesc;
  cudnnConv.InitializeFilterDescriptor(nbDimsW, filterDimA, &wDesc);

  // convolution
  size_t arrayLength = 3;
  MatrixIndexT padA[] = {5, 5, 5};
  MatrixIndexT filterStrideA[] = {1, 1, 1};
  cudnnConvolutionDescriptor_t convDesc;
  cudnnConv.InitializeConvolutionDescriptor(arrayLength, padA, filterStrideA,
      CUDNN_CONVOLUTION, &convDesc);

  // output tensor
  MatrixIndexT dimY[nbDimsX];
  cudnnConv.GetConvolutionNdForwardOutputDim(convDesc, xDesc, wDesc, nbDimsX,
                                             dimY);
  MatrixIndexT sizeY = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeY *= dimY[i];
  }
  MatrixIndexT strideY[nbDimsX];
  strideY[nbDimsX - 1] = 1;
  for (int32 i = nbDimsX - 2; i >= 0; i--)
    strideY[i] = dimY[i + 1] * strideY[i + 1];
  cudnnTensorDescriptor_t yDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimY, strideY, &yDesc);

  // find best forward algorithm
  const int requestedAlgoCount = 5;
  cudnnConvolutionFwdAlgo_t algo;
  cudnnConv.FindBestConvolutionFwdAlgo(xDesc, wDesc, convDesc, yDesc, requestedAlgoCount, &algo);

  // work space size needed according to the selected forwarding algorithm
  size_t workSpaceSizeInBytes;
  cudnnConv.GetConvolutionFwdWorkspaceSize(xDesc, wDesc, convDesc, yDesc, algo, &workSpaceSizeInBytes);
  CuMatrix<Real> workSpace;
  if (workSpaceSizeInBytes != 0)
    workSpace.Resize(1, workSpaceSizeInBytes / sizeof(Real), kUndefined, kStrideEqualNumCols);

  // GPU memory allocations
  CuMatrix<Real> x(n_x, sizeX / n_x, kUndefined, kStrideEqualNumCols);
  x.SetRandn();
  CuMatrix<Real> w(1, sizeW, kUndefined, kStrideEqualNumCols);
  w.SetRandn();
  CuMatrix<Real> y(n_x, sizeY / n_x, kUndefined, kStrideEqualNumCols);

  // forward
  cudnnConv.ConvolutionForward(xDesc, x, wDesc, w, convDesc, algo, &workSpace, workSpaceSizeInBytes, yDesc, &y);

  // destroy
  cudnnConv.DestroyTensorDescriptor(xDesc);
  cudnnConv.DestroyTensorDescriptor(yDesc);
  cudnnConv.DestroyFilterDescriptor(wDesc);
  cudnnConv.DestroyConvolutionDescriptor(convDesc);
}

template<typename Real>
static void UnitTestCudnnConvolutionBackwardData() {
  CuDnnConvolution<Real> cudnnConv;
  // gradient w.r.t. input tensor
  size_t nbDimsX = 5;
  MatrixIndexT n_x = 50, c_x = 10, d_x = 50, h_x = 50, w_x = 50;
  MatrixIndexT dimX[] = {n_x, c_x, d_x, h_x, w_x};
  MatrixIndexT sizeX = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeX *= dimX[i];
  } 
  MatrixIndexT strideX[] = {c_x * d_x * h_x * w_x, d_x * h_x * w_x, h_x * w_x, w_x, 1};
  cudnnTensorDescriptor_t dxDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimX, strideX, &dxDesc);

  // filter
  size_t nbDimsW = 5;
  MatrixIndexT filterDimA[] = {1, 10, 10, 10, 10};
  MatrixIndexT sizeW = 1;
  for (int32 i = 0; i < nbDimsW; i++) {
    sizeW *= filterDimA[i];
  }
  cudnnFilterDescriptor_t wDesc;
  cudnnConv.InitializeFilterDescriptor(nbDimsW, filterDimA, &wDesc);

  // convolution
  size_t arrayLength = 3;
  MatrixIndexT padA[] = {5, 5, 5};
  MatrixIndexT filterStrideA[] = {1, 1, 1};
  cudnnConvolutionDescriptor_t convDesc;
  cudnnConv.InitializeConvolutionDescriptor(arrayLength, padA, filterStrideA,
      CUDNN_CONVOLUTION, &convDesc);

  // gradient w.r.t. output tensor
  MatrixIndexT dimY[nbDimsX];
  cudnnConv.GetConvolutionNdForwardOutputDim(convDesc, dxDesc, wDesc, nbDimsX,
                                             dimY);
  MatrixIndexT sizeY = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeY *= dimY[i];
  }
  MatrixIndexT strideY[nbDimsX];
  strideY[nbDimsX - 1] = 1;
  for (int32 i = nbDimsX - 2; i >= 0; i--)
    strideY[i] = dimY[i + 1] * strideY[i + 1];
  cudnnTensorDescriptor_t dyDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimY, strideY, &dyDesc);

  // find best backward algorithm
  const int requestedAlgoCount = 5;
  cudnnConvolutionBwdDataAlgo_t algo;
  cudnnConv.FindBestConvolutionBwdDataAlgo(wDesc, dyDesc, convDesc, dxDesc, requestedAlgoCount, &algo);

  // work space size needed according to the selected forwarding algorithm
  size_t workSpaceSizeInBytes;
  cudnnConv.GetConvolutionBwdDataWorkspaceSize(wDesc, dyDesc, convDesc, dxDesc, algo, &workSpaceSizeInBytes);
  CuMatrix<Real> workSpace;
  if (workSpaceSizeInBytes != 0)
    workSpace.Resize(1, workSpaceSizeInBytes / sizeof(Real), kUndefined, kStrideEqualNumCols);

  // GPU memory allocations
  CuMatrix<Real> dx(n_x, sizeX / n_x, kUndefined, kStrideEqualNumCols);
  CuMatrix<Real> w(1, sizeW, kUndefined, kStrideEqualNumCols);
  w.SetRandn();
  CuMatrix<Real> dy(n_x, sizeY / n_x, kUndefined, kStrideEqualNumCols);
  dy.SetRandn();

  // backward 
  cudnnConv.ConvolutionBackwardData(wDesc, w, dyDesc, dy, convDesc, algo, &workSpace, workSpaceSizeInBytes, dxDesc, &dx);

  // destroy
  cudnnConv.DestroyTensorDescriptor(dxDesc);
  cudnnConv.DestroyTensorDescriptor(dyDesc);
  cudnnConv.DestroyFilterDescriptor(wDesc);
  cudnnConv.DestroyConvolutionDescriptor(convDesc);
}

template<typename Real>
static void UnitTestCudnnConvolutionBackwardFilter() {
  CuDnnConvolution<Real> cudnnConv;
  // input tensor
  size_t nbDimsX = 5;
  MatrixIndexT n_x = 50, c_x = 10, d_x = 50, h_x = 50, w_x = 50;
  MatrixIndexT dimX[] = {n_x, c_x, d_x, h_x, w_x};
  MatrixIndexT sizeX = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeX *= dimX[i];
  } 
  MatrixIndexT strideX[] = {c_x * d_x * h_x * w_x, d_x * h_x * w_x, h_x * w_x, w_x, 1};
  cudnnTensorDescriptor_t xDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimX, strideX, &xDesc);

  // gradient w.r.t. filter
  size_t nbDimsW = 5;
  MatrixIndexT filterDimA[] = {1, 10, 10, 10, 10};
  MatrixIndexT sizeW = 1;
  for (int32 i = 0; i < nbDimsW; i++) {
    sizeW *= filterDimA[i];
  }
  cudnnFilterDescriptor_t dwDesc;
  cudnnConv.InitializeFilterDescriptor(nbDimsW, filterDimA, &dwDesc);

  // convolution
  size_t arrayLength = 3;
  MatrixIndexT padA[] = {5, 5, 5};
  MatrixIndexT filterStrideA[] = {1, 1, 1};
  cudnnConvolutionDescriptor_t convDesc;
  cudnnConv.InitializeConvolutionDescriptor(arrayLength, padA, filterStrideA,
      CUDNN_CONVOLUTION, &convDesc);

  // gradient w.r.t. output tensor
  MatrixIndexT dimY[nbDimsX];
  cudnnConv.GetConvolutionNdForwardOutputDim(convDesc, xDesc, dwDesc, nbDimsX,
                                             dimY);
  MatrixIndexT sizeY = 1;
  for (int32 i = 0; i < nbDimsX; i++) {
    sizeY *= dimY[i];
  }
  MatrixIndexT strideY[nbDimsX];
  strideY[nbDimsX - 1] = 1;
  for (int32 i = nbDimsX - 2; i >= 0; i--)
    strideY[i] = dimY[i + 1] * strideY[i + 1];
  cudnnTensorDescriptor_t dyDesc;
  cudnnConv.InitializeTensorDescriptor(nbDimsX, dimY, strideY, &dyDesc);

  // find best backward algorithm
  const int requestedAlgoCount = 5;
  cudnnConvolutionBwdFilterAlgo_t algo;
  cudnnConv.FindBestConvolutionBwdFilterAlgo(xDesc, dyDesc, convDesc, dwDesc, requestedAlgoCount, &algo);

  // work space size needed according to the selected forwarding algorithm
  size_t workSpaceSizeInBytes;
  cudnnConv.GetConvolutionBwdFilterWorkspaceSize(xDesc, dyDesc, convDesc, dwDesc, algo, &workSpaceSizeInBytes);
  CuMatrix<Real> workSpace;
  if (workSpaceSizeInBytes != 0)
    workSpace.Resize(1, workSpaceSizeInBytes / sizeof(Real), kUndefined, kStrideEqualNumCols);

  // GPU memory allocations
  CuMatrix<Real> x(n_x, sizeX / n_x, kUndefined, kStrideEqualNumCols);
  x.SetRandn();
  CuMatrix<Real> dw(1, sizeW, kUndefined, kStrideEqualNumCols);;
  CuMatrix<Real> dy(n_x, sizeY / n_x, kUndefined, kStrideEqualNumCols);
  dy.SetRandn();

  // backward 
  cudnnConv.ConvolutionBackwardFilter(xDesc, x, dyDesc, dy, convDesc, algo, &workSpace, workSpaceSizeInBytes, dwDesc, &dw);

  // destroy
  cudnnConv.DestroyTensorDescriptor(xDesc);
  cudnnConv.DestroyTensorDescriptor(dyDesc);
  cudnnConv.DestroyFilterDescriptor(dwDesc);
  cudnnConv.DestroyConvolutionDescriptor(convDesc);
}


template<typename Real> void CudnnConvolutionUnitTest() {
  UnitTestCudnnConvolutionForward<Real>();
  UnitTestCudnnConvolutionBackwardData<Real>();
  UnitTestCudnnConvolutionBackwardFilter<Real>();
}

} // namespace kaldi 
#endif // HAVE_CUDA == 1 && HAVE_CUDNN

int main() {
#if HAVE_CUDA == 1 && HAVE_CUDNN == 1
  CuDevice::Instantiate().SelectGpuId("yes");
  kaldi::CudnnConvolutionUnitTest<float>();
  if (CuDevice::Instantiate().DoublePrecisionSupported()) {
    kaldi::CudnnConvolutionUnitTest<double>();
  } else {
    KALDI_WARN << "Double precision not supported";
  }
#else
    KALDI_WARN << "Test only when GPU is available.";
#endif
  KALDI_LOG << "Tests with GPU use succeeded.";
  SetVerboseLevel(4);
#if HAVE_CUDA == 1 && HAVE_CUDNN == 1
  CuDevice::Instantiate().PrintProfile();
#endif
  return 0;
}

