#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <mysql/mysql.h>
#include <pthread.h>

#include "db.h"

static pthread_mutex_t db_mutex;

#define LOCK( )         pthread_mutex_lock( &db_mutex)
#define UNLOCK( )       pthread_mutex_unlock( &db_mutex)
#define DESTROY_LOCK( ) pthread_mutex_destroy( &db_mutex)
#define CREATE_LOCK( )  pthread_mutex_init( &db_mutex, NULL)

static MYSQL *db_connection = NULL;

#define DEFAULT_PORT 3306  // default MySQL port

int db_open( const unsigned char *user,
	     const unsigned char *password,
	     const unsigned char *host,
	     const unsigned char *database,
	     long port) {

  if ( CREATE_LOCK() != 0) {
    fprintf( stderr, "pthread_mutex_init failed\n");
    return -1;
  }

  LOCK();

  if ( port < 0) {
    port = DEFAULT_PORT;
  }

  if ( db_connection != NULL) {
    UNLOCK();
    return 0;
  }

  db_connection = mysql_init(NULL);
  if ( db_connection == NULL) {
    fprintf(stderr, "mysql_init failure: %s\n", mysql_error(db_connection));

    UNLOCK();
    DESTROY_LOCK();

    return -1;
  }

  if (!mysql_real_connect( db_connection, host, user, password, database, port, NULL, 0)) {
    fprintf(stderr, "mysql_real_connect failure: %s\n", mysql_error( db_connection));
    mysql_close( db_connection);
    db_connection = NULL;

    UNLOCK();
    DESTROY_LOCK();

    return -1;
  }

  UNLOCK();
  return 0;
}

int db_close() {

  LOCK();

  if ( db_connection == NULL) {
    UNLOCK();
    return 0;
  }

  mysql_close( db_connection);
  db_connection = NULL;

  UNLOCK();
  DESTROY_LOCK();

  return 0;
}


int main( int argc, char **argv) {

  if ( db_open( "root", "welcome123", "localhost", "test", -1) < 0) {
    fprintf( stderr, "failure to connect\n");
    return -1;
  } else {
    fprintf( stderr, "connected to DB\n");
  }
  if ( db_close() < 0) {
    fprintf( stderr, "failure to close\n");
    return -1;
  }
  fprintf( stderr, "done\n");
  return 0;
}
