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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#include <arpa/inet.h>

#include "mem.h"
#include "utils.h"
#include "logger.h"
#include "queue.h"
#include "nlkup.h"

// decompress into allocated buffer of given size.
int decompress_to_buf( const unsigned char data[], unsigned char dest[], const int dest_sz) {

  int len = data[0]; // length of final string, excluding null byte

  if ( dest_sz < len + 1) { // test buffer space, accounting 0 byte
    log_msg( ERR, "decompress_to_buf: %d <= %d\n", dest_sz, (len+1));
    return FAILURE;
  }

  // initialize buffer
  unsigned char *str_chars = dest;
  memset( str_chars, 0, dest_sz);

  int i, j = 0;
		
  // MSB at index 1, length byte at index 0
  int max_j = (len+1)/2 + 1; // upper index for compressed data, account for length byte
  for ( i = 0, j = 1; (i < len) && (j < max_j); i++) {

    unsigned char b = data[j];
    unsigned char c = '0';
    
    if ( i % 2 == 1) {
      c += b & 0xF;
      j++; // 2 nibbles per byte, i.e. 2 chars per byte.
    } else {
      c += (b & 0xF0) >> 4;      
    }
    str_chars[i] = c;
  }

  return SUCCESS;
  
}

/*
  decompress the compressed string of decimal digits and return
  the allocated C string data, null-terminated.
  returns NULL on failure
*/
unsigned char *decompress( const unsigned char data[]) {
		
  int len = data[0]; // length of final string

  unsigned char *str_chars = mem_alloc( sizeof( unsigned char) * len + 1);
  if ( str_chars == NULL) {
    return NULL;
  }

  int s = decompress_to_buf( data, str_chars, len+1);

  if ( s >= 0) {
    return str_chars;
  } else {
    mem_free( str_chars);
    return NULL;
  }
		
}

// to compress into given buffer. buffer must have enough space
int compress_to_buf( const unsigned char nbr[], const int from, const int nbr_len, unsigned char *dest, const int dest_sz) {
  
  // 2 digits per byte, one length byte. account for uneven nbr of digits... plus one byte for length
  unsigned int dest_len = (nbr_len+1)/2 + 1;
  if ( dest_len > dest_sz) {
    return FAILURE;
  }
  // dest_sz >= dest_len: we have enough space

  memset( dest, 0, dest_sz); // wipe out destination

  int len = 0; // counting string length
  int i = 0;
  int j = 0;

  // account for length byte for j >= 1
  // MSB at lower address, past length byte. starting at offset 1, going to (nbr_len+1)/2 + 1
  for ( i = from, j = 1; (i < (from+nbr_len)) && (j < dest_len); i++) {
			
    len++;
    
    unsigned char c = nbr[i];
    if ( c < '0' || c > '9') {
      log_msg( ERR, "illegal, non-digital, digit in %s\n", nbr);
      return FAILURE;
    }
			
    unsigned char nibble = (unsigned char) ((c - '0') & 0xF); // 0..9
			
    // 2 digits per byte
    if ( i % 2 == 1) {
      dest[j] |= nibble;
      j++;
    } else {
      dest[j] = (nibble << 4);
    }
  }
		
  dest[0] = (len & 0xFF); // LSB

  return len;
}

/*
  compress the given (sub-)string and return the compressed data as array of bytes.
  the length of the original string is stored at offset 0.
  most-significant character is at highest address.
  The size of the returned array is (nbr_len+1)/2 + 1 to account for uneven lengths and the length byte at offset 0.
  returns NULL in case of failure.
 */
unsigned char *compress( const unsigned char nbr[], const int from, const int nbr_len) {
  
  // 2 digits per byte, one length byte. account for uneven nbr of digits... plus one byte for length
  unsigned int dest_len = (nbr_len+1)/2 + 1;
  unsigned char *dest = mem_alloc( dest_len);

  if ( dest == NULL) {
    return NULL;
  }
  memset( dest, 0, dest_len);

  if ( compress_to_buf( nbr, from, nbr_len, dest, dest_len) < 0) {
    mem_free( dest);
    return NULL;
  }

  return dest;
}

  
static int dump_tbl_entry( LkupTblEntry *e, FILE *f, int binary) {
  if ( binary) {
    if ( fwrite( &e->postfix, sizeof( unsigned char), POSTFIX_LENGTH, f) != POSTFIX_LENGTH) {
      return FAILURE;
    }
    if ( fwrite( &e->alias, sizeof( unsigned char), ALIAS_LENGTH, f) != ALIAS_LENGTH) {
      return FAILURE;
    }
  } else {
    unsigned char *postfix = decompress( e->postfix);
    unsigned char *alias = decompress( e->alias);

    fprintf( f, "%s %s\n", postfix, alias);

    mem_free( postfix);
    mem_free( alias);
  }
  return SUCCESS;
}

