
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>

#include "mem.h"
#include "logger.h"
#include "nlkup.h"

static LkupTblEntry *alloc_lkup_tbl_entry() {
  LkupTblEntry *e = mem_alloc( sizeof( LkupTblEntry));
  memset( (void *) e, 0, sizeof( LkupTblEntry));
  return e;
}

static void free_lkup_tbl_entry( LkupTblEntry *e) {
  memset( e, 0, sizeof( LkupTblEntry));
  mem_free( e);
}

// creates a copy of given lookup table
static LkupTbl *copy_lkup_tbl( LkupTbl *old_t) {
  if ( old_t == NULL) 
    return NULL;

  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = old_t->table_sz;
  t->table_len = old_t->table_len; // used entry count

  t->table = mem_alloc( t->table_sz * sizeof( LkupTblEntry));
  memcpy( t->table, old_t->table, t->table_len * sizeof( LkupTblEntry));

  return t;
  
}

// allocates an empty lookup table
static LkupTbl *alloc_lkup_tbl() {
  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = DEF_LKUP_BLK_SIZE;
  t->table_len = 0; // used entry count

  int tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  // fprintf( stderr, "table size = %d [bytes]\n", tbl_sz);

  t->table = mem_alloc( tbl_sz);
 
  return t;
}

// copying [from..to] from origin table into a newly allocated table
static LkupTbl *copy_lkup_tbl_range( LkupTbl *origin, int from_idx, int to_idx) {
  
  if ( to_idx - 1 <= from_idx) {
    return NULL;
  }

  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = to_idx - from_idx + 1;
  t->table_len = t->table_sz;

  int tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  // fprintf( stderr, "table size = %d [bytes]\n", tbl_sz);

  t->table = mem_alloc( tbl_sz);
  
  int i = 0;
  for ( i = from_idx; i <= to_idx; i++) {
    t->table[i-from_idx] = origin->table[i];
  }

  return t;
}

static void grow_table( LkupTbl *t) {

  assert( t->table_sz <= t->table_len);

  int tbl_sz = (t->table_sz + DEF_LKUP_BLK_SIZE) * sizeof( LkupTblEntry); // bytes
  int old_tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  LkupTblEntry *nt = mem_alloc( tbl_sz);

  // copy old table
  if ( t->table_sz > 0) {
    memcpy( nt, t->table, old_tbl_sz);
  }

  // clear out upper part of new table
  unsigned char *cp = (unsigned char *) nt;
  cp += t->table_sz * sizeof( LkupTblEntry);  // bytes...
  memset( cp, 0, tbl_sz - old_tbl_sz);

  if ( t->table != NULL) {
    mem_free( t->table);
    t->table = NULL;
  }

  t->table = nt;
  t->table_sz = t->table_sz + DEF_LKUP_BLK_SIZE; // # of records
  
}

static void shrink_table( LkupTbl *t) {

  assert( t->table_sz >= t->table_len + DEF_LKUP_BLK_SIZE);

  int new_tbl_sz = (t->table_sz - DEF_LKUP_BLK_SIZE) * sizeof( LkupTblEntry); // bytes
  int old_tbl_len = t->table_len * sizeof( LkupTblEntry);

  LkupTblEntry *nt = mem_alloc( new_tbl_sz);

  // copy old table into new.
  memcpy( nt, t->table, old_tbl_len);

  // ensure new table is zeroed out if spare space
  if ( new_tbl_sz > old_tbl_len) {
    unsigned char *cp = (unsigned char *) nt;
    cp += old_tbl_len;
    memset( cp, 0, new_tbl_sz - old_tbl_len);
  }

  // switch tables and adjust size
  mem_free( t->table);
  t->table = nt;
  t->table_sz -= DEF_LKUP_BLK_SIZE;

}

static void shift_table_up( LkupTbl *t, int idx) {
  assert( t->table_sz > t->table_len);
  // if idx is at end of table, nothing to do...
  if ( idx >= t->table_len) return;
  int i = 0;
  for ( i = t->table_len; i > idx; i--) {
    t->table[i] = t->table[i-1];
  }
}

