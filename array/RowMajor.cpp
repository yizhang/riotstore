
#include <assert.h>
#include <math.h>
#include <string.h>
#include "RowMajor.h"

RowMajor::RowMajor(u8 nDims, const i64* coords)
{
   assert(nDims != 0);
   for (int k = 0; k < nDims; k++)
      assert(coords[k] > 0); // cannot have dimension length of 0

   this->nDims = nDims;
   dimSizes = new i64[nDims];
   memcpy(dimSizes, coords, nDims*sizeof(i64));
}

RowMajor::RowMajor(const MDCoord &coord)
{
   assert(coord.nDims != 0);
   for (int k = 0; k < coord.nDims; k++)
      assert(coord.coords[k] > 0); // cannot have dimension length of 0

   nDims = coord.nDims;
   dimSizes = new i64[nDims];
   memcpy(dimSizes, coord.coords, nDims*sizeof(i64));
}

RowMajor::~RowMajor()
{
   delete[] dimSizes;
}

Key_t RowMajor::linearize(const MDCoord &coord)
{
   assert(coord.nDims == nDims);

   Key_t key = 0;
   for (int k = 0; k < nDims; k++)
   {
      assert(coord.coords[k] < dimSizes[k]);
      key = key*dimSizes[k] + coord.coords[k];
   }
   return key;
}

MDCoord RowMajor::unlinearize(Key_t key)
{
   i64 coords[nDims];
   for (int k = nDims - 1; k >= 0; k--)
   {
      coords[k] = key % dimSizes[k];
      key /= dimSizes[k];
   }
   return MDCoord(coords, nDims);
}

MDCoord RowMajor::move(const MDCoord &from, KeyDiff_t diff)
{
   assert(from.nDims == nDims);
   MDCoord to(from);
   to.coords[nDims - 1] += diff;
   for (int k = nDims - 1; k >= 0; k--)
   {
      if (to.coords[k] < dimSizes[k]) // carry-over done propogating
         break;
      if (k > 0)
         to.coords[k-1] += to.coords[k]/dimSizes[k];
      to.coords[k] %= dimSizes[k];
   }
   return to;
}

RowMajor* RowMajor::clone()
{
   return new RowMajor(nDims, dimSizes);
}