int dump_table( IdxTblEntry index_table[], int idx, FILE *f, int binary) {

  if ( f == NULL) {
    f = stderr;
  }

  lock_table( index_table, idx);

  LkupTblPtr t = index_table[idx].table;

  if ( t == NULL) {
    if ( binary) {

      long block_header[3];
      block_header[0] = htonl( (long) (idx + INDEX_OFFSET));
      block_header[1] = block_header[2] = 0;

      if ( fwrite( block_header, sizeof( long), 3, f) != 3) {
	unlock_table( index_table, idx);
	return FAILURE;
      }

    } else {
      fprintf( f, "idx: %ld, sz: 0, len: 0\n", (long) (idx + INDEX_OFFSET));
    }

    unlock_table( index_table, idx);
    return SUCCESS;
  }

  if ( binary) {
      long block_header[3];

      // network byte order....
      block_header[0] = htonl( (long) (idx + INDEX_OFFSET));
      block_header[1] = htonl( (long) t->table_sz);
      block_header[2] = htonl( (long) t->table_len);

      if ( fwrite( block_header, sizeof( long), 3, f) != 3) {
	unlock_table( index_table, idx);
	return FAILURE;
      }

  } else {
    fprintf( f, "idx: %ld, sz: %ld, len: %ld\n", (long) (idx+INDEX_OFFSET), t->table_sz, t->table_len);
  }

  int i = 0;
  for ( i = 0; i < t->table_len; i++) {
    LkupTblEntry *e = &(t->table[i]);
    if ( dump_tbl_entry( e, f, binary) < SUCCESS) {
      unlock_table( index_table, idx);
      return FAILURE;
    }
  }

  unlock_table( index_table, idx);
  return SUCCESS;

}

int dump_all( IdxTblEntry index_table[], FILE *f, int binary) {
  if ( f == NULL) {
    f = stderr;
  }
  int i = 0;
  for ( i = 0; i < INDEX_SIZE-INDEX_OFFSET; i++) {
    if ( dump_table( index_table, i, f, binary) < SUCCESS) {
      return FAILURE;
    }
  }
  return SUCCESS;
}

int dump_all_fn( IdxTblEntry index_table[], const unsigned char *fn, int binary) {

  assert( fn != NULL && strlen( fn) > 0);

  FILE *f = fopen( fn, "w");
  if ( f == NULL) {
    log_msg( ERR, "failure to write-open %s\n", fn);
    return FAILURE;
  }

  log_msg( INFO, "starting dump to %s\n", fn);

  int s = dump_all( index_table, f, binary);

  log_msg( INFO, "dump done\n");
  
  if ( fclose( f) < 0) {
    log_msg( ERR, "failure to close %s\n", fn);
    return FAILURE;
  }
  return s;
}

static int restore_table( IdxTblEntry index_table[], int idx, FILE *f) {

  lock_table( index_table, idx);
  LkupTblPtr t = index_table[idx].table;

  // read the block header
  long block_header[3];
  if ( fread( block_header, sizeof( long), 3, f) != 3) {
    unlock_table( index_table, idx);
    return FAILURE;
  }

  // ensure host byte order
  int i = 0;
  for ( i = 0; i < 3; i++) {
    block_header[i] = ntohl( block_header[i]);
  }

  // first long is the index + INDEX_OFFSET
  assert( block_header[0] - INDEX_OFFSET == idx);

  if ( t == NULL) { // in-memory table empty
    if ( block_header[1] == 0) { // stays empty, we're done
      unlock_table( index_table, idx);
      return SUCCESS;
    } else { // in-memory table is empty, allocate table to accommodate file data
      index_table[idx].table = t = mem_alloc( sizeof( LkupTbl));
    }
  } else { // in-memory table not empty
    if ( block_header[1] == 0) { // table in file is empty

      // free in-memory table...
      if ( t->table != NULL) {
	mem_free( t->table); 
	t->table = NULL;
      }

      mem_free( t);
      index_table[idx].table = NULL;

      unlock_table( index_table, idx);
      return SUCCESS;      
    } // table in file not empty
  }

  // free old in-memory table
  if ( t->table != NULL) {
    mem_free( t->table); t->table = NULL;
  }

  // restore size and length
  t->table_sz = block_header[1];
  t->table_len = block_header[2];

  assert( t->table_sz >= t->table_len);

  // newly allocate in-memory table
  t->table = mem_alloc( sizeof( LkupTblEntry) * t->table_sz);

  int s = SUCCESS;

  // load table entries from file
  for ( i = 0; i < t->table_len; i++) {
    LkupTblEntry *e = &t->table[i];

    if ( fread( &e->postfix, sizeof( unsigned char), POSTFIX_LENGTH, f) != POSTFIX_LENGTH) {
      s = FAILURE;
      break;
    }

    if ( fread( &e->alias, sizeof( unsigned char), ALIAS_LENGTH, f) != ALIAS_LENGTH) {
      s = FAILURE;
      break;
    }
  }

  unlock_table( index_table, idx);
  return s;

}

