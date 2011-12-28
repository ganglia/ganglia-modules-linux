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
#include <unistd.h>

#include <linux/major.h>

#include "file.h"

/* Iostat related info from jbkim
 *  The contents of /proc/partitions is different from kernel 2.4 to 2.6.
 *  Also new to 2.6 kernel is /proc/diskstats. The field descriptions can be found here:
 *   http://devresources.linux-foundation.org/dev/robustmutexes/src/fusyn.hg/Documentation/iostats.txt
 *  The io metrics are aggreagted for all the disks on the host.
 *  The partition specific info is best supplied via gmetric.
 */
#ifndef IDE_DISK_MAJOR
#define IDE_DISK_MAJOR(M) ((M) == IDE0_MAJOR || (M) == IDE1_MAJOR || \
         (M) == IDE2_MAJOR || (M) == IDE3_MAJOR || \
         (M) == IDE4_MAJOR || (M) == IDE5_MAJOR || \
         (M) == IDE6_MAJOR || (M) == IDE7_MAJOR || \
         (M) == IDE8_MAJOR || (M) == IDE9_MAJOR)
#endif  /* !IDE_DISK_MAJOR */

#ifndef SCSI_DISK_MAJOR
#ifndef SCSI_DISK8_MAJOR
#define SCSI_DISK8_MAJOR 128
#endif
#ifndef SCSI_DISK15_MAJOR
#define SCSI_DISK15_MAJOR 135
#endif
#define SCSI_DISK_MAJOR(M) ((M) == SCSI_DISK0_MAJOR || \
         ((M) >= SCSI_DISK1_MAJOR && \
          (M) <= SCSI_DISK7_MAJOR) || \
         ((M) >= SCSI_DISK8_MAJOR && \
          (M) <= SCSI_DISK15_MAJOR))
#endif  /* !SCSI_DISK_MAJOR */

#define MAX_PARTITIONS 64
#define PER_SEC(x) (1000.0 * (x) / deltams)

/* Kernel: 2.4 uses /proc/partitions and 2.6 uses /proc/diskstats) */
unsigned int kernel_type;   
unsigned int n_partitions;
unsigned int print_device = 1;
unsigned int print_partition = 0; // don't print the partitions

struct part_info {
  unsigned int major; /* Device major number */
  unsigned int minor; /* Device minor number */
  char name[64];
} partition[MAX_PARTITIONS];

struct blkio_info {
  unsigned int rd_ios;  /* Read I/O operations */
  unsigned int rd_merges; /* Reads merged */
  unsigned long long rd_sectors; /* Sectors read */
  unsigned int rd_ticks;  /* Time in queue + service for read */
  unsigned int wr_ios;  /* Write I/O operations */
  unsigned int wr_merges; /* Writes merged */
  unsigned long long wr_sectors; /* Sectors written */
  unsigned int wr_ticks;  /* Time in queue + service for write */
  unsigned int ticks; /* Time of requests in queue */
  unsigned int aveq;  /* Average queue length */
} new_blkio[MAX_PARTITIONS], old_blkio[MAX_PARTITIONS];

struct cpu_info {
  unsigned long long user;
  unsigned long long system;
  unsigned long long idle;
  unsigned long long iowait;
} new_cpu, old_cpu;

void init_partition_info(char **wanted_partitions, int wanted_partitions_n);
void print_io_info(void);

/* Never changes */
#ifndef BUFFSIZE
#define BUFFSIZE 16384
#endif

typedef struct {
  struct timeval last_read;
  float thresh;
  char *name;
  char buffer[BUFFSIZE];
} timely_file;

timely_file proc_stat       = { {0,0} , 1., "/proc/stat" };
timely_file proc_partitions = { {0,0} , 1., "/proc/partitions" };
timely_file proc_diskstats  = { {0,0} , 1., "/proc/diskstats" };



float timediff(const struct timeval *thistime, const struct timeval *lasttime)
{
  float diff;

  diff = ((double) thistime->tv_sec * 1.0e6 +
          (double) thistime->tv_usec -
          (double) lasttime->tv_sec * 1.0e6 -
          (double) lasttime->tv_usec) / 1.0e6;

  return diff;
}

