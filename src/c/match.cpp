/*************************************************************************//**
 * \file match.cpp
 * AUTHOR: Chris Park
 * CREATE DATE: 11/27 2006
 * \brief Object for matching a peptide and a spectrum, generate
 * a preliminary score(e.g., Sp) 
 ****************************************************************************/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include "carp.h"
#include "parse_arguments.h"
#include "spectrum.h"
#include "spectrum_collection.h"
#include "ion.h"
#include "ion_series.h"
#include "crux-utils.h"
#include "objects.h"
#include "parameter.h"
#include "scorer.h" 
#include "match.h" 
#include "match_collection.h" 
#include "generate_peptides_iterator.h" 
#include "peptide.h"

#include <string>

#include "MatchFileReader.h"

using namespace std;

/**
 * README!
 * Issues on the overall_type field in match struct
 * 
 * Outstanding question: How do you determine the
 * peptide trypticity for multiple protein sources?
 * 
 * For example, if one protein src is tryptic while the other is 
 * not tryptic, what is the peptide trypticity used for feature and 
 * shuffling peptide sequence?
 *
 * Currently, we use the "Tryptic wins all" approach, where
 * if N-terminus is tryptic in any of the src proteins we claim it
 * tryptic on the N terminus. Same applies for C-terminus.
 * Thus, even if no peptide is tryptic in any of its src protein,
 * if there are one src protein where it is N tryptic and another where
 * it is C tryptic, overall we will call the peptide in the match tryptic.
 * 
 * There are for sure other methods, for example to randomly sample the src 
 * protein and consider that protein as its src or to shuffle the flanking 
 * sequence of the peptide in each of peptide and randomly sample from the 
 * shuffled flanking sequence to determine the shuffled peptide's trypticity.
 *
 */

/**
 *\struct match
 *\brief An object that stores the score & rank for each pepide-spectrum match
 */
struct match{
  SPECTRUM_T* spectrum; ///< the spectrum we are scoring with
  PEPTIDE_T* peptide;  ///< the peptide we are scoring
  FLOAT_T match_scores[NUMBER_SCORER_TYPES]; 
    ///< array of scores, one for each type (index with SCORER_TYPE_T) 
  int match_rank[NUMBER_SCORER_TYPES];  
    ///< rank of this match for each type scored (index with SCORER_TYPE_T)
  int pointer_count; 
    ///< number of pointers to this match object (when reach 0, free memory)
  FLOAT_T b_y_ion_fraction_matched; 
    ///< the fraction of the b, y ion matched while scoring for SP
  int b_y_ion_matched; ///< number of b, y ion matched while scoring SP
  int b_y_ion_possible; ///< number of possible b, y ion while scoring SP
  BOOLEAN_T null_peptide; ///< Is the match a null (decoy) peptide match?
  char* peptide_sequence; ///< peptide sequence is that of peptide or shuffled
  MODIFIED_AA_T* mod_sequence; ///< seq of peptide or shuffled if null peptide
  DIGEST_T digest;
  //  PEPTIDE_TYPE_T overall_type; 
    ///< overall peptide trypticity, set in set_match_peptide, see README above
  int charge; ///< the charge state of the match 
  // post_process match object features
  // only valid when post_process_match is TRUE
  BOOLEAN_T post_process_match; ///< Is this a post process match object?
  FLOAT_T delta_cn; ///< the difference in top and second Xcorr scores
  FLOAT_T ln_delta_cn; ///< the natural log of delta_cn
  FLOAT_T ln_experiment_size; 
     ///< natural log of total number of candidate peptides evaluated
};

/**
 * \returns a new memory allocated match
 */
MATCH_T* new_match(void){
  MATCH_T* match = (MATCH_T*)mycalloc(1, sizeof(MATCH_T));
  
  // initialize score, rank !!!!DEBUG
  int index = 0;
  for(index = 0; index < NUMBER_SCORER_TYPES; ++index){
    match->match_rank[index] = 0;
    match->match_scores[index] = NOT_SCORED;
  }
  
  ++match->pointer_count;

  // default is not a null peptide match
  match->null_peptide = FALSE;

  // set default as not tryptic
  // a full evaluation is done when set peptide
  //  match->overall_type = NOT_TRYPTIC;

  return match;
}

/**
 * free the memory allocated match
 * spectrum is not freed by match
 */
void free_match(
  MATCH_T* match ///< the match to free -in
  )
{
  --match->pointer_count;
  
  // only free match when pointer count reaches
  if(match->pointer_count == 0){

    // but aren't there multiple matches pointing to the same peptide?
    // if so, create a new free_shallow_match which doesn't touch the members
    if (match->peptide != NULL){
      free_peptide(match->peptide);
    }
    if(match->post_process_match && match->spectrum !=NULL){
      free_spectrum(match->spectrum);
    }
    if (match->peptide_sequence != NULL){
      free(match->peptide_sequence);
    }
    if (match->mod_sequence != NULL){
      free(match->mod_sequence);
    }

    free(match);  
  }
}

/* ************ SORTING FUNCTIONS ****************** */
/**
 * Compare two matches by spectrum scan number.
 * \return 0 if scan numbers are equal, -1 if a is smaller, 1 if b is
 * smaller.
 */
int compare_match_spectrum(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  ){

  SPECTRUM_T* spec_a = get_match_spectrum((*match_a));
  SPECTRUM_T* spec_b = get_match_spectrum((*match_b));
  int scan_a = get_spectrum_first_scan(spec_a);
  int scan_b = get_spectrum_first_scan(spec_b);
  int charge_a = get_match_charge((*match_a));
  int charge_b = get_match_charge((*match_b));

  if( scan_a < scan_b ){
    return -1;
  }else if( scan_a > scan_b ){
    return 1;
  }else{// else scan numbers equal
    if(charge_a < charge_b){
      return -1;
    }else if(charge_a > charge_b){
      return 1;
    }
  }// else scan number and charge equal
  return 0;
}

