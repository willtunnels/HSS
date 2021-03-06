//standard header files
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <iostream>
#include <string.h>
#include "testsorting.decl.h"

//header files for libraries in Charm
#include "sortinglib/sortinglib.h"

#define DEBUG(a) //a

unsigned int seed;

uint64_t getRandom(){
    if (sizeof(int) < sizeof(long))
        return (static_cast<long>(rand_r(&seed)) << (sizeof(int) * 8)) |
               rand_r(&seed);
    return rand_r(&seed);
}

const int NUM_DISTS = 6; 
const std::string distributions[NUM_DISTS]={"UNIF", "SKEW1", "SKEW2", "SKEW3", "GAUSS", "AllZeros"};

int getDistCode(std::string input_dist){
    for(int i=0; i<NUM_DISTS; i++){
       if(distributions[i] == input_dist)
          return i;
    }
    return 0; //default
}

class testsorting : public CBase_testsorting{
  public:
    testsorting(CkArgMsg *m){
        srand (time(NULL));
        int num_elems = atoi(m->argv[1]);
        int rand_seed = atoi(m->argv[2]);
        int probe_max = -1;
        if(m->argc > 2)
            probe_max = atoi(m->argv[3]);
        std::string input_dist = "UNIF";
        if(m->argc > 3)
            input_dist = m->argv[4];
        ckout<<"Number of elements per PE: "<<num_elems<<". Random seed "<<rand_seed<<endl;
        CProxy_dataManager dMProxy = CProxy_dataManager::ckNew(CkNumPes() ,num_elems, probe_max, getDistCode(input_dist));
    }
};

uint64_t getDuplicateDist1(){
    int coin = rand_r(&seed)%2;
    if(coin==0)
       return getRandom();
    else{
       uint64_t base = 1000000;
       return base + (getRandom() & (uint64_t)1023);
    }
}

//Box-Muller method
uint64_t getGaussianRandom(){
  double e=2.7182818284590452;
  double log_e=log(e);
  uint64_t mean = ((uint64_t)1)<<(sizeof(uint64_t) * 4);
  uint64_t std_dev = ((uint64_t)1)<<(sizeof(uint64_t) * 2);
  return mean + (sqrt(-2*log(rand_r(&seed)*1.0/RAND_MAX)/log_e)
                 * cos(rand_r(&seed)*2*M_PI/RAND_MAX)
                 * (double)std_dev);
}

class dataManager : public CBase_dataManager{
  private:
      uint64_t* dataIn;
      uint64_t* dataOut;
      int out_elems;
      CkCallback CB;
      double startTime, endTime;
  public:
    dataManager(int numBuckets, int numElem, int probe_max, int input_dist){
        //num_elems = num_elems*(1+newid);
        dataIn = new uint64_t[numElem];
        int peid = CkMyPe();
        seed = CkMyPe();
        if(!CkMyPe())
          std::cout<<"Using Input distribution: ["<<distributions[input_dist]<<"]"<<std::endl;
        for (int i = 0; i < numElem; i++){
            if(distributions[input_dist] == "UNIF")
              dataIn[i]  = getRandom();
            else if(distributions[input_dist] == "SKEW1")
              dataIn[i]  = getDuplicateDist1();
            else if(distributions[input_dist] == "SKEW2")
              dataIn[i]  = getRandom()%100;
            else if(distributions[input_dist] == "SKEW3")
              dataIn[i]  = getRandom() & getRandom();
            else if(distributions[input_dist] == "GAUSS")
              dataIn[i]  = getGaussianRandom();
            else if(distributions[input_dist] == "AllZeros")
              dataIn[i]  = 0;
        }
        DEBUG(printf("In elems on %d are %d\n",peid, numElem);)
        int entryMethodIndex = CkIndex_dataManager::SortingDone(); 
        CB = CkCallback(entryMethodIndex, thisProxy[CkMyPe()]);
        startTime = CmiWallTimer();
        HistSorting<uint64_t>(numElem, dataIn, &out_elems, &dataOut, probe_max, &CB);
    }

    void SortingDone(){
        delete[] dataIn;
        delete[] dataOut;
        endTime = CmiWallTimer();
        if(!CkMyPe()){
            printf("Time taken in first sorting %lf s\n", endTime - startTime);
            CkExit();
        }
    }
};




#include "testsorting.def.h"
