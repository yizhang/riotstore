#include "SparseMatrix.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

using namespace std;

cholmod_common *SparseMatrix::chmCommon = SparseMatrix::initCholmodCommon();

cholmod_common *SparseMatrix::initCholmodCommon()
{
    cholmod_common *comm = new cholmod_common;
    cholmod_start(comm);
    return comm;
}

SparseMatrix::SparseMatrix(std::vector<Element> &elements, const Coord &begin,
        const Coord &end, bool inColMajor)
{
    if (!inColMajor)
        std::sort(elements.begin(), elements.end());
	int size = elements.size();
    sp = cholmod_allocate_sparse(
            end[0] - begin[0] + 1,
            end[1] - begin[1] + 1,
            size, // nzmax
            1, // sorted
            1, // packed
            0, // stype, unsymmetric
            CHOLMOD_REAL,
            chmCommon);
    int *pa = (int*) (sp)->p;
    int *ia = (int*) (sp)->i;
    double *xa = (double*) (sp)->x;

    // Note: There can be duplicate coordinates in elements. In such a case,
    // we'll add the values up for the duplicates, as matlab does.
    int ncols = end[1] - begin[1] + 1;
    int i = 0, j = 0;
    for (int col = 0; col <= ncols; ++col) {
        pa[col] = i;
        int lastRow = -1;
        while (j < size && elements[j].coord[1] - begin[1] == col) {
            if (lastRow == elements[j].coord[0] - begin[0]) {
                xa[i-1] += elements[j].datum;
            }
            else {
                ia[i] = lastRow = elements[j].coord[0] - begin[0];
                xa[i] = elements[j].datum;
                i++;
            }
            j++;
        }
    }
}

SparseMatrix::SparseMatrix(cholmod_sparse *p) : sp(p)
{
}

SparseMatrix::SparseMatrix(int nrow, int ncol)
{
    sp = cholmod_allocate_sparse(nrow, ncol, 10, 1, 1, 0, CHOLMOD_REAL,
            chmCommon);
}

SparseMatrix::SparseMatrix(const SparseMatrix &other)
{
    sp = cholmod_copy_sparse(other.sp, chmCommon);
}

SparseMatrix::~SparseMatrix()
{
    cholmod_free_sparse(&sp, chmCommon);
}

SparseMatrix SparseMatrix::operator+(const SparseMatrix &other) const
{
    double alpha[] = {1, 0};
    double beta[] = {1, 0};
    return SparseMatrix(cholmod_add(
                sp, 
                other.sp,
                alpha, beta,
                1 /* values */,
                1 /* sorted */,
                chmCommon));
}

SparseMatrix SparseMatrix::operator*(const SparseMatrix &other) const
{
    return SparseMatrix(cholmod_ssmult(
                sp,
                other.sp,
                0, // stype: unsymmetric
                1, // compute values instead of pattern only
                1, // sorted columns
                chmCommon));
}

SparseMatrix &SparseMatrix::operator=(const SparseMatrix &other)
{
    if (this != &other) {
        cholmod_free_sparse(&sp, chmCommon);
        sp = cholmod_copy_sparse(other.sp, chmCommon);
    }
    return *this;
}

SparseMatrix &SparseMatrix::operator+=(const SparseMatrix &other)
{
    // safe even if &other == this
    double alpha[] = {1, 0};
    double beta[] = {1, 0};
    cholmod_sparse *temp = cholmod_add(
            sp, 
            other.sp,
            alpha, beta,
            1 /* values */,
            1 /* sorted */,
            chmCommon);
    cholmod_free_sparse(&sp, chmCommon);
    sp = temp;
    return *this;
}

double SparseMatrix::operator() (int i, int j) const
{
    int ncol = (int) sp->ncol;
    if (j >=0 && j < ncol) {
        int *p = (int*) sp->p;
        int *r = (int*) sp->i;
        double *x = (double*) sp->x;
        int *q;
        if ((q = std::find(r+p[j], r+p[j+1], i)) != r+p[j+1])
            return x[q-r];
    }
    return 0.0;
}

void SparseMatrix::swap(SparseMatrix &other)
{
    cholmod_sparse *temp = sp;
    sp = other.sp;
    other.sp = temp;
}

ostream & operator<< (ostream &out, const SparseMatrix &sm)
{
    int ncol = (int) sm.sp->ncol;
    int nrow = (int) sm.sp->nrow;
    int *p = (int*) sm.sp->p;
    int *r = (int*) sm.sp->i;
    double *x = (double*) sm.sp->x;
    // have a buffer for each row
    stringstream *ss = new stringstream[nrow];
    for (int i=0; i<nrow; ++i) {
	ss[i].setf(ios::right, ios::adjustfield);
    }
    for (int j=0; j<ncol; ++j) {
	int lastRow = -1;
	for (int i=p[j]; i<p[j+1]; ++i) {
	    // column j, row r[i], value x[i]
	    // fill rows [lastRow+1, r[i]-1] with 0
	    for (int k=lastRow+1; k<r[i]; ++k)
		ss[k]<<setw(6)<<0<<" ";
	    lastRow = r[i];
	    ss[r[i]]<<setw(6)<<x[i]<<" ";
	}
	for (int k=lastRow+1; k<nrow; ++k)
	    ss[k]<<setw(6)<<0<<" ";
    }
    for(int i=0; i<nrow; ++i)
	out<<ss[i].str()<<endl;
    delete[] ss;
    return out;
}

void SparseMatrix::print() const
{
    cout<<*this<<endl;
}