/**
 * compare two matches, used for qsort
 * \returns 0 if sp scores are equal, -1 if a is less than b, 1 if a
 * is greather than b.
 */
int compare_match_sp(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  )
{
  // might have to worry about cases below 1 and -1
  // return(int)((*match_b)->match_scores[SP] - (*match_a)->match_scores[SP]);

  if((*match_b)->match_scores[SP] > (*match_a)->match_scores[SP]){
    return 1;
  }
  else if((*match_b)->match_scores[SP] < (*match_a)->match_scores[SP]){
    return -1;
  }
  return 0;

}

/**
 * Compare two matches by spectrum scan number and sp score, used for qsort.
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if sp score of match a is less than
 * match b.  1 if scan number and sp are equal, else 0.
 */
int compare_match_spectrum_sp(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  ){

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_sp(match_a, match_b);
  }

  return return_me;
}

/**
 * compare two matches, used for qsort
 * \returns 0 if xcorr scores are equal, -1 if a is less than b, 1 if a
 * is greather than b.
 */
int compare_match_xcorr(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
)
{

  if((*match_b)->match_scores[XCORR] > (*match_a)->match_scores[XCORR]){
    return 1;
  }
  else if((*match_b)->match_scores[XCORR] < (*match_a)->match_scores[XCORR]){
    return -1;
  }
  return 0;

}

/**
 * Compare two matches by spectrum scan number and xcorr, used for qsort.
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_xcorr(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
){

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_xcorr(match_a, match_b);
  }

  return return_me;
}

/**
 * compare two matches, used for qsort
 * \returns the difference between p_value (LOGP_BONF_WEIBULL_XCORR)
 * score in match_a and match_b 
 */
int compare_match_p_value(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  ){

  if((*match_b)->match_scores[LOGP_BONF_WEIBULL_XCORR] 
     > (*match_a)->match_scores[LOGP_BONF_WEIBULL_XCORR]){
    return 1;
  }
  else if((*match_b)->match_scores[LOGP_BONF_WEIBULL_XCORR] 
          < (*match_a)->match_scores[LOGP_BONF_WEIBULL_XCORR]){
    return -1;
  }
  return 0;

}


/**
 * compare two matches, used for qsort
 * The smaller the Q value the better!!!, this is opposite to other scores
 * \returns 0 if q-value scores are equal, -1 if a is less than b, 1 if a
 * is greather than b.
 */
int compare_match_q_value(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
)
{

  if((*match_b)->match_scores[PERCOLATOR_QVALUE] 
     < (*match_a)->match_scores[PERCOLATOR_QVALUE]){
    return 1;
  }
  else if((*match_b)->match_scores[PERCOLATOR_QVALUE]
	  > (*match_a)->match_scores[PERCOLATOR_QVALUE]){
    return -1;
  }
  return 0;
}

/**
 * compare two matches, used for qsort
 * The smaller the Q value the better!!!, this is opposite to other scores
 * \returns 0 if q-value scores are equal, -1 if a is less than b, 1 if a
 * is greather than b.
 */
int compare_match_qranker_q_value(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
)
{

  if((*match_b)->match_scores[QRANKER_QVALUE] < 
      (*match_a)->match_scores[QRANKER_QVALUE]){
    return 1;
  }
  else if((*match_b)->match_scores[QRANKER_QVALUE] > 
          (*match_a)->match_scores[QRANKER_QVALUE]){
    return -1;
  }
  return 0;
}

/**
 * Compare two matches by spectrum scan number and q-value, used for qsort.
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_q_value(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
){

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_q_value(match_a, match_b);
  }

  return return_me;
}

/**
 * Compare two matches by spectrum scan number and qranker q-value, 
 * used for qsort.
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_qranker_q_value(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
){

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_q_value(match_a, match_b);
  }

  return return_me;
}


/**
 * compare two matches, used for PERCOLATOR_SCORE
 * \returns 0 if percolator scores are equal, -1 if a is less than b,
 * 1 if a is greather than b.
 */
int compare_match_percolator_score(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
)
{
  if((*match_b)->match_scores[PERCOLATOR_SCORE] > (*match_a)->match_scores[PERCOLATOR_SCORE]){
    return 1;
  }
  else if((*match_b)->match_scores[PERCOLATOR_SCORE] < (*match_a)->match_scores[PERCOLATOR_SCORE]){
    return -1;
  }
  return 0;

}

/**
 * Compare two matches by spectrum scan number and percolator score,
 * used for qsort. 
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_percolator_score(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  )
{

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_percolator_score(match_a, match_b);
  }

  return return_me;
}

/**
 * compare two matches, used for QRANKER_SCORE
 * \returns 0 if qranker scores are equal, -1 if a is less than b,
 * 1 if a is greather than b.
 */
int compare_match_qranker_score(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
)
{
  if((*match_b)->match_scores[QRANKER_SCORE] > (*match_a)->match_scores[QRANKER_SCORE]){
    return 1;
  }
  else if((*match_b)->match_scores[QRANKER_SCORE] < (*match_a)->match_scores[QRANKER_SCORE]){
    return -1;
  }
  return 0;

}

/**
 * Compare two matches by spectrum scan number and qranker score,
 * used for qsort. 
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_qranker_score(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  )
{

  int return_me = compare_match_spectrum( match_a, match_b );

  if( return_me == 0 ){
    return_me = compare_match_qranker_score(match_a, match_b);
  }

  return return_me;
}

/**
 * Compare two matches by spectrum scan number and q-value (from the decoys and xcorr score),
 * used for qsort. 
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_decoy_xcorr_qvalue(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  )
{
  // delete this, just for the compiler
  if( match_a == NULL || match_b == NULL ){
    return 0;
  }
  carp(CARP_FATAL, "HEY, you haven't implemented sorting by decoy-qvalue yet!");
  return 0; // Return value to avoid compiler warning.
}

/**
 * Compare two matches by spectrum scan number and q-value (from the decoys and weibull est p-values),
 * used for qsort. 
 * \returns -1 if match a spectrum number is less than that of match b
 * or if scan number is same, if score of match a is less than
 * match b.  1 if scan number and score are equal, else 0.
 */
