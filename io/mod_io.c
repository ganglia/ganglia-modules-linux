/*******************************************************************************
* Ported the disk IO metric work I had done on 3.0.x to 3.1.x.
* The code I originally had in libmetric/linux/metric.c was stripped and 
* placed in its own module.
*
* Author: JB Kim (jbremnant gmail.com)
******************************************************************************/

/*
 * The ganglia metric "C" interface, required for building DSO modules.
 */
#include <gm_metric.h>
#include <libmetrics.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
extern mmodule iostat_module;

static int iostat_metric_init ( apr_pool_t *p )
{
    const char* str_params = iostat_module.module_params;
    apr_array_header_t *list_params = iostat_module.module_params_list;
    mmparam *params;
    int i;

    libmetrics_init();

    /* Read the parameters from the gmond.conf file. */
    /* Single raw string parameter */
    if (str_params) {
        debug_msg("[mod_iostat] Received string params: %s", str_params);
    }
    /* Multiple name/value pair parameters. */
    if (list_params) {
        debug_msg("[mod_iostat] Received following params list: ");
        params = (mmparam*) list_params->elts;
        for(i=0; i< list_params->nelts; i++) {
            debug_msg("\tParam: %s = %s", params[i].name, params[i].value);
        }
    }

    for (i = 0; iostat_module.metrics_info[i].name != NULL; i++) {
        MMETRIC_INIT_METADATA(&(iostat_module.metrics_info[i]),p);
        MMETRIC_ADD_METADATA(&(iostat_module.metrics_info[i]),MGROUP,"disk");
    }

    return 0;
}


static void iostat_metric_cleanup ( void )
{
}

static g_val_t iostat_metric_handler ( int metric_index )
{
    g_val_t val;
		val.f = 0; // default

    /* The metric_index corresponds to the order in which
       the metrics appear in the metric_info array
    */
    switch (metric_index) {
#ifdef LINUX
    case 0:
			return io_readtot_func();
    case 1:
			return io_readkbtot_func();
    case 2:
			return io_writetot_func();
    case 3:
			return io_writekbtot_func();
    case 4:
			return io_svctmax_func();
    case 5:
			return io_queuemax_func();
    case 6:
			return io_busymax_func();
#endif
    default:
      return val; /* default fallback */
    }
    return val;
}

static Ganglia_25metric iostat_metric_info[] = 
{
#ifdef LINUX
	{0, "io_readtot",   120, GANGLIA_VALUE_FLOAT,          "Count",          "both",  "%.2f",UDP_HEADER_SIZE+8, "total number of reads"},
  {0, "io_readkbtot", 120, GANGLIA_VALUE_FLOAT,          "KB",         "both",  "%.3f",UDP_HEADER_SIZE+8, "total kilobytes read"},
  {0, "io_writetot",  120, GANGLIA_VALUE_FLOAT,          "Count",          "both",  "%.2f",UDP_HEADER_SIZE+8, "total number of writes"},
  {0, "io_writekbtot",120, GANGLIA_VALUE_FLOAT,          "KB",         "both",  "%.3f",UDP_HEADER_SIZE+8, "total kilobytes written"},
  {0, "io_svctmax",   120, GANGLIA_VALUE_FLOAT,          "msec",       "both",  "%.2f",UDP_HEADER_SIZE+8, "max service time across disks"},
  {0, "io_queuemax",  120, GANGLIA_VALUE_FLOAT,          " ",          "both",  "%.2f",UDP_HEADER_SIZE+8, "max queue time across disks"},
  {0, "io_busymax",   120, GANGLIA_VALUE_FLOAT,          "%",          "both",  "%.2f",UDP_HEADER_SIZE+8, "max busy time across disks"},
#endif
  {0, NULL}
};

mmodule iostat_module =
{
    STD_MMODULE_STUFF,
    iostat_metric_init,
    iostat_metric_cleanup,
    iostat_metric_info,
    iostat_metric_handler,
};
