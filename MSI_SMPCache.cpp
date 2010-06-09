#include "MSI_SMPCache.h"

MSI_SMPCache::MSI_SMPCache(int cpuid, std::vector<SMPCache * > * cacheVector,
                 int csize, int cassoc, int cbsize, int caddressable, const char * repPol, bool cskew) : 
               SMPCache(cpuid,cacheVector){
  fprintf(stderr,"Making a MSI cache with cpuid %d\n",cpuid);
  CacheGeneric<MSI_SMPCacheState> *c = CacheGeneric<MSI_SMPCacheState>::create(csize, cassoc, cbsize, caddressable, repPol, cskew);
  cache = (CacheGeneric<StateGeneric<> >*)c; 

}

void MSI_SMPCache::fillLine(uint32_t addr, uint32_t mesi_state){
  MSI_SMPCacheState *st = (MSI_SMPCacheState *)cache->findLine2Replace(addr); //this gets the contents of whateverline this would go into
  if(st==0){
    return;
  }
  st->setTag(cache->calcTag(addr));
  st->changeStateTo((MSIState_t)mesi_state);
  return;
    
}
  

MSI_SMPCache::RemoteReadService MSI_SMPCache::readRemoteAction(uint32_t addr){
  std::vector<SMPCache * >::iterator cacheIter;
  std::vector<SMPCache * >::iterator lastCacheIter;
  for(cacheIter = this->getCacheVector()->begin(), lastCacheIter = this->getCacheVector()->end(); cacheIter != lastCacheIter; cacheIter++){
    MSI_SMPCache *otherCache = (MSI_SMPCache*)*cacheIter; 
    if(otherCache->getCPUId() == this->getCPUId()){
      continue;
    }
      
    MSI_SMPCacheState* otherState = (MSI_SMPCacheState *)otherCache->cache->findLine(addr);
    if(otherState){
      if(otherState->getState() == MSI_MODIFIED){

        otherState->changeStateTo(MSI_SHARED);
        return MSI_SMPCache::RemoteReadService(false,true);

      }else if(otherState->getState() == MSI_SHARED){  //doesn't matter except that someone's got it

        return MSI_SMPCache::RemoteReadService(true,false);

      }else if(otherState->getState() == MSI_INVALID){ //doesn't matter at all.

      }
    }

  }//done with other caches

  //This happens if everyone was MSI_INVALID
  return MSI_SMPCache::RemoteReadService(false,false);
}

void MSI_SMPCache::readLine(uint32_t rdPC, uint32_t addr){
  MSI_SMPCacheState *st = (MSI_SMPCacheState *)cache->findLine(addr);    
  //fprintf(stderr,"In MSI ReadLine\n");
  if(!st || (st && !(st->isValid())) ){//Read Miss -- i need to look in other peoples' caches for this data

    //Query the other caches and get a remote read service object.
    MSI_SMPCache::RemoteReadService rrs = readRemoteAction(addr);
      
    MSIState_t newMesiState = MSI_INVALID;//It will never end up being invalid, though.

    if(rrs.providedData == false){ //If no one had it, then we have it Exclusive

      //No Valid Read-Reply: Need to get this data from Memory
      newMesiState = MSI_SHARED;

    }else{ //Someone had it.
    
      if(rrs.isShared){
        
        //Valid Read-Reply From Shared
        newMesiState = MSI_SHARED;

      }else{

        //Valid Read-Reply From Modified/Exclusive
        newMesiState = MSI_SHARED;
        numReadMissesServicedByOthers++;

      }
    } 
    //Fill the line
    fillLine(addr,newMesiState); 

    //Update event counter for read misses
    numReadMisses++;
      
  }else{ //Read Hit

    numReadHits++; 
    return; 

  }

}


MSI_SMPCache::InvalidateReply MSI_SMPCache::writeRemoteAction(uint32_t addr){
    
    bool empty = true;
    std::vector<SMPCache * >::iterator cacheIter;
    std::vector<SMPCache * >::iterator lastCacheIter;
    for(cacheIter = this->getCacheVector()->begin(), lastCacheIter = this->getCacheVector()->end(); cacheIter != lastCacheIter; cacheIter++){
      MSI_SMPCache *otherCache = (MSI_SMPCache*)*cacheIter; 
      if(otherCache->getCPUId() == this->getCPUId()){
        continue;
      }

      //Get the line from the current other cache 
      MSI_SMPCacheState* otherState = (MSI_SMPCacheState *)otherCache->cache->findLine(addr);

      //if it was actually in the other cache:
      if(otherState && otherState->isValid()){
          /*Invalidate the line, cause we're writing*/
          otherState->invalidate();
          empty = false;
      }

    }//done with other caches

    //Empty=true indicates that no other cache 
    //had the line or there were no other caches
    //
    //This data in this object is not used as is, 
    //but it might be useful if you plan to extend 
    //this simulator, so i left it in.
    return MSI_SMPCache::InvalidateReply(empty);
}


void MSI_SMPCache::writeLine(uint32_t wrPC, uint32_t addr){
  MSI_SMPCacheState * st = (MSI_SMPCacheState *)cache->findLine(addr);    
  MSI_SMPCache::InvalidateReply inv_ack = writeRemoteAction(addr);
    
  if(!st || (st && !(st->isValid())) ){ //Write Miss

    //Fill the line with the new write
    fillLine(addr,MSI_MODIFIED);
    numWriteMisses++;
    return;

  }else if(st->getState() == MSI_SHARED){ //Coherence Miss
      
    st->changeStateTo(MSI_MODIFIED);
    numCoherenceMisses++;
    return;

  }else{ //Write Hit

    st->changeStateTo(MSI_MODIFIED);
    numWriteHits++;
    return;

  }

}

MSI_SMPCache::~MSI_SMPCache(){

}