int compare_match_spectrum_decoy_pvalue_qvalue(
  MATCH_T** match_a, ///< the first match -in  
  MATCH_T** match_b  ///< the scond match -in
  )
{
  // delete this, just for the compiler
  if( match_a == NULL || match_b == NULL ){
    return 0;
  }
  carp(CARP_FATAL, "HEY, you haven't implemented sorting by decoy-qvalue yet!");
  return 0; // Return value to avoid compiler warning.
}

/* ****** End of sorting functions ************/

/**
 * \brief Print the match information in sqt format to the given file
 *
 * Only crux sequest-search produces sqt files so the two scores
 * printed are always Sp and xcorr.
 */
void print_match_sqt(
  MATCH_T* match,             ///< the match to print -in  
  FILE* file                  ///< output stream -out
  ){

  if( match == NULL || file == NULL ){
    carp(CARP_ERROR, "Cannot print match to sqt file from null inputs");
    return;
  }

  PEPTIDE_T* peptide = get_match_peptide(match);
  // this should get the sequence from the match, not the peptide
  char* sequence = get_match_sequence_sqt(match);

  int b_y_total = get_match_b_y_ion_possible(match);
  int b_y_matched = get_match_b_y_ion_matched(match);
  
  FLOAT_T delta_cn = get_match_delta_cn(match);
  FLOAT_T score_main = get_match_score(match, XCORR);

  // write format string with variable precision
  int precision = get_int_parameter("precision");

  // print match info
  fprintf(file, "M\t%i\t%i\t%.4f\t%.2f\t%.*g\t%.*g\t%i\t%i\t%s\tU\n",
          get_match_rank(match, XCORR),
          get_match_rank(match, SP),
          get_peptide_peptide_mass(peptide),
          delta_cn,
          precision,
          score_main,
          precision,
          get_match_score(match, SP),
          b_y_matched,
          b_y_total,
          sequence
          );
  free(sequence);
  
  PEPTIDE_SRC_ITERATOR_T* peptide_src_iterator = 
    new_peptide_src_iterator(peptide);
  PEPTIDE_SRC_T* peptide_src = NULL;
  char* protein_id = NULL;
  PROTEIN_T* protein = NULL;
  const char* rand = "";
  if( match->null_peptide ){
    rand = "rand_";
  }
  
  while(peptide_src_iterator_has_next(peptide_src_iterator)){
    peptide_src = peptide_src_iterator_next(peptide_src_iterator);
    protein = get_peptide_src_parent_protein(peptide_src);
    protein_id = get_protein_id(protein);
    
    // print match info (locus line), add rand_ to locus name for decoys
    fprintf(file, "L\t%s%s\n", rand, protein_id);      
    free(protein_id);
  }
  
  free_peptide_src_iterator(peptide_src_iterator);
  
  return;
}


/**
 * Print one field in the tab-delimited output file, based on column index.
 */
