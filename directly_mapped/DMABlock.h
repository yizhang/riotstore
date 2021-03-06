#ifndef DMA_BLOCK
#define DMA_BLOCK

#include "common/common.h"
#include "../common/ArrayInternalIterator.h"
#include <vector>

class DirectlyMappedArray;
/**
 * A DenseArrayBlock does not need a header for the block.  The page
 * simply stores a data array.  It stores a continuguous subrange for a
 * DirectlyMappedArray.
 */
class DMABlock
{
public:
    // header is placed at the end of the block
    struct Header {
        size_t nnz;
    } *header;

private:
    PageHandle ph;
    Key_t lowerBound;
    Key_t upperBound;

    /// A data array that corresponds to the portion of the
    /// DirectlyMappedArray in the index range [beginsAt, endsBy).
    Datum_t *data;

public:
    const static Datum_t DefaultValue;
    //const static size_t CAPACITY;

    /// Initializes the block by reading from a page image.  The index
    /// range and the default data value will be passed in---the caller,
    /// with the knowledge of the overall DirectlyMappedArray, should
    /// know the exact index range that this page is responsible for.
    /// The default data value will also be supplied by the caller.
    DMABlock(PageHandle ph, Key_t lower, Key_t upper, bool create);

    // Clears all values to zero
    void init();

    /// assume key is within range
    Datum_t get(Key_t key) const;
    void batchGet(i64 getCount, Entry *gets);
    int batchGet(Key_t beginsAt, Key_t endsBy, Datum_t *);
    int batchGet(Key_t beginsAt, Key_t endsBy, std::vector<Entry> &v);

    /// assume key is within range
    int put(Key_t key, Datum_t datum);
    int batchPut(i64 putCount, const Entry *puts);
    int batchPut(Key_t beginsAt, Key_t endsBy, Datum_t *);
    int batchPut(std::vector<Entry>::const_iterator &begin,
            std::vector<Entry>::const_iterator end);

    const PageHandle getPageHandle() const { return ph; }
    Key_t getUpperBound() const { return upperBound; }
    Key_t getLowerBound() const { return lowerBound; }
    /// assume beginsAt and endsBy are within upperBound and lowerBound
    ArrayInternalIterator* getIterator(Key_t beginsAt, Key_t endsBy);
    ArrayInternalIterator* getIterator();
};

void *createDMABlock(void *p,
        PageHandle ph_, Key_t lower, Key_t upper, bool create, int param);
#endif
