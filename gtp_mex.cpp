#include "mex.h"
#include <iostream>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <mutex> // for cout lock
#include <math.h>
#include <algorithm> // for std::binary_search
#include <stdexcept>

int num_threads;

size_t* tft_indices_cardinalities;
size_t* tft_indices_ids;
size_t tft_indices_length;
mxArray* tft_indices_mx;

double* output_data;
mxArray* output_data_mx;
// mwIndex* output_irs;
// size_t output_irs_index;
// mwIndex* output_jcs;
size_t output_data_maximum_numel;
size_t output_data_numel_nzmax;
size_t* output_index_cardinalities;
size_t* output_indices_full_cardinality;
size_t* output_indices_full_strides;
size_t output_indices_length;
mxArray* output_indices_mx;

double** input_data;
mwIndex** input_irs;
mwIndex** input_jcs;
size_t* input_data_numel;
size_t** input_indices_full_cardinality;
size_t** input_indices_full_strides;
double** input_cache;
std::vector<bool>** input_cache_bitmap; // vector<bool> stores in bits
std::mutex* input_cache_lock;

size_t* contraction_index_inds; //indexes tft_indices
size_t contraction_index_inds_length;

// TODO comment this mutex code, no need to depend on c++-11 for proper printing
std::mutex print_lock;

bool is_sparse;
bool* is_sparse_input;
size_t input_length;

const size_t INITIAL_SPARSE_NZMAX = 100000; // TODO: increase this number after testing
const float GROW_FACTOR = 1.5;

//size_t compute_output_tensor_part_helper_call_count = 0;

// static void clear_mem(void){
//   mxFree( output_irs );
//   mxFree( output_data );
// }

// http://stackoverflow.com/a/446327/1056345
template<class Iter, class T>
Iter binary_find(Iter begin, Iter end, T val)
{
  Iter i = std::lower_bound(begin, end, val);

  if (i != end && !(val < *i))
    return i;

  else
    return end;
}

double get_tensor_data_by_full_index_configuration_dense(double* tensor_data, size_t* index_configuration, size_t* tensor_indices_full_strides, size_t tensor_data_numel){
  size_t tensor_numel_index = 0;
  for (int tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++){
    tensor_numel_index += index_configuration[tft_indices_ind] * tensor_indices_full_strides[tft_indices_ind];
  }

  if ( tensor_numel_index < 0 || tensor_numel_index >= tensor_data_numel ){
    std::cout << "ERROR: get_tensor_data_by_index_configuration_dense tensor_numel_index " << tensor_numel_index << " can not be smaller than zero or greater than tensor_data_numel " << tensor_data_numel << std::endl;
    return 0;
  }else{
    return tensor_data[tensor_numel_index];
  }
}

double get_tensor_data_by_full_index_configuration_sparse(double* tensor_data, size_t* index_configuration, size_t* tensor_indices_full_strides, size_t tensor_data_numel, mwIndex* target_irs, mwIndex* target_jcs, double* cache, std::vector<bool>* cache_bitmap, std::mutex* cache_lock){
  //print_lock.lock(); std::cout << "osman300" << std::endl; print_lock.unlock();
  // search for given index, return stored value if given index is found, otherwise return zero

  //std::cout << "miss " << output_numel_index << std::endl;
  size_t tensor_numel_index = 0;
  for (int tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++){
    //print_lock.lock(); std::cout << "osman301" << std::endl; print_lock.unlock();
    tensor_numel_index += index_configuration[tft_indices_ind] * tensor_indices_full_strides[tft_indices_ind];
    //std::cout << "get_tensor_data_by_full_index_configuration_sparse tensor_numel_index " << tensor_numel_index << " " << index_configuration[tft_indices_ind] << " * " << tensor_indices_full_strides[tft_indices_ind] << std::endl;
  }
  //print_lock.lock(); std::cout << "osman302" << std::endl; print_lock.unlock();
  if ( tensor_numel_index < 0 || tensor_numel_index >= tensor_data_numel ){
    std::cout << "ERROR: get_tensor_data_by_index_configuration_sparse tensor_numel_index " << tensor_numel_index << " can not be smaller than zero or greater than tensor_data_numel " << tensor_data_numel << std::endl;
    return 0;
  }

  //print_lock.lock(); std::cout << "osman303" << std::endl; print_lock.unlock();
  // query cache first
  if ( cache_bitmap->at(tensor_numel_index) == true ){
    //print_lock.lock(); std::cout << "osman304" << std::endl; print_lock.unlock();
    return cache[tensor_numel_index];
  }else{
    //print_lock.lock(); std::cout << "osman305" << std::endl; print_lock.unlock();
    mwIndex* result = binary_find(target_irs, target_irs+target_jcs[1], tensor_numel_index);
    //print_lock.lock(); std::cout << "osman306" << std::endl; print_lock.unlock();
    if ( result == target_irs+target_jcs[1] ){
      //print_lock.lock(); std::cout << "osman307" << std::endl; print_lock.unlock();
      //std::cout << "store1 " << output_numel_index << " 0" << std::endl;
      cache_lock->lock();
      //print_lock.lock(); std::cout << "osman307.1" << std::endl; print_lock.unlock();
      cache[tensor_numel_index] = 0;
      //print_lock.lock(); std::cout << "osman308" << std::endl; print_lock.unlock();
      cache_bitmap->at(tensor_numel_index) = true;
      //print_lock.lock(); std::cout << "osman309" << std::endl; print_lock.unlock();
      cache_lock->unlock();
      //std::cout << "store1 complete" << std::endl;
      return 0;
    }else{
      //std::cout << "store2 " << output_numel_index << " " << tensor_data[result - target_irs] << std::endl;
      //print_lock.lock(); std::cout << "osman310" << std::endl; print_lock.unlock();
      cache_lock->lock();
      //print_lock.lock(); //std::cout << "osman311" << std::endl; print_lock.unlock();
      cache[tensor_numel_index] = tensor_data[result - target_irs];
      //print_lock.lock(); //std::cout << "osman312" << std::endl; print_lock.unlock();
      cache_bitmap->at(tensor_numel_index) = true;
      cache_lock->unlock();
      //std::cout << "store2 complete" << std::endl;
      //print_lock.lock(); //std::cout << "osman313" << std::endl; print_lock.unlock();
      return tensor_data[ result - target_irs ];
    }
  }
}

