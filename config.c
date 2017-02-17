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
#include <pthread.h>

#include "utils.h"
#include "mem.h"
#include "logger.h"
#include "hashtable.h"

static HT_Table lkup_tbl = NULL;

int CFG_init( const char *fn) {
  
  lkup_tbl = HT_new( 257, (HT_CompareTo) strcmp, (HT_Hash) HT_DJB_hash);
  if ( lkup_tbl == NULL) {
    fprintf( stderr, "config: HT_new failure\n");
    return -1;
  }

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
