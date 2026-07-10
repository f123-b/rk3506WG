#include "config_file.h"
#include <stdlib.h>
config_file_t *config_load(const char *p){(void)p;return NULL;}
void config_free(config_file_t *c){(void)c;}
const char *config_get_str(config_file_t *c,const char *k,const char *d){(void)c;(void)k;return d;}
int config_get_int(config_file_t *c,const char *k,int d){(void)c;(void)k;return d;}