void compute_output_tensor_part_helper(size_t* output_full_index_configuration, size_t output_numel_index, size_t increment_index_ind=0){
  //print_lock.lock(); //std::cout << "osman200" << std::endl; print_lock.unlock();
  for ( size_t contraction_index_value=0;
	contraction_index_value<tft_indices_cardinalities[contraction_index_inds[increment_index_ind]];
	contraction_index_value++ ){
    //print_lock.lock(); //std::cout << "osman201" << std::endl; print_lock.unlock();

    output_full_index_configuration[ contraction_index_inds[increment_index_ind] ] = contraction_index_value;
    //print_lock.lock(); //std::cout << "osman203" << std::endl; print_lock.unlock();
    if ( increment_index_ind == (contraction_index_inds_length-1) ){
      if ( is_sparse == true ){
	//print_lock.lock(); //std::cout << "osman204" << std::endl; print_lock.unlock();
	// TODO: input tensors may or may not be sparse!
	// output_data[output_irs_index] += ( get_tensor_data_by_full_index_configuration_sparse(input0_data, output_full_index_configuration, input0_indices_full_strides, input0_data_numel, input0_irs, input0_jcs, input0_cache, input0_cache_bitmap, &input0_cache_lock) *
	// 				   get_tensor_data_by_full_index_configuration_sparse(input1_data, output_full_index_configuration, input1_indices_full_strides, input1_data_numel, input1_irs, input1_jcs, input1_cache, input1_cache_bitmap, &input1_cache_lock) );
	double product = get_tensor_data_by_full_index_configuration_sparse(input_data[0], output_full_index_configuration, input_indices_full_strides[0], input_data_numel[0], input_irs[0], input_jcs[0], input_cache[0], input_cache_bitmap[0], &(input_cache_lock[0]));
	//print_lock.lock(); //std::cout << "osman204.1" << std::endl; print_lock.unlock();
	for ( size_t input_ind=1; input_ind<input_length; input_ind++ ){
	  //print_lock.lock(); //std::cout << "osman204.2" << std::endl; print_lock.unlock();
	  product *= get_tensor_data_by_full_index_configuration_sparse(input_data[input_ind], output_full_index_configuration, input_indices_full_strides[input_ind], input_data_numel[input_ind], input_irs[input_ind], input_jcs[input_ind], input_cache[input_ind], input_cache_bitmap[input_ind], &(input_cache_lock[input_ind]));
	}
	//print_lock.lock(); //std::cout << "osman204.3" << std::endl; print_lock.unlock();
	output_data[output_numel_index] += product;
	//print_lock.lock(); //std::cout << "osman205" << std::endl; print_lock.unlock();
      }else{
	//print_lock.lock(); //std::cout << "osman206" << std::endl; print_lock.unlock();
	double product = get_tensor_data_by_full_index_configuration_dense(input_data[0], output_full_index_configuration, input_indices_full_strides[0], input_data_numel[0]);
	for ( size_t input_ind=1; input_ind<input_length; input_ind++ ){
	  product *= get_tensor_data_by_full_index_configuration_dense(input_data[input_ind], output_full_index_configuration, input_indices_full_strides[input_ind], input_data_numel[input_ind]);
	}
	output_data[output_numel_index] += product;
	//print_lock.lock(); //std::cout << "osman207" << std::endl; print_lock.unlock();
      }
      // print_lock.lock();
      // std::cout << output_numel_index << " ";
      // print_lock.unlock();
      
    }else{
      //print_lock.lock(); //std::cout << "osman208" << std::endl; print_lock.unlock();
      return compute_output_tensor_part_helper( output_full_index_configuration, output_numel_index, increment_index_ind+1 );
    }
  }
  //print_lock.lock(); //std::cout << "osman209" << std::endl; print_lock.unlock();
}
void print_all_values(double* data, bool is_tensor_sparse, size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides, std::vector<size_t> index_configuration=std::vector<size_t>(), size_t iter_index=0, mwIndex* target_irs=NULL, mwIndex* target_jcs=NULL, double* target_cache=NULL, std::vector<bool>* target_cache_bitmap=NULL, std::mutex* target_cache_lock=NULL){
  if (is_tensor_sparse && target_irs==NULL){
    mexErrMsgTxt( "print_all_values target_irs can not be NULL if data is sparse" );
  }

  //std::cout << "print_all_values iter_index " << iter_index << std::endl;
  if( index_configuration.size() == 0 ){
    for (size_t i_ind=0; i_ind<tft_indices_length; i_ind++){
      index_configuration.push_back(0);
    }
  }

  for( size_t iter_value=0; iter_value<(*target_indices_full_cardinality)[iter_index]; iter_value++){
    //std::cout << "print_all_values iter_value " << iter_value << std::endl;
    index_configuration.at(iter_index) = iter_value;

    if (iter_index != (tft_indices_length-1)){
      print_all_values(data, is_tensor_sparse, target_indices_full_cardinality, target_data_numel, target_indices_full_strides, index_configuration, iter_index+1, target_irs, target_jcs, target_cache, target_cache_bitmap, target_cache_lock);
    }
  }

  if ( (*target_indices_full_cardinality)[iter_index] == 0 && iter_index != (tft_indices_length-1) ){
    print_all_values(data, is_tensor_sparse, target_indices_full_cardinality, target_data_numel, target_indices_full_strides, index_configuration, iter_index+1, target_irs, target_jcs, target_cache, target_cache_bitmap, target_cache_lock);
  }

  if (iter_index == (tft_indices_length-1)){
    for( size_t i=0; i<tft_indices_length; i++){
      std::cout << " " << index_configuration[i];
    }
    if (is_tensor_sparse){
      std::cout << ": " << get_tensor_data_by_full_index_configuration_sparse(data, &index_configuration[0], *target_indices_full_strides, *target_data_numel,
									      target_irs, target_jcs, target_cache, target_cache_bitmap, target_cache_lock) << std::endl;
    }else{
      std::cout << ": " << get_tensor_data_by_full_index_configuration_dense(data, &index_configuration[0], *target_indices_full_strides, *target_data_numel) << std::endl;
    }
  }
}