static void print_one_match_field(
  int      column_idx,             ///< Index of the column to print. -in
  char*    float_format,           ///< Formatting string for floats. -in
  MATCH_COLLECTION_T* collection,  ///< collection holding this match -in 
  MATCH_T* match,                  ///< the match to print -in    
  FILE*    output_file,            ///< output stream -out
  int      scan_num,               ///< starting scan number -in
  FLOAT_T  spectrum_precursor_mz,  ///< m/z of spectrum precursor -in
  FLOAT_T  spectrum_mass,          ///< spectrum neutral mass -in
  int      num_matches,            ///< num matches in spectrum -in
  int      charge,                 ///< charge -in
  const BOOLEAN_T* scores_computed,///< scores_computed[TYPE] = T if match was scored for TYPE
 int      b_y_total,              ///< total b/y ions -in
  int      b_y_matched             ///< Number of b/y ions matched. -in
) {

  switch ((MATCH_COLUMNS_T)column_idx) {
  case SCAN_COL:
    fprintf(output_file, "%d", scan_num);
    break;
  case CHARGE_COL:
    fprintf(output_file, "%d", charge);
    break;
  case SPECTRUM_PRECURSOR_MZ_COL:
    fprintf(output_file, "%.4f", spectrum_precursor_mz);
    break;
  case SPECTRUM_NEUTRAL_MASS_COL:
    fprintf(output_file, "%.4f", spectrum_mass);
    break;
  case PEPTIDE_MASS_COL:
    {
      PEPTIDE_T* peptide = get_match_peptide(match);
      double peptide_mass = get_peptide_peptide_mass(peptide);
      fprintf(output_file, "%.6f", peptide_mass);
    }
    break;
  case DELTA_CN_COL:
    {
      FLOAT_T delta_cn = get_match_delta_cn(match);
      if( delta_cn == 0 ){// I hate -0, this prevents it
	delta_cn = 0.0;
      }
      fprintf(output_file, float_format, delta_cn);
    }
    break;
  case SP_SCORE_COL:
    if (scores_computed[SP] == TRUE) {
      fprintf(output_file, float_format, get_match_score(match, SP));
    }
    break;
  case SP_RANK_COL:
    if (scores_computed[SP] == TRUE) {
      fprintf(output_file, "%d", get_match_rank(match, SP));
    }
    break;
  case XCORR_SCORE_COL:
    fprintf(output_file, float_format, get_match_score(match, XCORR));
    break;
  case XCORR_RANK_COL:
    fprintf(output_file, "%d", get_match_rank(match, XCORR));
    break;
  case PVALUE_COL:
    if( scores_computed[LOGP_BONF_WEIBULL_XCORR] == TRUE ){ 
      double log_pvalue = get_match_score(match, LOGP_BONF_WEIBULL_XCORR);
      if (P_VALUE_NA == log_pvalue) {
	fprintf(output_file, "NaN");
      }
      else {
	fprintf(output_file, float_format, exp(-1 * log_pvalue));
      }
    }
    break;
  case WEIBULL_QVALUE_COL:
    if( scores_computed[LOGP_QVALUE_WEIBULL_XCORR] == TRUE ){ 
      double weibull_qvalue = get_match_score(match, LOGP_QVALUE_WEIBULL_XCORR);
      fprintf(output_file, float_format, exp(-1 * weibull_qvalue));
    }
    break;
#ifdef NEW_COLUMNS
  case WEIBULL_PEPTIDE_QVALUE_COL:
    break;
#endif
  case DECOY_XCORR_QVALUE_COL:
    if( scores_computed[DECOY_XCORR_QVALUE]  && match->null_peptide == FALSE ){
      fprintf(output_file, float_format, 
	      get_match_score(match, DECOY_XCORR_QVALUE));
    }
    break;
#ifdef NEW_COLUMNS
  case DECOY_XCORR_PEPTIDE_QVALUE_COL:
    break;
#endif
  case PERCOLATOR_SCORE_COL:
    if (scores_computed[PERCOLATOR_SCORE] == TRUE)  {
      fprintf(output_file, float_format, 
	      get_match_score(match, PERCOLATOR_SCORE));
    }
    break;
  case PERCOLATOR_RANK_COL:
    if (scores_computed[PERCOLATOR_SCORE] == TRUE)  {
      fprintf(output_file, float_format,
	      get_match_rank(match, PERCOLATOR_SCORE));
    }
    break;
  case PERCOLATOR_QVALUE_COL:
    if (scores_computed[PERCOLATOR_SCORE] == TRUE)  {
      fprintf(output_file, float_format,
	      get_match_score(match, PERCOLATOR_QVALUE));
    }
    break;
#ifdef NEW_COLUMNS
  case PERCOLATOR_PEPTIDE_QVALUE_COL:
    break;
#endif
  case QRANKER_SCORE_COL:
    if (scores_computed[QRANKER_SCORE] == TRUE) {
      fprintf(output_file, float_format, get_match_score(match, QRANKER_SCORE));
    }
    break;
  case QRANKER_QVALUE_COL:
    if (scores_computed[QRANKER_SCORE] == TRUE) {
      fprintf(output_file, float_format, 
	      get_match_score(match, QRANKER_QVALUE));
    }
    break;
#ifdef NEW_COLUMNS
  case QRANKER_PEPTIDE_QVALUE_COL:
    break;
#endif
  case BY_IONS_MATCHED_COL:
    if (scores_computed[SP] != 0){
      fprintf(output_file, "%d", b_y_matched);
    }
    break;
  case BY_IONS_TOTAL_COL:
    fprintf(output_file, "%d", b_y_total);
    break;
  case MATCHES_SPECTRUM_COL:
    fprintf(output_file, "%d", num_matches);
    break;
  case SEQUENCE_COL:
    {
      // this should get the sequence from the match, not the peptide
      char* sequence = get_match_mod_sequence_str_with_masses(match, 
                           get_boolean_parameter("display-summed-mod-masses"));
      if( sequence != NULL ){ // for post-search, no shuffled sequences
	fprintf(output_file, "%s", sequence);
      }
      free(sequence);
    }
    break;
  case CLEAVAGE_TYPE_COL:
    {
      ENZYME_T enzyme = get_enzyme_type_parameter("enzyme");
      char* enzyme_string = enzyme_type_to_string(enzyme);
      DIGEST_T digestion = get_digest_type_parameter("digestion");
      char* digestion_string = digest_type_to_string(digestion);
      fprintf(output_file, "%s-%s", enzyme_string, digestion_string);
      free(enzyme_string);
      free(digestion_string);
    }
    break;
  case PROTEIN_ID_COL:
    {
      PEPTIDE_T* peptide = get_match_peptide(match);
      string protein_ids_string = get_protein_ids_peptide_locations(peptide);
      fprintf(output_file, "%s", protein_ids_string.c_str());
    }
    break;
  case FLANKING_AA_COL:
    {
      PEPTIDE_T* peptide = get_match_peptide(match);
      char* flanking_aas = get_flanking_aas(peptide);
      fprintf(output_file, "%s", flanking_aas);
      free(flanking_aas);
    }
    break;
  case UNSHUFFLED_SEQUENCE_COL:
    if(match->null_peptide == TRUE){
      char* seq = get_peptide_unshuffled_sequence(match->peptide);
      fprintf(output_file, "%s", seq);
      free(seq);
    }
    break;
  case ETA_COL:
    if (scores_computed[LOGP_BONF_WEIBULL_XCORR]) {
      fprintf(output_file, "%g", get_calibration_eta(collection));
    }
    break;
  case BETA_COL:
    if (scores_computed[LOGP_BONF_WEIBULL_XCORR]) {
      fprintf(output_file, "%g", get_calibration_beta(collection));
    }
  case SHIFT_COL:
    if (scores_computed[LOGP_BONF_WEIBULL_XCORR]) {
      fprintf(output_file, "%g", get_calibration_shift(collection));
    }
  case CORR_COL:
    if (scores_computed[LOGP_BONF_WEIBULL_XCORR]) {
      fprintf(output_file, "%g", get_calibration_corr(collection));
    }
    break;
  case NUMBER_MATCH_COLUMNS:
    carp(CARP_FATAL, "Error in printing code (match.cpp).");
    break;
  }
}

/**
 * \brief Print the match information in tab delimited format to the given file
 *
 */
