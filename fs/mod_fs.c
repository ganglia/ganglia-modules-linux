/*******************************************************************************
    ganglia-modules-linux: modules for collecting metrics on Linux
    Copyright (C) 2011 Daniel Pocock

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <gm_metric.h>

#include <math.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <errno.h>

#include <apr_tables.h>
#include <apr_strings.h>

#ifndef NAN
#define NAN (0.0f/0.0f) /* <math.h> only includes <bits/nan.h> in C99 */
#endif

/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
mmodule fs_module;



#define UPDATE_INTERVAL 5
struct timespec last_update;

/*
 *  /proc/mounts  = device_node mountpoint fs opts dump passno   (same as /etc/fstab)
 *    - we use this to get a list of filesystems
 *
 *  statvfs()
 *    - we use this function to get the statistics for an FS
 *
 */


typedef struct fs_info {
	char *device;
	char *mount_point;
	char *fs_type;
	char *ganglia_name;
} fs_info_t;


/* Linux Specific, but we are in the Linux machine file. */
#define MOUNTS "/proc/mounts"
#define STRMAX 128
#define LINEMAX 256


/* globals for mounts, filesystems, and metrics */
apr_array_header_t *mounts      = NULL;
apr_array_header_t *filesystems = NULL;
apr_array_header_t *metric_info = NULL;


/* --------------------------------------------------------------------------- */
int remote_mount(const char *device, const char *type)
{
   /* From ME_REMOTE macro in mountlist.h:
   A file system is `remote' if its Fs_name contains a `:'
   or if (it is of type smbfs and its Fs_name starts with `//'). */
   return ((strchr(device,':') != 0)
      || (!strcmp(type, "smbfs") && device[0]=='/' && device[1]=='/')
      || (!strncmp(type, "nfs", 3)) || (!strcmp(type, "autofs"))
      || (!strcmp(type,"gfs")) || (!strcmp(type,"none")) );
}

typedef g_val_t (*fs_func_t)(fs_info_t *fs);




/* Check the "filesystem" global list for the mount point in question.
 * Return true if listed, false if not.  Note that this could be up to
 * UPDATE_INTERVAL seconds out of date, due to caching the results of
 * /proc/mounts
 */
int is_mounted(char *mount_point) {

    fs_info_t *cur_fs;
    int i, found=0;

    if (!filesystems)
	return 0;

    for (i=0; i < filesystems->nelts; i++) {
	int len, diff;
	cur_fs = &((fs_info_t*)filesystems->elts)[i];

	len = MAX(strlen(cur_fs->mount_point), strlen(mount_point));

	//debug_msg("Checking [%s] against filesystem[%s] (%d)", mount_point, cur_fs->mount_point, len);
	if (!strncmp(mount_point, cur_fs->mount_point, len)) {
	    found=1;
	    break;
	}

    }
    //debug_msg("%s:%d: [%s] is %s", __FILE__, __LINE__, mount_point, (found ? "mounted" : "not mounted"));
    return found;

}


static g_val_t fs_capacity_bytes_func (fs_info_t *fs)
{
	g_val_t val;

	struct statvfs svfs;
	unsigned long blocksize;

	fsblkcnt_t size;

	val.f = (float) NAN;

	/* return NAN if mount point not in /proc/mounts */
	if (!is_mounted(fs->mount_point))
		return val;

	if (statvfs(fs->mount_point, &svfs)) {
		/* Ignore funky devices... */
		return val;
	}


	size  = svfs.f_blocks;
	blocksize = svfs.f_frsize;

	val.f = (float) size * blocksize;
	return val;

}


static g_val_t fs_used_bytes_func (fs_info_t *fs)
{
	g_val_t val;

	struct statvfs svfs;
	unsigned long blocksize;
	fsblkcnt_t free;
	fsblkcnt_t size;
	fsblkcnt_t used;

	val.f = (float) NAN;

	/* return NAN if mount point not in /proc/mounts */
	if (!is_mounted(fs->mount_point))
		return val;

	if (statvfs(fs->mount_point, &svfs)) {
		/* Ignore funky devices... */
		return val;
	}


	size  = svfs.f_blocks;
	free = svfs.f_bavail;
	used = size - free;
	blocksize = svfs.f_frsize;

	val.f = (float) used * blocksize;
	return val;

}

