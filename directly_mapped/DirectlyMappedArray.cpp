#include <iostream>
#include "../lower/LRUPageReplacer.h"
#include "../lower/BitmapPagedFile.h"
#include "DMADenseIterator.h"
#include "DMASparseIterator.h"
#include "DirectlyMappedArray.h"

int DirectlyMappedArray::BufferSize = DirectlyMappedArray::getBufferSize();
int DirectlyMappedArray::getBufferSize() {
    int n = 5000;
    if (getenv("RIOT_DMA_BUFFER") != NULL) {
        n = atoi(getenv("RIOT_DMA_BUFFER"));
    }
    debug("Using buffer size %dKB", n*4);
    return n;
}


/// If numElements > 0, create a new array; otherwise read from disk.
/// Whether file exists is ignored.
DirectlyMappedArray::DirectlyMappedArray(const char* fileName, uint32_t numElements) 
{
   if (numElements > 0)		// new array to be created
   {
      remove(fileName);
      file = new BitmapPagedFile(fileName, BitmapPagedFile::F_CREATE);
      buffer = new BufferManager(file, BufferSize); 
      this->numElements = numElements;
      PageHandle ph;
      assert(RC_OK == buffer->allocatePageWithPID(0, ph));
      // page is already marked dirty
      DirectlyMappedArrayHeader* header = (DirectlyMappedArrayHeader*)
          (buffer->getPageImage(ph));
      header->numElements = numElements;
      Datum_t x;
      header->dataType = GetDataType(x);
      header->ch = 'z';
      buffer->markPageDirty(ph);
      buffer->unpinPage(ph);
   }
   else 						// existing array
   {
      if (access(fileName, F_OK) != 0)
         throw ("File for array does not exist.");
      file = new BitmapPagedFile(fileName, BitmapPagedFile::F_NO_CREATE);
      buffer = new BufferManager(file, BufferSize); 
      PageHandle ph;
      buffer->readPage(0, ph);
      DirectlyMappedArrayHeader* header = (DirectlyMappedArrayHeader*)
          (buffer->getPageImage(ph));
      buffer->unpinPage(ph);
      this->numElements = header->numElements;
      Datum_t x;
      assert(IsSameDataType(x, header->dataType));
   }
}

   /// should delete buffer first, because flushAll() is called in
   /// buffer's destructor, at which time file is updated.
DirectlyMappedArray::~DirectlyMappedArray() 
{
   delete buffer;
   delete file;
}

int DirectlyMappedArray::get(const Key_t &key, Datum_t &datum) 
{
   if (key < 0 || numElements <= key) 
   {
      return AC_OutOfRange;
   }

   PID_t pid;
   DenseArrayBlock *dab;
   RC_t ret;
   findPage(key, &(pid));
   if ((ret=readBlock(pid, &dab)) != RC_OK) {
       if (ret == RC_NotAllocated) {
           datum = DefaultValue;
           return AC_OK;
       }
       else {
           Error("cannot read page %d", pid);
           exit(ret);
       }
   }

   datum = dab->get(key);
   buffer->unpinPage(dab->getPageHandle());
   delete dab;
   return AC_OK;
}

int DirectlyMappedArray::put(const Key_t &key, const Datum_t &datum) 
{
   PID_t pid;
   DenseArrayBlock *dab;
   findPage(key, &(pid));
   if (readBlock(pid, &dab) != RC_OK && newBlock(pid, &dab) != RC_OK) {
       Error("cannot read/allocate page %d",pid);
       exit(1);
   }   
   
   dab->put(key, datum);
   buffer->markPageDirty(dab->getPageHandle());
   buffer->unpinPage(dab->getPageHandle());
   delete dab;
   return AC_OK;
}

ArrayInternalIterator *DirectlyMappedArray::createIterator(IteratorType t, Key_t &beginsAt, Key_t &endsBy)
{
   if (t == Dense)
      return new DMADenseIterator(beginsAt, endsBy, this);
   else if (t == Sparse)
      return NULL;
   return NULL;
   // return new DMASparseIterator(beginsAt, endsBy, this);
}

void DirectlyMappedArray::findPage(const Key_t &key, PID_t *pid) 
{
   Key_t CAPACITY = DenseArrayBlock::CAPACITY;
   *pid = key/CAPACITY + 1;
}

RC_t DirectlyMappedArray::readBlock(PID_t pid, DenseArrayBlock** block) 
{
   PageHandle ph;
   RC_t ret;
   if ((ret=buffer->readPage(pid, ph)) != RC_OK)
      return ret;
   Key_t CAPACITY = DenseArrayBlock::CAPACITY;
   *block = new DenseArrayBlock(this, ph, CAPACITY*(pid-1), CAPACITY*pid);
   return RC_OK;
}

RC_t DirectlyMappedArray::readNextBlock(PageHandle ph, DenseArrayBlock** block) 
{
    PID_t pid = buffer->getPID(ph);
    if (DenseArrayBlock::CAPACITY*pid >= numElements)
        return RC_OutOfRange;
    RC_t ret;
    if ((ret=buffer->readPage(pid+1, ph)) != RC_OK)
        return ret;
   Key_t CAPACITY = DenseArrayBlock::CAPACITY;
   *block = new DenseArrayBlock(this, ph, CAPACITY*(pid), CAPACITY*(pid+1));
   return RC_OK;
}

RC_t DirectlyMappedArray::newBlock(PID_t pid, DenseArrayBlock** block) 
{
   PageHandle ph;
   RC_t ret;
   if ((ret=buffer->allocatePageWithPID(pid, ph)) != RC_OK)
      return ret;
   Key_t CAPACITY = DenseArrayBlock::CAPACITY;
   *block = new DenseArrayBlock(this, ph, CAPACITY*(pid-1), CAPACITY*pid);
   return RC_OK;
}

RC_t DirectlyMappedArray::releaseBlock(DenseArrayBlock* block, bool dirty) 
{
    if (dirty) {
        buffer->markPageDirty(block->getPageHandle());
    }
    return buffer->unpinPage((block->getPageHandle()));
}

uint32_t DirectlyMappedArray::getLowerBound() 
{
   return 0;
}

uint32_t DirectlyMappedArray::getUpperBound() 
{
   return numElements;
}

uint32_t DirectlyMappedArray::getPageLowerBound(PID_t pid) 
{
    return DenseArrayBlock::CAPACITY*(pid-1);
}

uint32_t DirectlyMappedArray::getPageUpperBound(PID_t pid) 
{
    return DenseArrayBlock::CAPACITY*(pid);
}

void *DirectlyMappedArray::getPageImage(PageHandle ph)
{
    return buffer->getPageImage(ph);
}
