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

#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>

#include <cuda_fp16.h>
#include <cuda_runtime_api.h>
#include <NvInfer.h>

#include <tensorrt_sparsedrive/tensor.h>

namespace nv {
using namespace nvinfer1;

struct Net {
  ICudaEngine* engine;
  IExecutionContext* context;
  nv::TensorMap bindings;
  bool use_cuda_graph = false;
  cudaGraph_t graph;
  cudaGraphExec_t graph_exec;

  Net(
    std::string engine_path, 
    IRuntime* runtime, 
    nv::TensorMap& ext
  ) {
    std::ifstream engine_file(engine_path, std::ios::binary);
    if (!engine_file) {
      throw std::runtime_error("Error opening engine file: " + engine_path);
    }
    engine_file.seekg(0, engine_file.end);
    long int fsize = engine_file.tellg();
    engine_file.seekg(0, engine_file.beg);

    // Read the engine file into a buffer
    std::vector<char> engineData(fsize);

    engine_file.read(engineData.data(), fsize);
    engine = runtime->deserializeCudaEngine(engineData.data(), fsize);
    if (!engine) {
      throw std::runtime_error("deserializeCudaEngine returned null for: " + engine_path);
    }
    context = engine->createExecutionContext(); 
    if (!context) {
      throw std::runtime_error("createExecutionContext returned null for: " + engine_path);
    }

    int nb = engine->getNbIOTensors();  

    for( int n=0; n<nb; n++ ) {
      std::string name = engine->getIOTensorName(n);
      Dims d = engine->getTensorShape(name.c_str());      
      DataType dtype = engine->getTensorDataType(name.c_str());
      // TRT 10: set input shape for all input tensors (fixed or dynamic)
      if( engine->getTensorIOMode(name.c_str()) == TensorIOMode::kINPUT ) {
        try {
          context->setInputShape(name.c_str(), d);
        } catch(...) {}
      }
      if( ext.find(name) != ext.end() ) {
        // use external memory
        context->setTensorAddress(name.c_str(), ext[name]->ptr);
        bindings[name] = ext[name];
      } else {
        bindings[name] = std::make_shared<Tensor>(name, d, dtype);
        // bindings[name]->iomode = engine->getTensorIOMode(name.c_str());
        /* P7: cout of tensor dropped (operator<< lives in P1 tensor.cpp) */
        context->setTensorAddress(name.c_str(), bindings[name]->ptr);
      }      
    }
  }

  void Enqueue(cudaStream_t stream) {
    if( this->use_cuda_graph ) {
      cudaGraphLaunch(graph_exec, stream);
    } else {
      context->enqueueV3(stream);
    }  
  }

  ~Net() {
  }

  void EnableCudaGraph(cudaStream_t stream) {    
    // run first time to avoid allocation
    this->Enqueue(stream);
    cudaStreamSynchronize(stream);

    cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
    this->Enqueue(stream);
    cudaStreamEndCapture(stream, &graph);
    this->use_cuda_graph = true;
#if CUDART_VERSION >= 12000
    cudaGraphInstantiate(&graph_exec, graph, 0); // cuda-12.x (x86 && orin)
#else
    cudaGraphInstantiate(&graph_exec, graph, NULL, NULL, 0); // cuda-11.x
#endif
  }
}; // class Net

// 添加 inline 关键字来避免多重定义
inline void validate(Net& net, std::string frame_dir, std::string key) {
  auto ref = net.bindings[key]->load_ref(frame_dir + key + ".bin");
  float* f_ref = reinterpret_cast<float*>(ref.data());
  auto fresult = net.bindings[key]->cpu<float>();
  std::cout << key << std::endl;
  for(int n=0; n<16; n++ ) {
      printf("%6.4f ", fresult[n]);
  }
  printf("\n");
  for(int n=0; n<16; n++ ) {
      printf("%6.4f ", f_ref[n]);
  }
  printf("\n");
}

} // namespace nv