static void shift_table_down( LkupTbl *t, int idx) {
  if ( idx >= t->table_len-1) return;
  int i = 0;
  for ( i = idx; i < t->table_len-1; i++) {
    t->table[i] = t->table[i+1];
  }
}

void free_lkup_tbl( LkupTbl *t) {
  if ( t->table != NULL) {
    memset( t->table, 0, t->table_sz * sizeof( LkupTblEntry));
    mem_free( t->table);
    t->table = NULL;
  }
  memset( t, 0, sizeof( LkupTbl));
  mem_free( t);
}

static int alloc_lkup_tbl_in_index( IdxTblEntry index_table[], int idx) {

  assert( idx >= 0 && idx <= INDEX_SIZE - INDEX_OFFSET);
  if ( index_table[idx].table != NULL) {
    return 0;
  }
  if ( ( index_table[idx].table = alloc_lkup_tbl()) == NULL) {
    return -1;
  }
  return 0;
}

static int free_lkup_tbl_in_index( IdxTblEntry index_table[], int idx) {
  assert( idx >= 0 && idx <= INDEX_SIZE - INDEX_OFFSET);
  if ( index_table[idx].table == NULL) {
    return 0;
  }
  free_lkup_tbl( index_table[idx].table);
  index_table[idx].table = NULL;
  return 0;
}

static int compare_entry( LkupTblEntry *e1, LkupTblEntry *e2) {
  assert( e1 != NULL && e2 != NULL);

  if ( e1 == e2) return 0;
  
  unsigned char *p1 = e1->postfix;
  unsigned char *p2 = e2->postfix;
  int i = 0;
  for ( i = 0; i < POSTFIX_LENGTH; i++) {
    if ( *p1 < *p2) return -1;
    if ( *p1 > *p2) return +1;
    p1++; p2++;
  }
  return 0;
}

// returns the first 6 digits of number as integer
static int get_index( const unsigned char *nbr) {

  if ( nbr == NULL || strlen( nbr) < PREFIX_LENGTH) {
    log_msg( ERR, "get_index: string too short or null\n");
    return NBR_TOO_SHORT;
  }

  unsigned char pfx[PREFIX_LENGTH+1];
  memcpy( pfx, nbr, PREFIX_LENGTH);
  pfx[PREFIX_LENGTH] = 0;

  long idx = atol( pfx);
  if ( idx <= 0) {
    log_msg( ERR, "get_index: negative or illegal conversion %s\n", nbr);
    return ILLEGAL_NUMBER;
  }
  assert( idx >= INDEX_OFFSET);
  return (int) idx - INDEX_OFFSET;
}

// binary search
// returns index (>= 0) if found or -insertion_point-1 if not found
static int search_entry_in_table( LkupTblPtr tbl, LkupTblEntry *e) {

  assert( tbl != NULL && e != NULL);

  if ( tbl->table_len == 0) return -1;
  
  // binary search
  int left = 0;
  int right = tbl->table_len - 1;
  int mid = 0;
  int cmp = 0;

  while ( left <= right) {

    mid = (left + right)/2;

    cmp = compare_entry( &tbl->table[mid], e);
    log_msg( DEBUG, "%d %d %d %d\n", left, right, mid, cmp);

    if ( cmp < 0) {
      left = mid + 1;
    } else if ( cmp > 0) {
      right = mid - 1;
    } else {
      return mid;
    }
  }

  // we return -insertion point - 1
  int ret_val = -mid - 1;
  if ( cmp < 0)
    ret_val -= 1;

  return ret_val;

}