void print_match_tab(
  MATCH_COLLECTION_T* collection,  ///< collection holding this match -in 
  MATCH_T* match,                  ///< the match to print -in  
  FILE*    output_file,            ///< output stream -out
  int      scan_num,               ///< starting scan number -in
  FLOAT_T  spectrum_precursor_mz,  ///< m/z of spectrum precursor -in
  FLOAT_T  spectrum_mass,          ///< spectrum neutral mass -in
  int      num_matches,            ///< num matches in spectrum -in
  int      charge,                 ///< charge -in
  const BOOLEAN_T* scores_computed ///< scores_computed[TYPE] = T if match was scored for TYPE
  ){

  // Usually because no decoy file to print to.
  if( output_file == NULL ){ 
    return;
  }

  if( match == NULL  ){
    carp(CARP_ERROR, "Cannot print NULL match to tab delimited file.");
    return;
  }

  // TODO (BF 27-Apr-10) This should no longer be a problem since we
  // now read only .txt files
  // NOTE (BF 12-Feb-08) Here is another ugly fix for post-analysis.
  // Only the fraction matched is serialized.  The number possible can
  // be calculated from the length of the sequence and the charge, but
  // the charge of the match is not serialized and I'm not sure when
  // it is set.  But I know it exists by here, so recalculate it.
  int b_y_total = get_match_b_y_ion_possible(match);
  int b_y_matched = get_match_b_y_ion_matched(match);
  
  if( b_y_total == 0 ){
    int factor = get_match_charge(match);
    if( factor == 3 ){
      factor = 2;  //there are +1 and +2 b/y ions for charge==3
    }else{
      factor = 1;
    }
    PEPTIDE_T* peptide = get_match_peptide(match);
    b_y_total = (get_peptide_length(peptide)-1) * 2 * factor;
    b_y_matched = (int)((get_match_b_y_ion_fraction_matched(match)) 
			* b_y_total);
  }


  // Decide on formatting.
  int precision = get_int_parameter("precision");
  char float_format[16];
  sprintf(float_format, "%%.%ig", precision);

  // Print tab delimited fields
  int column_idx;
  for (column_idx = 0; column_idx < NUMBER_MATCH_COLUMNS; column_idx++) {
    print_one_match_field(column_idx, 
			  float_format,
			  collection,
			  match,
			  output_file,
			  scan_num,
			  spectrum_precursor_mz,
			  spectrum_mass,
			  num_matches,
			  charge,
			  scores_computed,
			  b_y_total,
			  b_y_matched);
    if (column_idx < NUMBER_MATCH_COLUMNS - 1) {
      fprintf(output_file, "\t");
    } else {
      fprintf(output_file, "\n");
    }
  }
}

/**
 * shuffle the matches in the array between index start and end-1
 */
void shuffle_matches(
  MATCH_T** match_array, ///< the match array to shuffle  
  int start_index,       ///< index of first element to shuffle
  int end_index          ///< index AFTER the last element to shuffle
  ){
  if( match_array == NULL ){
    carp(CARP_ERROR, "Cannot shuffle null match array.");
    return;
  }
  //  srand(time(NULL));

  int match_idx = 0;
  for(match_idx = start_index; match_idx < end_index-1; match_idx++){
    MATCH_T* cur_match = match_array[match_idx];

    // pick a random index between match_index and end_index-1
    int rand_idx = get_random_number_interval(match_idx+1, end_index-1);

    //    fprintf(stderr, "%i values between %i and %i, rand %.4f, index %i\n",
    //            range, match_idx, end_index, rand_scaler, rand_idx);
    match_array[match_idx] = match_array[rand_idx];    
    match_array[rand_idx] = cur_match;
  }
}


/**
 * sort the match array with the corresponding compare method
 */
void qsort_match(
  MATCH_T** match_array, ///< the match array to sort -in  
  int match_total,  ///< the total number of match objects -in
  int (*compare_method)(const void*, const void*) ///< the compare method to use -in
  )
{
  qsort(match_array, match_total, sizeof(MATCH_T*), compare_method);
}


/*******************************************
 * match post_process extension
 ******************************************/

/**
 * Constructs the 20 feature array that pass over to percolator registration
 * Go to top README for N,C terminus tryptic feature info.
 *\returns the feature FLOAT_T array
 */
