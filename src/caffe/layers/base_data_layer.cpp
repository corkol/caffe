#include <boost/thread.hpp>
#include <string>
#include <vector>

#include "caffe/data_layers.hpp"
#include "caffe/net.hpp"
#include "caffe/util/io.hpp"

namespace caffe {

template <typename Dtype>
BaseDataLayer<Dtype>::BaseDataLayer(const LayerParameter& param)
    : Layer<Dtype>(param),
      transform_param_(param.transform_param()) {
}

template <typename Dtype>
void BaseDataLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  if (top.size() == 1) {
    output_labels_ = false;
  } else {
    output_labels_ = true;
  }
  data_transformer_.reset(
      new DataTransformer<Dtype>(transform_param_, this->phase_));
  data_transformer_->InitRand();
  // The subclasses should setup the size of bottom and top
  DataLayerSetUp(bottom, top);
}

template <typename Dtype>
BasePrefetchingDataLayer<Dtype>::BasePrefetchingDataLayer(
    const LayerParameter& param)
    : BaseDataLayer<Dtype>(param),
      prefetch_free_(), prefetch_full_() {
  for (int i = 0; i < PREFETCH_COUNT; ++i) {
    prefetch_free_.push(&prefetch_[i]);
  }
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  BaseDataLayer<Dtype>::LayerSetUp(bottom, top);
  // Before starting the prefetch thread, we make cpu_data and gpu_data
  // calls so that the prefetch thread does not accidentally make simultaneous
  // cudaMalloc calls when the main thread is running. In some GPUs this
  // seems to cause failures if we do not so.
  for (int i = 0; i < PREFETCH_COUNT; ++i) {
    prefetch_[i].data_.mutable_cpu_data();
    if (this->output_labels_) {
      prefetch_[i].label_.mutable_cpu_data();
      prefetch_[i].label1_.mutable_cpu_data();
      prefetch_[i].label2_.mutable_cpu_data();
      prefetch_[i].label3_.mutable_cpu_data();
      prefetch_[i].label4_.mutable_cpu_data();
      prefetch_[i].label5_.mutable_cpu_data();
      prefetch_[i].label6_.mutable_cpu_data();
      prefetch_[i].imageindex_.mutable_cpu_data();
      prefetch_[i].windowindex_.mutable_cpu_data();
    }
  }
#ifndef CPU_ONLY
  if (Caffe::mode() == Caffe::GPU) {
    for (int i = 0; i < PREFETCH_COUNT; ++i) {
      prefetch_[i].data_.mutable_gpu_data();
      if (this->output_labels_) {
        prefetch_[i].label_.mutable_gpu_data();
        prefetch_[i].label1_.mutable_gpu_data();
      	prefetch_[i].label2_.mutable_gpu_data();
      	prefetch_[i].label3_.mutable_gpu_data();
      	prefetch_[i].label4_.mutable_gpu_data();
      	prefetch_[i].label5_.mutable_gpu_data();
      	prefetch_[i].label6_.mutable_gpu_data();
      	prefetch_[i].imageindex_.mutable_gpu_data();
      	prefetch_[i].windowindex_.mutable_gpu_data();
      }
    }
  }
#endif
  DLOG(INFO) << "Initializing prefetch";
  this->data_transformer_->InitRand();
  StartInternalThread();
  DLOG(INFO) << "Prefetch initialized.";
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::InternalThreadEntry() {
#ifndef CPU_ONLY
  cudaStream_t stream;
  if (Caffe::mode() == Caffe::GPU) {
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
  }
#endif

  try {
    while (!must_stop()) {
      Batch<Dtype>* batch = prefetch_free_.pop();
      load_batch(batch);
#ifndef CPU_ONLY
      if (Caffe::mode() == Caffe::GPU) {
        batch->data_.data().get()->async_gpu_push(stream);
        CUDA_CHECK(cudaStreamSynchronize(stream));
      }
#endif
      prefetch_full_.push(batch);
    }
  } catch (boost::thread_interrupted&) {
    // Interrupted exception is expected on shutdown
  }
#ifndef CPU_ONLY
  if (Caffe::mode() == Caffe::GPU) {
    CUDA_CHECK(cudaStreamDestroy(stream));
  }
#endif
}

template <typename Dtype>
void BasePrefetchingDataLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  Batch<Dtype>* batch = prefetch_full_.pop("Data layer prefetch queue empty");
  // Reshape to loaded data.
  top[0]->ReshapeLike(batch->data_);
  // Copy the data
  caffe_copy(batch->data_.count(), batch->data_.cpu_data(),
             top[0]->mutable_cpu_data());
  DLOG(INFO) << "Prefetch copied";
  if (this->output_labels_) {
    // Reshape to loaded labels.
    top[1]->ReshapeLike(batch->label_);
    top[2]->ReshapeLike(batch->label1_);
    top[3]->ReshapeLike(batch->label2_);
    top[4]->ReshapeLike(batch->label3_);
    top[5]->ReshapeLike(batch->label4_);
    top[6]->ReshapeLike(batch->label5_);
    top[7]->ReshapeLike(batch->label6_);
    top[8]->ReshapeLike(batch->imageindex_);
    top[9]->ReshapeLike(batch->windowindex_);
    // Copy the labels.
    caffe_copy(batch->label_.count(), batch->label_.cpu_data(),
        top[1]->mutable_cpu_data());
    caffe_copy(batch->label1_.count(), batch->label1_.cpu_data(),
        top[2]->mutable_cpu_data());
    caffe_copy(batch->label2_.count(), batch->label2_.cpu_data(),
        top[3]->mutable_cpu_data());
    caffe_copy(batch->label3_.count(), batch->label3_.cpu_data(),
        top[4]->mutable_cpu_data());
    caffe_copy(batch->label4_.count(), batch->label4_.cpu_data(),
        top[5]->mutable_cpu_data());
    caffe_copy(batch->label5_.count(), batch->label5_.cpu_data(),
        top[6]->mutable_cpu_data());
    caffe_copy(batch->label6_.count(), batch->label6_.cpu_data(),
        top[7]->mutable_cpu_data());
    caffe_copy(batch->imageindex_.count(), batch->imageindex_.cpu_data(),
           top[8]->mutable_cpu_data());
     caffe_copy(batch->windowindex_.count(), batch->windowindex_.cpu_data(),
            top[9]->mutable_cpu_data());
  }

  prefetch_free_.push(batch);
}

#ifdef CPU_ONLY
STUB_GPU_FORWARD(BasePrefetchingDataLayer, Forward);
#endif

INSTANTIATE_CLASS(BaseDataLayer);
INSTANTIATE_CLASS(BasePrefetchingDataLayer);

}  // namespace caffe