static g_val_t fs_free_func (fs_info_t *fs)
{
        g_val_t val;

        struct statvfs svfs;
        fsblkcnt_t blocks_free;
        fsblkcnt_t total_blocks;

        val.f = (float) NAN;

	/* return NAN if mount point not in /proc/mounts */
	if (!is_mounted(fs->mount_point))
		return val;

        if (statvfs(fs->mount_point, &svfs)) {
                /* Ignore funky devices... */
                err_msg("statvfs failed for %s: %s", fs->mount_point, strerror(errno));
                return val;
        }

        total_blocks = svfs.f_blocks;
        blocks_free = svfs.f_bavail;

        val.f = (float)100.0 * blocks_free / total_blocks;
        return val;

}

typedef struct metric_spec {
	fs_func_t fs_func;
	const char *name;
	const char *units;
	const char *desc;
	const char *fmt;
} metric_spec_t;


#define NUM_FS_METRICS 3
metric_spec_t metrics[] = {

		{ fs_capacity_bytes_func, "capacity_bytes", "bytes", "capacity in bytes", "%.0f" },
		{ fs_used_bytes_func, "used_bytes", "bytes", "space used in bytes", "%.0f" },
                { fs_free_func, "free_pct", "%", "percentage space free", "%.0f" },

		{ NULL, NULL, NULL, NULL, NULL }
};


void create_metrics_for_device(apr_pool_t *p, apr_array_header_t *ar, fs_info_t *fs) {
		metric_spec_t *metric;
		Ganglia_25metric *gmi;
		char *metric_name;

		for (metric = metrics; metric->fs_func != NULL; metric++) {
			gmi = apr_array_push(ar);

			/* gmi->key will be automatically assigned by gmond */
			metric_name = apr_psprintf(p, "fs_%s_%s", metric->name,
					fs->ganglia_name);
			debug_msg("fs: creating metric %s", metric_name);
			gmi->name = metric_name;
			gmi->tmax = 90;
			gmi->type = GANGLIA_VALUE_FLOAT;
			gmi->units = apr_pstrdup(p, metric->units);
			gmi->slope = apr_pstrdup(p, "both");
			gmi->fmt = apr_pstrdup(p, metric->fmt);
			gmi->msg_size = UDP_HEADER_SIZE + 8;
			gmi->desc = apr_pstrdup(p, metric->desc);
		}
}

void set_ganglia_name(apr_pool_t *p, fs_info_t *fs) {
	int i, j=0;

	if(strcmp(fs->mount_point, "/") == 0) {
		fs->ganglia_name = apr_pstrdup(p, "root");
		return;
	}
	fs->ganglia_name = apr_pstrdup(p, fs->mount_point);
	for (i = 0; fs->mount_point[i] != 0; i++) {
		if(fs->mount_point[i] == '/') {
			if(i > 0)
				fs->ganglia_name[j++] = '_';
		} else {
			fs->ganglia_name[j++] = fs->mount_point[i];
		}
	}
	fs->ganglia_name[j] = 0;
}