char *update_file(timely_file *tf)
{
  int rval;
  struct timeval now;
  gettimeofday(&now, NULL);
  if(timediff(&now,&tf->last_read) > tf->thresh) {
    rval = slurpfile(tf->name, tf->buffer, BUFFSIZE);
    if(rval == SYNAPSE_FAILURE) {
      err_msg("update_file() got an error from slurpfile() reading %s",
              tf->name);
      return (char *)SYNAPSE_FAILURE;
    }
    else tf->last_read = now;
  }
  return tf->buffer;
}


/*
** A helper function to determine the number of cpustates in /proc/stat (MKN)
*/
#define NUM_CPUSTATES_24X 4
#define NUM_CPUSTATES_26X 7
static unsigned int num_cpustates;

unsigned int
num_cpustates_func ( void )
{
   char *p;
   unsigned int i=0;

   proc_stat.last_read.tv_sec=0;
   proc_stat.last_read.tv_usec=0;
   p = update_file(&proc_stat);
   proc_stat.last_read.tv_sec=0;
   proc_stat.last_read.tv_usec=0;

/*
** Skip initial "cpu" token
*/
   p = skip_token(p);
   p = skip_whitespace(p);
/*
** Loop over file until next "cpu" token is found.
** i=4 : Linux 2.4.x
** i=7 : Linux 2.6.x
*/
   while (strncmp(p,"cpu",3)) {
     p = skip_token(p);
     p = skip_whitespace(p);
     i++;
     }

   return i;
}



/*
 * From here starts the subroutines that implement disk io metric
 * functions for ganglia linux libmetric. I borrowed bulk of the logic
 * from iostat v2.2. (not part of the sysstat pkg),
 *
 * The source code was modified to fit the ganglia libmetric framework.
 * Note also that the IO metrics are aggregated (sum or max) over the
 * physical disks. This is _not_ the default behavior of iostat v2.2 pkg.
 * If you need granular, disk-by-disk or partition-by-partition io stats,
 * you should stay with using gmetric.
 *
 * - jbkim -
 */

/* nifty wrapper to return buffer for either diskstats or partitions
 * depending on the kernel version
 */
char * update_file_iostat(unsigned int kernel_type)
{
  if(kernel_type == 4)
    return update_file(&proc_partitions);
  else
    return update_file(&proc_diskstats);
}

/* to filter out the physical disks from the entire list */
int printable(unsigned int major, unsigned int minor)
{
  if (IDE_DISK_MAJOR(major)) {
    return (!(minor & 0x3F) && print_device) ||
      ((minor & 0x3F) && print_partition);
  } else if (SCSI_DISK_MAJOR(major)) {
    return (!(minor & 0x0F) && print_device) ||
      ((minor & 0x0F) && print_partition);
  } else {
    return 1; /* if uncertain, print it */
  }
}