double* get_match_percolator_features(
  MATCH_T* match, ///< the match to work -in 
  MATCH_COLLECTION_T* match_collection ///< the match collection to iterate -in
  )
{
  int feature_count = 20;
  PEPTIDE_SRC_ITERATOR_T* src_iterator = NULL;
  PEPTIDE_SRC_T* peptide_src = NULL;
  PROTEIN_T* protein = NULL;
  unsigned int protein_idx = 0;
  double* feature_array = (double*)mycalloc(feature_count, sizeof(double));
  FLOAT_T weight_diff = get_peptide_peptide_mass(match->peptide) -
    get_spectrum_neutral_mass(match->spectrum, match->charge);

  
  carp(CARP_DETAILED_DEBUG, "spec: %d, charge: %d", 
    get_spectrum_first_scan(match -> spectrum),
    match -> charge);

  carp(CARP_DETAILED_DEBUG,"peptide mass:%f", get_peptide_peptide_mass(match -> peptide));
  carp(CARP_DETAILED_DEBUG,"spectrum neutral mass:%f", get_spectrum_neutral_mass(match -> spectrum, match -> charge));

  // Xcorr
  feature_array[0] = get_match_score(match, XCORR);
  // DeltCN
  feature_array[1] = match->delta_cn;
  // DeltLCN
  feature_array[2] = match->ln_delta_cn;
  // SP
  feature_array[3] = get_match_score(match, SP);
  // lnrSP
  feature_array[4] = logf(get_match_rank(match, SP));
  // SP is no longer scored so we need place holder values
  if( feature_array[3] == NOT_SCORED ){ 
    feature_array[3] = 0;
    feature_array[4] = 0;
  }
  // dM
  feature_array[5] = weight_diff;
  // absdM
  feature_array[6] = fabsf(weight_diff);
  // Mass
  feature_array[7] = get_spectrum_neutral_mass(match->spectrum, match->charge);
  // ionFrac
  feature_array[8] = match->b_y_ion_fraction_matched;
  // lnSM
  feature_array[9] = match->ln_experiment_size;
  
  // peptide cleavage info.
  // START figure out the right way to set these features for on the fly
  // peptide generation
/*
  if(match->overall_type == TRYPTIC){
    feature_array[10] = TRUE;
    feature_array[11] = TRUE;
  }
  else if(match->overall_type == N_TRYPTIC){
    feature_array[10] = TRUE;
  }
  else if(match->overall_type == C_TRYPTIC){
    feature_array[11] = TRUE;
  }
  */
  feature_array[10] = TRUE; // TODO(if perc support continues, figure out what these should be
  feature_array[11] = TRUE;
  // get the missed cleave sites
  feature_array[12] = get_peptide_missed_cleavage_sites(match->peptide);
  
  // pepLen
  feature_array[13] = get_peptide_length(match->peptide);
  
  // set charge
  if(match->charge == 1){
    feature_array[14] = TRUE;
  }
  else if(match->charge == 2){
    feature_array[15] = TRUE;
  }
  else if(match->charge == 3){
    feature_array[16] = TRUE;
  }
  
  // run specific features
  if (strcmp(
        get_string_parameter_pointer("percolator-intraset-features"), "T")==0){
    carp(CARP_DETAILED_DEBUG, "Using intraset features!");
    feature_array[17] 
      = get_match_collection_hash(match_collection, match->peptide);
    
    src_iterator = new_peptide_src_iterator(match->peptide);
    // iterate overall parent proteins
    // find largest numProt and pepSite among the parent proteins
    while(peptide_src_iterator_has_next(src_iterator)){
      peptide_src = peptide_src_iterator_next(src_iterator);
      protein = get_peptide_src_parent_protein(peptide_src);
      protein_idx = get_protein_protein_idx(protein);
      
      // numProt
      if(feature_array[18] < get_match_collection_protein_counter(
                              match_collection, protein_idx)){
        feature_array[18] = get_match_collection_protein_counter(
                              match_collection, protein_idx);
      }
      
      // pepSite
      if(feature_array[19] < get_match_collection_protein_peptide_counter(
                              match_collection, protein_idx)){
        feature_array[19] = get_match_collection_protein_peptide_counter(
                              match_collection, protein_idx);      
      }
    }
  } else {
    feature_array[17] = feature_array[18] = feature_array[19] = 0.0;
  }
  
  // now check that no value is with in infinity
  int check_idx;
  for(check_idx=0; check_idx < 20; ++check_idx){
    
    FLOAT_T feature = feature_array[check_idx];
    carp(CARP_DETAILED_DEBUG, "feature[%d]=%f", check_idx, feature);
    if(feature <= -BILLION || feature  >= BILLION){
      carp(CARP_ERROR,
          "Percolator feature out of bounds: %d, with value %.2f. Modifying.",
           check_idx, feature);
      feature_array[check_idx] = feature <= - BILLION ? -BILLION : BILLION;
    }
  }

  free_peptide_src_iterator(src_iterator);

  return feature_array;
}


/**
 *
 *\returns a match object that is parsed from the tab-delimited result file
 */
MATCH_T* parse_match_tab_delimited(
  MatchFileReader& result_file,  ///< the result file to parse PSMs -in
  DATABASE_T* database ///< the database to which the peptides are created -in
  ) {

  //TODO - FINISH and TEST
  MATCH_T* match = new_match();

  SPECTRUM_T* spectrum = NULL;
  PEPTIDE_T* peptide = NULL;

  // this is a post_process match object
  match->post_process_match = TRUE;

  if((peptide = parse_peptide_tab_delimited(result_file, database, TRUE))== NULL){
    carp(CARP_ERROR, "Failed to parse peptide (tab delimited)");
    // FIXME should this exit or return null. I think sometimes we can get
    // no peptides, which is valid, in which case NULL makes sense.
    // maybe this should be fixed at the output match level however.
    return NULL;
  }

  if ((result_file.getString(SP_SCORE_COL) == "") || 
    (result_file.getString(SP_RANK_COL) == "")) {

    match -> match_scores[SP] = NOT_SCORED;
    match -> match_rank[SP] = 0;
  } else {
    match -> match_scores[SP] = result_file.getFloat(SP_SCORE_COL);
    match -> match_rank[SP] = result_file.getInteger(SP_RANK_COL);
  }

  match -> match_scores[XCORR] = result_file.getFloat(XCORR_SCORE_COL);
  match -> match_rank[XCORR] = result_file.getInteger(XCORR_RANK_COL);

  match -> match_scores[DECOY_XCORR_QVALUE] = result_file.getFloat(DECOY_XCORR_QVALUE_COL);
  /* TODO I personally would like access to the raw p-value as well as the bonferonni corrected one (SJM).
  match -> match_scores[LOGP_WEIBULL_XCORR] = result_file.getFloat("logp weibull xcorr");
  */
  match -> match_scores[LOGP_BONF_WEIBULL_XCORR] = -log(result_file.getFloat(PVALUE_COL));
  
  match -> match_scores[PERCOLATOR_QVALUE] = result_file.getFloat(PERCOLATOR_QVALUE_COL);

  match -> match_scores[PERCOLATOR_SCORE] = result_file.getFloat(PERCOLATOR_SCORE_COL);
  match -> match_rank[PERCOLATOR_SCORE] = result_file.getInteger(PERCOLATOR_RANK_COL);

  match -> match_scores[LOGP_QVALUE_WEIBULL_XCORR] = result_file.getFloat(WEIBULL_QVALUE_COL);
  
  match -> match_scores[QRANKER_SCORE] = result_file.getFloat(QRANKER_SCORE_COL);
  match -> match_scores[QRANKER_QVALUE] = result_file.getFloat(QRANKER_QVALUE_COL);

   // parse spectrum
  if((spectrum = parse_spectrum_tab_delimited(result_file))== NULL){
    carp(CARP_ERROR, "Failed to parse spectrum (tab delimited).");
  }

  // spectrum specific features
  if (result_file.getString(BY_IONS_MATCHED_COL) == "") {
    match -> b_y_ion_matched = 0;
    match -> b_y_ion_possible = 0;
    match -> b_y_ion_fraction_matched = 0.0;
  } else {
    match -> b_y_ion_matched = result_file.getInteger(BY_IONS_MATCHED_COL);
    match -> b_y_ion_possible = result_file.getInteger(BY_IONS_TOTAL_COL);

    match -> b_y_ion_fraction_matched = 
      (FLOAT_T)match -> b_y_ion_matched /
      (FLOAT_T)match -> b_y_ion_possible;
  }
  //parse match overall digestion
  match -> digest = string_to_digest_type((char*)result_file.getString(CLEAVAGE_TYPE_COL).c_str()); 

  //Parse if match is it null_peptide?
  //We could check if unshuffled sequence is "", since that field is not
  //set for not null peptides.
  match -> null_peptide = !result_file.empty(UNSHUFFLED_SEQUENCE_COL);

  //assign fields
  match -> peptide_sequence = NULL;
  match -> spectrum = spectrum;
  match -> peptide = peptide;
  
  return match;
}



