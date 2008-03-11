/**
 * FILE: \file hit_collection.h 
 * AUTHOR: Aaron Klammer
 * DESCRIPTION: \brief A collection of hits.
 * CREATE DATE: 2008 March 11
 * REVISION: $Revision: 1.2 $
 ****************************************************************************/*

#ifndef HIT_COLLECTION_H
#define HIT_COLLECTION_H

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include "carp.h"
#include "objects.h"
#include "crux-utils.h"
#include "parameter.h"
#include "match_collection.h"
#include "match.h"

#define _MAX_NUMBER_HITS 10000000 // TODO make max number of proteins

/**
 * \returns An (empty) hit_collection object.
 */
HIT_COLLECTION_T* allocate_hit_collection(void);

/**
 * free the memory allocated match collection
 */
void free_hit_collection(
  HIT_COLLECTION_T* hit_collection ///< the match collection to free -out
  );

/**
 * \brief Creates a new hit collection from a scored match collection.
 * \detail This is the main protein assembly routine.  Allocates memory for
 * the hit collection. 
 * \returns A new hit_collection object.
 */
HIT_COLLECTION_T* new_hit_collection_from_match_collection(
 MATCH_COLLECTION_T* match_collection, ///< the match collection to assemble
 PROTEIN_SCORER_TYPE_T protein_scorer_type
 );

/**
 * Prints out the contents of the hit collection
 */
void print_hit_collection(
 FILE* output,                    ///< the output file -out
 HIT_COLLECTION_T* hit_collection ///< hit collection -in
);

/**
 * hit_iterator routines
 */

/**
 * \returns a new memory allocated match iterator
 */
HIT_ITERATOR_T* new_hit_iterator(
  HIT_COLLECTION_T* hit_collection ///< the match collection to iterate -in
  );

/**
 * Does the hit_iterator have another hit struct to return?
 * \returns TRUE, if hit iter has a next hit, else False
 */
BOOLEAN_T hit_iterator_has_next(
  HIT_ITERATOR_T* hit_iterator ///< the working  match iterator -in
  );

/**
 * \returns the next hit object
 */
MATCH_T* hit_iterator_next(
  HIT_ITERATOR_T* hit_iterator ///< the working match iterator -in
  );

/**
 * free the memory allocated iterator
 */
void free_hit_iterator(
  HIT_ITERATOR_T* hit_iterator ///< the match iterator to free
  );

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
#endif
