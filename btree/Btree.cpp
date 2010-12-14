#include <iostream>
#include "lower/BitmapPagedFile.h"
#include "BtreeDenseLeafBlock.h"
#include "BtreeSparseBlock.h"
#include "Btree.h"
#include "Config.h"
#ifdef USE_BATCH_BUFFER
#include "BtreeStat.h"
#include "BatchBufferFWF.h"
#include "BatchBufferLRU.h"
#include "BatchBufferLS.h"
#include "BatchBufferLSRand.h"
#include "BatchBufferLG.h"
#include "BatchBufferLGRand.h"
#endif

using namespace Btree;
using namespace std;

void BTree::init(const char *fileName, int fileFlag)
{
	file = new BitmapPagedFile(fileName, fileFlag);
    buffer = new BufferManager(file, config->btreeBufferSize);
	if (fileFlag&BitmapPagedFile::CREATE)
		buffer->allocatePageWithPID(0, headerPage);
	else
		buffer->readPage(0, headerPage);
    header = (BtreeHeader*) buffer->getPageImage(headerPage);
}

#ifdef USE_BATCH_BUFFER
void BTree::initBatching()
{
	leafHist = new LeafHist(config->batchHistogramNum, header->endsBy);
	switch (config->batchMethod) {
	case kFWF:
		batbuf = new BatchBufferFWF(config->batchBufferSize, this);
		break;
	case kLRU:
		if (config->batchUseHistogram) 
			batbuf = new BatchBufferLRU<HistPageId>(config->batchBufferSize, this);
		else 
			batbuf = new BatchBufferLRU<BoundPageId>(config->batchBufferSize, this);
		break;
	case kLS:
		if (config->batchUseHistogram)
			batbuf = new BatchBufferLS<HistPageId>(config->batchBufferSize, this);
		else
			batbuf = new BatchBufferLS<BoundPageId>(config->batchBufferSize, this);
		break;
	case kLS_RAND:
		if (config->batchUseHistogram)
			batbuf = new BatchBufferLSRand<HistPageId>(config->batchBufferSize, this);
		else
			batbuf = new BatchBufferLSRand<BoundPageId>(config->batchBufferSize, this);
		break;
	case kLG:
		if (config->batchUseHistogram)
			batbuf = new BatchBufferLG<HistPageId>(config->batchBufferSize, this);
		else
			batbuf = new BatchBufferLG<BoundPageId>(config->batchBufferSize, this);
		break;
	case kLG_RAND:
		if (config->batchUseHistogram)
			batbuf = new BatchBufferLGRand<HistPageId>(config->batchBufferSize, this);
		else
			batbuf = new BatchBufferLGRand<BoundPageId>(config->batchBufferSize, this);
		break;
	default:
		batbuf = NULL;
	}
}
#endif

BTree::BTree(const char *fileName, Key_t endsBy,
			 LeafSplitter *leafSp, InternalSplitter *intSp
			 ): leafSplitter(leafSp), internalSplitter(intSp)
{
	// The tree should have one leaf node after initialization.
	// The leaf node also serves as the root node.
    init(fileName, BitmapPagedFile::CREATE);
    /*
<<<<<<< Updated upstream
=======

    header->endsBy = endsBy;
    header->nLeaves = 0;
    header->depth = 0;
    header->root = INVALID_PID;
    header->firstLeaf = 0;
    buffer->markPageDirty(headerPage);

    >>>>>>> Stashed changes*/
#ifdef USE_BATCH_BUFFER
	initBatching();
#endif

	upper = header->endsBy = endsBy;
	PageHandle ph;
	buffer->allocatePage(ph);
	header->depth = 1;
	header->nLeaves = 1;
	header->root = header->firstLeaf = buffer->getPID(ph);
	buffer->markPageDirty(headerPage);

	Block *block = Block::create(Block::kSparseLeaf,
			ph, buffer->getPageImage(ph), 0, header->endsBy);
	buffer->markPageDirty(ph);
	onNewLeaf(block);
	delete block;
}

