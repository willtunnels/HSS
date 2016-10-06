#ifndef __NODEMANAGER_H__
#define __NODEMANAGER_H__


#include <map>

const int SAMPLE_FACTOR = 20;

int maxSampleSize(){
	int lognnodes = 1, numnodes = CkNumNodes();
	while((1<<lognnodes) <= numnodes) lognnodes++;
	int sampleSize = (SAMPLE_FACTOR *  numnodes * lognnodes);
	return sampleSize + 2;
}


int sampleSizePerNode(){
	int lognnodes = 1, numnodes = CkNumNodes(), numpes = CkNodeSize(CkMyNode());
	while((1<<lognnodes) <= numnodes) lognnodes++;
	int sampleSize = (SAMPLE_FACTOR * lognnodes);
	return sampleSize;
}


int ls_getMaxSampleSize(){
	uint64_t elemsPerNode = numTotalElems/CkNumNodes();
	uint64_t npes = CkNodeSize(CkMyNode());
	return (2 * npes * npes * npes);
}
int ls_getSampleSize(int locElems){
	uint64_t elemsPerNode = numTotalElems/CkNumNodes();
	uint64_t npes = CkNodeSize(CkMyNode());
	return (npes * npes * npes * locElems)/elemsPerNode;
}


class wrap_ptr{
public:
	void *ptr;
	wrap_ptr() {} 
	wrap_ptr(void *_ptr): ptr(_ptr) {}
	void pup(PUP::er &p){
		pup_bytes(&p, this, sizeof(*this));
	}
};



class sendInfo{
	public:
		int ind1, ind2;
		void* base;
		sendInfo() {}
		sendInfo(void *_base, int _ind1, int _ind2) : base(_base), ind1(_ind1), ind2(_ind2) {}
		void pup(PUP::er &p){
			pup_bytes(&p, this, sizeof(*this));
		}
};



template <class key, class value>
class NodeManager : public CBase_NodeManager<key, value> {
	private:
		int numnodes;
		int numpes;
		int* pelist;
		int* numElem;
		int sampleSize;
		CProxy_Sorter<key, value> sorter;
		CProxy_Bucket<key, value> bucket_arr;
		array_msg<key> *sample;
		key minkey, maxkey;
		int samplesRcvd;
		int numSent, numRecvd, numFinished;
		key* ls_sample; //ls_ :: localsort
		key* splitters;
		int ls_numTotSamples; 
		int numDeposited;
		//assemble info
		std::vector<std::vector<sendInfo> > ainfo;
		std::vector<data_msg<key, value> *> recvdMsgs;
		std::vector<data_msg<key, value> *> bufMsgs;
		std::map<int, uint64_t*> histLocCounts; //one for each pe
		uint64_t* finalHistCounts;
      Bucket<key, value>* getLocalBucket(){
			for(int i=0; i<numpes; i++){
				Bucket<key, value> *obj = bucket_arr[pelist[i]].ckLocal();
				if(obj != NULL) return obj;
			}
			CkAbort("GetLocalBucket, getLocalBucket not found \n");
		}


	public:    
		NodeManager(key _minkey, key _maxkey): minkey(_minkey), maxkey(_maxkey){
			ckout<<"Node Manager created at "<<CkMyNode()<<endl;
			numnodes = CkNumNodes();
			numpes = CkNodeSize(CkMyNode());
			numElem = new int[numpes];
			pelist = new int[numpes];
			splitters = new key[numpes];
			ainfo.resize(numnodes);
			numRecvd = numFinished = numDeposited = numSent = 0;
			ls_numTotSamples=0;
		}

