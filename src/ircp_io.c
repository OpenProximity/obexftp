#include <sys/stat.h>
#include <unistd.h>

#include <fcntl.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <openobex/obex.h>

#include "debug.h"
#include "ircp_io.h"

//
// Get some file-info. (size and lastmod)
//
static gint get_fileinfo(const char *name, char *lastmod)
{
	struct stat stats;
	struct tm *tm;
	
	stat(name, &stats);
	tm = gmtime(&stats.st_mtime);
	g_snprintf(lastmod, 21, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	return (gint) stats.st_size;
}


//
// Create an object from a file. Attach some info-headers to it
//
obex_object_t *build_object_from_file(obex_t *handle, const gchar *localname, const gchar *remotename)
{
	obex_object_t *object = NULL;
	obex_headerdata_t hdd;
	guint8 *ucname;
	gint ucname_len, size;
	gchar lastmod[21*2] = {"1970-01-01T00:00:00Z"};
		
	/* Get filesize and modification-time */
	size = get_fileinfo(localname, lastmod);

	object = OBEX_ObjectNew(handle, OBEX_CMD_PUT);
	if(object == NULL)
		return NULL;

	ucname_len = strlen(remotename)*2 + 2;
	ucname = g_malloc(ucname_len);
	if(ucname == NULL)
		goto err;

	ucname_len = OBEX_CharToUnicode(ucname, remotename, ucname_len);

	hdd.bs = ucname;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_NAME, hdd, ucname_len, 0);
	g_free(ucname);

	hdd.bq4 = size;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_LENGTH, hdd, sizeof(guint32), 0);

#if 0
	/* Win2k excpects this header to be in unicode. I suspect this in
	   incorrect so this will have to wait until that's investigated */
	hdd.bs = lastmod;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_TIME, hdd, strlen(lastmod)+1, 0);
#endif
		
	hdd.bs = NULL;
	OBEX_ObjectAddHeader(handle, object, OBEX_HDR_BODY,
				hdd, 0, OBEX_FL_STREAM_START);

	DEBUG(4, G_GNUC_FUNCTION "() Lastmod = %s\n", lastmod);
	return object;

err:
	if(object != NULL)
		OBEX_ObjectDelete(handle, object);
	return NULL;
}

//
// Check for dangerous filenames.
//
static gboolean ircp_nameok(const gchar *name)
{
	DEBUG(4, G_GNUC_FUNCTION "()\n");
	
	/* No abs paths */
	if(name[0] == '/')
		return FALSE;

	if(strlen(name) >= 3) {
		/* "../../vmlinuz" */
		if(name[0] == '.' && name[1] == '.' && name[2] == '/')
			return FALSE;
		/* "dir/../../../vmlinuz" */
		if(strstr(name, "/../") != NULL)
			return FALSE;
	}
	return TRUE;
}
	
//
// Open a file, but do some sanity-checking first.
//
gint ircp_open_safe(const gchar *path, const gchar *name)
{
	GString *diskname;
	gint fd;

	DEBUG(4, G_GNUC_FUNCTION "()\n");
	
	/* Check for dangerous filenames */
	if(ircp_nameok(name) == FALSE)
		return -1;

	diskname = g_string_new(path);
	if(diskname == NULL)
		return -1;

	//TODO! Rename file if already exist.
	
	if(diskname->len > 0)
		g_string_append(diskname, "/");
	g_string_append(diskname, name);

	DEBUG(4, G_GNUC_FUNCTION "() Creating file %s\n", diskname->str);

	fd = open(diskname->str, O_RDWR | O_CREAT | O_TRUNC, DEFFILEMODE);
	g_string_free(diskname, TRUE);
	return fd;
}

//
// Go to a directory. Create if not exists and create is true.
//
gint ircp_checkdir(const gchar *path, const gchar *dir, cd_flags flags)
{
	GString *newpath;
	struct stat statbuf;
	gint ret = -1;

	if(!(flags & CD_ALLOWABS))	{
		if(ircp_nameok(dir) == FALSE)
			return -1;
	}

	newpath = g_string_new(path);
	if(strcmp(path, "") != 0)
		g_string_append(newpath, "/");
	g_string_append(newpath, dir);

	DEBUG(4, G_GNUC_FUNCTION "() path = %s dir = %s, flags = %d\n", path, dir, flags);
	if(stat(newpath->str, &statbuf) == 0) {
		// If this directory aleady exist we are done
		if(S_ISDIR(statbuf.st_mode)) {
			DEBUG(4, G_GNUC_FUNCTION "() Using existing dir\n");
			ret = 1;
			goto out;
		}
		else  {
			// A non-directory with this name already exist.
			DEBUG(4, G_GNUC_FUNCTION "() A non-dir called %s already exist\n", newpath->str);
			ret = -1;
			goto out;
		}
	}
	if(flags & CD_CREATE) {
		DEBUG(4, G_GNUC_FUNCTION "() Will try to create %s\n", newpath->str);
		ret = mkdir(newpath->str, DEFFILEMODE | S_IXGRP | S_IXUSR | S_IXOTH);
	}
	else {
		ret = -1;
	}

out:	g_string_free(newpath, TRUE);
	return ret;
}
	