std::pair <size_t,size_t> get_thr_output_data_start_end(int tid){
  size_t step_size = output_data_maximum_numel / num_threads;
  size_t thr_output_data_index_start = tid * step_size;
  size_t thr_output_data_index_end;
  if ( tid < (num_threads-1)){
    thr_output_data_index_end = (tid+1) * step_size;
  }else{
    thr_output_data_index_end = output_data_maximum_numel;
  }
  return std::make_pair(thr_output_data_index_start, thr_output_data_index_end);
}

void* compute_output_tensor_part(void *args){
  //print_lock.lock(); //std::cout << "osman100" << std::endl; print_lock.unlock();

  int tid = (intptr_t) args;

  std::pair <size_t,size_t> start_end = get_thr_output_data_start_end(tid);

  size_t* output_full_index_configuration = (size_t*) calloc( tft_indices_length, sizeof(size_t) );
  int loop_count = 0;
  //std::cout << "osman101" << std::endl;
  for ( size_t output_numel_ind=start_end.first; output_numel_ind<start_end.second; output_numel_ind++ ){
    //print_lock.lock(); //std::cout << "osman101" << std::endl; print_lock.unlock();

    // calculate output_full_index_configuration for output_numel_ind
    // for s_1 = 2, s_2 = 3, s_3 = 4
    // x = 0:23
    // [x ; mod(floor(x/12),2); mod(floor(x / 4), 3); mod(floor(x/1),4) ]'  <- data order incremented from rightmost tft_index
    // [x ; mod(floor(x/1),2) ; mod(floor(x / 2), 3);  mod(floor(x/6),4)]'  <- data order incremented from leftmost tft_index (MATLAB)
    //size_t right_hand_inds_step_divider = 1;
    size_t left_hand_inds_step_divider = 1;
    size_t output_numel_index = 0;
    for( size_t tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++ ){
      if ( output_indices_full_strides[tft_indices_ind] > 0 ){
	output_full_index_configuration[tft_indices_ind] = ((size_t)floor(output_numel_ind / left_hand_inds_step_divider)) % tft_indices_cardinalities[tft_indices_ind];
	//right_hand_inds_step_divider *= tft_indices_cardinalities[tft_indices_ind];
	left_hand_inds_step_divider *= tft_indices_cardinalities[tft_indices_ind];
	output_numel_index += output_full_index_configuration[tft_indices_ind] * output_indices_full_strides[tft_indices_ind];
      }
    }
    //print_lock.lock(); //std::cout << "osman102" << std::endl; print_lock.unlock();

    if ( contraction_index_inds_length == 0 ){
      // no contraction, just multiply and store result
      // TODO: TEST, not tested with matrix product test
      if ( is_sparse == true ){
	//output_irs[output_numel_index] = output_numel_index;
	// TODO: input tensors may or may not be sparse!
	double product = get_tensor_data_by_full_index_configuration_sparse(input_data[0], output_full_index_configuration, input_indices_full_strides[0], input_data_numel[0], input_irs[0], input_jcs[0], input_cache[0], input_cache_bitmap[0], &(input_cache_lock[0]));
	for ( size_t input_ind=1; input_ind<input_length; input_ind++ ){
	  product *= get_tensor_data_by_full_index_configuration_sparse(input_data[input_ind], output_full_index_configuration, input_indices_full_strides[input_ind], input_data_numel[input_ind], input_irs[input_ind], input_jcs[input_ind], input_cache[input_ind], input_cache_bitmap[input_ind], &(input_cache_lock[input_ind]));
	}
	output_data[output_numel_index] = product;

      }else{
	double product = get_tensor_data_by_full_index_configuration_dense(input_data[0], output_full_index_configuration, input_indices_full_strides[0], input_data_numel[0]);
	for ( size_t input_ind=1; input_ind<input_length; input_ind++ ){
	  product *= get_tensor_data_by_full_index_configuration_dense(input_data[input_ind], output_full_index_configuration, input_indices_full_strides[input_ind], input_data_numel[input_ind]);
	}
	output_data[output_numel_index] = product;
      }
      
    }else{
      // loop for each combination of contraction indexes' values and store result
      if ( is_sparse == true ){
	//output_data[output_irs_index] = 0;
	output_data[output_numel_index] = 0;
      }else{
	output_data[output_numel_index] = 0;
      }

      compute_output_tensor_part_helper(output_full_index_configuration, output_numel_index);

      // if ( is_sparse == true && output_data[output_irs_index] != 0 ){
      // 	output_irs[output_irs_index] = output_numel_index;
      // 	output_irs_index++;
      // 	if ( output_irs_index == output_data_numel_nzmax ){
      // 	  // increase output size by GROW_FACTOR
      // 	  size_t nbytes;
      // 	  size_t new_numel = output_data_numel_nzmax * GROW_FACTOR;

      // 	  nbytes = new_numel * sizeof(double);
      // 	  double* newptr_pr = (double*) mxRealloc(output_data, nbytes);
      // 	  //mexMakeMemoryPersistent(newptr_pr);
      // 	  mxSetPr(output_data_mx, newptr_pr);
      // 	  output_data = (double*) mxGetData(output_data_mx);

      // 	  // double* ptr = mxGetPi(output_data_mx);
      // 	  // double* newptr;
      // 	  // if(ptr != NULL) {
      // 	  //   newptr = (double*) mxRealloc(ptr, nbytes);
      // 	  //   mxSetPi(output_data_mx, newptr);
      // 	  //   std::cout << "yes" << std::endl;
      // 	  // }

      // 	  nbytes = new_numel * sizeof(mwIndex);
      // 	  mwIndex* newptr_ir = (mwIndex*) mxRealloc(output_irs, nbytes);
      // 	  //mexMakeMemoryPersistent(newptr_ir);
      // 	  mxSetIr(output_data_mx, newptr_ir);
      // 	  output_irs = mxGetIr(output_data_mx);

      // 	  //output_jcs = mxGetJc(output_data_mx);

      // 	  mxSetNzmax(output_data_mx, new_numel);

      // 	  output_data_numel_nzmax = new_numel;
      // 	  std::cout << "compute_output_tensor_part grow sparse output data to new_numel " << new_numel << std::endl;
      // 	}
      // }
    }
  }
}
void print_meta_data( size_t** target_indices_full_cardinality, size_t** target_indices_full_strides, size_t* target_data_numel){
  std::cout << "cardinalities:";
  for (size_t tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++){
    std::cout << " " << (*target_indices_full_cardinality)[tft_indices_ind];
  }
  std::cout << std::endl;

  std::cout << "strides:";
  for (size_t tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++){
    std::cout << " " << (*target_indices_full_strides)[tft_indices_ind];
  }
  std::cout << std::endl;

  std::cout << "target_data_numel: " << *target_data_numel << std::endl;
}

