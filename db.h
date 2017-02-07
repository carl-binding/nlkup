#ifndef _DB_H_
#define _DB_H_

int db_open( const unsigned char *user_name,
	     const unsigned char *password,
	     const unsigned char *host,
	     const unsigned char *db_name,
	     long port); // default if < 0

int db_close();

#endif