/* registers disks (can also work with partitions, but we don't use it here) */
void init_partition_info(char **wanted_partitions, int wanted_partitions_n)
{
  const char *scan_fmt = NULL;
  char * buf;

  debug_msg("initializing partition info for mod_iostat");

  // supposedly older kernels don't have this file
  if(access("/proc/diskstats", R_OK)) {
    kernel_type = 4;
    scan_fmt = "%4d %4d %*d %31s %u";
  } else {
    kernel_type = 6;
    scan_fmt = "%4d %4d %31s %u";
  }

  if(!scan_fmt)
    err_msg("logic error in initialize(). cannot set scan_fmt");

  buf = update_file_iostat(kernel_type);

  while ( buf != NULL ) {
    unsigned int reads = 0;
    struct part_info curr;

    if (sscanf(buf, scan_fmt, &curr.major, &curr.minor,
         curr.name, &reads) == 4) {
      unsigned int p;

      // skipping other potential non-disk devices like VxDMP.
      // Better way to check valid disk devices should replace this.
      if(curr.major > 128) {
        buf = index(buf, '\n');
        if(buf != NULL) buf++;
        continue;
      }

      // to skip over the ones that exist and register a new one
      for (p = 0; p < n_partitions
             && (partition[p].major != curr.major
           || partition[p].minor != curr.minor);
           p++);

      if (p == n_partitions && p < MAX_PARTITIONS) {
        // if user specified the partition names
        if (wanted_partitions_n) {
          unsigned int j;

          for (j = 0;
            j < wanted_partitions_n && wanted_partitions[j]; j++) {
            if (!strcmp(curr.name, wanted_partitions[j])) { // if they match
              partition[p] = curr;
              n_partitions = p + 1;
            }
          }
        } else if (reads && printable(curr.major, curr.minor)) {
          partition[p] = curr;
          n_partitions = p + 1;
        }
      }
    } // sscanf
    // printf("looping with buf:\n%s\n", buf);

    // buf = index(buf, '\n')+1; // next line
    buf = index(buf, '\n');
    if(buf != NULL) buf++;
  }
}


/* this is where we process the contents of the /proc file line by line and
 * save them to our old and new structs
*/
void get_kernel_io_stats()
{
  const char *scan_fmt = NULL;
	const char * buffer;
  static struct timeval stamp= {0,0}; // permanent var for this func
	static int entry_count; // to detect the first count
	int i;

	buffer = update_file_iostat(kernel_type);

  if(kernel_type == 4)
    if ((proc_partitions.last_read.tv_sec != stamp.tv_sec) &&
        (proc_partitions.last_read.tv_usec != stamp.tv_usec)) {
      stamp = proc_partitions.last_read;
    } else {
      return;
    }
  else
    if ((proc_diskstats.last_read.tv_sec != stamp.tv_sec) &&
        (proc_diskstats.last_read.tv_usec != stamp.tv_usec)) {
      stamp = proc_diskstats.last_read;
    } else {
      return;
    }

	// save the new to old
	for (i = 0; i < n_partitions; i++)
    old_blkio[i] = new_blkio[i];

  old_cpu = new_cpu;


	// notice it skips the part names with %*s
	if(kernel_type == 4)
    scan_fmt = "%4d %4d %*d %*s %u %u %llu %u %u %u %llu %u %*u %u %u";
	else
    scan_fmt = "%4d %4d %*s %u %u %llu %u %u %u %llu %u %*u %u %u";

	if(!scan_fmt)
 		err_msg("logic error in get_kernel_io_stats(): can't set scan_fmt");


	while ( buffer != NULL ) {
		int items;
		struct part_info curr;
		struct blkio_info blkio;

		items = sscanf(buffer, scan_fmt,
             &curr.major, &curr.minor,
             &blkio.rd_ios, &blkio.rd_merges,
             &blkio.rd_sectors, &blkio.rd_ticks,
             &blkio.wr_ios, &blkio.wr_merges,
             &blkio.wr_sectors, &blkio.wr_ticks,
             &blkio.ticks, &blkio.aveq);


    /*
     * Unfortunately, we can report only transfer rates
     * for partitions in 2.6 kernels, all other I/O
     * statistics are unavailable.
     * For more info:
     * http://devresources.linux-foundation.org/dev/robustmutexes/src/fusyn.hg/Documentation/iostats.txt
     */
    if (items == 6) {
      blkio.rd_sectors = blkio.rd_merges;
      blkio.wr_ios = blkio.rd_sectors;
      blkio.wr_sectors = blkio.rd_ticks;
      // blkio.rd_ios = 0;
      blkio.rd_merges = 0;
      blkio.rd_ticks = 0;
      // blkio.wr_ios = 0;
      blkio.wr_merges = 0;
      blkio.wr_ticks = 0;
      blkio.ticks = 0;
      blkio.aveq = 0;
      items = 12;
    }

    if (items == 12) {
      unsigned int p;

      /* Locate partition in data table */
      for (p = 0; p < n_partitions; p++) {
        if (partition[p].major == curr.major
            && partition[p].minor == curr.minor) {
          new_blkio[p] = blkio; // set it to the new block
          break;
        }
      }
    }
		buffer = index(buffer, '\n');
		if(buffer != NULL) buffer++;
  }

	// now read in the cpu info at the "same" time so we can calculate
	// time passed
	buffer = update_file(&proc_stat);
	
	while ( buffer != NULL ) {
		// "cpu " is the line containing aggregated total
    if (!strncmp(buffer, "cpu ", 4)) {
      int items;
      unsigned long long nice, irq, softirq;

			items = sscanf(buffer,
				"cpu %llu %llu %llu %llu %llu %llu %llu",
				&new_cpu.user, &nice,
				&new_cpu.system,
				&new_cpu.idle,
				&new_cpu.iowait,
				&irq, &softirq);

			new_cpu.user += nice; 
			if (items == 4)
				new_cpu.iowait = 0;
			if (items == 7)
				new_cpu.system += irq + softirq;
		}
		buffer = index(buffer, '\n');
    if(buffer != NULL) buffer++;
	}

	if(entry_count == 0)
	{
		// save the new to old
  	for (i = 0; i < n_partitions; i++)
    	old_blkio[i] = new_blkio[i];
  	old_cpu = new_cpu;
		entry_count = 1;
	}
}