// enters a new entry. if duplicate, overwrites the old alias
int enter_entry( IdxTblEntry index_table[], const unsigned char *nbr, const unsigned char *alias) {

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "enter_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  if ( index_table[idx].table == NULL) {
    index_table[idx].table = alloc_lkup_tbl();
  }

  LkupTblEntry key;
  memset( &key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key.postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "enter_entry: failure to compress %s\n", nbr);

    unlock_table( index_table, idx);
    return FAILURE;
  }

  LkupTbl *t = index_table[idx].table;

  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) { // key not found
    // e_idx is -insertion_point - 1
    int i_idx = -(e_idx+1);

    if ( t->table_len >= t->table_sz) { // table full
      grow_table( t);
    }

    assert( t->table_len < t->table_sz);
    shift_table_up( t, i_idx);
    
    LkupTblEntry *e = &(t->table[i_idx]);
    memcpy( e->postfix, key.postfix, POSTFIX_LENGTH);
    if ( compress_to_buf( alias, 0, strlen( alias), e->alias, ALIAS_LENGTH) < 0) {
      log_msg( ERR, "enter_entry: failure to compress %s\n", alias);

      unlock_table( index_table, idx);
      return FAILURE;
    }

    t->table_len++; // bump up counter of used entries

  } else { // found key

    // fprintf( stderr, "duplicate nbr: %s\n", nbr);

    LkupTblEntry *e = &(t->table[e_idx]);
    assert( compare_entry( e, &key) == 0);
    // overwrite alias
    if ( compress_to_buf( alias, 0, strlen( alias), e->alias, ALIAS_LENGTH) < 0) {
      log_msg( ERR, "enter_entry: failure to compress %s\n", alias);

      unlock_table( index_table, idx);
      return FAILURE;
    }
  }

  unlock_table( index_table, idx);
  return e_idx;  
}

static int search_entry_with_buffer(IdxTblEntry index_table[], const unsigned char *nbr, 
				    unsigned char alias[], const int alias_sz) 
{

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "search_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  LkupTbl *t = index_table[idx].table;

  if ( t == NULL) {
    unlock_table( index_table, idx);
    return NO_SUCH_ENTRY;
  }

  // allocate search key
  LkupTblEntry key;
  memset( &key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key.postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "search_entry: failure to compress %s\n", nbr);

    unlock_table( index_table, idx);
    return FAILURE;
  }

  // do the search
  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) {
    unlock_table( index_table, idx);
    return NO_SUCH_ENTRY;
  }

  // decompress into buffer
  LkupTblEntry *e = &(t->table[e_idx]);
  decompress_to_buf( e->alias, alias, alias_sz);

  unlock_table( index_table, idx);
  return SUCCESS;

}


static int set_up_search_key( LkupTblEntry *key, const unsigned char *nbr) {

  memset( key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key->postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "set_up_search_key: failure to compress %s\n", nbr);
    return FAILURE;
  }
  return SUCCESS;
}

// searches entry and set alias if found. 
// returns negative value if entry not found
int search_entry( IdxTblEntry index_table[], const unsigned char *nbr, unsigned char **alias) {

  *alias = mem_alloc( MAX_NBR_LENGTH + 1);
  int s = search_entry_with_buffer( index_table, nbr, *alias, MAX_NBR_LENGTH+1);
  if ( s < 0) {
    mem_free( *alias);
    *alias = NULL;
  }
  return s;
}

// deletes the given entry if present. no-op otherwise.
int delete_entry( IdxTblEntry index_table[], const unsigned char *nbr) {

  int status = SUCCESS;

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "delete_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  // no table, nothing to delete....
  if ( index_table[idx].table == NULL) {
    goto out;
  }

  // allocate search key
  LkupTblEntry key;

  if ( set_up_search_key( &key, nbr) != SUCCESS) {
    log_msg( ERR, "delete_entry: failure to set up search key %s\n", nbr);
    status = FAILURE;
    goto out;
  }

  LkupTbl *t = index_table[idx].table;

  // do the search
  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) { // no such entry. we're done
    status = SUCCESS;
    goto out;
  }
  // found the entry

  if ( t->table_len == 1) { // last entry
    mem_free( t);
    index_table[idx].table = NULL;

    status = SUCCESS;
    goto out;
  }

  shift_table_down( t, e_idx);
  t->table_len--;

  if ( t->table_sz - t->table_len >= DEF_LKUP_BLK_SIZE) {
    shrink_table( t);
  }
  
 out:

  unlock_table( index_table, idx);
  return status;

}

static void test_compression( unsigned char *s) {

  unsigned char *cs = compress( s, 0, strlen( s));
  if ( cs == NULL) return;

  unsigned char *dcs = decompress( cs);
  if ( dcs == NULL) return;

  fprintf( stderr, "%s %s\n", s, dcs);

  mem_free( dcs);
  mem_free( cs);
}