/****************************
 * match get, set methods
 ***************************/


/**
 * Returns a heap allocated peptide sequence of the PSM
 * User must free the sequence.
 *
 * Go to top README for N,C terminus tryptic feature info.
 *
 *\returns the match peptide sequence, returns NULL if no sequence avaliable
 */
char* get_match_sequence(
  MATCH_T* match ///< the match to work -in
  )
{
  // if post_process_match and has a null peptide you can't get sequence
  if(match->post_process_match && match->null_peptide){
    carp(CARP_ERROR, 
         "Cannot retrieve null peptide sequence for post_process_match");
    return NULL;
  }
  
  if(match->peptide_sequence == NULL){
    match->peptide_sequence = get_peptide_sequence(match->peptide);
  }
  return my_copy_string(match->peptide_sequence); 
}

/**
 * Returns a heap allocated peptide sequence of the PSM formatted with
 * the flanking amino acids and modifiation symbols.
 *
 * Sequence is in the form of X.SEQ.X where X is the flanking amino
 * acid or - if peptide is at the end of the protein.
 * Sequence may not be the same as for the peptide if this is for a
 * decoy database.
 *\returns The sqt-formatted peptide sequence for this match.
 */
char* get_match_sequence_sqt(
  MATCH_T* match ///< the match to work -in
  ){
  if( match == NULL ){
    carp(CARP_ERROR, "Cannot return sequence from NULL match");
    return NULL;
  }
  // get_match_mod_sequence (use method in case match->mod_seq == NULL) 
  MODIFIED_AA_T* mod_seq = get_match_mod_sequence(match);
  if( mod_seq == NULL ){
    return NULL;
  }
  int length = get_peptide_length(get_match_peptide(match));

  // turn it into string
  char* seq = modified_aa_string_to_string_with_symbols(mod_seq, length);

  // get peptide flanking residues 
  char c_term = get_peptide_c_term_flanking_aa(match->peptide);
  char n_term = get_peptide_n_term_flanking_aa(match->peptide);

  // allocate seq + 4 length array
  char* final_string = (char*)mycalloc((strlen(seq)+5), sizeof(char));

  // copy pieces in
  final_string[0] = c_term;
  final_string[1] = '.';
  strcpy(&final_string[2], seq);
  final_string[strlen(seq) + 2] = '.';
  final_string[strlen(seq) + 3] = n_term;
  final_string[strlen(seq) + 4] = '\0';

  carp(CARP_DETAILED_DEBUG, "start string %s, final %s", seq, final_string);

  // delete mod seq and string version
  free(seq);
  free(mod_seq);
  return final_string;
}

/**
 * \brief Returns a newly allocated modified_aa sequence of the PSM.
 * Sequence is the same as the peptide, if target match or is a
 * shuffled sequence if a null (decoy) match.  If match field
 * 'mod_sequence' is non NULL, returns a copy of that value, otherwise
 * fills that field and returns a copy of the value.
 *
 * \returns the match peptide sequence, returns NULL if no sequence avaliable
 */
MODIFIED_AA_T* get_match_mod_sequence(
  MATCH_T* match ///< the match from which to get the sequence -in
  )
{
  if( match == NULL ){
    carp(CARP_ERROR, "Cannot get mod sequence from null match.");
  }
  // if post_process_match and has a null peptide you can't get sequence
  if(match->post_process_match && match->null_peptide){
    return NULL;
  }

  if(match->mod_sequence == NULL){
    match->mod_sequence = get_peptide_modified_aa_sequence(match->peptide);
  }

  return copy_mod_aa_seq(match->mod_sequence, get_peptide_length(match->peptide));
}

/**
 * \brief Returns a newly allocated string of sequence including any
 * modifications represented as symbols (*,@,#, etc) following the
 * modified residue. 
 * \returns The peptide sequence of the match including modification
 * characters. 
 */
char* get_match_mod_sequence_str_with_symbols( MATCH_T* match ){

  // if post_process_match and has a null peptide you can't get sequence
  if(match->post_process_match && match->null_peptide){
    return NULL;
  }

  if(match->mod_sequence == NULL){
    match->mod_sequence = get_peptide_modified_aa_sequence(match->peptide);
  }

  return modified_aa_string_to_string_with_symbols(match->mod_sequence, 
                                      get_peptide_length(match->peptide));
}

/**
 * \brief Returns a newly allocated string of sequence including any
 * modifications represented as mass values in brackets following the
 * modified residue. If merge_masses is true, the sum of multiple
 * modifications on one residue are printed.  If false, each mass is
 * printed in a comma-separated list.
 * \returns The peptide sequence of the match including modification
 * masses. 
 */