// just to make sure we collected everything
void print_io_info(void)
{
	int i;

	debug_msg("printing partition info\n");
  for(i=0;i<n_partitions;i++)
  {
    debug_msg("partition: %s %d %d\n", partition[i].name, partition[i].major, partition[i].minor);
  }
}

double get_deltams()
{
	double deltams = 1000.0 *
    ((new_cpu.user + new_cpu.system +
      new_cpu.idle + new_cpu.iowait) -
     (old_cpu.user + old_cpu.system +
      old_cpu.idle + old_cpu.iowait)) / num_cpustates / HZ;
	// fprintf(stderr, "deltams: %f, num_cpustates: %d, HZ: %d\n", deltams, num_cpustates, HZ);
	return deltams;
}


/* --------------------------------------------------------------------------- */
g_val_t
io_readtot_func( void )
{
	g_val_t val;
	int p;
	unsigned int rd_iops_tot = 0;
	unsigned int rd_iops_diff = 0;

	get_kernel_io_stats();
	double deltams = get_deltams();
	// fprintf(stderr, "deltams: %f\n", deltams);
	
	for (p = 0; p < n_partitions; p++) {
		rd_iops_diff = new_blkio[p].rd_ios - old_blkio[p].rd_ios;	
		// fprintf(stderr, "diff for part %d: %d (new: %d, old: %d)\n", p, rd_iops_diff, new_blkio[p].rd_ios, old_blkio[p].rd_ios);
		rd_iops_tot  += rd_iops_diff; // aggregate all the parts (includes raided disks too)
	}

	// fprintf(stderr, "total iops: %d\n", rd_iops_tot);

	val.f = (float) PER_SEC(rd_iops_tot);
	return val;
}


/* --------------------------------------------------------------------------- */
g_val_t
io_writetot_func( void )
{
	g_val_t val;
	int p;
	unsigned int wr_iops_tot = 0;
	unsigned int wr_iops_diff = 0;

	get_kernel_io_stats();
	double deltams = get_deltams();
	// fprintf(stderr, "deltams: %f\n", deltams);
	
	for (p = 0; p < n_partitions; p++) {
		wr_iops_diff = new_blkio[p].wr_ios - old_blkio[p].wr_ios;	
		wr_iops_tot  += wr_iops_diff; // aggregate all the disks (includes raided disks too)
	}

	// fprintf(stderr, "total iops: %d\n", rd_iops_tot);

	val.f = (float) PER_SEC(wr_iops_tot);
	return val;
}