// allocate index table with 1 million entries... of which we keep 100'000 empty
static IdxTblEntry index_table[INDEX_SIZE-INDEX_OFFSET];

static void test_search( unsigned char *nbr) {
  unsigned char *alias = NULL;
  if ( search_entry( index_table, nbr, &alias) < 0) {
    fprintf( stderr, "nbr not found\n");
  } else {
    fprintf( stderr, "nbr alias = %s\n", alias);
    mem_free( alias);
  }
}

// initializing index table
static void init_index( IdxTblEntry index_table[]) {
  int i = 0;
  for ( i = 0; i < INDEX_SIZE - INDEX_OFFSET; i++) {
    IdxTblEntry *e = &(index_table[i]);

    pthread_mutex_init( &(e->mutex), NULL);

  }
}

/*
  Copyright (c) 2017, Carl Binding, LI-9494 Schaan

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those
 of the authors and should not be interpreted as representing official policies,
 either expressed or implied, of the FreeBSD Project.
*/


// init the module
int nlkup_init() {

  init_index( index_table);

  if ( restore_all_fn( index_table, "dump.bin") != SUCCESS) {
    log_msg( ERR, "nlkup_init: restore_all_fn() failed");
    return -1;
  }

  return 0;
}

int nlkup_enter_entry( const unsigned char *nbr, const unsigned char *alias) {
  return enter_entry( index_table, nbr, alias);
}

int nlkup_delete_entry( const unsigned char *nbr) {
  return delete_entry( index_table, nbr);
}

// looks up the given number and returns the alias which must be mem_freed() if non NULL
int nlkup_search_entry( const unsigned char *nbr, unsigned char **alias) {
  return search_entry( index_table, nbr, alias);
}

int nlkup_get_range( const unsigned char *nbr, const unsigned char *postfix_range_len, LkupTblPtr *table) {

  *table = NULL;

  if ( nbr == NULL || strlen( nbr) < PREFIX_LENGTH || postfix_range_len == 0 || strlen( postfix_range_len) == 0) {
    return FAILURE;
  }

  int pfx_len = atoi( postfix_range_len);
  if ( pfx_len < 0) {
    log_msg( ERR, "nlkup_get_range: negative postfix range %s\n", postfix_range_len);
    return FAILURE;
  }

  // expand the nbr with pfx_lens "0" and "9": this gives us a range.
  unsigned char from_nbr[MAX_NBR_LENGTH+1];
  unsigned char to_nbr[MAX_NBR_LENGTH+1];
  
  memset( from_nbr, 0, sizeof( from_nbr));
  memset( to_nbr, 0, sizeof( to_nbr));

  strncpy( from_nbr, nbr, sizeof( from_nbr));
  strncpy( to_nbr, nbr, sizeof( to_nbr));

  int i = 0;
  int nbr_len = strlen( nbr);
  for ( i = nbr_len; (i < nbr_len + pfx_len) && (i <= MAX_NBR_LENGTH); i++) {
    from_nbr[i] = '0';
    to_nbr[i] = '9';
  }

  log_msg( DEBUG, "nlkup_get_range: from_nbr %s to_nbr %s\n", from_nbr, to_nbr);

  // find the indices of nbr+"00000" and nbr+"99999"
  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "nlkup_get_range: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  int status = SUCCESS;
  lock_table( index_table, idx);

  LkupTbl *t = index_table[idx].table;

  // no table, no data...
  if ( t == NULL) {
    goto out;
  }

  LkupTblEntry from_key;
  LkupTblEntry to_key;

  if ( set_up_search_key( &from_key, from_nbr) != SUCCESS || 
       set_up_search_key( &to_key, to_nbr) != SUCCESS) {
    log_msg( ERR, "nlkup_get_range: setting up search keys %s %s\n", from_nbr, to_nbr);
    status = FAILURE;
    goto out;
  }

  int from_idx = search_entry_in_table( t, &from_key);
  int to_idx = search_entry_in_table( t, &to_key);

  // log_msg( DEBUG, "nlkup_get_range: from_idx %d to_idx %d\n", from_idx, to_idx);

  // idx < 0: not found. index = -insertion_point - 1
  if ( from_idx < 0) { // insertion point is *after* next smaller entry
    from_idx = -from_idx-1;
  }
  if ( to_idx < 0) { // insertion point is *after* last largest entry. hence -1 once more
    to_idx = -to_idx-1-1;
  }

  log_msg( DEBUG, "nlkup_get_range: from_idx %d to_idx %d\n", from_idx, to_idx);

  assert( from_idx >= 0 && from_idx < t->table_len);
  assert( to_idx >= 0 && to_idx < t->table_len);
  assert( from_idx <= to_idx);

  // copy data into a newly allocated table, [from_idx..to_idx]
  LkupTbl *new_table = copy_lkup_tbl_range( t, from_idx, to_idx);

  *table = new_table;

 out:
  unlock_table( index_table, idx);
  return status;

}


