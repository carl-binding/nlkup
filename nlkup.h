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

#ifndef _NLKUP_H_
#define _NLKUP_H_

#include <pthread.h>

#include "queue.h"

// E.164 stipulates max length of 15...
#define PREFIX_LENGTH 6
#define POSTFIX_MAX_LENGTH 9
#define MAX_NBR_LENGTH (PREFIX_LENGTH + POSTFIX_MAX_LENGTH)  // 15 decimal digits

// 9 bytes
#define ALIAS_LENGTH ((MAX_NBR_LENGTH+1)/2 + 1)  
// 6 bytes
#define POSTFIX_LENGTH ((POSTFIX_MAX_LENGTH+1)/2 + 1)

typedef struct {
  // one length byte, 2 digits per byte
  unsigned char postfix[POSTFIX_LENGTH]; // 6 bytes
  // one length byte, 2 digits per byte
  unsigned char alias[ALIAS_LENGTH];  // 9 bytes
} LkupTblEntry; // 15 bytes

typedef struct {
  LkupTblEntry *table;     // pointer to data
  unsigned long table_sz;  // total size
  unsigned long table_len; // in use count
} LkupTbl, *LkupTblPtr;

// we allow locking of individual slots in the index table to enable
// multithreading
typedef struct {
  pthread_mutex_t mutex; // thread-safety 
  LkupTblPtr table;  // loookup table for a 6 digit number prefix
} IdxTblEntry;

// 6 decimal digits => 1'000'000 entries of which 100'000 are not used
#define INDEX_SIZE 1000000L
#define INDEX_OFFSET 100000L

// we have 1 million index table entries. to reach 100 million, we would have
// about 100 entries per lookup table....
#define DEF_LKUP_BLK_SIZE 10 // 100L 

/* we allocate an index table containing references to LkupTbl. Each LkupTbl is then
   allocated dynamically if needed. Assumption is that numbers are not distributed
   uniformly over the prefix space. Allocating 1 million lookup-tables eats up a lot
   of storage....
*/

#define FALSE 0
#define TRUE 1

#define SUCCESS         0

#define FAILURE        -1
#define NBR_TOO_SHORT  -2
#define ILLEGAL_NUMBER -3
#define NO_LOOKUP_TABLE -4
#define NO_SUCH_ENTRY   -5
#define NOT_LOGGED_IN   -6
#define NOT_ENOUGH_DATA -7

// to lock an index table entry for a given prefix
void lock_table( IdxTblEntry index_table[], int idx);
// to unlock an index table entry for a given prefix
void unlock_table( IdxTblEntry index_table[], int idx);

// nlkup.c 
int enter_entry( IdxTblEntry index_table[], const unsigned char *nbr, const unsigned char *alias);
int search_entry( IdxTblEntry index_table[], const unsigned char *nbr, unsigned char **alias);
int delete_entry( IdxTblEntry index_table[], const unsigned char *nbr);

// entry points from HTTP server code.
int nlkup_search_entry( const unsigned char *nbr, unsigned char **alias);
int nlkup_get_block( const unsigned char *nbr, LkupTblPtr *table);
int nlkup_get_range( const unsigned char *nbr, const unsigned char *postfix_range_len, LkupTblPtr *table);

typedef struct {
  char nbr[MAX_NBR_LENGTH+1];
  char alias[MAX_NBR_LENGTH+1];
} NumberAliasStruct;

int nlkup_get_range_around( const unsigned char *nbr, const int nbr_before, const int nbr_after, 
			    int *data_len, NumberAliasStruct *data[]);

int nlkup_delete_entry( const unsigned char *nbr);
int nlkup_enter_entry( const unsigned char *nbr, const unsigned char *alias);
int nlkup_dump_file( const unsigned char *fn, int binary);
int nlkup_restore_file( const unsigned char *fn, int binary);
int nlkup_process_file( const unsigned char *fn);

int nlkup_init();

// dumping one lookup table
int dump_table( IdxTblEntry index_table[], int idx, FILE *f, int binary);
// dumping entire index table
int dump_all( IdxTblEntry index_table[], FILE *f, int binary);
// dumping entire index table to given file name
int dump_all_fn( IdxTblEntry index_table[], const unsigned char *fn, int binary);

// restoring (binary) dump from file
int restore_all_fn( IdxTblEntry index_table[], const unsigned char *fn);

unsigned char *table_to_json( const LkupTblPtr table, const int status, const unsigned char *nbr);
unsigned char *status_to_json( const int status, const unsigned char *msg);
JSON_Buffer number_aliases_to_json( const int data_len, const NumberAliasStruct *data);

#endif