/* --------------------------------------------------------------------------- */
g_val_t
io_readkbtot_func( void )
{
	g_val_t val;
	int p;
	unsigned int rd_iopskb_tot = 0;
	unsigned int rd_iopskb_diff = 0;

	get_kernel_io_stats();
	double deltams = get_deltams();
	// fprintf(stderr, "deltams: %f\n", deltams);
	
	for (p = 0; p < n_partitions; p++) {
		rd_iopskb_diff = new_blkio[p].rd_sectors - old_blkio[p].rd_sectors;	
		rd_iopskb_tot  += rd_iopskb_diff;
	}

	val.f = (float) PER_SEC(rd_iopskb_tot) / 2.0;
	return val;
}

/* --------------------------------------------------------------------------- */
g_val_t
io_writekbtot_func( void )
{
	g_val_t val;
	int p;
	unsigned int wr_iopskb_tot = 0;
	unsigned int wr_iopskb_diff = 0;

	get_kernel_io_stats();
	double deltams = get_deltams();
	// fprintf(stderr, "deltams: %f\n", deltams);
	
	for (p = 0; p < n_partitions; p++) {
		wr_iopskb_diff = new_blkio[p].wr_sectors - old_blkio[p].wr_sectors;	
		wr_iopskb_tot  += wr_iopskb_diff; 
	}

	val.f = (float) PER_SEC(wr_iopskb_tot) / 2.0;
	return val;
}


/* --------------------------------------------------------------------------- */
g_val_t
io_svctmax_func( void )
{
	g_val_t val;
	int p;
	unsigned int rd_iops = 0;
	unsigned int wr_iops = 0;
	double iops, ticks;
	double svct, svct_max;
	svct_max = 0.0;

	get_kernel_io_stats();
	
	for (p = 0; p < n_partitions; p++) {
		rd_iops = new_blkio[p].rd_ios - old_blkio[p].rd_ios;	
		wr_iops = new_blkio[p].wr_ios - old_blkio[p].wr_ios;	
		ticks   = new_blkio[p].ticks  - old_blkio[p].ticks;
		iops    = rd_iops + wr_iops;

		svct = iops ? ticks / iops : 0.0;
		if(svct > svct_max) svct_max = svct;
	}

	val.f = (float) svct_max;
	return val;
}

/* --------------------------------------------------------------------------- */
g_val_t
io_queuemax_func( void )
{
	g_val_t val;
	int p;
	double queue, queue_max;
	double deltams = get_deltams();
	queue_max = 0.0;

	get_kernel_io_stats();
	
	for (p = 0; p < n_partitions; p++) {
		queue = (new_blkio[p].aveq - old_blkio[p].aveq) / deltams;
		if(queue > queue_max) queue_max = queue;
	}

	val.f = (float) queue_max;
	return val;
}


g_val_t
io_busymax_func( void )
{
	g_val_t val;
	int p;
	double ticks, busy, busy_max;
	double deltams = get_deltams();
	busy_max = 0.0;

	get_kernel_io_stats();
	
	for (p = 0; p < n_partitions; p++) {
		ticks   = new_blkio[p].ticks  - old_blkio[p].ticks;
		busy    = 100.0 * ticks / deltams;
		if(busy > 100.0) busy = 100.0;
		if(busy > busy_max) busy_max = busy;
	}

	val.f = (float) busy_max;
	return val;
}




/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
extern mmodule io_module;

static int iostat_metric_init ( apr_pool_t *p )
{
    const char* str_params = io_module.module_params;
    apr_array_header_t *list_params = io_module.module_params_list;
    mmparam *params;
    int i;

    //libmetrics_init();
    num_cpustates = num_cpustates_func();
    init_partition_info(NULL, 0);
    print_io_info(); // prints debug msg


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

    for (i = 0; io_module.metrics_info[i].name != NULL; i++) {
        MMETRIC_INIT_METADATA(&(io_module.metrics_info[i]),p);
        MMETRIC_ADD_METADATA(&(io_module.metrics_info[i]),MGROUP,"disk");
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

mmodule io_module =
{
    STD_MMODULE_STUFF,
    iostat_metric_init,
    iostat_metric_cleanup,
    iostat_metric_info,
    iostat_metric_handler,
};