int scan_mounts(apr_pool_t *p) {
	FILE *mounts;
	char procline[LINEMAX];
	char mount[STRMAX], device[STRMAX], type[STRMAX], mode[STRMAX];
	int rc;
	fs_info_t *fs;
        struct timespec now;

        /* update global */
        rc = clock_gettime(CLOCK_REALTIME, &now);
        //debug_msg(" now=%u < (last_update=%u + update_interval=%u)", now.tv_sec, last_update.tv_sec, UPDATE_INTERVAL);
        /* Too soon to update the mounts */
        if (now.tv_sec < (last_update.tv_sec + UPDATE_INTERVAL)) {
            return 1;
        }

	//debug_msg("%s:%d: Updating filesystem cache", __FILE__,__LINE__);

        if (NULL == filesystems) {
            filesystems = apr_array_make(p, 2, sizeof(fs_info_t));
        } else {
            apr_array_clear(filesystems);
        }

        if (NULL == metric_info) {
            metric_info = apr_array_make(p, 2, sizeof(Ganglia_25metric));
        } else {
            apr_array_clear(metric_info);
        }


	mounts = fopen(MOUNTS, "r");
	if (!mounts) {
		debug_msg("Df Error: could not open mounts file %s. Are we on the right OS?\n", MOUNTS);
		return -1;
	}
	while ( fgets(procline, sizeof(procline), mounts) ) {

		rc=sscanf(procline, "%s %s %s %s ", device, mount, type, mode);
		if (!rc)
			continue;

		//if (!strncmp(mode, "ro", 2)) continue;
		
		/* Skip remote filesystems */
		if (remote_mount(device, type))
			continue;

		/* Skip non-device and non-tmpfs mounts
		 * (this excludes all the "wierd" stuff in /sys,
		 * autofs placeholders, etc */
		if ( strncmp(device, "/dev/",  5) != 0 &&
		     strncmp(device, "/dev2/", 6) != 0 ) {
			continue;
		}

		fs = apr_array_push(filesystems);
		bzero(fs, sizeof(fs_info_t));

		fs->device = apr_pstrdup(p, device);
		fs->mount_point = apr_pstrdup(p, mount);
		fs->fs_type = apr_pstrdup(p, type);
		set_ganglia_name(p, fs);

		create_metrics_for_device(p, metric_info, fs);

		//thispct = device_space(mount, device, total_size, total_free);
		debug_msg("%s: Found device %s (%s)", __FILE__, device, type);

	}
	fclose(mounts);

        last_update.tv_sec = now.tv_sec;

        return 0;
}

apr_pool_t *pool = NULL;

static int ex_metric_init (apr_pool_t *p)
{
    int i;
    Ganglia_25metric *gmi;

    /* Allocate a pool that will be used by this module */
    apr_pool_create(&pool, p);

    /* Initialize each metric */
    /* side effects: filesystems and metric_info arrays */
    scan_mounts(pool);

    /* Add a terminator to the array and replace the empty static metric definition
        array with the dynamic array that we just created
    */
    gmi = apr_array_push(metric_info);
    memset (gmi, 0, sizeof(*gmi));

    fs_module.metrics_info = (Ganglia_25metric *)metric_info->elts;

    for (i = 0; fs_module.metrics_info[i].name != NULL; i++) {
        /* Initialize the metadata storage for each of the metrics and then
         *  store one or more key/value pairs.  The define MGROUPS defines
         *  the key for the grouping attribute. */
        MMETRIC_INIT_METADATA(&(fs_module.metrics_info[i]),p);
        MMETRIC_ADD_METADATA(&(fs_module.metrics_info[i]),MGROUP,"disk");
    }

    return 0;
}

static void ex_metric_cleanup ( void )
{
}

static g_val_t ex_metric_handler(int metric_index) {
	g_val_t val;
	int fs_index;
	int _metric_index;
	fs_info_t *all_fs = (fs_info_t *)filesystems->elts, *fs;

	fs_index = metric_index / NUM_FS_METRICS;
	_metric_index = metric_index % NUM_FS_METRICS;

	fs = &all_fs[fs_index];

	debug_msg("fs: handling read for metric %d fs %d idx %d (%s)",
			metric_index, fs_index, _metric_index, fs->mount_point);
	scan_mounts(pool);
	val = metrics[_metric_index].fs_func(fs);

	return val;
}

mmodule fs_module =
{
    STD_MMODULE_STUFF,
    ex_metric_init,
    ex_metric_cleanup,
    NULL, /* defined dynamically */
    ex_metric_handler,
};