		void registerLocalChare(int nElem, int pe, CProxy_Bucket<key, value> _bucket_arr,
				CProxy_Sorter<key, value> _sorter){
			ckout<<"registerLocalchare called from PE "<<pe<<" at "<<CkMyNode()<<endl;
			//ckout<<"numnodes :"<<numnodes<<" numpes:"<<numpes<<endl;
			//!This should be a bucket_proxy ID
			bucket_arr = _bucket_arr;
			sorter = _sorter;
			static int currnpes = 0;
			pelist[currnpes] = pe;
			numElem[currnpes++] = nElem;
			int lognnodes = 1;
			while((1<<lognnodes) <= numnodes) lognnodes <<= 1;
			if(currnpes == numpes){ //all PE's registered
				sampleSize = sampleSizePerNode();
				//ckout<<"sample size: "<<sampleSize<<endl;
				genSampleIndices();
			}
		}

		//call this function
		void genSampleIndices(){
			int *randIndices = new int[sampleSize];
			int cum = numElem[0];
			for(int i=1; i<numpes; i++)
				cum += numElem[i]; 

			typedef std::chrono::high_resolution_clock myclock;
			myclock::time_point beginning = myclock::now();
			myclock::duration d = myclock::now() - beginning;
			unsigned seed = d.count();
			seed = CkMyNode();
			//ckout<<"Seed is "<<seed<<endl;

			std::default_random_engine generator(seed);
			std::uniform_int_distribution<int> distribution(0,cum-1);

			distribution(generator);

			for(int i=0; i<sampleSize; i++){
				randIndices[i] = distribution(generator); 
				//ckout<<"randIndices "<<randIndices[i]<<endl;
				distribution.reset();
			}

			sample= new (sampleSize) array_msg<key>;
			sample->numElem = sampleSize;

			std::sort(randIndices, randIndices + sampleSize);
			int cumIndex = 0, cumFreq = 0;
			samplesRcvd = 0;
			for(int i=0; i<numpes; i++){
				int ub = std::upper_bound(randIndices + cumIndex, 
						randIndices + sampleSize, cumFreq + numElem[i]) - randIndices;   
				//send  randIndices[cumIndex, ub) - cumFreq to PE pelist[i]
				array_msg<int> *am = new (ub-cumIndex) array_msg<int>;
				am->numElem = ub-cumIndex;
				for(int i=0; i<am->numElem; i++){
					am->data[i] = randIndices[cumIndex+i] - cumFreq;
				}
				ckout<<"Sending randIndices["<<cumIndex<<","<<ub<<") - ";
				ckout<<cumFreq<<" to PE "<<pelist[i]<<endl;
				bucket_arr[pelist[i]].genSample(am);
				cumIndex = ub;
				cumFreq += numElem[i];
			}
		}

		void collectSamples(array_msg<key> *s){
			static int count = 0;
			memcpy(sample->data + samplesRcvd, s->data, s->numElem * sizeof(key));
			samplesRcvd += s->numElem;
			count++;
			//can enter here twice, check on the basis of number of msgs received
			if(count == numpes){
				ckout<<"Sending ******************* to Sorter ************** size: "<< s->numElem << "from "<<CkMyNode()<<endl;
				//for(int i=0; i<sample->numElem; i++)
				//	ckout<<"From nodemgr "<<CkMyNode()<<" : "<<sample->data[i]<<endl;
				sorter.recvSample(sample);
			}
		}
		void loadkeys(int dest, sendInfo inf){
			ainfo[dest].push_back(inf);	
			if(ainfo[dest].size() == numpes){
				numSent++;
				this->thisProxy[CkMyNode()].sendOne(dest);
				if(numSent == CkNumNodes())
					this->thisProxy[CkMyNode()].releaseBufMsgs();
			}
		}

		void sendOne(int dest){
			//CkPrintf("[%d][%d] sendOne to node: %d \n", CkMyNode(), CkMyPe(), dest);
			int numelem = 0;
			for(int i=0; i<ainfo[dest].size(); i++)
				numelem += ainfo[dest][i].ind2 - ainfo[dest][i].ind1;
			data_msg<key, value> *dm = new (numelem) data_msg<key,value>;
			dm->num_vals = numelem;
			dm->sorted = false;
			int curr = 0;
			for(int i=0; i<ainfo[dest].size(); i++){
				int ind1 = ainfo[dest][i].ind1, ind2 = ainfo[dest][i].ind2;
				kv_pair<key, value>* bucket_data = (kv_pair<key, value>*)ainfo[dest][i].base;
				memcpy(dm->data + curr, bucket_data + ind1, (ind2-ind1)*sizeof(kv_pair<key, value>));
				curr += ind2 - ind1;
			}
			this->thisProxy[dest].recvOne(dm);
		}


