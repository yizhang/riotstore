#include "BtreeDenseLeafBlock.h"
#include "BtreeSparseBlock.h"
#include <iostream>

using namespace Btree;

//u16 DenseLeafBlock::capacity = DenseLeafBlock::initCapacity();

/*
u16 DenseLeafBlock::initCapacity()
{
	u16 cap = (PAGE_SIZE-HeaderSize)/sizeof(Datum_t);
	BtreeConfig *config = BtreeConfig::getInstance();
	if (config) {
		const char *val;
		if ((val=config->get("dleaf_cap"))) {
			cap = atoi(val);
		}
	}
	return cap;
}
*/

DenseLeafBlock::DenseLeafBlock(PageHandle ph, Key_t beginsAt, Key_t endsBy, bool create)
	:LeafBlock(ph, beginsAt, endsBy)
{
	capacity	= config->denseLeafCapacity;
	header		= (Header*) ph->getImage();
	data		= (Datum_t*)(ph->getImage()+sizeof(Header));
	// force alignment
	//if (data % sizeof(Datum_t))
	//	data += sizeof(Datum_t) - data % sizeof(Datum_t);
	i16 diff	= header->tailIndex - header->headIndex;
	if (diff < 0) diff += capacity;
	if (diff == 0 && header->nEntries > 0) diff += capacity;
	tailKey		= header->headKey + diff;
	if (create) {
		header->flag = kDenseLeaf; // flag
		header->nEntries = 0;
		header->nextLeaf = INVALID_PID;
		header->headIndex = 0;
		header->tailIndex = 0;
		header->headKey = beginsAt;
		tailKey = beginsAt;
		// Set all cells to 0.0; The correct way should be using a loop but
		// the following works on common Intel platforms.
		memset(data, 0, sizeof(Datum_t)*capacity);
	}
}

Status DenseLeafBlock::extendStoredRange(Key_t key) 
{
	if (header->nEntries == 0) {
		header->headKey = key;
		header->headIndex = 0;
		header->tailIndex = 1;
		tailKey = key+1;
        value_(0) = kDefaultValue;
		return kOK;
	}

	if (key < header->headKey) {
		// tighten the current range by trying to move tail backward
		if (tailKey-key > capacity) {
			--header->tailIndex;
			--tailKey;
			if (header->tailIndex < 0) header->tailIndex += capacity;
			while (data[header->tailIndex] == kDefaultValue) {
				--header->tailIndex;
				--tailKey;
				if (header->tailIndex < 0) header->tailIndex += capacity;
			}
			++header->tailIndex;
			++tailKey;
			if (header->tailIndex == capacity) header->tailIndex -= capacity;
		}
		if (tailKey-key <= capacity) {
			// expand the current segment
			i16 oldIndex = header->headIndex;
			header->headIndex -= (header->headKey - key);
			if (header->headIndex < 0) { // wrap-around
				header->headIndex += capacity;
				for (i16 i=header->headIndex; i<capacity; i++)
					data[i] = kDefaultValue;
				for (i16 i=0; i<oldIndex; i++)
					data[i] = kDefaultValue;
			}
			else {
				for (i16 i=header->headIndex; i<oldIndex; i++)
					data[i] = kDefaultValue;
			}
			header->headKey = key;
		}
		else { // cannot fit
			if (canSwitchFormat())
				return kSwitchFormat;
			return kOverflow;
		}
	}
	else if (key >= tailKey) {
		// tighten the current range by trying to move head forward
		if (key+1-header->headKey > capacity) {
			while (data[header->headIndex] == kDefaultValue) {
				++header->headIndex;
				++header->headKey;
				if (header->headIndex == capacity) header->headIndex -= capacity;
			}
		}
		if (key+1-header->headKey <= capacity) {
			// expand the current segment
			i16 oldIndex = header->tailIndex;
			header->tailIndex += key+1-tailKey;
			tailKey = key+1;
			if (header->tailIndex >= capacity) { // wrap-around
				header->tailIndex -= capacity;
				for (i16 i=oldIndex; i<capacity; i++)
					data[i] = kDefaultValue;
				for (i16 i=0; i<header->tailIndex; i++)
					data[i] = kDefaultValue;
			}
			else {
				for (i16 i=oldIndex; i<header->tailIndex; i++)
					data[i] = kDefaultValue;
			}
		}
		else { // cannot fit
			if (canSwitchFormat())
				return kSwitchFormat;
			return kOverflow;
		}
	}

	return kOK;
}
/*
 * Searches the current block for the specified key. If such key is
 * not stored, kNotFound is returned. Otherwise, the index of the
 * key among all (sorted) records is returned in [out] index.
 */
int DenseLeafBlock::search(Key_t key, int &index) const
{
	assert(key >= lower && key < upper);
	index = key-header->headKey;
	return (key >= header->headKey && key < tailKey) ? kOK : kNotFound;
}

//TODO: if key is outside of [head, tail) range, should we return value
//0 and signal kOK or just kNotFound?
int DenseLeafBlock::get(Key_t key, Datum_t &v) const
{
	assert(key >= lower && key < upper);
	if (key >= header->headKey && key < tailKey) {
		v = value_(key-header->headKey);
		return kOK;
	}
	return kNotFound;
}

int DenseLeafBlock::get(int index, Key_t &key, Datum_t &v) const
{
	assert(index >= 0 && index < tailKey-header->headKey);
	key = key_(index);
	v = value_(index);
	return kOK;
}