void init_tensor_meta_data( size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides, const mxArray* target_mxArray){
  *target_data_numel = 1;
  *target_indices_full_cardinality = (size_t*) malloc( sizeof(size_t) * tft_indices_length );
  *target_indices_full_strides = (size_t*) malloc( sizeof(size_t) * tft_indices_length );
  mxArray* target_indices_mx = mxGetProperty( target_mxArray, 0, "indices" );
  size_t target_indices_length = mxGetNumberOfElements(target_indices_mx);
  size_t current_stride = 1;
  for (int tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++){
    bool found = false;
    for ( size_t target_indices_ind=0; target_indices_ind<target_indices_length; target_indices_ind++ ){
      mxArray* prop_id = mxGetProperty( mxGetCell(target_indices_mx, target_indices_ind), 0, "id");
      size_t target_index_id = (size_t) ( ((double*)mxGetData(prop_id))[0] );
      if ( tft_indices_ids[tft_indices_ind] == target_index_id ){
	found = true;
	break;
      }
    }

    if ( found == true ){
      size_t current_cardinality = (size_t) (((double*)mxGetData((( mxGetProperty( tft_indices_mx, tft_indices_ind, "cardinality")))))[0]);
      (*target_indices_full_cardinality)[tft_indices_ind] = current_cardinality;
      *target_data_numel *= (*target_indices_full_cardinality)[tft_indices_ind];

      (*target_indices_full_strides)[tft_indices_ind] = current_stride; // TODO: check data access order - stride order
      current_stride *= current_cardinality;
    }else{
      (*target_indices_full_cardinality)[tft_indices_ind] = 0;
      (*target_indices_full_strides)[tft_indices_ind] = 0;
    }
  }
}