		void releaseBufMsgs(){
			for(int i=0; i<bufMsgs.size(); i++)
				recvOne(bufMsgs[i]);
			bufMsgs.clear();
		}


		void recvOne(data_msg<key, value> *dm){
			//CkPrintf("[%d] message received of size: %d \n", CkMyNode(), dm->num_vals);
			//for(int i=0; i<dm->num_vals; i++)
			//	ckout<<"message  "<<i<<" : "<<dm->data[i].k<<endl;
			if(numSent != CkNumNodes()){
				bufMsgs.push_back(dm);
				return;
				CkAbort("Haven't sent all messages yet");
			}
				
			if(recvdMsgs.size() == 0){
				Bucket<key, value> *obj = getLocalBucket();
				obj->setTotalKeys();
				//ckout<<" bucket obj : "<<obj<<"   pelist[0]: "<< pelist[0]<<" "<<numTotalElems<<endl;
				ls_sample = new key[ls_getMaxSampleSize()];	
			}
			int numsamples = ls_getSampleSize(dm->num_vals);
			recvdMsgs.push_back(dm);
			ls_numTotSamples += numsamples;
			if(ls_numTotSamples >= ls_getMaxSampleSize()) {
				CkPrintf("[%d] *** maxsamplesize: %d *** ls_numTotSamples: %d \n", CkMyNode(), ls_getMaxSampleSize(), ls_numTotSamples);
				CmiAbort("sample size exceeds expectations");
			}	
			this->thisProxy[CkMyNode()].handleOne(recvdMsgs.size()-1, ls_numTotSamples - numsamples, numsamples);
			++numRecvd;
			//if(numRecvd == CkNumNodes())
			//	CkPrintf("[%d] Received all messages \n", CkMyNode());
		}


		void handleOne(int ind, int sampleInd, int numsamples){
			//CkPrintf("[%d, %d]handleOne,  numSamples: %d \n", CkMyNode(), CkMyPe(), numsamples);
			data_msg<key, value> *dm = recvdMsgs[ind];

			typedef std::chrono::high_resolution_clock myclock;
			myclock::time_point beginning = myclock::now();
			myclock::duration d = myclock::now() - beginning;
			unsigned seed = d.count();
			seed = CkMyNode();
			//ckout<<"Seed is "<<seed<<endl;
			std::default_random_engine generator(seed);
			std::uniform_int_distribution<int> distribution(0,dm->num_vals-1);
			distribution(generator);
			int randIndex;
			for(int i=0; i<numsamples; i++){
				randIndex = distribution(generator);
				ls_sample[sampleInd + i] = dm->data[randIndex].k;
				//ckout<<"randIndex "<<randIndex<<endl;
				distribution.reset();
			}
			std::sort(dm->data, dm->data + dm->num_vals);
			this->thisProxy[CkMyNode()].finishOne();
		}


		void finishOne(){
			if(++numFinished == CkNumNodes()){ //all messages have been received, processed
				ls_sample[ls_numTotSamples++] = maxkey;
				std::sort(ls_sample, ls_sample + ls_numTotSamples); //sort all sampled keys
				for(int i=0; i<numpes-1; i++){
					splitters[i] = ls_sample[((i+1) * ls_numTotSamples)/numpes];
				}
				splitters[numpes-1] = maxkey;
				std::sort(pelist, pelist + numpes);
				//for(int i=0; i<numpes; i++){
				//	CkPrintf("[%d] Splitterss #%d: %llu\n", CkMyNode(), i, splitters[i]);
				//}
				//CkPrintf("[%d] RecvdMsgs.size: %d\n", CkMyNode(), recvdMsgs.size());
				for(int i=0; i<recvdMsgs.size(); i++){
					//for(int j=0; j<recvdMsgs[i]->num_vals; j++)
					//	CkPrintf("[%d] recvdmsg[%d] #(%d): %llu\n", CkMyNode(), i, j, recvdMsgs[i]->data[j].k);
					this->thisProxy[CkMyNode()].sendToBuckets(recvdMsgs[i]);
				}
				//CkPrintf("[%d] Finished sorting, sampling from all messages \n", CkMyNode());
				//for(int i=0; i<recvdMsgs.size(); i++){
				//	this->thisProxy[CkMyNode()].localhist(recvdMsgs[i]);
				//}
			}
		}

