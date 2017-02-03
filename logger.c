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
#include <pthread.h>

#include "logger.h"

static LOG_Level log_level = DEBUG;

LOG_Level log_get_level() {
  return log_level;
}

void log_set_level( LOG_Level level) {
  log_level = level;
}

static FILE *log_file = NULL;

int log_open( const unsigned char *fn) {
  if ( log_file != NULL) {
    return 0;
  }
  log_file = fopen( fn, "a");
  if ( log_file == NULL) {
    fprintf( stderr, "failure to open for append %s\n", fn);
    return -1;
  }
  return 0;
}

int log_close() {
  if ( log_file == NULL) {
    return 0;
  }
  int s = fclose( log_file);
  log_file = NULL;
  return s;
}


int log_msg( LOG_Level level, char *fmt, ...) {

  if ( level > log_level)
    return 0;
  // level <= log_level

  const int buf_sz = 4096;
  int cnt = 0;
  unsigned char buf[buf_sz];
  memset( buf, 0, buf_sz);

  time_t curtime;
  struct tm *loc_time;
 
  //Getting current time of system
  curtime = time (NULL);
 
  // Converting current time to local time
  loc_time = localtime (&curtime);
 
  // Displaying date and time in standard format
  cnt += strftime (buf, buf_sz, "%d-%m-%y %H:%M:%S : ", loc_time);

  // formatting message into buffer
  va_list argptr;
  va_start(argptr,fmt);
  cnt += vsnprintf(buf+cnt, buf_sz-cnt, fmt, argptr);
  va_end(argptr);

  if ( log_file != NULL) {
    fprintf( log_file, "%s", buf);
    fflush( log_file);
  }

  fprintf( stderr, "%s", buf);

  return 0;
}