char* get_match_mod_sequence_str_with_masses( 
 MATCH_T* match, 
 BOOLEAN_T merge_masses)
{

  // if post_process_match and has a null peptide you can't get sequence
  if(match->post_process_match && match->null_peptide){
    return NULL;
  }

  if(match->mod_sequence == NULL){
    match->mod_sequence = get_peptide_modified_aa_sequence(match->peptide);
  }

  return modified_aa_string_to_string_with_masses(match->mod_sequence, 
                                        get_peptide_length(match->peptide),
                                                  merge_masses);
}
/**
 * Must ask for score that has been computed
 *\returns the match_mode score in the match object
 */
FLOAT_T get_match_score(
  MATCH_T* match, ///< the match to work -in  
  SCORER_TYPE_T match_mode ///< the working mode (SP, XCORR) -in
  )
{
  assert(match != NULL );
  return match->match_scores[match_mode];
}

/**
 * sets the match score
 */
void set_match_score(
  MATCH_T* match, ///< the match to work -out
  SCORER_TYPE_T match_mode, ///< the working mode (SP, XCORR) -in
  FLOAT_T match_score ///< the score of the match -in
  )
{
  match->match_scores[match_mode] = match_score;
}

/**
 * Must ask for score that has been computed
 *\returns the match_mode rank in the match object
 */
int get_match_rank(
  MATCH_T* match, ///< the match to work -in  
  SCORER_TYPE_T match_mode ///< the working mode (SP, XCORR) -in
  )
{
  return match->match_rank[match_mode];
}

/**
 * sets the rank of the match
 */
void set_match_rank(
  MATCH_T* match, ///< the match to work -in  
  SCORER_TYPE_T match_mode, ///< the working mode (SP, XCORR) -in
  int match_rank ///< the rank of the match -in
  )
{
  match->match_rank[match_mode] = match_rank;
}

/**
 *\returns the spectrum in the match object
 */
SPECTRUM_T* get_match_spectrum(
  MATCH_T* match ///< the match to work -in  
  )
{
  return match->spectrum;
}

/**
 * sets the match spectrum
 */
void set_match_spectrum(
  MATCH_T* match, ///< the match to work -out
  SPECTRUM_T* spectrum  ///< the working spectrum -in
  )
{

  match->spectrum = spectrum;
}

/**
 *\returns the peptide in the match object
 */
PEPTIDE_T* get_match_peptide(
  MATCH_T* match ///< the match to work -in  
  )
{
  return match->peptide;
}

/**
 * Sets the match's peptide field.  Only cache a copy of the peptide
 * sequence after it has been requested.
 *
 * Go to top README for N,C terminus tryptic feature info.
 */
void set_match_peptide(
  MATCH_T* match, ///< the match to work -out
  PEPTIDE_T* peptide  ///< the working peptide -in
  )
{
  // set peptide 
  match->peptide = peptide;

  match->digest = NON_SPECIFIC_DIGEST;  // FIXME
}

/**
 * sets the match if it is a null_peptide match
 */
void set_match_null_peptide(
  MATCH_T* match, ///< the match to work -out
  BOOLEAN_T is_null_peptide  ///< is the match a null peptide? -in
  )
{
  match->null_peptide = is_null_peptide;  
}

/**
 * gets the match if it is a null_peptide match
 *\returns TRUE if match is null peptide, else FALSE
 */
BOOLEAN_T get_match_null_peptide(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->null_peptide;
}

/**
 * sets the match charge
 */
void set_match_charge(
  MATCH_T* match, ///< the match to work -out
  int charge  ///< the charge of spectrum -in
  )
{
  match->charge = charge;
}

/**
 * gets the match charge
 */
int get_match_charge(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->charge;
}

/**
 * sets the match delta_cn
 */
void set_match_delta_cn(
  MATCH_T* match, ///< the match to work -out
  FLOAT_T delta_cn  ///< the delta cn value of PSM -in
  )
{
  match->delta_cn = delta_cn;
}

/**
 * gets the match delta_cn
 */
FLOAT_T get_match_delta_cn(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->delta_cn;
}

/**
 * sets the match ln_delta_cn
 */
void set_match_ln_delta_cn(
  MATCH_T* match, ///< the match to work -out
  FLOAT_T ln_delta_cn  ///< the ln delta cn value of PSM -in
  )
{
  match->ln_delta_cn = ln_delta_cn;
}

/**
 * gets the match ln_delta_cn
 */
FLOAT_T get_match_ln_delta_cn(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->ln_delta_cn;
}

/**
 * sets the match ln_experiment_size
 */
void set_match_ln_experiment_size(
  MATCH_T* match, ///< the match to work -out
  FLOAT_T ln_experiment_size ///< the ln_experiment_size value of PSM -in
  )
{
  match->ln_experiment_size = ln_experiment_size;
}

/**
 * gets the match ln_experiment_size
 */
FLOAT_T get_match_ln_experiment_size(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->ln_experiment_size;
}

/**
 * sets the match b_y_ion information
 */
void set_match_b_y_ion_info(
  MATCH_T* match, ///< the match to work -out
  SCORER_T* scorer ///< the scorer from which to extract information -in
  )
{
  match->b_y_ion_fraction_matched = 
    get_scorer_sp_b_y_ion_fraction_matched(scorer); 
  match->b_y_ion_matched = 
    get_scorer_sp_b_y_ion_matched(scorer); 
  match->b_y_ion_possible = 
    get_scorer_sp_b_y_ion_possible(scorer); 
}

/**
 * gets the match b_y_ion_fraction_matched
 */
FLOAT_T get_match_b_y_ion_fraction_matched(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->b_y_ion_fraction_matched;
}

/**
 * gets the match b_y_ion_matched
 */
int get_match_b_y_ion_matched(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->b_y_ion_matched;
}

/**
 * gets the match b_y_ion_possible
 */
int get_match_b_y_ion_possible(
  MATCH_T* match ///< the match to work -out
  )
{
  return match->b_y_ion_possible;
}

/**
 *Increments the pointer count to the match object
 */
void increment_match_pointer_count(
  MATCH_T* match ///< the match to work -in  
  )
{
  ++match->pointer_count;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */

