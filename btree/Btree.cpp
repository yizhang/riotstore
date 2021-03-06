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
#include "BatchBufferSP.h"
#include "BatchBufferLSRand.h"
#include "BatchBufferLG.h"
#include "BatchBufferLGRand.h"
#endif

using namespace Btree;
using namespace std;

void BTree::init(const char *fileName, int fileFlag)
{
    file = new BitmapPagedFile(fileName, fileFlag);
    size_t objsize = std::max(sizeof(DenseLeafBlock), sizeof(SparseLeafBlock));
    objsize = std::max(objsize, sizeof(InternalBlock));
    buffer = new BufferManager(file, config->btreeBufferSize, objsize);
    if (fileFlag&BitmapPagedFile::CREATE)
        buffer->allocatePageWithPID(0, headerPage);
    else
        buffer->readPage(0, headerPage);
    header = (Header*) headerPage->getImage();

    putcount = 0;
}

#ifdef USE_BATCH_BUFFER
void BTree::initBatching()
{
    switch (config->batchMethod) {
    case kFWF:
        batbuf = new BatchBufferFWF(config->batchBufferSize, this);
        break;
    case kLRU:
        batbuf = new BatchBufferLRU<BoundPageId>(config->batchBufferSize, this);
        break;
    case kLS:
        batbuf = new BatchBufferLS<BoundPageId>(config->batchBufferSize, this);
        break;
    case kSP:
        batbuf = new BatchBufferSP<BoundPageId>(config->batchBufferSize, this);
        break;
    case kLS_RAND:
        batbuf = new BatchBufferLSRand<BoundPageId>(config->batchBufferSize, this);
        break;
    case kLG:
        batbuf = new BatchBufferLG<BoundPageId>(config->batchBufferSize, this);
        break;
    case kLG_RAND:
        batbuf = new BatchBufferLGRand<BoundPageId>(config->batchBufferSize, this);
        break;
    default:
        batbuf = NULL;
    }
}
#endif

BTree::BTree(const char *fileName, Key_t endsBy,
        char leafSpType, char intSpType, bool useDenseLeaf)
{
    // The tree should have one leaf node after initialization.
    // The leaf node also serves as the root node.
    init(fileName, BitmapPagedFile::CREATE);
#ifdef USE_BATCH_BUFFER
    initBatching();
#endif

    leafSplitter = SplitterFactory<Datum_t>::createSplitter(leafSpType, useDenseLeaf);
    internalSplitter = SplitterFactory<PID_t>::createSplitter(intSpType, useDenseLeaf);

    // create first child
    buffer->allocatePage(rootPage);

    header->storageType = BTREE;
    header->endsBy = endsBy;
    header->depth = 1;
    header->nLeaves = 1;
    header->root = header->firstLeaf = rootPage->getPid();
    header->nnz = 0;
    header->leafSpType = leafSpType;
    header->intSpType = intSpType;
    header->useDenseLeaf = useDenseLeaf;
    headerPage->markDirty();
	assert(headerPage->pinCount == 1);

    //SparseLeafBlock block(rootPage, 0, header->endsBy, true);
	Block *block = (Block*) buffer->getBlockObject(rootPage, 0, header->endsBy, true,
		   	Block::kSparseLeaf, createBtreeBlock);
    onNewLeaf(block);
}

BTree::BTree(const char *fileName)
{
    init(fileName, 0);
	assert(headerPage->pinCount == 1);
    assert(header->storageType == BTREE);
#ifdef USE_BATCH_BUFFER
    initBatching();
#endif
    leafSplitter = SplitterFactory<Datum_t>::createSplitter(header->leafSpType,
            header->useDenseLeaf);
    internalSplitter = SplitterFactory<PID_t>::createSplitter(header->intSpType,
            header->useDenseLeaf);
	buffer->readPage(header->root, rootPage);
}

BTree::~BTree()
{
#ifdef USE_BATCH_BUFFER
    if (batbuf)
        delete batbuf;
#endif
    //headerPage.reset();
	headerPage->unpin();
	assert(headerPage->pinCount == 0);
	rootPage->unpin();
	assert(rootPage->pinCount == 0);
    delete buffer;
    delete file;
    delete leafSplitter;
    delete internalSplitter;
}

