#ifndef __OTS_HTTPDOWNLOAD_H__
#define __OTS_HTTPDOWNLOAD_H__

struct curl_data_struct{
	long size;
	char *ptr;	
};
struct curl_download_writedata_struct{
	FILE* fp;
	struct curl_data_struct* curldata;
};
struct ots_httpdownload_para{
    int timeout;
    int connecttimeout;
    long code;
    char *url;
	char *downloadfile_path;
	char *log_path;
	int is_write_file;
	int is_write_log;
};

struct ots_httpdownload_para* ots_httpdownload_init(void);
int ots_httpdownload_run(struct ots_httpdownload_para* data,int thread_num);
int ots_httpdownload_destory(struct ots_httpdownload_para* data);

#endif