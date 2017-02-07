#ifndef _CONFIG_H_
#define _CONFIG_H_

int CFG_open( const char *fn);

int CFG_get_int( const char *key, int def_val);

char *CFG_get_str( const char *key, char *def_val);

#endif