void init_dense_tensor( double** target_data, size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides, const mxArray* input_mxArray){
  //std::cout << "osman3.20 " << std::endl;
  mxArray* data_array = mxGetProperty( input_mxArray, 0, "data" );
  //std::cout << "osman3.21" << std::endl;
  *target_data = (double*) mxGetData(data_array);
  //std::cout << "osman3.22" << std::endl;
  init_tensor_meta_data( target_indices_full_cardinality, target_data_numel, target_indices_full_strides, input_mxArray );
  //std::cout << "osman3.23" << std::endl;

  //std::cout << "init_dense_tensor print_all_values" << std::endl;
  //print_all_values(*target_data, mxIsSparse(input_mxArray), target_indices_full_cardinality, target_data_numel, target_indices_full_strides);

}

void init_sparse_tensor(double** target_data, size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides, const mxArray* input_mxArray, mwIndex** target_irs, mwIndex** target_jcs, double** target_cache, std::vector<bool>** target_cache_bitmap, std::mutex* target_cache_lock){
  mxArray* data_array_mx = mxGetProperty( input_mxArray, 0, "data" );
  *target_irs = mxGetIr( data_array_mx );
  *target_jcs = mxGetJc( data_array_mx );
  *target_data = (double*) mxGetData( data_array_mx );
  //*target_cache_lock = *(new std::mutex);
  init_tensor_meta_data( target_indices_full_cardinality, target_data_numel, target_indices_full_strides, input_mxArray );

  //std::cout << "init_sparse_tensor print_all_values" << std::endl;
  //print_all_values(*target_data, mxIsSparse(input_mxArray), target_indices_full_cardinality, target_data_numel, target_indices_full_strides, std::vector<size_t>(), 0, *target_irs, *target_jcs, *target_cache, *target_cache_bitmap, target_cache_lock);

}

