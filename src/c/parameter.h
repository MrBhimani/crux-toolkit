/**
 * \file parameter.h
 * $Revision: 1.28.2.2 $
 * \brief General parameter handling utilities. All values stored here.

 * \detail MUST declare ALL optional command line parameters and
 * required command line arguments here in initalialize_parameters.
 * Parameters can only be set via parse_cmd_line_into_params_hash()
 * which takes values from the command line and from an optional
 * parameter file (provided on the command line with the --parameter
 * option). Options are checked for correct type and legal values.
 * Exits with usage statement on error.  Parameter values can be
 * retrieved with get_<type>_paramter functions.
 * 
 ****************************************************************************/
#ifndef PARAMETER_FILE_H
#define PARAMETER_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "utils.h"
#include "crux-utils.h"
#include "carp.h"
#include "hash.h"
#include "objects.h"
#include "peptide.h"
#include "spectrum.h"
#include "peak.h"
#include "mass.h"
#include "scorer.h"
#include "parse_arguments.h"
#include "modifications.h"

#define PARAMETER_LENGTH 1024 
///< maximum length of parameter name and value in characters
#define NUM_PARAMS 512 ///< maximum number of parameters allowed
#define MAX_LINE_LENGTH 4096 ///< maximum line length in the parameter file
#define BILLION 1000000000.0 
#define SMALL_BUFFER 256
#define MAX_SET_PARAMS 256

#define NUMBER_PARAMETER_TYPES 11
///< number of elements in the parameter type enum

// TODO (BF 1-28-08): these should be private. move to parameter.c
/**
 * \enum Data types of parameters.  Used for checking valid parameter input
 * from user.
 *
 * To add a new parameter type:  
 *  (for NEW types) 1.create enum, 
 *                  2. create array of strings, 
 *                  3. write string-to-type,
 *                  4. write type-to-string
 *  (for ALL types) 5. add to parameter-type enum and strings
 *                  6. write get-type-parameter
 *                  7. write set-type-parameter
 *                  8. add to the check_type_and_bounds
 *
 */
enum parameter_type {
  INT_P,             ///< parameters of type int
  DOUBLE_P,          ///< parameters of type double
  STRING_P,          ///< parameters of type char*
  MASS_TYPE_P,       ///< parameters of type MASS_TYPE_T
  PEPTIDE_TYPE_P,    ///< parameters of type PEPTIDE_TYPE_T
  BOOLEAN_P,         ///< parameters of type BOOLEAN_T
  SORT_TYPE_P,       ///< parameters of type SORT_TYPE_T
  SCORER_TYPE_P,     ///< parameters of type SCORER_TYPE_T
  OUTPUT_TYPE_P,     ///< parameters of type MATCH_SEARCH_OUTPUT_MODE_T
  ION_TYPE_P,        ///< parameters of type ION_TYPE_T
  ALGORITHM_TYPE_P}; ///< parameters of type ALGORITHM_TYPE_T
typedef enum parameter_type PARAMETER_TYPE_T;

/**
 * /brief Initialize parameters to default values.
 *
 * Every required argument for every executable and every option and
 * its default value must be declared here.  Allocates hash tables for
 * holding parameter values.
 */
void initialize_parameters(void);

/**
 * free heap allocated parameters hash table
 */
void free_parameters(void);

/**
 * /brief Identify which of the parameters can be changed 
 * on the command line.  
 * /detail Provide a list of the parameter names
 * and the number of parameters in that list.
 * Requires that initialize_parameters() has been run.
 * /returns TRUE on success.
 */
BOOLEAN_T select_cmd_line_options(char**, int);

/**
 * /brief Identify the required command line arguments.
 * /detail  Provide a list of the argument names
 * and the number of arguments in that list.
 * Requires that initialize_parameters() has been run.
 * /returns TRUE on success.
 */
BOOLEAN_T select_cmd_line_arguments(char**, int);

/**
 * Take the command line string from main, find the parameter fil
 * option (if present), parse it's values into the hash, and parse
 * the command line options and arguments into the hash
 * main then retrieves the values through get_value
 */
BOOLEAN_T parse_cmd_line_into_params_hash(int, char**, char*);

