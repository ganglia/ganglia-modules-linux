#include <gm_metric.h>
/* #include <libmetrics.h> */
#include <utmp.h>

#include <apr_thread_mutex.h>
#include <apr_strings.h>
#include <apr_hash.h>

mmodule user_module;

static apr_pool_t *pool;
static apr_thread_mutex_t *mutex;

static int user_metric_init ( apr_pool_t *p )
{
    int i;

    libmetrics_init();

    pool = p;
    apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_UNNESTED, pool);

    for (i = 0; user_module.metrics_info[i].name != NULL; i++) {
        /* Initialize the metadata storage for each of the metrics and then
         *  store one or more key/value pairs.  The define MGROUPS defines
         *  the key for the grouping attribute. */
        MMETRIC_INIT_METADATA(&(user_module.metrics_info[i]),p);
        MMETRIC_ADD_METADATA(&(user_module.metrics_info[i]),MGROUP,"user");
    }

    return 0;
}

static void user_metric_cleanup ( void )
{
    apr_thread_mutex_destroy(mutex);
}

/* count the number of users */
static int get_users ( void )
{
    int numuser;
    struct utmp *utmpstruct;

    numuser = 0;

    apr_thread_mutex_lock(mutex);
    setutent();
    while ((utmpstruct = getutent())) {
      if ((utmpstruct->ut_type == USER_PROCESS) &&
         (utmpstruct->ut_name[0] != '\0'))
        numuser++;
    }
    endutent();
    apr_thread_mutex_unlock(mutex);

    return numuser;
}

/* count the number of users */
static int get_unique ( void )
{
    apr_pool_t *subpool;
    apr_hash_t *hashuser;
    char *name;
    void *user;
    unsigned int numunique = 0;
    struct utmp *utmpstruct;

    apr_pool_create(&subpool, pool);
    hashuser = apr_hash_make(subpool);
    apr_thread_mutex_lock(mutex);
    setutent();
    while ((utmpstruct = getutent())) {
        if ((utmpstruct->ut_type == USER_PROCESS) &&
           (utmpstruct->ut_name[0] != '\0')) {
            name = apr_pstrndup(subpool, utmpstruct->ut_name, UT_NAMESIZE);
            user = name; /* use key for value, not interested in it anyway */
            apr_hash_set(hashuser, name, APR_HASH_KEY_STRING, user);
        }
    }
    endutent();
    apr_thread_mutex_unlock(mutex);
    numunique = apr_hash_count(hashuser);
    apr_pool_destroy(subpool);

    return numunique;
}

static g_val_t user_metric_handler ( int metric_index )
{
    g_val_t val;

    /* The metric_index corresponds to the order in which
       the metrics appear in the metric_info array
    */
    switch (metric_index) {
    case 0:
        val.uint32 = get_users();
        return val;
    case 1:
        val.uint32 = get_unique();
        return val;
    }

    /* default case */
    val.uint32 = 0;
    return val;
}

static Ganglia_25metric user_metric_info[] = 
{
    {0, "user_total", 950, GANGLIA_VALUE_UNSIGNED_INT, " ", "both", "%u", UDP_HEADER_SIZE+8, "Total number of users logged in"},
    {0, "user_unique", 950, GANGLIA_VALUE_UNSIGNED_INT, " ", "both", "%u", UDP_HEADER_SIZE+8, "Number of unique users logged in"},
    {0, NULL}

};

mmodule user_module =
{
    STD_MMODULE_STUFF,
    user_metric_init,
    user_metric_cleanup,
    user_metric_info,
    user_metric_handler,
};