// attempts to retrieve the block of given number. must be mem_freed() if non NULL
int nlkup_get_block( const unsigned char *nbr, LkupTblPtr *table) {
  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "nlkup_get_block: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);
  LkupTbl *t = index_table[idx].table;
  if ( t == NULL) {
    *table = NULL;
  } else {
    *table = copy_lkup_tbl( t);
  }
  unlock_table( index_table, idx);
  return SUCCESS;
}

#ifdef _NLKUP_MAIN_

int main( int argc, char **argv) {

#if 0

  void *pp = mem_alloc( 100);
  mem_free( pp);
  exit( 0);

  LkupTblEntry *e = alloc_lkup_tbl_entry();
  free_lkup_tbl_entry( e);

  LkupTbl *t = alloc_lkup_tbl();
  free_lkup_tbl( t);

#endif

#if 0

  fprintf( stderr, "sizeof( LkupTblEntry) = %ld\n", sizeof( LkupTblEntry));
  alloc_lkup_tbl_in_index( index_table, 100);
  alloc_lkup_tbl_in_index( index_table, 200);
  alloc_lkup_tbl_in_index( index_table, 12345);
  alloc_lkup_tbl_in_index( index_table, 12346);

  free_lkup_tbl_in_index( index_table, 12346);

#endif

#if 0

  {
    test_compression( "12345678901234");
    test_compression( "123");
    test_compression( "1232341204812034810239481023948123");
    test_compression( "12345AA");

  }

  enter_entry( index_table, "1234561000", "1234562000");
  enter_entry( index_table, "1234561005", "1234562005");
  enter_entry( index_table, "1234561010", "1234562010");
  enter_entry( index_table, "1234561015", "1234562015");
  enter_entry( index_table, "1234561020", "1234562020");
  enter_entry( index_table, "1234561025", "1234562025");
  enter_entry( index_table, "1234561030", "1234562030");
  enter_entry( index_table, "1234561035", "1234562035");
  enter_entry( index_table, "1234561012", "1234562012");
  enter_entry( index_table, "1234561033", "1234562033");
  enter_entry( index_table, "1234561003", "1234562003");
  enter_entry( index_table, "1234561002", "1234562002");

  // enter_entry( index_table, "1234561012", "1234562112");
  // enter_entry( index_table, "1234561033", "1234562133");
  // enter_entry( index_table, "1234561003", "1234562103");
  // enter_entry( index_table, "1234561002", "1234562102");

  dump_table( index_table, 123456, stderr, FALSE);

  test_search( "1234561000");
  test_search( "1234561012");
  test_search( "1234561003");
  test_search( "1234561002");
  test_search( "1234561033");
  test_search( "1234561014");

  delete_entry( index_table, "1234561012");
  delete_entry( index_table, "1234561033");
  delete_entry( index_table, "1234561003");
  delete_entry( index_table, "1234561002");

  dump_table( index_table, 123456, stderr, FALSE);

  dump_all_fn( index_table, "dump.txt", FALSE);

#endif

  test_index( index_table);
  dump_all_fn( index_table, "dump.txt", FALSE);
  dump_all_fn( index_table, "dump.bin", TRUE);

  restore_all_fn( index_table, "dump.bin");
  dump_all_fn( index_table, "dump2.txt", FALSE);

  fprintf( stderr, "done\n");

}

#endif