static int restore_all( IdxTblEntry index_table[], FILE *f) {
  int i = 0;
  for ( i = 0; i < INDEX_SIZE-INDEX_OFFSET; i++) {
    if ( restore_table( index_table, i, f) < SUCCESS) {
      return FAILURE;
    }
  }
  return SUCCESS;
}

// restore from binary dump file
int restore_all_fn( IdxTblEntry index_table[], const unsigned char *fn) {

  assert( fn != NULL && strlen( fn) > 0);

  FILE *f = fopen( fn, "r");
  if ( f == NULL) {
    log_msg( ERR, "failure to read-open %s\n", fn);
    return FAILURE;
  }

  log_msg( INFO, "starting restore from %s\n", fn);

  int s = restore_all( index_table, f);

  log_msg( INFO, "restore done\n");
  
  if ( fclose( f) < 0) {
    log_msg( ERR, "failure to close %s\n", fn);
    return FAILURE;
  }

  return s;
}

// detect duplicate nbrs
static int duplicate_nbr(unsigned char *nbrs[], int idx) {
  int i = 0;
  for ( i = 0; i < idx; i++) {
    if ( strcmp( nbrs[idx], nbrs[i]) == 0) return TRUE;
  }
  return FALSE;
}

// generate random phone numbers
static void generate_nbrs( const unsigned char *pfx, unsigned char *nbrs[], int nbr_sz) {
  int i = 0;
  for ( i = 0; i < nbr_sz; i++) {
    long rnbr = rand() % 1000 + 1000;
    nbrs[i] = mem_alloc( 24);
    memset( nbrs[i], 0, 24);
    snprintf( nbrs[i], 24, "%s%ld", pfx, rnbr);

    // ensure that we have no duplicates
    if ( duplicate_nbr( nbrs, i)) {
      i--;
    }

  }
}

void test_index( IdxTblEntry index_table[]) {
#define NBR_TEST_PFIXES 4
#define NBR_TEST_NBRS   53
  unsigned char *prefixes[ NBR_TEST_PFIXES] = {"123456", "223456", "323456", "423456"};
  int i = 0;

  for ( i = 0; i < NBR_TEST_PFIXES; i++) {
    unsigned char *nbrs[NBR_TEST_NBRS];
    generate_nbrs( prefixes[i], nbrs, NBR_TEST_NBRS);

    int j = 0;
    for ( j = 0; j < NBR_TEST_NBRS; j++) {
      fprintf( stderr, "entering %d %s\n", j, nbrs[j]);
      enter_entry( index_table, nbrs[j], nbrs[j]);
    }

    for ( j = 0; j < (NBR_TEST_NBRS/10); j++) {
      // could try to delete twice same nbr...
      int idx = rand() % NBR_TEST_NBRS;
      fprintf( stderr, "deleting %d %s\n", idx, nbrs[idx]);
      delete_entry( index_table, nbrs[idx]);
    }

    for ( j = 0; j < NBR_TEST_NBRS; j++) {
      mem_free( nbrs[j]);
      nbrs[j] = NULL;
    }

  }
}


// memory statistics. allocated # of bytes on heap. zeroed out.
static long mem_cnt = 0;

void *mem_alloc( size_t sz) {
  sz += sizeof( long);
  mem_cnt += sz;

  char *cp = calloc( sz, sizeof( unsigned char));
  if ( cp == NULL) {
    return NULL;
  }

  *((long *) cp) = sz;

  // return ptr after length field
  return cp + sizeof( long);
}


void mem_free( void *ptr) {
  assert( ptr != NULL);
  char *lp = ((char *) ptr) - sizeof( long);
  long sz = *((long *) lp);
  mem_cnt -= sz;
  free( lp);
}

long mem_usage() {
  return mem_cnt;
}

void lock_table( IdxTblEntry index_table[], int idx) {
  pthread_mutex_lock( &index_table[idx].mutex);
}

void unlock_table( IdxTblEntry index_table[], int idx) {
  pthread_mutex_unlock( &index_table[idx].mutex);
}

#define JSON_BUFR_TABLE_HEADER_SIZE 2048