		void localhist(data_msg<key, value>* dm){
		   	uint64_t* histCounts;
		  	if(histLocCounts.find(CkMyPe()) == histLocCounts.end()){
				histCounts = new uint64_t[ls_numTotSamples+1];	
				memset(histCounts, 0, (ls_numTotSamples+1) * sizeof(uint64_t));
				histLocCounts[CkMyPe()] = histCounts;
			}
			else
				histCounts = (histLocCounts.find(CkMyPe()))->second;
			//CkPrintf("localhist [%d, %d] histCounts: %p \n", CkMyNode(), CkMyPe(), histCounts);
			//for(int i=0; i<dm->num_vals; i++)
			//	CkPrintf("dm->val[%d]: %llu \n", i, dm->data[i].k);	
			int cumCount = 0;
			kv_pair<key, value> comp;
			int prb = -1;
			do{
				prb++;
				comp.k = ls_sample[prb];
				int cnt = std::lower_bound(dm->data + cumCount,
						dm->data + dm->num_vals, comp) - dm->data;
				histCounts[prb] += cnt - cumCount;
				cumCount = cnt;
			}while(prb<ls_numTotSamples-1);  //the second condition from Bucket.C is not necessary
			this->thisProxy[CkMyNode()].depositHist();
	  }

	  //how will this be an entry method
	  void depositHist(){
		numDeposited++;
		if(numDeposited == CkNumNodes()){
			int numHists = histLocCounts.size();
			uint64_t *histograms[numHists];
			std::map<int, uint64_t*>::iterator it;
			int i;
			for(i=0,it = histLocCounts.begin(); it != histLocCounts.end(); it++, i++)
				histograms[i] = it->second;
			uint64_t cum = 0;
			for(int i=0; i<ls_numTotSamples; i++){
				for(int j=1; j<numHists; j++)
					histograms[0][i] += histograms[j][i];
				cum += histograms[0][i];
				//CkPrintf("[%d] FinalHist[%llu]: %llu \n",  CkMyNode(), ls_sample[i], cum);
			}
			CkPrintf("[%d] FinalHist: %llu, sample-size: %d \n",  CkMyNode(),  cum, ls_numTotSamples);
			finalHistCounts	= histograms[0];
			CkPrintf("Deposited [%d] All localsort hists deposited \n", CkMyNode());
		}
	  }


	void sendToBuckets(data_msg<key, value>* dm){
	    	key prev = minkey;	
		for(int i=0; i<numpes; i++){
                	key sep1 = prev;
                        key sep2 = splitters[i];
                	//find keys and send
                	kv_pair<key, value> comp;
                	comp.k = sep1;
                	int ind1 = std::lower_bound(dm->data,
                                        dm->data + dm->num_vals, comp) - dm->data;
                	comp.k = sep2;
                	int ind2 = std::lower_bound(dm->data,
                                        dm->data + dm->num_vals, comp) - dm->data;
               		 //ckout<<"Finalsending ["<< CkMyNode() <<"] "<<ind1<<"-"<<ind2<<" to "<<pelist[i]<<endl;
                	bucket_arr[pelist[i]].recvFinalKeys(i, sendInfo(dm->data, ind1, ind2));
			prev = splitters[i];
	
		}	
	}

};

#endif
