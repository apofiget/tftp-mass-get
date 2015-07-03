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
 *     Update #: 224
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

#include <libconfig.h>
#include <curl/curl.h>

#include "tftp-get.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

static size_t wc_cb(void*, size_t, size_t, void*);
static void *get_request(void*);

pthread_mutex_t idx_mtx;

int main(int argc, char *argv[]) {

    char *configFile = NULL, *savePath;
    char *ip, *file;
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

        if(!(config_setting_lookup_string(config_setting_get_elem(sources, i), "ip", (const char**)&ip)
             && config_setting_lookup_string(config_setting_get_elem(sources, i), "file", (const char**)&file)
             && config_setting_lookup_int(config_setting_get_elem(sources, i), "saveWithTime", &saveWithTime)))
            continue;
        __MALLOC(list.links[i], thread_data_t*, sizeof(thread_data_t));

        __MALLOC(list.links[i]->ip, char*, sizeof(char) * (strlen(ip) + 1));
        strcpy(list.links[i]->ip, ip);

        __MALLOC(list.links[i]->filename, char*, sizeof(char) * (strlen(file) + 1));
        strcpy(list.links[i]->filename, file);

        __MALLOC(list.links[i]->dstDir, char*, sizeof(char) * (strlen(savePath) + 1));
        strcpy(list.links[i]->dstDir, savePath);
        list.idx++;

        printf("IP: %s  File: %s\n", list.links[i]->ip, list.links[i]->filename);

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
    char *url = NULL, *dstFile;


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

        __MALLOC(dstFile, char*, sizeof(char) * (strlen(list->links[cur_idx]->filename) + strlen(list->links[cur_idx]->dstDir) + 1));
        sprintf(dstFile, "%s%s", list->links[cur_idx]->dstDir, list->links[cur_idx]->filename);

        if(access(list->links[cur_idx]->dstDir, W_OK) == -1) {
            syslog(LOG_ERR, "[%lu] Can create file in %s %s", syscall(SYS_gettid), list->links[cur_idx]->dstDir, strerror(errno));
            free(url);
            free(dstFile);
            continue;
        }

        if((fd = open((const char*)dstFile, O_RDWR | O_CREAT , S_IRUSR | S_IWUSR | S_IRGRP)) == -1) {
            syslog(LOG_ERR, "[%lu] Can't create file: %s %s", syscall(SYS_gettid), dstFile, strerror(errno));
            free(url);
            free(dstFile);
            continue;
        }

        curl_easy_setopt(curl, CURLOPT_TFTP_BLKSIZE, __TFTP_BLK_SIZE_);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, __CURL_CONN_TIMEOUT_);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&fd);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
            syslog(LOG_ERR, "[%lu] %s, %s\n", syscall(SYS_gettid), curl_easy_strerror(res), list->links[cur_idx]->ip);

        close(fd);

        free(list->links[cur_idx]->ip);
        free(list->links[cur_idx]->filename);
        free(list->links[cur_idx]->dstDir);
        free(list->links[cur_idx]);
        free(url);
        free(dstFile);
    }

    curl_easy_cleanup(curl);
    syslog(LOG_NOTICE, "[%lu] Terminating...\n", syscall(SYS_gettid));
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

/* tftp-get.c ends here */