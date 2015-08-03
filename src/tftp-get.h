/* tftp-get.h ---
 *
 * Filename: tftp-get.h
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Вт июн 30 15:20:04 2015 (+0300)
 * Version:
 * Last-Updated:
 *           By:
 *     Update #: 36
 * URL: https://github.com/Apofiget/tftp-mass-get
 * Keywords: TFTP, backup
 * Compatibility:
 *
 */

/* Code: */

#ifndef __TFTP_GET_H_
#define __TFTP_GET_H_

#define __LOG_FACILITY          LOG_DAEMON

#define __SOURCES_OPT_          "sources"
#define __PATH_OPT_             "savePath"
#define __FILES_PER_THREAD_OPT_ "filesPerThread"
#define __THREADS_OPT_          "maxThreads"

#define __DATETPL_PAR_          "dateTemplate"
#define __IP_PAR_               "ip"
#define __FILE_PAR_             "file"
#define __WITHTIME_PAR_         "saveWithTime"

#define __THREADS_DEFAULT_      32
#define __FILES_PER_THREAD_     3
#define __TFTP_BLK_SIZE_        1024L
#define __CURL_CONN_TIMEOUT_    40L

#define __MAX_DATE_LEN_         256
#define __DEFAULT_DATE_TEMPLATE_ "%Y%m%d-%H%M"

typedef struct __thread_data_ {
    unsigned int useTime;
    char *dateTpl;
    char *ip;
    char *filename;
    char *dstDir;
} thread_data_t;

typedef struct __f_list_ {
    int idx;
    thread_data_t **links;
} f_list_t;

#define __MALLOC(v,t,s)                                 \
    if((v = (t)malloc(s)) == NULL) {                    \
        syslog(LOG_ERR, "Memory allocation error!\n");  \
        exit(EXIT_FAILURE); }

#endif
/* tftp-get.h ends here */
