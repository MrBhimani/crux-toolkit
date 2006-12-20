#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "check-peak.h"
#include "../mass.h"
#include "../objects.h"
#include "../spectrum.h"
#include "../peak.h"
#include "../peptide.h"
#include "../peptide_src.h"
#include "../protein.h"
#include "../database.h"
#include "../carp.h"
#include "../crux-utils.h"
#include "../database.h"



PEPTIDE_T* peptide2;
PEPTIDE_T* peptide3;
PEPTIDE_T* peptide4;
PEPTIDE_T* peptide5;
PEPTIDE_T* peptide6;
PEPTIDE_T* peptide1;
PROTEIN_T* protein1;
PROTEIN_T* protein2;
PROTEIN_T* protein3;
PROTEIN_PEPTIDE_ITERATOR_T* iterator;
PEPTIDE_CONSTRAINT_T* constraint;

PEPTIDE_SRC_T* association1;
PEPTIDE_SRC_T* association2;
PEPTIDE_SRC_T* association3;

DATABASE_T* db;

START_TEST (test_create){
  char* name = NULL;

  //try create a new database
  db = new_database("fasta_file", TRUE);
  fail_unless(parse_database(db), "failed to parse database");
  fail_unless(strncmp((name = get_database_filename(db)), "fasta_file", 10) == 0, "database filename not set correctly");
  free(name);
  fail_unless(get_database_is_parsed(db), "database parsed field not correctly set");
  fail_unless(get_database_num_proteins(db) == 3, "database number of proteins not set correctly");
  fail_unless(get_database_use_light_protein(db), "database use_light_protein not correctly set");
  set_database_use_light_protein(db, FALSE);
  fail_unless(!get_database_use_light_protein(db), "database use_light_protein not correctly set");
  set_database_use_light_protein(db, TRUE);

  //peptide constraint
  constraint = new_peptide_constraint(TRYPTIC, 0, 1200, 1, 10, 1, AVERAGE);
  
  /* test database peptide iterator */
  DATABASE_PEPTIDE_ITERATOR_T* iterator3 =
    new_database_peptide_iterator(db, constraint);

  int n = 0;
  while(database_peptide_iterator_has_next(iterator3)){
    peptide4 = database_peptide_iterator_next(iterator3);
    if(n == 0){
      peptide6 = database_peptide_iterator_next(iterator3);
      peptide5 = database_peptide_iterator_next(iterator3);
    }
    //print_peptide(peptide5, stdout);
    free_peptide(peptide4);
    ++n;
  }
  
  fail_unless(merge_peptides(peptide5, peptide6), "failed to merge");
  //print_peptide(peptide5, stdout);
  //print_peptide_in_format(peptide5, TRUE,  stdout);
  
  free_peptide(peptide5);
    
  /* test database protein iterator */
  DATABASE_PROTEIN_ITERATOR_T* iterator2 =
    new_database_protein_iterator(db);

  n = 0;
  while(database_protein_iterator_has_next(iterator2)){
    protein1 = database_protein_iterator_next(iterator2);
    ++n;
  }
  fail_unless(n == 3, "failed to iterate over all proteins");

  /* test database sorted peptide iterator */
  
  DATABASE_SORTED_PEPTIDE_ITERATOR_T* iterator4 =
    new_database_sorted_peptide_iterator(db, constraint, MASS, TRUE);

  /**
   * You should have to keep the database pointer around
   * The database pointer count system should be able to free database once
   * no longer any valid pointers are avaliable to the database
   */
  free_database(db);


  while(database_sorted_peptide_iterator_has_next(iterator4)){
    peptide5 = database_sorted_peptide_iterator_next(iterator4);

    //this print statement should still work eventhough we call "free_database(db)" before.
    print_peptide_in_format(peptide5, TRUE,  stdout);
    //print_peptide(peptide5, stdout);
    free_peptide(peptide5);
  }
  
  //free
  free_database_protein_iterator(iterator2);
  free_database_peptide_iterator(iterator3);
  free_database_sorted_peptide_iterator(iterator4);
  free_peptide_constraint(constraint);
}
END_TEST

Suite* database_suite(void){
  Suite *s = suite_create("database");
  TCase *tc_core = tcase_create("Core");
  suite_add_tcase(s, tc_core);
  tcase_add_test(tc_core, test_create);
  //tcase_add_checked_fixture(tc_core, setup, teardown);
  return s;
}
