#include <gtest/gtest.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <map>

#include "../MDCoord.h"
#include "../BlockBased.h"
#include "../RowMajor.h"
#include "../ColMajor.h"
#include "../MDArray.h"

using namespace std;

TEST(BlockBased, Linearize)
{
	i64 arrayDims[] = {5L, 7L};
	i64 rows = arrayDims[0];
	i64 cols = arrayDims[1];
    i64 blockDims[] = {2L, 3L};
    u8 blockOrders[] = {0, 1};
    u8 microOrders[] = {0, 1};
    MDCoord<2> dim(arrayDims);
    BlockBased<2> *block = new BlockBased<2>(arrayDims, blockDims,
                                          blockOrders, microOrders);
    for (int i=0; i<rows; i++) {
        for (int j=0; j<cols; j++) {
            MDCoord<2> c(i,j);
            cout<<block->linearize(c)<<"\t";
           }
        cout<<endl;
    }
}

TEST(BlockBased, Unlinearize)
{
	i64 arrayDims[] = {4L, 6L};
	i64 rows = arrayDims[0];
	i64 cols = arrayDims[1];
    i64 blockDims[] = {2L, 3L};
    u8 blockOrders[] = {0, 1};
    u8 microOrders[] = {0, 1};
    MDCoord<2> dim(arrayDims);
    BlockBased<2> *block = new BlockBased<2>(arrayDims, blockDims,
                                          blockOrders, microOrders);
	i64 count = rows*cols;
    for (i64 i=0; i<count; i++) {
            cout<<block->unlinearize(i).toString()<<"\t";
	}
	cout<<endl;
}

TEST(BlockBased, Inverse)
{
	i64 arrayDims[] = {5L, 7L};
	i64 rows = arrayDims[0];
	i64 cols = arrayDims[1];
    i64 blockDims[] = {2L, 3L};
    u8 blockOrders[] = {0, 1};
    u8 microOrders[] = {0, 1};
    MDCoord<2> dim(arrayDims);
    BlockBased<2> *block = new BlockBased<2>(arrayDims, blockDims,
                                          blockOrders, microOrders);
    for (int i=0; i<rows; i++) {
        for (int j=0; j<cols; j++) {
            MDCoord<2> c(i,j);
            ASSERT_TRUE(block->unlinearize(block->linearize(c))==c);
           }
    }
}

TEST(BlockBased, Move)
{
	i64 arrayDims[] = {15L, 17L};
	i64 rows = arrayDims[0];
	i64 cols = arrayDims[1];
    i64 blockDims[] = {3L, 4L};
    u8 blockOrders[] = {0, 1};
    u8 microOrders[] = {0, 1};
    MDCoord<2> dim(arrayDims);
    BlockBased<2> *block = new BlockBased<2>(arrayDims, blockDims,
                                          blockOrders, microOrders);
    for (int i=0; i<rows; i++) {
        for (int j=0; j<cols; j++) {
            MDCoord<2> c(i,j);
			Key_t kc = block->linearize(c);
			for (int u=0; u<rows; u++) {
				for (int v=0; v<cols; v++) {
					MDCoord<2> d(u,v);
					Key_t kd = block->linearize(d);
					KeyDiff_t diff = kd-kc;
					ASSERT_EQ(d, block->move(c, diff));
				}
			}
		}
    }
}