int DenseLeafBlock::put(Key_t key, const Datum_t &v)
{
	int index = key - header->headKey;
	return put(index, key, v);
}

int DenseLeafBlock::del(int index)
{
	assert(index >= 0 && index < header->nEntries);
	value_(index) = kDefaultValue;
	--header->nEntries;
	pageHandle->markDirty();
	return kOK;
}

int DenseLeafBlock::put(int index, Key_t key, const Datum_t &v)
{
	assert(key >= lower && key < upper);
	if (v == Block::kDefaultValue) {
		if (header->nEntries == 0 || key < header->headKey || key >= tailKey
				|| value_(index) == Block::kDefaultValue)
			return kOK;
		else  // deleting a value
			return del(index);
	}
			
	pageHandle->markDirty();

	Status s;
	switch(s=extendStoredRange(key)) {
	case kOK:
		header->nEntries += (value_(key-header->headKey) == Block::kDefaultValue);
		value_(key-header->headKey) = v;
		return kOK;
	case kOverflow:
	case kSwitchFormat:
		isOverflowed = true;
		overflow.key = key;
		overflow.value = v;
		overflow.index = key-header->headKey;
		return s;
	default:
		return s;
	}
}

int DenseLeafBlock::getRange(Key_t beginsAt, Key_t endsBy, Datum_t *values) const
{
	//Datum_t *dv = static_cast<Datum_t*>(values);
	DenseIterator it(this, beginsAt-header->headKey);
	DenseIterator end(this, endsBy-header->headKey);
	for(int i=0; it !=end; ++it,++i) {
		values[i] = it->datum;
	}
	return kOK;
}

int DenseLeafBlock::getRange(int beginsAt, int endsBy, Key_t *keys, Datum_t *values) const
{
	//Datum_t *dv = static_cast<Datum_t*>(values);
	SparseIterator it(this, beginsAt);
	SparseIterator end(this, endsBy);
	for(int j=0; j<endsBy-beginsAt && it !=end; ++it,++j) {
		keys[j] = it->key;
		values[j] = it->datum;
	}
	return kOK;
}

int DenseLeafBlock::batchGet(Key_t beginsAt, Key_t endsBy, std::vector<Entry> &v) const
{
	SparseKeyIterator it(this, beginsAt), end(this, endsBy);
	for(; it != end; ++it)
		v.push_back(*it);
	return kOK;
}

int DenseLeafBlock::getRangeWithOverflow(int beginsAt, int endsBy, Key_t *keys, Datum_t *values) const
{
	//Datum_t *dv = static_cast<Datum_t*>(values);
	if (isOverflowed) {
		if (overflow.index < 0) {
			if (beginsAt == 0) {
				// fill the first slot
				keys[0] = overflow.key;
				values[0] = overflow.value;
				++beginsAt;
				++keys;
				++values;
			}
			getRange(beginsAt-1,endsBy-1,keys,values);
		}
		else {
			if (endsBy == 1+(header->nEntries)) {
				// fill the last slot
				int i = endsBy-beginsAt-1;
				keys[i] = overflow.key;
				values[i] = overflow.value;
				--endsBy;
			}
			getRange(beginsAt,endsBy,keys,values);
		}
	}
	else {
		getRange(beginsAt,endsBy,keys,values);
	}

	return kOK;
}

int DenseLeafBlock::putRangeSorted(Key_t *keys, Datum_t *values, int num, int *numPut)
{
	//Datum_t *dv = static_cast<Datum_t*>(values);
	*numPut = 0;

	int ret;
	for (int i=0; i<num; ++i) {
		//Value v;
		//v.datum = dv[i];
		switch(ret=put(keys[i], values[i])) {
		case kOK:
			++(*numPut);
			break;
		default:
			return ret;
		}
	}
	return kOK;
}

// Truncate the block before the pos-th nonzero element, and set the upperbound to end
void DenseLeafBlock::truncate(int pos, Key_t end)
{
	upper = end;
	if (!isOverflowed || overflow.index > 0) {
		header->nEntries = pos;
        if (end > tailKey) end = tailKey;
        // set [end .. tailKey) to 0
        for (Key_t k = end; k < tailKey; k++)
            value_(k-header->headKey) = kDefaultValue;
        // update tailIndex and tailKey
        header->tailIndex -= tailKey - end;
		if (header->tailIndex < 0) header->tailIndex += capacity;
		tailKey = end;
		isOverflowed = false;
	}
	else { // must be that overflow.index < 0
		header->nEntries = --pos; // pos includes the overflow entry
        // set [max(headKey,end) .. tailKey) to 0
        for (Key_t k = std::max(header->headKey,end); k < tailKey; k++)
            value_(k-header->headKey) = kDefaultValue;
        // update tailIndex and tailKey
		header->tailIndex -= tailKey - end;
		if (header->tailIndex < 0) header->tailIndex += capacity;
		tailKey = end;
		if (kOK == put(overflow.key, overflow.value))
			isOverflowed = false;
		// put could return kSwitchFormat, which will be dealt by the caller
	}
	pageHandle->markDirty();
}

void DenseLeafBlock::print() const
{
	using namespace std;
	cout<<"D";
    cout<<this->pageHandle->getPid();
	cout<<"{"<<size()<<"}";
	cout<<"["<<lower<<","<<upper<<"] ";
	SparseIterator it(this, 0), end(this, size());
	for (; it != end; ++it) {
		cout<<"("<<it->key<<","<<it->datum<<")";
	}
	cout<<endl;
}