int BTree::search(Key_t key, Cursor &cursor)
{
    int current = cursor.current;

    for (; current>=0; --current) {
        Block *&block = cursor[current].block;
        if (block->inRange(key))
            break;
        //block->~Block();
		block->pageHandle->unpin();
		assert(block->pageHandle->pinCount >= 0);
        block = NULL;
    }
    // current should be >=0 if cursor's trace wasn't empty, and -1 if
    // otherwise

    if (current < 0) {
        // start with the root page
        rootPage->pin();
        cursor[0].block = (Block*) buffer->getBlockObject(
				rootPage, 0, header->endsBy,
                false, 0, createBtreeBlock);
        current = 0;
    }

    for (; current < header->depth-1; current++) {
        PIDBlock *pidblock = static_cast<PIDBlock*>(cursor[current].block);
        int idx;
        int ret = pidblock->search(key, idx);
        // idx is the position where the key should be inserted at.
        // To follow the child pointer, the position should be
        // decremented.
        idx -= (ret == kNotFound);
        Key_t l, u;
        PID_t child;
        PageHandle ph;
        pidblock->get(idx, l, child);
        RC_t x = buffer->readPage(child, ph);
        u = pidblock->size()==idx+1 ? pidblock->getUpperBound() : pidblock->key(idx+1);
        // load child block
        cursor[current+1].block = (Block*) buffer->getBlockObject(ph, l, u, 
                false, 0, createBtreeBlock);
		cursor[current].index = idx;
    }
	cursor.current = current;

    // already at the leaf level
    LeafBlock *leafBlock = static_cast<LeafBlock*>(cursor[current].block);
    return leafBlock->search(key, cursor[current].index);
}

#ifdef USE_BATCH_BUFFER
void BTree::locate(Key_t key, BoundPageId &pageId)
{
#ifdef DTRACE_SDT
    //RIOT_BTREE_LOCATE_BEGIN();
#endif

    // If almost all leaves are present, we can calculate pid instead of
    // accessing the disk
    // Assumptions: the B split strategy is used
    if (header->nLeaves > header->endsBy / config->denseLeafCapacity / 1.5) {
        pageId.lower = key / config->denseLeafCapacity * config->denseLeafCapacity;
        pageId.upper = pageId.lower + config->denseLeafCapacity;
        return;
    }

    PID_t pid;
    Key_t l=0, u=header->endsBy;
	rootPage->pin();
    PageHandle ph = rootPage;
    InternalBlock *block;

    for (int i=1; i<header->depth; i++) {
        // read, do not create!
        //block = new InternalBlock(ph, l, u, false);
        block = (InternalBlock*) buffer->getBlockObject(ph, l, u, false, 0,
                createBtreeBlock);
        int idx;
        // idx is the position where the key should be inserted at.
        // To follow the child pointer, the position should be
        // decremented.
        if (block->search(key, idx) == kNotFound)
            idx--;
        block->get(idx, l, pid);
        ++idx;
        u = block->size()==idx ? block->getUpperBound() : block->key(idx);
        //delete block;
        //block->~Block();
		ph->unpin();
		assert(ph->pinCount >= 0);
        if (i != header->depth -1)
			buffer->readPage(pid, ph);
    }
    pageId.lower = l;
    pageId.upper = u;
#ifdef DTRACE_SDT
    //RIOT_BTREE_LOCATE_END(header->depth);
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

    Cursor cursor;
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
    if (++putcount == 1000) {
        RIOT_BTREE_PUT();
        putcount = 0;
    }
#endif
#ifdef USE_BATCH_BUFFER
    if (batbuf) {
        batbuf->put(key, datum);
        return kOK;
    }
#endif

    Cursor cursor;
    return putHelper(key, datum, cursor);
}

// Caller should guarantee that datum is not kDefaultValue, i.e.,
// this is not a remove operation.
int BTree::putHelper(Key_t key, Datum_t datum, Cursor &cursor)
{
    cursor.key = key;  // remember this key
    int ret = search(key, cursor);
    assert(cursor.current >= 0);

    LeafBlock *block = static_cast<LeafBlock*>(cursor[cursor.current].block);
    header->nnz -= block->size();
    ret = block->put(cursor[cursor.current].index, key, datum);
    header->nnz += block->sizeWithOverflow();
    //buffer->markPageDirty(block->pageHandle);
    switch (ret) {
    case kOK:
        break;
    case kOverflow:
        split(cursor);
        break;
        //#ifndef DISABLE_DENSE_LEAF
    case kSwitchFormat:
        if (header->useDenseLeaf)
            switchFormat(&cursor[cursor.current].block);
        else
            split(cursor);
        //TODO: if needed, cursor->indices[cursor->current] should be
        //updated by calling the new block's search method
        break;
        //#endif
    default:
        Error("invalid return value %d from block->put()", ret);
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
    if (leafSplitter->split(block, &newLeafBlock, newPh, buffer)) {
        switchFormat(&cursor[cur].block);
    }
    onNewLeaf(newLeafBlock);
    block->setNextLeaf(newPh->getPid());
    //buffer->markPageDirty(block->pageHandle);
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
        PID_t newPid = newBlock->pageHandle->getPid();
        ret = parent->put(cursor[cur].index+1, newBlock->getLowerBound(), newPid);
        if (newBlock->inRange(cursor.key))
            cursor[cur].index++;
        //tempBlock->~Block(); // explicitly destruct
		tempBlock->pageHandle->unpin();
		assert(tempBlock->pageHandle->pinCount >= 0);
        if (ret != kOverflow)
            break;
        buffer->allocatePage(newPh);
        PIDBlock *newSibling;
        if (internalSplitter->split(parent, &newSibling, newPh, buffer)) {
            switchFormat(&cursor[cur].block);
        }
#ifdef DTRACE_SDT
        RIOT_BTREE_NEW_INTERNAL(newSibling->size());
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
#ifdef DTRACE_SDT
        RIOT_BTREE_NEW_INTERNAL(2);
#endif
        PageHandle newrootPage;
        buffer->allocatePage(newrootPage);  // pinned
        InternalBlock *newRoot = static_cast<InternalBlock*> (
                buffer->getBlockObject(
                newrootPage, 0, header->endsBy, true,
                Block::kInternal, createBtreeBlock));
        PID_t oldRootPid = header->root;
        newRoot->put(0, 0, oldRootPid);
        PID_t newChildPid = newPh->getPid();
        newRoot->put(1, newBlock->getLowerBound(), newChildPid);
        cursor.grow();
        cursor[0].block = newRoot;
        if (newBlock->inRange(cursor.key)) {
            cursor[0].index = 1;
        }
        else {
            cursor[0].index = 0;
        }
        header->root = newrootPage->getPid();
        header->depth++;
        headerPage->markDirty();
		rootPage->unpin();
		rootPage = newrootPage;
		rootPage->pin();
        //tempBlock->~Block();
		tempBlock->pageHandle->unpin();
		assert(tempBlock->pageHandle->pinCount >= 0);
    }
}

void BTree::print(PID_t pid, Key_t beginsAt, Key_t endsBy, int depth, 
        PrintStat *ps, int flag) const
{
    PageHandle ph;
    buffer->readPage(pid, ph);
    //Block *block = Block::create(ph, beginsAt, endsBy);
    Block *block = (Block*) buffer->getBlockObject(ph, beginsAt, endsBy, false,
            0, createBtreeBlock);

    if (flag & LSP_FULL) {
        int indent = 2*depth;
        char *buf =  new char[indent+1];
        memset(buf, ' ', indent);
        buf[indent] = '\0';
        std::cout<<buf;
        block->print();
        delete[] buf;
    }
    if (depth < header->depth-1) {
        InternalBlock *iblock = static_cast<InternalBlock*>(block);
        int num = iblock->size();
        int i;
        for (i=0; i<num-1; ++i)
            print(iblock->value(i), iblock->key(i), iblock->key(i+1), depth+1, ps, flag);
        print(iblock->value(i), iblock->key(i), iblock->getUpperBound(), depth+1, ps, flag);
        ps->internalCount++;
    }
    else {
        if (block->type() == Block::kDenseLeaf)
            ps->denseCount++;
        else if (block->type() == Block::kSparseLeaf)
            ps->sparseCount++;
        int size = block->size();
        ps->hist[size/ps->bin]++;
    }
    //delete block;
}

void BTree::print(int flag) const
{
    using namespace std;
    PrintStat ps = {0,0,0};
    if (header->useDenseLeaf)
        ps.bin = (config->denseLeafCapacity + 9) / 10;
    else 
        ps.bin = (config->sparseLeafCapacity + 9)/ 10;
    for (int i=0; i<10; ++i)
        ps.hist[i] = 0;

    cout<<"BEGIN--------------------------------------"<<endl;
    cout<<"BTree with depth "<<header->depth<<endl;
    print(header->root, 0, header->endsBy, 0, &ps, flag);
    if (flag & LSP_STAT) {
        cout<<"Dense leaves: "<<ps.denseCount<<endl
            <<"Sparse leaves: "<<ps.sparseCount<<endl
            <<"Internal nodes: "<<ps.internalCount<<endl
            <<"Total nodes: "<<ps.denseCount+ps.sparseCount+ps.internalCount<<endl
            <<"Histogram: "<<endl;
        for (int i=0; i<10; ++i)
            cout<<"  ["<<i*ps.bin<<","<<(i+1)*ps.bin-1<<")\t"<<ps.hist[i]<<endl;
    }
    if (flag & LSP_BM)
        buffer->print();
    cout<<"END----------------------------------------"<<endl;
}

