
#ifndef __GANGLIA_MOD_WORKAROUND
#define __GANGLIA_MOD_WORKAROUND


/* This seems to be needed for multicpu and IO, it should be added
   to the public interface of Ganglia-dev */
#ifndef SYNAPSE_FAILURE
#define SYNAPSE_FAILURE -1
#endif

/* This is some handy stuff from lib/file.h that should be available
   to module implementors too */
char *skip_whitespace (const char *p);
char *skip_token (const char *p);


#endif

