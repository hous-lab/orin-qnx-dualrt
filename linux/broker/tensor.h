/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _TENSOR_H_
#define _TENSOR_H_

#include <memory>
#include <fstream>
#include <iostream>
#include <vector>
#include <unordered_map>

#include <cuda_fp16.h>
#include <cuda_runtime_api.h>
#include <NvInfer.h>
#include <NvInferRuntime.h>

namespace nv {

inline unsigned int getElementSize(nvinfer1::DataType t) {
  switch (t) {
    case nvinfer1::DataType::kFLOAT: return 4;
    case nvinfer1::DataType::kHALF: return 2;
    case nvinfer1::DataType::kINT8: return 1;
    case nvinfer1::DataType::kINT32: return 4;
    case nvinfer1::DataType::kINT64: return 8;
    case nvinfer1::DataType::kBOOL: return 1;
    case nvinfer1::DataType::kUINT8: return 1;
    case nvinfer1::DataType::kFP8: return 1;
    default: throw std::runtime_error("Unknown data type");
  }
  return 0;
}

struct Tensor {
  std::string name;
  void* ptr;
  nvinfer1::Dims dim;
  int32_t volume = 1;
  nvinfer1::DataType dtype;
  // nvinfer1::TensorIOMode iomode;

  Tensor(std::string name, nvinfer1::Dims dim, nvinfer1::DataType dtype): 
    name(name), dim(dim), dtype(dtype) 
  {
    if( dim.nbDims == 0 ) {
      volume = 0;
    } else {
      volume = 1;
      for(int i=0; i<dim.nbDims; i++) {
          volume *= dim.d[i];
      }
    }
    cudaMalloc(&ptr, volume * getElementSize(dtype));
  }

  int32_t nbytes() {
    return volume * getElementSize(dtype);
  }

  void mov(std::shared_ptr<Tensor> other, cudaStream_t stream) {
    // copy from 'other'
    cudaMemcpyAsync(
      ptr, other->ptr, 
      nbytes(), 
      cudaMemcpyHostToDevice,
      stream);
  }

  void fill(float value) {
    const size_t element_size = getElementSize(dtype);
    const size_t total_bytes = volume * element_size;
    if (value == 0.0f) {
      if (ptr) { // ensure the pointer is valid
        cudaMemset(ptr, 0, total_bytes); // zero the device memory
      }
    } else {
      // non-zero values need special handling (example implementation)
      std::vector<char> host_buffer(total_bytes);
      const float pattern = value;
      for(int32_t i=0; i<volume; ++i) {
        memcpy(host_buffer.data() + i*element_size, &pattern, sizeof(pattern));
      }
      cudaMemcpy(ptr, host_buffer.data(), total_bytes, cudaMemcpyHostToDevice);
    }
  }

  std::shared_ptr<Tensor> clone(cudaStream_t stream = nullptr) const {
    auto new_tensor = std::make_shared<Tensor>(name, dim, dtype);
    const size_t bytes = volume * getElementSize(dtype);
    if(stream) {
      cudaMemcpyAsync(new_tensor->ptr, ptr, bytes, 
                    cudaMemcpyDeviceToDevice, stream);
    } else {
      cudaMemcpy(new_tensor->ptr, ptr, bytes, 
                cudaMemcpyDeviceToDevice);
    }
    return new_tensor;
  }

  // device-to-device copy
  void copyFromDevice(const std::shared_ptr<Tensor>& src, cudaStream_t stream = nullptr) {
    if (src->dtype != dtype || src->volume != volume) {
      throw std::runtime_error("Tensor copy dimension/type mismatch");
    }
    const size_t bytes = volume * getElementSize(dtype);
    if(stream) {
      cudaMemcpyAsync(ptr, src->ptr, bytes, cudaMemcpyDeviceToDevice, stream);
    } else {
      cudaMemcpy(ptr, src->ptr, bytes, cudaMemcpyDeviceToDevice);
    }
  }

  // copy from a raw device pointer
  void copyFromDevicePtr(const void* src_ptr, cudaStream_t stream = nullptr) {
    const size_t bytes = volume * getElementSize(dtype);
    if(stream) {
      cudaMemcpyAsync(ptr, src_ptr, bytes, cudaMemcpyDeviceToDevice, stream);
    } else {
      cudaMemcpy(ptr, src_ptr, bytes, cudaMemcpyDeviceToDevice);
    }
  }
  
  // copy from host memory to device memory
  void copyFromHost(const void* src_ptr, cudaStream_t stream = nullptr) {
    const size_t bytes = volume * getElementSize(dtype);
    if(stream) {
      cudaMemcpyAsync(ptr, src_ptr, bytes, cudaMemcpyHostToDevice, stream);
    } else {
      cudaMemcpy(ptr, src_ptr, bytes, cudaMemcpyHostToDevice);
    }
  }
  
  template<class Htype=float, class Dtype=float>
  void load(std::string fname) {
    size_t hsize = volume * sizeof(Htype);
    size_t dsize = volume * getElementSize(dtype);
    std::vector<char> b1(hsize);
    std::vector<char> b2(dsize);
    std::ifstream file_(fname, std::ios::binary);
    if( file_.fail() ) {
      std::cerr << fname << " missing!" << std::endl;
      return;
    }
    file_.read(b1.data(), hsize);
    Htype* hbuffer = reinterpret_cast<Htype*>(b1.data());
    Dtype* dbuffer = reinterpret_cast<Dtype*>(b2.data());
    // in some cases we want to load from different dtype
    for( int i=0; i<volume; i++ ) {
      dbuffer[i] = (Dtype)hbuffer[i];
    }

    cudaMemcpy(ptr, b2.data(), dsize, cudaMemcpyHostToDevice);
  }

  template<class Htype=float, class Dtype=float>
  void save(std::string fname) {
    size_t hsize = volume * sizeof(Htype);
    size_t dsize = volume * getElementSize(dtype);
    std::vector<char> b1(hsize);
    std::vector<char> b2(dsize);
    std::ofstream file_(fname, std::ios::binary);
    if( file_.fail() ) {
        std::cerr << fname << " can't open!" << std::endl;
        return;
    }
    // file_.read(b1.data(), hsize);
    Htype* hbuffer = reinterpret_cast<Htype*>(b1.data());
    Dtype* dbuffer = reinterpret_cast<Dtype*>(b2.data());
    cudaMemcpy(b2.data(), ptr, dsize, cudaMemcpyDeviceToHost);
    // in some cases we want to load from different dtype
    for( int i=0; i<volume; i++ ) {
        hbuffer[i] = (Htype)dbuffer[i];
    }
    file_.write(b2.data(), hsize);
    file_.close();
  }

  template<class T>
  std::vector<T> cpu() {
    std::vector<T> buffer(volume);
    cudaMemcpy(buffer.data(), ptr, volume * sizeof(T), cudaMemcpyDeviceToHost);
    return buffer;
  }
  
  // getter for the GPU pointer
  void* gpu() {
    return ptr;
  }

  std::vector<char> load_ref(std::string fname) {
    size_t bsize = volume * sizeof(float);
    std::vector<char> buffer(bsize);
    std::ifstream file_(fname, std::ios::binary);
    file_.read(buffer.data(), bsize);
    return buffer;
  }
}; // struct Tensor

using TensorMap = std::unordered_map<std::string, std::shared_ptr<Tensor>>;

} // namespace nv

std::ostream& operator<<(std::ostream& os, nv::Tensor& t);

#endif // _TENSOR_H_
