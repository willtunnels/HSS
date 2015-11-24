#ifndef __BUCKET_H__
#define __BUCKET_H__


template <class key, class value>
class Bucket : public CBase_Bucket<key, value> {
  private:
	
	
	kv_pair<key, value>* bucket_data;
	int numElem;
	static const int indexFactor = 10;

	int nBuckets;
	
	key minkey, maxkey;
	key mymin, mymax;
	
	CProxy_Sorter<key, value> sorter_proxy;
	tuning_params *params;
	
	key* finalSplitters;
	bool* achieved;
    int achievedSplitters;

    key* lastProbe;
    int lastProbeSize;

    int *sepCounts;
    key *sepKeys;
    int lastSortedChunk;
   	int numChunks;
   	bool doneHists;


	//kv_pair<key, value>* scratch;
	int* cumHist;
	int* indices;
	int numIndicesShift;
    int* histCounts;
    uint64_t *longhistCounts;

    int numProbes;
    
   
   	void Reset(); 
   	void localProbe();
  public:
      Bucket(CkMigrateMessage *);
	  Bucket(tuning_params par, key _min, key _max, int nBuckets_);
	  void SetData(CProxy_Sorter<key, value> _sorter_proxy);
	  void stepSort();
	  void firstProbe(key firstkey, key lastkey, key step, int probeSize);
	  void firstLocalProbe(int lastProbeSize);
	  void histCountProbes(probeMessage<key> *pm);
};

//need to include .C file in order to have it instantiated when the .h file is included externally
//such instantiation is necessary here since the compiler generates templated code on demand
#include "Bucket.C"
#endif
