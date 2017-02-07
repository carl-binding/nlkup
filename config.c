
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>

#include "utils.h"
#include "mem.h"
#include "logger.h"
#include "hashtable.h"

static HT_Table lkup_tbl = NULL;

int CFG_init( const char *fn) {
  
  lkup_tbl = HT_new( 257, (HT_CompareTo) strcmp, (HT_Hash) HT_DJB_hash);

  FILE *f = fopen( fn, "r");
  if ( f == NULL) {
    fprintf( stderr, "config: failure to open %s\n", fn);
    return -1;
  }

  char str_buf[1024];

  while ( fgets( str_buf, sizeof( str_buf), f) != NULL) {
    
    if ( strlen( str_buf) == 0 || all_spaces( str_buf) || str_buf[0] == '#') {
      continue;
    }

    int nbr_tokens;
    char **tokens = str_split( str_buf, '=', &nbr_tokens);
    if ( tokens == NULL) {
      fprintf( stderr, "config: str_split returned NULL\n");
      continue;
    }
    if ( nbr_tokens < 2 || tokens[0] == NULL || tokens[1] == NULL) {
      fprintf( stderr, "config: str_split returned not enough tokens\n");      
      continue;
    }

    fprintf( stderr, "config: %s %s\n", tokens[0], tokens[1]);

    HT_insert( lkup_tbl, strdup( tokens[0]), strdup( tokens[1]), 1);
    
    free_tokens( tokens);
  }
  
  fclose( f);
}

char *CFG_get_str( const char *key, char *def_val) {
  char *cp = HT_lookup( lkup_tbl, key);
  if ( cp == NULL) {
    return def_val;
  }
  return cp;
}

int CFG_get_int( const char *key, int def_val) {
  char *cp = HT_lookup( lkup_tbl, key);
  if ( cp == NULL) {
    return def_val;
  }
  int i = atoi( cp);
  return i;
}
