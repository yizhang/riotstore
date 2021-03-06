#ifndef LINEARIZATION_H
#define LINEARIZATION_H

#include "../common/common.h"
#include "MDCoord.h"
#include <vector>

enum LinearizationType {
	NONE = 0x01,
	ROW = 0x11,
	COL = 0x12,
	BLOCK = 0x10
};

struct Segment
{
	Key_t begin;// inclusive
	Key_t end;	// inclusive
	Segment(Key_t b, Key_t e): begin(b), end(e)
	{}
};

/**
 * A class representing the linearization that translates n-D
 * coordinates into 1-D linear space. Linearization is used by MDArray
 * to map its n-D appearance to its underlying 1-D physical
 * storage. It is also used by MDIterator to define its next
 * operation.
 *
 * Essentially, a Linearization object is defined by a bijection \f$
 * f:N_1\times N_2\times \cdots \times N_m \to
 * \{0,1,\ldots,\prod_{i=1}^{m}|N_i|-1\} \f$, where \f$ m \f$ is the
 * total number of dimensions and \f$ N_i \f$ is the set of possible
 * indices in each dimension. Any concrete subclass of Linearization
 * must provide the implementation of \f$ f \f$ and \f$ f^{-1} \f$.
 *
 * Each (un)linearize operation can be expensive, especially if the
 * transformation is complex. For efficiency reasons, Linearization
 * also provides optional incremental computation. The implementer
 * of a concrete subclass of Linearization should take advantage of
 * the efficiency of incremental computation if such an opportunity
 * exists.
 *
 * This abstract class serves as the template for implementing
 * concrete linearization subclasses such as a row-major order based
 * one. These subclasses maintain their own private data for carrying
 * out the linearizing operation. For example, a row-major order
 * Linearization subclass would have to keep the dimension information
 * of the n-D space it maps. When copying subclass objects, some
 * subclasses may have to implement their specific copying logic. So
 * we need to simulate virtual copy constructors, using the clone()
 * method.
 * 
 * \sa MDArray
 * \sa MDIterator
 */

template<int nDim>
class Linearization
{
public:
	Linearization() {}
	virtual ~Linearization() {}

	/**
	 * Linearizes the given coord.
	 *
	 * \param coord The coord to be linearized.
	 * \return Result of linearization.
	 */
	virtual Key_t linearize(const MDCoord<nDim> &coord) const = 0;

	/**
	 * Unlinearizes the given 1-D coord.
	 *
	 * \param key Key in 1-D to be unlinearized.
	 * \return The result f unlinearization.
	 */
	virtual MDCoord<nDim> unlinearize(Key_t key) const = 0;

	/*
	 * Incrementally linearizes the coord specified as the sum of the
	 * remembered last state and the give difference. Provided as a
	 * potential optimization for quick linearization if the change in
	 * coordinates is incremental.
	 *
	 * \param diff The difference in coordinate based on last state.
	 * \return The result of linearization.
	 */
	// virtual Key_t linearizeIncremental(MDCoord<N> &diff) = 0;

	/**
	 * Incrementally calculate \f$y\f$ such that \f$ f(y)=f(from)+diff
	 * \f$ from the given anchor and diff. Provided as a potential
	 * optimization for quick calculation of the next or pervious
	 * coordinate when iterating through an array.
	 *
	 * \param from The anchor coordinate in n-D space.
	 * \param diff The difference in linearized space.
	 * \return The new coordinate in n-D space.
	 */
	virtual MDCoord<nDim> move(const MDCoord<nDim> &from, KeyDiff_t diff) const = 0;

	/**
	 * Clone method to simulate virtual copy constructor. IMPORTANT: a
	 * subclass A shoule return type A* instead of Linearization*.
	 *
	 * \return A pointer to a clone of subclass type.
	 */
	virtual Linearization* clone() const = 0;

	virtual bool equals(Linearization<nDim> *) const = 0;

	virtual LinearizationType getType() const = 0;

	virtual Linearization<nDim>* transpose() const { return NULL; }
	
	//virtual MDCoord<nDim> getActualDims() const = 0;

	/**
	 * Draw a curve through the elements in the MD space, the given
	 * rectangle overlaps with the line and cuts it into segments.	This
	 * function returns the list containing these segments, sorted according
	 * to the linearized key order.
	 */
	virtual std::vector<Segment> *getOverlap(const MDCoord<nDim> &begin,
											 const MDCoord<nDim> &end) = 0;

	virtual void setDims(const MDCoord<nDim> &) = 0;

	static Linearization<nDim> *parse(char **p);
	virtual void serialize(char **p) = 0;
	MDCoord<nDim> getActualDims() const { return this->actualDims; }

protected:
	// size of the array the linearization manages
	i64 arrayDims[nDim];
	// the linearization many manage a larger space, due to round-up or padding
	i64 actualDims[nDim];

private:
	/**
	 * Making these private forces the use of clone().
	 */
	Linearization(const Linearization &lin) {}
	Linearization & operator=(const Linearization &src) {return *this;}
};

#endif
