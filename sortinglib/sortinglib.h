#ifndef __SORTING_LIB_H__
#define __SORTING_LIB_H__
#include <cstdio>
#include <cmath>
#include <time.h>
#include <list>
#include <algorithm>
#include <vector>
#include <cstring>
#include <algorithm>
//#include "mpi-interoperate.h"
#include "defs.h"

#define UINT64_MAX (18446744073709551615ULL)

template <class key, class value>
class kv_pair {
  public:
    key k;
    value v;
    /*inline*/ bool operator< (const kv_pair<key, value>& other) const{
      return k < other.k;
    }    
};

//Global variables
int in_elems, *out_elems;
void* dataIn, **dataOut;
CkCallback *CB; bool callBackSet;

#include "HistSort.decl.h"
#include "Sorter.h"
#include "Bucket.h"

#ifndef CK_TEMPLATES_ONLY
template <class key, class value>
void registerSortingLib();
#endif

template <class key, class value>
void HistSorting(int input_elems_, kv_pair<key, value>* dataIn_, 
                 int *output_elems_, kv_pair<key, value>** dataOut_,
                 int probe_max, CkCallback *CB_) {
  registerSortingLib<key, value>();
  dataIn = (void*)dataIn_;
  dataOut = (void**)dataOut_;
  in_elems = input_elems_;
  out_elems = output_elems_;
  
  callBackSet = false;
  CB = CB_;
  if(CB_){
    callBackSet = true;
  }


  if(CkMyPe() == 0) {
#if DEBUG
    CkPrintf("[%d] Histogram Sorting on %d at %.3lf MB\n",CkMyPe(),CkNumPes(),CmiMemoryUsage()/(1024.0*1024));
#endif
   static CProxy_Main<key, value> mainProxy = CProxy_Main<key, value>::ckNew(CkNumPes(), probe_max);
    mainProxy.DataReady();
  }
  //StartCharmScheduler();
}

//main entrypoint of tester, coordinates testing of sorting library
template <class key, class value>
class Main : public CBase_Main<key, value> {
  private:
    int num_buckets;
    //chare array proxies
    CProxy_Sorter<key, value> sorter;
    CProxy_Bucket<key, value> bucket_arr;
    tuning_params pars; 
  public:
    //main constructor
    Main (int num_buckets_, int probe_max) {
      //input parameter is number of buckets (data chares)
      num_buckets = num_buckets_;
      // Create the sorting entities
      CkArrayOptions opts(num_buckets);
      pars.probe_size = num_buckets - 1;
      if(probe_max == -1)
        pars.probe_max = 64;
      else
        pars.probe_max = probe_max;
      
      pars.hist_thresh = 5;
      pars.splice_thresh = 1024;
      //try a variety of communication stages
      if (num_buckets <= 256)
        pars.eager_send = num_buckets;
      else
        pars.eager_send = num_buckets/16; //need to play with this

      pars.reuse_probe_results = true;
  
      bucket_arr = CProxy_Bucket<key, value>::ckNew(pars, 0, UINT64_MAX,  num_buckets, opts);
      
      // maxkey should be MAXINT - c * p
      sorter = CProxy_Sorter<key, value>::ckNew(bucket_arr, num_buckets, 0, 
        UINT64_MAX-1000000000, pars, this->thisProxy);
    }
    //migration constructor
    Main (CkMigrateMessage *m) { }
    //starts sorting
    void DataReady();
    //a reduction that exists application
    void Exit() {
      CkExit();
    }
    void init_dsum(CkReductionMsg *msg) {
      printf("Initial value sum %lf\n",*((double *) msg->getData()));
      delete msg;
    }
    void final_dsum(CkReductionMsg *msg) {
      printf("Final value sum %lf\n",*((double *) msg->getData()));
      delete msg;
    }
    void init_isum(CkReductionMsg *msg) {
      printf("Initial key sum %d\n",*((int *) msg->getData()));
      delete msg;
    }
    void final_isum(CkReductionMsg *msg) {
      printf("Final key sum %d\n",*((int *) msg->getData()));
      delete msg;
    }
};

//a reduction that signifies that data chare array has generated its data and sorting can begin
template <class key, class value>
void Main<key, value>::DataReady() {
  //There is a possibility of a data race here
  bucket_arr.SetData(sorter, this->thisProxy);     
  //sorter.Begin();       
}

#define CK_TEMPLATES_ONLY
#include "HistSort.def.h"
#undef CK_TEMPLATES_ONLY 

#ifndef CK_TEMPLATES_ONLY
template <class key, class value>
void registerSortingLib() {
  static int initDone = 0;
  if(initDone) return;
  initDone = 1;

/* REG: chare Main<uint64_t,data_value >: Chare;
*/
  CkIndex_Main<key, value >::__register("Main<key, value >", sizeof(Main<key, value >));

/* REG: chare Sorter<key,value >: Chare;
*/
  CkIndex_Sorter<key,value >::__register("Sorter<key,value >", sizeof(Sorter<key,value >));

/* REG: array Bucket<key,value >: ArrayElement;
*/
  CkIndex_Bucket<key,value >::__register("Bucket<key,value >", sizeof(Bucket<key,value >));

/* REG: message next_probe<key >;
*/
  CMessage_probeMessage<key >::__register("probeMessage<key >", sizeof(probeMessage<key >),(CkPackFnPtr) probeMessage<key >::pack,(CkUnpackFnPtr) probeMessage<key >::unpack);

/* REG: message next_probe<key >;
*/
  CMessage_data_msg<key, value >::__register("data_msg<key,value >", sizeof(data_msg<key,value >),(CkPackFnPtr) data_msg<key,value >::pack,(CkUnpackFnPtr) data_msg<key,value >::unpack);
}


template <class key>
std::pair<int, key> grtstPow2(key n){
  key ret = 1;
  int cnt = 0;
  while(ret < n){
    ret *= 2;
    cnt++;
  }
  return std::pair<int, key>(cnt-1, ret>>1);
}

template<int> std::pair<int, int> grtstPow2(int);


//template <class key, class value>
template<>
void recursive_pup_impl<Sorter<unsigned long, int >, 1>::operator()(Sorter<unsigned long, int > *obj, PUP::er &p) {
    obj->parent_pup(p);
    obj->Sorter<unsigned long, int  >::pup(p);
}


template <>
void recursive_pup_impl<Bucket<unsigned long, int >, 1>::operator()(Bucket<unsigned long, int > *obj, PUP::er &p) {
    obj->parent_pup(p);
    obj->Bucket<unsigned long, int >::pup(p);
}

template <>
void recursive_pup_impl<Main<unsigned long, int >, 1>::operator()(Main<unsigned long, int > *obj, PUP::er &p) {
    obj->parent_pup(p);
    obj->Main::pup(p);
}


#endif //#ifndef CK_TEMPLATES_ONLY
#endif
