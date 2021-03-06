#ifndef MMPARSER_H
#define MMPARSER_H

#include <string>
#include <sstream>
#include "Parser.h"

class MMParser : public Parser<2>
{
public:
    MMParser(const char *filename) : in(filename), cur(0)
    {
		using namespace std;
		string s;
		in>>s;
		assert(s.compare("%%MatrixMarket")==0);
		in>>s;
		assert(s.compare("matrix")==0);
		in>>s;
		assert(s.compare("coordinate")==0);
		in>>s;
        if (s.compare("integer")==0)
            type = INTEGER;
        else if (s.compare("real")==0)
            type = REAL;
        else if (s.compare("pattern")==0)
            type = PATTERN;
        else {
            Error("unsupported MM file type");
            assert(false);
        }
		in>>s;
		assert(s.compare("general")==0 || s.compare("symmetric")==0);
        in.ignore(INT_MAX, '\n'); // go to next line
        
		char buf[256];
		do {
			in.getline(buf, 256);
		} while (buf[0]=='%');
        stringstream ss(buf);
		ss>>dim_[0]>>dim_[1];
		ss>>nnz; 
        nElem = nnz;
    }

    ~MMParser()
    {
		in.close();
    }

    int parse(int size, MDCoord<2> *coords, Datum_t *data)
    {
		int limit = std::min(size, nnz-cur);
		int i = 0;
        if (type == REAL || type == INTEGER) {
            for (; i < limit; ++i) {
                in >> coords[i][0] >> coords[i][1] >> data[i];
                coords[i][0]--;
                coords[i][1]--;
                cur++;
            }
        }
        else if (type == PATTERN) {
            for (; i < limit; ++i) {
                in >> coords[i][0] >> coords[i][1];
                data[i] = 1;
                coords[i][0]--;
                coords[i][1]--;
                cur++;
            }
        }

		return i;
    }

private:
    enum Type {
        INTEGER,
        REAL,
        PATTERN
    };
    std::ifstream in;
    int cur;
    // nrow, ncol;
    int nnz;
    Type type;
};
#endif
