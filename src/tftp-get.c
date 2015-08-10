/* tftp-get.c ---
 *
 * Filename: tftp-get.c
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Вт июн 30 14:55:38 2015 (+0300)
 * Version: 0.1
 * Last-Updated:
 *           By:
 *     Update #: 288
 * URL: https://github.com/Apofiget/tftp-mass-get
 * Keywords:  TFTP, backup
 * Compatibility:
 *
 */

/* Code: */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <time.h>

#include <libconfig.h>
#include <curl/curl.h>

#include "tftp-get.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

static size_t wc_cb(void*, size_t, size_t, void*);
static void *get_request(void*);
static char *get_formated_date(const char*);

pthread_mutex_t idx_mtx;

int main(int argc, char *argv[]) {

    char *configFile = NULL, *savePath;
    char *ip, *file, *dateTpl, *defaultPrefixTemplate;
    config_t config;
    config_setting_t *sources = NULL;
    int threads, i, saveWithTime, opt, filesPerThread, settings_count;
    f_list_t list = {.idx = 0};
    pthread_t pthread[__THREADS_DEFAULT_];

    while ((opt = getopt (argc, argv, "c:?")) != -1)
        switch (opt) {
        case 'c':
            __MALLOC(configFile, char*, sizeof(char) * (strlen(optarg) + 1));
            strcpy(configFile, optarg);
            break;
        default:
            break;
        }

    if(configFile == NULL) {
        fprintf(stdout, "Usage: tftp-mass-get -c <config file>\n");
        exit(EXIT_FAILURE);
    }

    setlogmask (LOG_UPTO (LOG_NOTICE));
    openlog("tftp-mass-get", LOG_CONS | LOG_PID | LOG_NDELAY, __LOG_FACILITY);

    config_init(&config);

    if(curl_global_init(CURL_GLOBAL_ALL) !=0 ) {
        syslog(LOG_ERR, "libcurl initialization error!");
        exit(EXIT_FAILURE);
    }

    if(access(configFile, F_OK | R_OK) != 0) {
        syslog(LOG_ERR, "%s %s", strerror(errno), configFile);
        exit(EXIT_FAILURE);
    }

/* Read config and retrieve mandatory params */
    if(!config_read_file(&config, configFile)) {
        syslog(LOG_ERR, "Config error: %d - %s\n", config_error_line(&config), config_error_text(&config));
        config_destroy(&config);
        exit(EXIT_FAILURE);
    }

    if(!(config_lookup_string(&config, __PATH_OPT_, (const char**)&savePath))) {
        syslog(LOG_ERR, "No %s param in config file!", __PATH_OPT_);
        exit(EXIT_FAILURE);
    }

    if(!(config_lookup_string(&config, __DEF_TPL_OPT_, (const char**)&defaultPrefixTemplate))) {
        __MALLOC(defaultPrefixTemplate, char*, sizeof(__DEFAULT_DATE_TEMPLATE_));
        strcpy(defaultPrefixTemplate, __DEFAULT_DATE_TEMPLATE_);
    }

    if((config_lookup_int(&config, __THREADS_OPT_, &threads)) != CONFIG_TRUE) {
        syslog(LOG_WARNING, "No %s param in config, will use default value %d", __THREADS_OPT_, __THREADS_DEFAULT_);
        threads = __THREADS_DEFAULT_;
    } else threads = threads > __THREADS_DEFAULT_ ? __THREADS_DEFAULT_ : threads;

    if((config_lookup_int(&config, __FILES_PER_THREAD_OPT_, &filesPerThread)) != CONFIG_TRUE) {
        syslog(LOG_WARNING, "No %s param in config, will use default value %d", __FILES_PER_THREAD_OPT_, __FILES_PER_THREAD_);
        filesPerThread = __FILES_PER_THREAD_;
    }

    if((sources = config_lookup(&config, __SOURCES_OPT_)) == NULL) {
        syslog(LOG_ERR, "No mandatory %s section in config!", __SOURCES_OPT_);
        exit(EXIT_FAILURE);
    }

    settings_count = config_setting_length(sources);
    __MALLOC(list.links, thread_data_t**, sizeof(thread_data_t*) * settings_count);

    for(i = 0; i < settings_count; i++) {

        if(!(config_setting_lookup_string(config_setting_get_elem(sources, i), __IP_PAR_, (const char**)&ip)
             && config_setting_lookup_string(config_setting_get_elem(sources, i), __FILE_PAR_, (const char**)&file)))
            continue;

        if(!config_setting_lookup_int(config_setting_get_elem(sources, i), __WITHTIME_PAR_, &saveWithTime))
            saveWithTime = 0;

        __MALLOC(list.links[i], thread_data_t*, sizeof(thread_data_t));

        list.links[i]->useTime = saveWithTime;

        __MALLOC(list.links[i]->ip, char*, sizeof(char) * (strlen(ip) + 1));
        strcpy(list.links[i]->ip, ip);

        __MALLOC(list.links[i]->filename, char*, sizeof(char) * (strlen(file) + 1));
        strcpy(list.links[i]->filename, file);

        __MALLOC(list.links[i]->dstDir, char*, sizeof(char) * (strlen(savePath) + 1));
        strcpy(list.links[i]->dstDir, savePath);

        if(list.links[i]->useTime != 0)
            if(!(config_setting_lookup_string(config_setting_get_elem(sources, i), __DATETPL_PAR_, (const char**)&dateTpl))) {
                __MALLOC(list.links[i]->dateTpl, char*, strlen(defaultPrefixTemplate) + 1);
                strcpy(list.links[i]->dateTpl, defaultPrefixTemplate);
            } else {
                __MALLOC(list.links[i]->dateTpl, char*, strlen(dateTpl) + 1);
                strcpy(list.links[i]->dateTpl, dateTpl);
            }
        else list.links[i]->dateTpl = NULL;

        list.idx++;

    }

    threads = (list.idx <= 0 ) ? 0 : (list.idx / filesPerThread) < 1 ? 1 \
        : (list.idx / filesPerThread) > threads ? threads : (list.idx % filesPerThread) \
        ? (list.idx / filesPerThread) + 1 : (list.idx / filesPerThread);

    fprintf(stdout, "Files download: %d, threads: %d\n", list.idx, threads);

    pthread_mutex_init(&idx_mtx, NULL);

    for(i = 0; i < threads; i++) {
        if(pthread_create(&pthread[i], NULL, get_request, &list)) {
            fprintf(stderr, "Uploader thread creation failure: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for(i = 0; i < threads; i++)
        pthread_join(pthread[i], NULL);

    config_destroy(&config);
    free(configFile);
    free(list.links);
    curl_global_cleanup();
}

void *get_request(void *arg) {

    f_list_t *list = (f_list_t*)arg;
    CURL *curl;
    CURLcode res;
    int fd, cur_idx;
    char *url = NULL, *dstFile, *datePrefix = NULL;
    unsigned long threadId = syscall(SYS_gettid);


    curl = curl_easy_init();

    if(!curl) {
        fprintf(stderr, "libcurl init failure!\n");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_NOTICE, "[%lu] Starting...\n", syscall(SYS_gettid));

    while(list->idx > 0) {

        pthread_mutex_lock(&idx_mtx);
        cur_idx = list->idx - 1;
        list->idx--;
        pthread_mutex_unlock(&idx_mtx);

        /* tftp://$ip/$filename + \0 */
        __MALLOC(url, char*, sizeof(char) *(strlen(list->links[cur_idx]->ip) + strlen(list->links[cur_idx]->filename) + 9));
        sprintf(url, "tftp://%s/%s",list->links[cur_idx]->ip, list->links[cur_idx]->filename);

        if(list->links[cur_idx]->useTime != 0)
            if((datePrefix = get_formated_date((const char*)list->links[cur_idx]->dateTpl)) == NULL) {
                syslog(LOG_ERR, "[%lu] Error in template: %s", threadId, list->links[cur_idx]->dateTpl);
                free(url);
                continue;
            } else {
                __MALLOC(dstFile, char*, sizeof(char) * (strlen(datePrefix) + strlen(list->links[cur_idx]->filename) + strlen(list->links[cur_idx]->dstDir) + 1));
                sprintf(dstFile, "%s%s%s", list->links[cur_idx]->dstDir, datePrefix, list->links[cur_idx]->filename);
            }
        else {
            __MALLOC(dstFile, char*, sizeof(char) * (strlen(list->links[cur_idx]->filename) + strlen(list->links[cur_idx]->dstDir) + 1));
            sprintf(dstFile, "%s%s", list->links[cur_idx]->dstDir, list->links[cur_idx]->filename);
        }

        if(access(list->links[cur_idx]->dstDir, W_OK) == -1) {
            syslog(LOG_ERR, "[%lu] Can create file in %s %s", threadId, list->links[cur_idx]->dstDir, strerror(errno));
            free(url);
            free(dstFile);
            __FREE(datePrefix);
            continue;
        }

        if((fd = open((const char*)dstFile, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP)) == -1)
            syslog(LOG_ERR, "[%lu] Can't create file: %s %s", threadId, dstFile, strerror(errno));
        else {

            syslog(LOG_NOTICE, "[%lu] tftp://%s/%s -> %s", threadId, list->links[cur_idx]->ip, list->links[cur_idx]->filename, dstFile);

            curl_easy_setopt(curl, CURLOPT_TFTP_BLKSIZE, __TFTP_BLK_SIZE_);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, __CURL_CONN_TIMEOUT_);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&fd);

            res = curl_easy_perform(curl);

            if(res != CURLE_OK)
                syslog(LOG_ERR, "[%lu] %s, %s\n", threadId, curl_easy_strerror(res), list->links[cur_idx]->ip);

            close(fd);
        }

        free(list->links[cur_idx]->ip);
        free(list->links[cur_idx]->dateTpl);
        free(list->links[cur_idx]->filename);
        free(list->links[cur_idx]->dstDir);
        free(list->links[cur_idx]);
        free(url);
        free(dstFile);
        __FREE(datePrefix);
    }

    curl_easy_cleanup(curl);
    syslog(LOG_NOTICE, "[%lu] Terminating...\n", threadId);
    return NULL;
}

static size_t wc_cb(void *contents, size_t size, size_t nmemb,
                    void *userp) {

    size_t realsize = size * nmemb;
    int fd = *(int *)userp, bytes_writed;

    if((bytes_writed = write(fd, contents, realsize)) == -1) {
        syslog(LOG_ERR, "[%lu] Data writes error, fd: %d : %s", syscall(SYS_gettid), fd, strerror(errno));
        return -1;
    }

    return bytes_writed;
}

static char* get_formated_date(const char *format) {

    time_t now;
    struct tm *ltime;
    char *formated_date = NULL;

    __MALLOC(formated_date, char*, __MAX_DATE_LEN_);

    now = time(NULL);
    ltime = localtime(&now);

    if(strftime(formated_date, __MAX_DATE_LEN_, format, ltime) == 0) {
        free(formated_date);
        formated_date = NULL;
    }

    return formated_date;
}

/* tftp-get.c ends here */