// generates a buffer containing a lookup table as JSON formatted data
// the returned buffer must be free()-ed by client
unsigned char *table_to_json( const LkupTblPtr table, const int status, const unsigned char *nbr) {
  
  unsigned char prefix[10]; 
  memset( prefix, 0, sizeof( prefix));
  strncpy( prefix, nbr, PREFIX_LENGTH);

  // we over-estimate the needed buffer size
  int buf_sz = JSON_BUFR_TABLE_HEADER_SIZE;
  if ( table != NULL && table->table_len > 0) {
    buf_sz += table->table_len * ( PREFIX_LENGTH + MAX_NBR_LENGTH + 32);
  }

  unsigned char *buf = calloc( buf_sz, sizeof( unsigned char));

  if ( table == NULL) {
    snprintf( buf, buf_sz, "{ \"status\" : %d, \"table\" : { \"idx\" : \"%s\", \"sz\" : %d, \"len\" : %d , \"data\" : [] }}\n", 
	      status, prefix, 0, 0);
    return buf;
  }

  unsigned char *cp = buf;
  unsigned long cnt = 0;
  int i = 0;

  cnt += snprintf( cp+cnt, buf_sz-cnt, "{ \"status\" : %d, \"table\" : { \"idx\" : \"%s\", \"sz\" : %ld, \"len\" : %ld, \"data\" : [ \n", 
		   status, prefix, table->table_sz, table->table_len);

  for ( i = 0; i < table->table_len; i++) {
    LkupTblEntry *e = &(table->table[i]);

    unsigned char postfix[32];
    unsigned char alias[48];

    decompress_to_buf( e->postfix, postfix, sizeof( postfix));
    decompress_to_buf( e->alias, alias, sizeof( alias));    

    cnt += snprintf( cp+cnt, buf_sz-cnt, "[ \"%s\", \"%s\" ]", postfix, alias);

  }

  cnt += snprintf( cp+cnt, buf_sz-cnt, "]}}\n");

  return buf;
}

long get_time_micro() {
  struct timeval  tv;
  gettimeofday(&tv, NULL);

  double time_in_micro = 
    (tv.tv_sec) * 1000000 + (tv.tv_usec);

  return (long) time_in_micro;
}

int all_digits( const unsigned char *s) {
  char *cp = (char *) s;

  while ( *cp != 0) {
    if ( *cp < '0' || *cp > '9') 
      return 0;
    cp++;
  }

  return 1;
}

char *str_trim( const char *s) {

  if ( s == NULL || strlen( s) == 0)
    return NULL;

  char *ss = calloc( sizeof( char), strlen(s) + 1);
  char *cp = (char *) s;
  char *cpp = ss;

  // get rid of left-hand spaces
  while ( *cp != 0 && isspace( *cp)) cp++;

  // get rid of right-hand spaces
  char *ep = (char *) (s + strlen( s) - 1);
  while ( ep != s && isspace( *ep)) ep--;
  ep++; // pointing to char AFTER left-most right-hand space

  // copy from [cp .. ep)
  while ( *cp != 0 && cp != ep) *cpp++=*cp++;
  
  return ss;
}

void free_tokens( char **tokens) {

  if ( tokens == NULL) 
    return;

  // last element in tokens is null
  int idx = 0;
  while ( *(tokens + idx) != NULL) {
    free( *(tokens + idx));
    *(tokens+idx) = NULL;
    idx++;
  }
  free( tokens);
}

// returns char [][] of split strings. last element is NULL, there is thus at least always one element in array which would be NULL.
// returned value must be freed
char** str_split(const char* a_str, const char a_delim, int *nbr_tokens)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = (char *) a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing last token. I.e. .... a_delim ..... */
    count += last_comma < (a_str + strlen(a_str) - 1);

    *nbr_tokens = count;  // the nbr of tokens without NULL marker

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = calloc(count, sizeof(char*));

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok( (char *) a_str, delim);

        while (token)
        {
            assert(idx < count);
	    char *tt = str_trim( token);
            *(result + idx++) = strdup(tt);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    } else {
      *nbr_tokens = 0;
    }

    return result;
}

int all_spaces( char *s) {
  char *cp = s;
  while ( *cp != 0 && isspace( *cp)) cp++;
  return ( *cp == 0); // reached end of string, all spaces
}

char *str_cat( const char *s, ...) {

  if ( s == NULL) 
    return NULL;
  
  // determine length of needed buffer
  int len = strlen( s);

  va_list argptr;
  va_start(argptr,s);
  while ( 1) {
    char *cp = va_arg(argptr, char *);
  
    if ( cp == NULL) break; // end marker

    len += strlen( cp);
  }
  va_end(argptr);

  char *buf = calloc( sizeof( char), len+1);

  strncpy( buf, s, len);

  va_start(argptr,s);
  while ( 1) {
    char *cp = va_arg(argptr, char *);
  
    if ( cp == NULL) break; // end marker

    int s_len = strlen( buf);
    strncat( buf+s_len, cp, len-s_len);
  }
  va_end(argptr);
  
  return buf;  
  
}