/**
 * Each of the following functions searches through the hash table of
 * parameters, looking for one whose name matches the string.  The
 * function returns the corresponding value.
 * \returns TRUE if paramater value is TRUE, else FALSE
 */ 
BOOLEAN_T get_boolean_parameter(
 char*     name  ///< the name of the parameter looking for -in
 );

/**
 * Searches through the list of parameters, looking for one whose
 * name matches the string.  This function returns the parameter value if the
 * parameter is in the parameter hash table.  This
 * function exits if there is a conversion error. 
 *\returns the int value of the parameter
 */
int get_int_parameter(
  char* name  ///< the name of the parameter looking for -in
  );

/**
 * Searches through the list of parameters, looking for one whose
 * name matches the string.  This function returns the parameter value if the
 * parameter is in the parameter hash table.  This
 * function exits if there is a conversion error. 
 *\returns the double value of the parameter
 */
double get_double_parameter(
  char* name   ///< the name of the parameter looking for -in
  );

/**
 * Searches through the list of parameters, looking for one whose
 * parameter_name matches the string. 
 * The return value is allocated here and must be freed by the caller.
 * If the value is not found, abort.
 * \returns the string value to which matches the parameter name, else aborts
 */
char* get_string_parameter(
  char* name  ///< the name of the parameter looking for -in
  );

/**
 * Searches through the list of parameters, looking for one whose
 * parameter_name matches the string. 
 * The return value is a pointer to the original string
 * Thus, user should not free, good for printing
 * \returns the string value to which matches the parameter name, else aborts
 */
char* get_string_parameter_pointer(
  char* name  ///< the name of the parameter looking for -in
  );

MASS_TYPE_T get_mass_type_parameter(
 char* name
 );

SORT_TYPE_T get_sort_type_parameter(
 char* name
 );

ALGORITHM_TYPE_T get_algorithm_type_parameter(
 char* name
 );

SCORER_TYPE_T get_scorer_type_parameter(
 char* name
 );

MATCH_SEARCH_OUTPUT_MODE_T get_output_type_parameter(
 char* name
 );

ION_TYPE_T get_ion_type_parameter(
 char* name
 );

/**
 * Searches through the list of parameters, 
 * looking for one whose name matches the string.  
 * Returns a peptide_type enumerated type (in objects.h)
 */ 
PEPTIDE_TYPE_T get_peptide_type_parameter(
  char* name
  );


/**
 * Prints the parameters.  If lead_string is not null, preprends it to
 * each line.
 */
void print_parameters(
  char* first_line,  ///< the first line to be printed before the parameter list -in
  char* parameter_filename,  ///< the parameter file name -in
  char* lead_string,  ///< the lead string to be printed before each line -in
  FILE* outstream  ///< the output stream -out
  );

/**
 * \brief Get the pointer to the list of AA_MODs requested by the
 * user.  Does not include the c- and n-term mods.  Argument is a
 * reference to an array of pointers.  Return 0 and set mods == NULL
 * if there are no aa_mods.
 * \returns The number of items pointed to by mods
 */
int get_aa_mod_list(AA_MOD_T*** mods);

/**
 * \brief Get the pointer to the list of AA_MODs for the peptide
 * c-terminus.  Argument is a reference to an array of
 * pointers. Return 0 and set mods==NULL if there are no c-term 
 * mods.
 *
 * \returns The number of items pointed to by mods
 */
int get_c_mod_list(AA_MOD_T*** mods);

/**
 * \brief Get the pointer to the list of AA_MODs for the peptide
 * n-terminus.  Argument is a reference to an array of
 * pointers. Return 0 and set mods==NULL if there are no n-term 
 * mods.
 *
 * \returns The number of items pointed to by mods
 */
int get_n_mod_list(AA_MOD_T*** mods);

/**
 * \brief Get the pointer to the list of AA_MODs requested by the
 * user.  Includes aa_mods, c- and n-term mods.  Argument is a
 * reference to an array of pointers.  Returns 0 and sets mods == NULL
 * if there are no aa_mods.
 * \returns The number of items pointed to by mods
 */
int get_all_aa_mod_list(AA_MOD_T*** mods);


#endif