BTree::BTree(const char *fileName, LeafSplitter *leafSp,
			 InternalSplitter *intSp
			 ): leafSplitter(leafSp), internalSplitter(intSp)
{
    init(fileName, 0);
#ifdef USE_BATCH_BUFFER
	initBatching();
#endif

}

BTree::~BTree()
{
#ifdef USE_BATCH_BUFFER
	if (leafHist) {
		//leafHist->print();
		delete leafHist;
	}
	if (batbuf)
		delete batbuf;
#endif
    buffer->unpinPage(headerPage);
    delete buffer;
    delete file;
}

int BTree::search(Key_t key, Cursor &cursor)
{
	int &current = cursor.current;

	for (; current>=0; --current) {
		Block *&block = cursor[current].block;
		if (block->inRange(key))
			break;
		buffer->unpinPage(block->pageHandle);
		delete block;
		block = NULL;
	}
	// current should be >=0 if cursor's trace wasn't empty, and -1 if
	// otherwise

	if (current < 0) {
		// start with the root page
		PageHandle ph;
		buffer->readPage(header->root, ph);
		cursor[0].block = Block::create(ph, buffer->getPageImage(ph), 0, header->endsBy);
		current = 0;
	}

    for (; current < header->depth-1; current++) {
		PIDBlock *pidblock = static_cast<PIDBlock*>(cursor[current].block);
        int &idx = cursor[current].index;
        int ret = pidblock->search(key, idx);
        // idx is the position where the key should be inserted at.
		// To follow the child pointer, the position should be
		// decremented.
        if (ret == kNotFound)
            --idx;
        Key_t l, u;
        PID_t child;
		PageHandle ph;
        pidblock->get(idx, l, child);
        buffer->readPage(child, ph);
        u = pidblock->size()==idx+1 ? pidblock->getUpperBound() : pidblock->key(idx+1);
        // load child block
        cursor[current+1].block = Block::create(ph, buffer->getPageImage(ph), l, u);
    }

    // already at the leaf level
	LeafBlock *leafBlock = static_cast<LeafBlock*>(cursor[current].block);
    return leafBlock->search(key, cursor[current].index);
}

#ifdef USE_BATCH_BUFFER
void BTree::locate(Key_t key, HistPageId &pageId)
{
	leafHist->locate(key, pageId);
}

void BTree::locate(Key_t key, BoundPageId &pageId)
{
  //<<<<<<< Updated upstream
#ifdef DTRACE_SDT
	RIOT_BTREE_LOCATE_BEGIN();
#endif

  /*=======
	// If tree is empty, assume every key goes into page 1. Thus at
	// initial stage, all requests in the buffer will have the same
	// destination and will be flushed together. Returning 1 here
	// won't affect future runs.
	Key_t l,u;
    if (header->depth == 0) {
		//pid = 1;
		l = 0;
		u = header->endsBy;
		pageId.lower = l;
		pageId.upper = u;
		return;
    }
    >>>>>>> Stashed changes*/

	PID_t pid = header->root;
	Key_t l=0, u=header->endsBy;
	PageHandle ph;
	InternalBlock *block;

    for (int i=1; i<header->depth; i++) {
		buffer->readPage(pid, ph);
		// read, do not creat!
		block = new InternalBlock(ph, buffer->getPageImage(ph), l, u, false);
        int idx;
        // idx is the position where the key should be inserted at.
		// To follow the child pointer, the position should be
		// decremented.
        if (block->search(key, idx) == kNotFound)
            idx--;
		block->get(idx, l, pid);
		++idx;
        u = block->size()==idx ? block->getUpperBound() : block->key(idx);
        //u = block->key(idx+1);
		buffer->unpinPage(ph);
		delete block;
    }
	pageId.lower = l;
	pageId.upper = u;
#ifdef DTRACE_SDT
	RIOT_BTREE_LOCATE_END(header->depth);
#endif
}
#endif

int BTree::get(const Key_t &key, Datum_t &datum)
{
	if (key >= header->endsBy) {
		datum = Block::kDefaultValue;
		return kOutOfBound;
	}

#ifdef USE_BATCH_BUFFER
	if (batbuf && batbuf->find(key, datum))
		return kOK;
#endif
	
    Cursor cursor(buffer);
	return getHelper(key, datum, cursor);
}

int BTree::getHelper(Key_t key, Datum_t &datum, Cursor &cursor)
{
    if (search(key, cursor) != kOK) {
        datum = Block::kDefaultValue;
        return kOK;
    }
    LeafBlock *block = static_cast<LeafBlock*>(cursor[cursor.current].block);
    return block->get(cursor[cursor.current].index, key, datum);
}

int BTree::put(const Key_t &key, const Datum_t &datum)
{
	if (key >= header->endsBy)
		return kOutOfBound;

#ifdef DTRACE_SDT
	RIOT_BTREE_PUT();
#endif
#ifdef USE_BATCH_BUFFER
	if (batbuf) {
		batbuf->put(key, datum);
		return kOK;
	}
#endif
	
    Cursor cursor(buffer);
	return putHelper(key, datum, cursor);
}



/*
 * chunkSize refers to the maximum buffer size available
 * maxRec refers to the max # of records to retrieve at each run
 */
/*
int BTree::batchPut(i64 putCount, const KVPair_t *puts)
{
   Cursor cursor(buffer);
   int ret = -1;
   for (int i=0; i<putCount; i++){
       ret = search(puts[i].key, cursor);
       putHelper(puts[i].key, *(puts[i].datum), cursor);
   }
   delete[] puts;
   return ret;
}
*/

// Caller should guarantee that datum is not kDefaultValue, i.e.,
// this is not a remove operation.
int BTree::putHelper(Key_t key, Datum_t datum, Cursor &cursor)
{
	cursor.key = key;  // remember this key
    int ret = search(key, cursor);
	assert(cursor.current >= 0);

    LeafBlock *block = static_cast<LeafBlock*>(cursor[cursor.current].block);
    ret = block->put(cursor[cursor.current].index, key, datum);
    buffer->markPageDirty(block->pageHandle);
    switch (ret) {
	case kOverflow:
        split(cursor);
		break;
#ifndef DISABLE_DENSE_LEAF
	case kSwitchFormat:
		cursor[cursor.current].block = block->switchFormat();
		//TODO: if needed, cursor->indices[cursor->current] should be
		//updated by calling the new block's search method
		delete block;
		break;
#endif
	default:
		;
    }
    return ret;
}

void BTree::split(Cursor &cursor)
{
    PageHandle newPh;
    int cur = cursor.current;
    LeafBlock *block = static_cast<LeafBlock*>(cursor[cur].block);
	LeafBlock *newLeafBlock;
    buffer->allocatePage(newPh);
    if (leafSplitter->split(&block, &newLeafBlock, newPh, buffer->getPageImage(newPh))) {
		cursor[cur].block = block;
	}
	onNewLeaf(newLeafBlock);
    block->setNextLeaf(buffer->getPID(newPh));
    buffer->markPageDirty(block->pageHandle);
    int ret = kOverflow;
	Block *newBlock = newLeafBlock;
	Block *tempBlock = newBlock;
	// If the inserted key ends up in the new block
	if (newLeafBlock->inRange(cursor.key)) {
		// the original block won't be in the trace anymore
		// so mark it as temp and it will be deleted shortly
		tempBlock = block;
		// store the new block in the trace
		cursor[cur].block = newLeafBlock;
		cursor[cur].index -= block->size();
	}
    for (cur--; cur>=0; cur--) {
        PIDBlock *parent = static_cast<InternalBlock*>(cursor[cur].block);
        PID_t newPid = buffer->getPID(newBlock->pageHandle);
        ret = parent->put(cursor[cur].index+1, newBlock->getLowerBound(), newPid);
		//assert(indexTemp == cursor[cur].index+1);
		if (newBlock->inRange(cursor.key))
			cursor[cur].index++;
        buffer->markPageDirty(parent->pageHandle);
        buffer->unpinPage(tempBlock->pageHandle);
        delete tempBlock;
        if (ret != kOverflow)
            break;
        buffer->allocatePage(newPh);
		PIDBlock *newSibling;
        if (internalSplitter->split(&parent, &newSibling, newPh, buffer->getPageImage(newPh))) {
			cursor[cur].block = parent;
		}
#ifdef DTRACE_SDT
		RIOT_BTREE_SPLIT_INTERNAL();
#endif
		newBlock = newSibling;
		tempBlock = newSibling;
		if (newSibling->inRange(cursor.key)) {
			tempBlock = parent;
			cursor[cur].block = newSibling;
			cursor[cur].index -= parent->size();
		}
    }
    if (ret == kOverflow) {
        // overflow has propagated to the root
        PageHandle rootPh;
        buffer->allocatePage(rootPh);
        InternalBlock *newRoot = new InternalBlock(rootPh, buffer->getPageImage(rootPh), 0,
												   header->endsBy, true);
        PID_t oldRootPid = header->root;
		//int indexTemp;
        newRoot->put(0, 0, oldRootPid);
        PID_t newChildPid = buffer->getPID(newPh);
        newRoot->put(1, newBlock->getLowerBound(), newChildPid);
		cursor.grow();
		cursor[0].block = newRoot;
		if (newBlock->inRange(cursor.key)) {
			cursor[0].index = 1;
		}
		else {
			cursor[0].index = 0;
		}
        header->root = buffer->getPID(rootPh);
        header->depth++;
        buffer->markPageDirty(headerPage);
        buffer->unpinPage(tempBlock->pageHandle);
        delete tempBlock;
    }
}

void BTree::print(PID_t pid, Key_t beginsAt, Key_t endsBy, int depth)
{
	int indent = 2*depth;
	char buf[indent+1];
	memset(buf, ' ', indent);
	buf[indent] = '\0';
	
	PageHandle ph;
	buffer->readPage(pid, ph);
	Block *block = Block::create(ph, buffer->getPageImage(ph), beginsAt, endsBy);
	std::cout<<buf;
	block->print();
	if (depth < header->depth-1) {
		InternalBlock *iblock = static_cast<InternalBlock*>(block);
		int num = iblock->size();
		int i;
		for (i=0; i<num-1; ++i)
			print(iblock->value(i), iblock->key(i), iblock->key(i+1), depth+1);
		print(iblock->value(i), iblock->key(i), iblock->getUpperBound(), depth+1);
	}
	buffer->unpinPage(ph);
	delete block;
}

void BTree::print()
{
    using namespace std;
    cout<<"BEGIN--------------------------------------"<<endl;
	cout<<"BTree with depth "<<header->depth<<endl;
	//if (header->depth > 0)
		print(header->root, 0, header->endsBy, 0);
    cout<<"END----------------------------------------"<<endl;
}

int BTree::batchPut(i64 putCount, const KVPair_t *puts)
{
	Cursor cursor(buffer);
	for (i64 i=0; i<putCount; ++i) {
		if (puts[i].key >= header->endsBy)
			continue;
		putHelper(puts[i].key, *puts[i].datum, cursor);
	}
	return AC_OK;
}

int BTree::batchGet(i64 getCount, KVPair_t *gets)
{
	Cursor cursor(buffer);
	Key_t key;
	int status;
	for (i64 i=0; i<getCount; ++i) {
		if (gets[i].key < header->endsBy) 
			getHelper(gets[i].key, *gets[i].datum, cursor);
		else
			*gets[i].datum = Block::kDefaultValue;
	}

	return AC_OK;
}

ArrayInternalIterator *BTree::createIterator(IteratorType t, Key_t &beginsAt, Key_t &endsBy)
{
    //if (t == Dense)
	//  return new BTreeDenseIterator(beginsAt, endsBy, this);
    //else
        return NULL;
}