int BTree::batchPut(i64 putCount, const Entry *puts)
{
    Cursor cursor;
    for (i64 i=0; i<putCount; ++i) {
        if (puts[i].key >= header->endsBy)
            continue;
#ifdef DTRACE_SDT
        if (++putcount == 1000) {
            RIOT_BTREE_PUT();
            putcount = 0;
        }
#endif
        putHelper(puts[i].key, puts[i].datum, cursor);
    }
    return AC_OK;
}

int BTree::batchPut(std::vector<Entry> &v)
{
    Cursor cursor;
    int size = v.size();
    for (int i=0; i<size; ++i) {
        if (v[i].key >= header->endsBy)
            continue;
#ifdef DTRACE_SDT
        if (++putcount == 1000) {
            RIOT_BTREE_PUT();
            putcount = 0;
        }
#endif
        putHelper(v[i].key, v[i].datum, cursor);
    }
    return AC_OK;
}

int BTree::batchGet(i64 getCount, Entry *gets)
{
    Cursor cursor;
    for (i64 i=0; i<getCount; ++i) {
        if (gets[i].key < header->endsBy) 
            getHelper(gets[i].key, *gets[i].pdatum, cursor);
        else
            *gets[i].pdatum = Block::kDefaultValue;
    }

    return AC_OK;
}

int BTree::batchGet(Key_t beginsAt, Key_t endsBy, std::vector<Entry> &v)
{
    Cursor cursor;
    search(beginsAt, cursor);
    Cursor cursor_end;
    search(endsBy-1, cursor_end); // upperbound is exclusive
    PID_t pid_end = cursor_end[cursor_end.current].block->pageHandle->getPid();

    LeafBlock *block = static_cast<LeafBlock*>(cursor[cursor.current].block);
    block->batchGet(beginsAt, endsBy, v);
    PID_t pid_last = block->pageHandle->getPid();
    PID_t pid = block->getNextLeaf();
    while(pid != INVALID_PID && pid_last != pid_end) {
        PageHandle ph;
        buffer->readPage(pid, ph);
        // we don't know the bounds, but that won't affect the batchGet op
        //block = static_cast<LeafBlock*>(pool.get(ph, 0, 0));
        block = (LeafBlock*) buffer->getBlockObject(ph, 0, 0, false, 0,
                createBtreeBlock);
        block->batchGet(beginsAt, endsBy, v);
        pid_last = pid;
        pid = block->getNextLeaf();
        //block->~Block(); // release resource
		ph->unpin();
		assert(ph->pinCount >= 0);
    }
    return kOK;
}

void BTree::flush()
{
#ifdef USE_BATCH_BUFFER
    if (batbuf)
        batbuf->flushAll();
#endif
    buffer->flushAllPages();
}

void BTree::switchFormat(Block **orig_)
{
    LeafBlock *orig = static_cast<LeafBlock*>(*orig_);
	int num = orig->sizeWithOverflow();
	Key_t *keys = new Key_t[num];
	Datum_t *vals = new Datum_t[num];
	(orig)->getRangeWithOverflow(0,num,keys,vals);
    PageHandle ph = orig->pageHandle;
    Key_t l = orig->lower, u = orig->upper;
    Block::Type t = orig->type();
    //orig->~Block();
	// WARNING: ph->unpin() is not needed because we're gonna reuse the page.
	// Instead of unpinning it and immediately pinning it, we do nothing.
	assert(ph->pinCount >= 0);
	buffer->freeBlockObject(ph);

	if (t == Block::kDenseLeaf)
		t = Block::kSparseLeaf;
	else if (t == Block::kSparseLeaf)
		t = Block::kDenseLeaf;
	else
		Error("wrong page type");
    Block *block = (Block*) buffer->getBlockObject(ph, l, u, true, t,
            createBtreeBlock);
    int numPut;
    ((LeafBlock*) block)->putRangeSorted(keys, vals, num, &numPut);
    assert(num == numPut);
    delete[] keys;
    delete[] vals;
    *orig_ = block;
}

ArrayInternalIterator *BTree::createIterator(IteratorType t, Key_t &beginsAt, Key_t &endsBy)
{
    //if (t == Dense)
    //  return new BTreeDenseIterator(beginsAt, endsBy, this);
    //else
    return NULL;
}