mwSize* init_output_tensor_meta_data(const mxArray* target_mxArray){
  output_indices_mx = mxGetProperty( target_mxArray, 0, "indices" );
  output_indices_length = mxGetNumberOfElements( output_indices_mx );
  // mwSize* output_data_array_cardinalities_size_dims = (mwSize*) malloc( sizeof(mwSize) * tft_indices_length );
  // std::cout << "output_data_array_cardinalities_size_dims: ";
  // for ( int i=0; i<tft_indices_length; i++ ){
  //   output_data_array_cardinalities_size_dims[i] = 1;
  //   std::cout << " " << output_data_array_cardinalities_size_dims[i];
  // }
  // std::cout << std::endl;
  // std::cout << "init_output_tensor_meta_data " << tft_indices_length << std::endl;
  // mxArray* output_data_array_cardinalities_mx = mxCreateNumericArray(tft_indices_length, output_data_array_cardinalities_size_dims, mxDOUBLE_CLASS, mxREAL);
  // if (output_data_mx == NULL){
  //   mexErrMsgTxt("Can not create output_data_array_cardinalities_mx, not enough memory");
  // }
  // mwSize* output_data_array_cardinalities = (mwSize*) mxGetData(output_data_array_cardinalities_mx);
  mwSize* output_data_array_cardinalities = (mwSize*) malloc( sizeof(mwSize) * tft_indices_length );

  size_t output_data_array_cardinalities_index = 0;
  for ( size_t tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++ ){
    bool found = false;
    for ( size_t output_indices_ind=0; output_indices_ind<output_indices_length; output_indices_ind++ ){
      mxArray* prop_id = mxGetProperty( mxGetCell(output_indices_mx, output_indices_ind), 0, "id");
      size_t output_index_id = (size_t) ( ((double*)mxGetData(prop_id))[0] );
      if ( tft_indices_ids[tft_indices_ind] == output_index_id ){
	//std::cout << "init_output_tensor_meta_data0 : " << output_data_array_cardinalities_index << " " << tft_indices_cardinalities[tft_indices_ind] << std::endl;
	output_data_array_cardinalities[output_data_array_cardinalities_index] = tft_indices_cardinalities[tft_indices_ind];
	output_data_array_cardinalities_index++;
	found = true;
	break;
      }
    }
    if ( found == false ){
      // dummy dimension (due to Matlab indexing compatability)
      //std::cout << "init_output_tensor_meta_data0 : " << output_data_array_cardinalities_index << " " << 1 << std::endl;
      output_data_array_cardinalities[output_data_array_cardinalities_index] = 1;
      output_data_array_cardinalities_index++;
      //std::cout << "init_output_tensor_meta_data1 : " << output_data_array_cardinalities_index << " " << output_data_array_cardinalities[output_data_array_cardinalities_index] << std::endl;
    }
  }

  // std::cout << "init_output_tensor_meta_data" << std::endl;
  // for ( int i=0; i<tft_indices_length; i++){
  //   std::cout << std::dec << output_data_array_cardinalities[i] << std::endl;
  // }

  return output_data_array_cardinalities;
}

// void init_sparse_output_tensor(const mxArray* target_mxArray, size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides ){
//   init_output_tensor_meta_data(target_mxArray);
//   init_tensor_meta_data( target_indices_full_cardinality, target_data_numel, target_indices_full_strides, target_mxArray );
//   output_data_numel_nzmax = INITIAL_SPARSE_NZMAX;
//   output_data_mx = mxCreateSparse(INITIAL_SPARSE_NZMAX, 1, output_data_numel_nzmax, mxREAL);
//   output_data = (double*) mxGetData(output_data_mx);

//   output_irs = mxGetIr( output_data_mx );
//   output_irs_index = 0;
//   output_jcs = mxGetJc( output_data_mx );
//   output_jcs[0] = 0;
// }

