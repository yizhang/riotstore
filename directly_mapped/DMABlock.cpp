#include <string.h>
#include "lower/PageRec.h"
#include "DMABlock.h"
#include "DirectlyMappedArray.h"
#include "DABIterator.h"

const Datum_t DMABlock::DefaultValue = 0.0;
//const size_t DMABlock::CAPACITY = (PAGE_SIZE-sizeof(Header))/sizeof(Datum_t);

void *createDMABlock(void *p,
        PageHandle ph_, Key_t lower, Key_t upper, bool create, int)
{
    return new (p) DMABlock(ph_, lower, upper, create);
}

DMABlock::DMABlock(PageHandle ph_, Key_t lower, Key_t upper, bool create)
    : ph(ph_), lowerBound(lower), upperBound(upper)
{
    // header is at the end of the block
    this->header = (Header*) (((char*)ph->getImage())+PAGE_SIZE-sizeof(Header));
    this->data = (Datum_t *) ph->getImage();
    if (create)
        init();
}

void DMABlock::init()
{
	//for (size_t i=0; i<CAPACITY; ++i)
	//	data[i] = DefaultValue;
    memset(data, 0, sizeof(Datum_t) * config->dmaBlockCapacity);
    header->nnz = 0;
}

/// assume key is within range
Datum_t DMABlock::get(Key_t key) const  
{
   return *(data + (key - this->lowerBound));
}

void DMABlock::batchGet(i64 getCount, Entry *gets)
{
    for (i64 i = 0; i < getCount; i++)
    {
        *(gets[i].pdatum) = *(data + (gets[i].key - this->lowerBound));
    }
}

int DMABlock::batchGet(Key_t beginsAt, Key_t endsBy, Datum_t *p)
{
    //TODO: should return the number of non-zeros
	Key_t b = std::max(beginsAt, this->lowerBound) - this->lowerBound;
	Key_t e = std::min(endsBy, this->upperBound) - this->lowerBound;
    int count = int(e - b);
	for (int i = 0; i < count; ++i)
        p[i] = data[b+i];
    return count;
}

int DMABlock::batchGet(Key_t beginsAt, Key_t endsBy, std::vector<Entry> &v)
{
	Key_t b = std::max(beginsAt, this->lowerBound) - this->lowerBound;
	Key_t e = std::min(endsBy, this->upperBound) - this->lowerBound;
    int count = 0;
	for (Key_t k = b; k < e; ++k)
		if (data[k] != DefaultValue) {
            count++;
			v.push_back(Entry(k+this->lowerBound, data[k]));
        }
    return count;
}

/// assume key is within range
int DMABlock::put(Key_t key, Datum_t datum)  
{
    int index = int(key-lowerBound);
    if (datum == data[index])
        return 0;
    int ret = 0;
    if (datum == DefaultValue && data[index] != DefaultValue)
        ret = -1;
    else if (datum != DefaultValue && data[index] == DefaultValue)
        ret = 1;
    data[index] = datum;
    header->nnz += ret;
    ph->markDirty();
    return ret;
}
      
int DMABlock::batchPut(i64 putCount, const Entry *puts)
{
    int index;
    int ret = 0;
    for (i64 i = 0; i < putCount; i++)
    {
        index = puts[i].key-lowerBound;
        if (puts[i].datum == data[index])
            continue;
        if (puts[i].datum == DefaultValue && data[index] != DefaultValue)
            ret--;
        else if (puts[i].datum != DefaultValue && data[index] == DefaultValue)
            ret++;
        data[index] = puts[i].datum;
    }
    header->nnz += ret;
    ph->markDirty();
    return ret;
}

int DMABlock::batchPut(std::vector<Entry>::const_iterator &begin, 
        std::vector<Entry>::const_iterator end)
{
    int nPut = 0;
    int index;
    for (; begin != end && begin->key < upperBound; ++begin) {
        index = int(begin->key - lowerBound);
        if (begin->datum == data[index])
            continue;
        if (begin->datum == DefaultValue && data[index] != DefaultValue)
            nPut--;
        else if (begin->datum != DefaultValue && data[index] == DefaultValue)
            nPut++;
        data[index] = begin->datum;
    }
    header->nnz += nPut;
    ph->markDirty();
    return nPut;
}

int DMABlock::batchPut(Key_t beginsAt, Key_t endsBy, Datum_t *p)
{
    //TODO: should return the number of non-zeros
	Key_t b = std::max(beginsAt, this->lowerBound) - this->lowerBound;
	Key_t e = std::min(endsBy, this->upperBound) - this->lowerBound;
    int count = int(e - b);
	for (int i = 0; i < count; ++i)
        data[b+i] = p[i];
    return count;
}

/// assume beginsAt and endsBy are within upperBound and lowerBound
ArrayInternalIterator *DMABlock::getIterator(Key_t beginsAt, Key_t endsBy) 
{
   return new DABIterator(data+(beginsAt-lowerBound), beginsAt, endsBy);
}

ArrayInternalIterator *DMABlock::getIterator() 
{
   return new DABIterator(data, lowerBound, upperBound);
}