void init_dense_output_tensor(const mxArray* target_mxArray, size_t** target_indices_full_cardinality, size_t* target_data_numel, size_t** target_indices_full_strides ){
  mwSize* output_data_array_cardinalities = init_output_tensor_meta_data(target_mxArray);
  init_tensor_meta_data(target_indices_full_cardinality, target_data_numel, target_indices_full_strides, target_mxArray );
  //std::cout << "init_dense_output_tensor: tft_indices_length " << tft_indices_length << std::endl;
  for ( int i=0; i<tft_indices_length; i++){
    if ( output_data_array_cardinalities[i] == 0 ){
      mexErrMsgTxt("init_dense_output_tensor: internal error: output_data_array_cardinalities elements can not be equal to zero");
    }
    //std::cout << std::dec << output_data_array_cardinalities[i] << std::endl;
  }
  output_data_mx = mxCreateNumericArray(tft_indices_length, output_data_array_cardinalities, mxDOUBLE_CLASS, mxREAL);
  if (output_data_mx == NULL){
    mexErrMsgTxt("Can not create output data, not enough memory");
  }
  output_data = (double*) mxGetData(output_data_mx);
  if (output_data == NULL){
    mexErrMsgTxt("init_dense_output_tensor: internal error: output data was not created");
  }

  mwSize* output_size = mxGetDimensions(output_data_mx);
  // std::cout << "created output size:";
  // for ( int i=0; i<tft_indices_length; i++){
  //   std::cout << " " << output_size[i];
  // }
  // std::cout << std::endl;
  // std::cout << "created output elements:";
  // for ( int i=0; i<10; i++){
  //   std::cout << " " << output_data[i];
  // }
  // std::cout << std::endl;
  // std::cout << "print_meta_data" << std::endl;
  // print_meta_data(target_indices_full_cardinality, target_indices_full_strides, target_data_numel);
  // std::cout << "output print_all_values" << std::endl;
  // print_all_values(output_data, mxIsSparse(output_data_mx), target_indices_full_cardinality, target_data_numel, target_indices_full_strides );
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
  if (nrhs < 4){
    mexErrMsgTxt("gtp_mex requires at least 4 arguments: num threads, output tensor, input_tensor_0, input_tensor1, ...");
  }

  // 0: degree of parallelism
  num_threads = (int) mxGetScalar(prhs[0]);
  // 1: output tensor
  const int output_tensor_prhs_index = 1;
  // 2...n: input tensors

  input_length = nrhs-output_tensor_prhs_index-1;
  //std::cout << "input_length " << input_length << std::endl;

  is_sparse_input = (bool*) malloc( sizeof(bool) * input_length );
  input_data = (double**) malloc( sizeof(double*) * input_length );
  input_irs = (mwIndex**) malloc( sizeof(mwIndex*) * input_length );
  input_jcs = (mwIndex**) malloc( sizeof(mwIndex*) * input_length );
  input_data_numel = (size_t*) malloc( sizeof(size_t*) * input_length );
  input_indices_full_cardinality = (size_t**) malloc( sizeof(size_t*) * input_length );
  input_indices_full_strides = (size_t**) malloc( sizeof(size_t*) * input_length );
  input_cache = (double**) malloc( sizeof(double*) * input_length );
  input_cache_bitmap = (std::vector<bool>**) malloc( sizeof(std::vector<bool>*) * input_length );
  input_cache_lock = new std::mutex[input_length]; //(std::mutex*) malloc( sizeof(std::mutex) * input_length );

  //std::cout << "osman1" << std::endl;
  tft_indices_mx = mexGetVariable("global", "tft_indices");
  tft_indices_length = mxGetNumberOfElements(tft_indices_mx);
  tft_indices_cardinalities = (size_t*) malloc( sizeof(size_t) * tft_indices_length );
  tft_indices_ids = (size_t*) malloc( sizeof(size_t) * tft_indices_length );
  for (int i=0; i<tft_indices_length; i++){
    tft_indices_cardinalities[i] = (size_t) (((double*)mxGetData((( mxGetProperty( tft_indices_mx, i, "cardinality")))))[0]);
    tft_indices_ids[i] = (size_t) (((double*)mxGetData((( mxGetProperty( tft_indices_mx, i, "id")))))[0]);
  }
  //std::cout << "osman2" << std::endl;

  is_sparse = false;
  for (int prhs_offset=1; prhs_offset<=input_length; prhs_offset++){
    size_t prhs_index = output_tensor_prhs_index+prhs_offset;
    bool is_tensor_sparse = mxIsSparse( mxGetProperty( prhs[prhs_index], 0, "data" ) );
    is_sparse = is_sparse || is_tensor_sparse;
    is_sparse_input[prhs_offset-1] = is_tensor_sparse;
    //std::cout << "is_sparse_input[" << prhs_offset-1 << "] = " << is_sparse_input[prhs_offset-1] << std::endl;
  }

  //std::cout << "osman3" << std::endl;
  init_dense_output_tensor(prhs[output_tensor_prhs_index], &output_indices_full_cardinality, &output_data_maximum_numel, &output_indices_full_strides);
  //std::cout << "osman3.0" << std::endl;
  //std::cout << "init_tensor_meta_data output_data_maximum_numel " << output_data_maximum_numel << std::endl;
  if ( is_sparse == true ){
    // sparse init

    //init_sparse_output_tensor(prhs[output_tensor_prhs_index], &output_indices_full_cardinality, &output_data_maximum_numel, &output_indices_full_strides);
    for ( size_t input_ind=0; input_ind<input_length; input_ind++ ){
      if ( is_sparse_input[input_ind] == true ){
	//std::cout << "osman3.10" << std::endl;
	init_sparse_tensor(&(input_data[input_ind]), &(input_indices_full_cardinality[input_ind]), &(input_data_numel[input_ind]), &(input_indices_full_strides[input_ind]), prhs[ output_tensor_prhs_index+input_ind+1 ], &(input_irs[input_ind]), &(input_jcs[input_ind]), &(input_cache[input_ind]), &(input_cache_bitmap[input_ind]), &(input_cache_lock[input_ind]));
	//std::cout << "osman3.11" << std::endl;
      }else {
	//std::cout << "osman3.12 prhs_index " << output_tensor_prhs_index << " " << input_ind << " " << 1 << " total " << output_tensor_prhs_index+input_ind+1 << std::endl;
	init_dense_tensor(&(input_data[input_ind]), &(input_indices_full_cardinality[input_ind]), &(input_data_numel[input_ind]), &(input_indices_full_strides[input_ind]), prhs[ output_tensor_prhs_index+input_ind+1 ]);
	//std::cout << "osman3.13" << std::endl;
      }
    }

    //std::cout << "osman3.14" << std::endl;
    // initialize input caches
    for ( size_t input_ind=0; input_ind<input_length; input_ind++ ){
      //std::cout << "osman3.14.1" << std::endl;
      input_cache[input_ind] = (double*) malloc( sizeof(double) * (input_data_numel[input_ind]) );
      //std::cout << "osman3.14.2" << std::endl;
      input_cache_bitmap[input_ind] = new std::vector<bool>(input_data_numel[input_ind]);
      //std::cout << "osman3.14.3" << std::endl;
    }
    //std::cout << "osman3.15" << std::endl;
  }else{
    // dense init
    //init_dense_output_tensor(prhs[output_tensor_prhs_index], &output_indices_full_cardinality, &output_data_maximum_numel, &output_indices_full_strides);
    for ( size_t input_ind=0; input_ind<input_length; input_ind++ ){
      //std::cout << "osman3.1" << std::endl;
      init_dense_tensor(&(input_data[input_ind]), &(input_indices_full_cardinality[input_ind]), &(input_data_numel[input_ind]), &(input_indices_full_strides[input_ind]), prhs[ output_tensor_prhs_index+input_ind+1 ]);
      //std::cout << "osman3.2" << std::endl;
    }
  }

  //std::cout << "osman4" << std::endl;
  // generate contraction_index_inds
  contraction_index_inds = (size_t*) calloc( tft_indices_length, sizeof(size_t) );
  contraction_index_inds_length = 0;
  for ( size_t tft_indices_ind=0; tft_indices_ind<tft_indices_length; tft_indices_ind++ ){
    bool is_contraction_index = true;
    // check if index appears in output_index
    for ( size_t output_indices_ind=0; output_indices_ind<output_indices_length; output_indices_ind++ ){
      mxArray* prop_id = mxGetProperty( mxGetCell(output_indices_mx, output_indices_ind), 0, "id");
      size_t output_index_id = (size_t) ( ((double*)mxGetData(prop_id))[0] );
      if ( tft_indices_ids[tft_indices_ind] == output_index_id ){
  	// index appears in output tensor -> not contraction index
  	is_contraction_index = false;
  	break;
      }
    }
    if ( is_contraction_index == true ){
      contraction_index_inds[contraction_index_inds_length] = tft_indices_ind;
      contraction_index_inds_length++;
    }
  }

  //std::cout << "osman4.1" << std::endl;
  pthread_t threads[num_threads];
  int rc;
  for( intptr_t i=0; i < num_threads; i++ ){
    rc = pthread_create(&threads[i], NULL, compute_output_tensor_part, (void *)i);
    if (rc){
      std::cout << "gtp_mex: error unable to create thread " << rc << " " << strerror(rc) << std::endl;
      exit(-1);
    }
  }

  //std::cout << "osman5" << std::endl;
  for( int i=0; i < num_threads; i++ ){
    rc = pthread_join(threads[i], NULL);
    if (rc) {
      std::cout << "gtp_mex: failed to join thread" << (long)i << " " << strerror(rc) << std::endl;
      exit(EXIT_FAILURE);
    }
  }
  // if( is_sparse == true ){
  //   output_jcs[1] = output_irs_index;
  //   mxSetM(output_data_mx, output_data_maximum_numel);
  // }

  //std::cout << "osman6" << std::endl;
  mxSetProperty( prhs[ output_tensor_prhs_index ], 0, "data", output_data_mx );

  // std::cout << "created calculated output elements:";
  // for ( int i=0; i<10; i++){
  //   std::cout << " " << output_data[i];
  // }
  // std::cout << std::endl;

  //mexAtExit(clear_mem);
}


/* reference
jcs
jcs[0] = 0
jcs[1] = 2
jcs[2] = 0
jcs[3] = 0
jcs[4] = 0
jcs[5] = 53
jcs[6] = 140189258224336
jcs[7] = 140188894770784
jcs[8] = 725
jcs[9] = 140187297212960
irs
irs[0] = 2
irs[1] = 5
irs[2] = 140188901745184
irs[3] = 140188901745184
irs[4] = 7867352474164620655
irs[5] = 49
irs[6] = 140188894768464
irs[7] = 140188889063472
irs[8] = 0
irs[9] = 12
data
data[0] = 8
data[1] = 9
data[2] = 6.92617e-310
data[3] = 6.92617e-310
data[4] = 6.92617e-310
data[5] = 6.92617e-310
data[6] = 0
data[7] = 2.42092e-322
data[8] = 6.92625e-310
data[9] = 6.92617e-310
  C-c C-cgtp_mex: all compute_output_tensor_part complete
Operation terminated by user during compile_mex_sparse (line 38)

 
>> a
a

a =

   (3,1)        8
   (6,1)        9

>> 
*/
