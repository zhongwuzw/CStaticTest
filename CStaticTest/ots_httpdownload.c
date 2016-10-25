#include<stdio.h>
#include<stdlib.h>
#include "curl.h"
#include <string.h>
#include<pthread.h>
#include <unistd.h>
#include<errno.h>
#include<time.h>
#include"ots_httpdownload.h"
#define THREADNUM 128
#define CONNECTTIMEOUT 30
#define TIMEOUT 60
/**************define global variable****************/
double download_sum = 0;
int start_calc_flag = 0;
int exit_calc_flag = 0;

int ots_mem_free(void* ptr);
int write_data_record(char* filename,char* content);

//读写互斥锁
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//获取当前系统时间
int get_current_time(char *time_buf){
	time_t t = time(NULL);
	struct tm *ptr;

	ptr = localtime(&t);
	strftime(time_buf, 64, "%Y%m%d%H%M%S", ptr);
	
	return 0;
}
//ots_curl_get的回调方法
int ots_curl_get_callback(void *ptr, size_t size, size_t nmemb, void *userdata){
    struct curl_download_writedata_struct *response_ptr = (struct curl_download_writedata_struct *) userdata;
    int bufsize = size * nmemb;
    if (bufsize <= 0)
        goto end;
	if(response_ptr->fp!=NULL)//描述符不为空则写入文件，否则就不操作
	{
		fwrite(ptr,bufsize,1,response_ptr->fp);
	}
    pthread_mutex_lock(&mutex);
    download_sum += bufsize * 8;
    pthread_mutex_unlock(&mutex);

    response_ptr->curldata->ptr = (char *) realloc(response_ptr->curldata->ptr, response_ptr->curldata->size + bufsize + 1);
    memcpy(response_ptr->curldata->ptr + response_ptr->curldata->size, ptr, bufsize);
    response_ptr->curldata->size += bufsize;
    response_ptr->curldata->ptr[response_ptr->curldata->size] = '\0';
	
    //printf("the curl_get_resp_data = %s\n",ptr); 
end: return bufsize;
}
//通用http get方法
int ots_curl_get(struct ots_httpdownload_para* data,char* response,int response_size){
    CURL* curl;
    CURLcode res;
    struct curl_data_struct curl_get_resp_data;
	struct curl_download_writedata_struct *callback_userdata;
	callback_userdata = (struct curl_download_writedata_struct *)malloc(sizeof(struct curl_download_writedata_struct));
	memset(callback_userdata, 0, sizeof(struct curl_download_writedata_struct));
	callback_userdata->curldata=(struct curl_data_struct *)malloc(sizeof(struct curl_data_struct));
	memset(callback_userdata->curldata, 0, sizeof(struct curl_data_struct));
	//todo 空间分配
	//callback_userdata->curldata->ptr=NULL;
	//callback_userdata->curldata->size=0;
	if(data->is_write_file==1){
	if(data->downloadfile_path!=NULL)
		callback_userdata->fp=fopen(data->downloadfile_path,"w+");
	else//设置默认存储文件名
		callback_userdata->fp=fopen("./httpdownload.file","w+");
	if(callback_userdata->fp==NULL){//文件打开失败也会执行下载过程，只是不写文件
		printf("downloadfile open failed!\n");
	};
	}
    //printf("threadid is %ld,j value is %d\n",pthread_self(),j);
    //printf("Url: %s\n", url);
  curl_global_init(CURL_GLOBAL_ALL);

    curl=curl_easy_init();
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,data->connecttimeout);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,data->timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,ots_curl_get_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,callback_userdata);

    /********************http请求************************/
    curl_easy_setopt(curl, CURLOPT_URL, data->url);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        printf("curl request %s failed: %s\n",data->url,curl_easy_strerror(res));
        response=NULL;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &data->code);

		if(callback_userdata->fp!=NULL){
			fclose(callback_userdata->fp);
			callback_userdata->fp=NULL;
		}
		if(callback_userdata->curldata->ptr!=NULL){
		ots_mem_free(callback_userdata->curldata->ptr);
		callback_userdata->curldata->ptr=NULL;
		}
        curl_easy_cleanup(curl);
		curl_global_cleanup();
        return -1;
    }else{
        if(response==NULL){
			if(callback_userdata->fp!=NULL){
			fclose(callback_userdata->fp);
			callback_userdata->fp=NULL;
			}
		if(callback_userdata->curldata->ptr!=NULL){
			ots_mem_free(callback_userdata->curldata->ptr);
			callback_userdata->curldata->ptr=NULL;
		}
            curl_easy_cleanup(curl);
			curl_global_cleanup();
            return 0;
        }
        snprintf(response,response_size,"%s",curl_get_resp_data.ptr);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,&data->code);

        //data check

		if(callback_userdata->fp!=NULL){
			fclose(callback_userdata->fp);
			callback_userdata->fp=NULL;
			}
		if(callback_userdata->curldata->ptr!=NULL){
			ots_mem_free(callback_userdata->curldata->ptr);
			callback_userdata->curldata->ptr=NULL;
		}
        curl_easy_cleanup(curl);
		curl_global_cleanup();
        return 0;
    }
    //memset(curl_get_resp_data.ptr,0,strlen(curl_get_resp_data.ptr));
}
//下载线程
void *download_thread(void* curl_data){
	struct ots_httpdownload_para* data=(struct ots_httpdownload_para*)curl_data;
    printf("Child threadid is %lu, %s\n", pthread_self(), data->url);

    pthread_mutex_lock(&mutex);
    start_calc_flag ++;
    pthread_mutex_unlock(&mutex);

    int res=ots_curl_get(data,NULL,0);
    return NULL;
}
//算速线程
void *calc_thread(void *arg)
{
    int _start_calc_flag = 0;
    int _exit_calc_flag = 0;
    double _last_download_sum = 0;
    double _speed;
    double _download_sum;
    /*struct timeval ts, te;

    memset(&ts, 0, sizeof(ts));
    memset(&te, 0, sizeof(te));*/
	struct ots_httpdownload_para* data=(struct ots_httpdownload_para*)arg;

    while(1){
        pthread_mutex_lock(&mutex);
        _start_calc_flag = start_calc_flag;
        _exit_calc_flag = exit_calc_flag;
        pthread_mutex_unlock(&mutex);
        if(_exit_calc_flag)
            return NULL;

        if(_start_calc_flag)
            break;
        else
            usleep(100000);
    }

    //gettimeofday(&ts, NULL);
    while(1){
         
        pthread_mutex_lock(&mutex);
        _download_sum = download_sum;
        pthread_mutex_unlock(&mutex);
            
        _speed = _download_sum - _last_download_sum;
        printf("Speed:%.2lfMb/s\n", _speed / 1024 / 1024);
		char time[64];
		get_current_time(time);
        char record[512];
        snprintf(record,sizeof(record),"timestramp,%s,total_download(MB),%.2lf,realtime_speed(Mbps),%.2lf",time,_download_sum/1024/1024/8, _speed / 1024 / 1024);
        if(data->is_write_log){
		int res=write_data_record(data->log_path,record);
		} 
        
        _last_download_sum = _download_sum;

        pthread_mutex_lock(&mutex);
        _exit_calc_flag = exit_calc_flag;
        pthread_mutex_unlock(&mutex);

        if(_exit_calc_flag){
            //gettimeofday(&te, NULL);
            //printf("Avg Speed: %0.2lfMb/s\n", total / 1024 / 1024 / (te.tv_sec + te.tv_usec / 1000000 - ts.tv_sec - ts.tv_usec / 1000000));
            break;
        }

        sleep(1);
    }
    return NULL;
}
//写数据记录文件
int write_data_record(char* filename,char* content){
char filename_tmp[256]; 
char time[64];
get_current_time(time);
if(filename==NULL)
    snprintf(filename_tmp,sizeof(filename_tmp),"./httpdownload_%d",getpid());
else
    snprintf(filename_tmp,strlen(filename)+1,"%s",filename);
FILE* fp=fopen(filename_tmp,"a+");
if(fp==NULL)
    return errno;
else{
        fwrite(content,strlen(content),1,fp);
        fwrite("\n",strlen("\n"),1,fp);
        fclose(fp);
        return 0;
    }
}
//下载函数调用前的初始化方法
struct ots_httpdownload_para* ots_httpdownload_init(){
	struct ots_httpdownload_para *httpdownload;

    httpdownload = (struct ots_httpdownload_para *)malloc(sizeof(struct ots_httpdownload_para));
    memset(httpdownload, 0, sizeof(struct ots_httpdownload_para));
	//设置默认值
	httpdownload->connecttimeout=CONNECTTIMEOUT;
	httpdownload->timeout=TIMEOUT;
	httpdownload->log_path=NULL;
    return httpdownload;
}
//下载执行函数
int ots_httpdownload_run(struct ots_httpdownload_para* data,int thread_num){
	    int i;	
        pthread_t t_calc;
        pthread_t thread[THREADNUM];
        pthread_create(&t_calc, NULL, calc_thread, data);

//		 start_calc_flag ++;
//		int res=ots_curl_get(data,NULL,0);
	printf("thread_num is %d\n",thread_num);
        for(i=0; i < thread_num; i++){
            pthread_create(&thread[i], NULL, download_thread, data);
        }
        for(i=0;i<thread_num;i++){
            pthread_join(thread[i],NULL);
            //printf("Thread: %lu Joined\n", thread[i]);
        }

        pthread_mutex_lock(&mutex);
        exit_calc_flag = 1;
        pthread_mutex_unlock(&mutex);
        pthread_join(t_calc, NULL);
    return 0;
}
//下载函数执行后的资源释放
int ots_httpdownload_destory(struct ots_httpdownload_para* data){
	if(data!=NULL){
		if(data->url!=NULL)
			ots_mem_free(data->url);
		if(data->downloadfile_path!=NULL)
			ots_mem_free(data->downloadfile_path);
		if(data->log_path!=NULL)
			ots_mem_free(data->log_path);
    //ots_mem_free(data);
	}
    return 0;
}
//通用的资源释放函数
int ots_mem_free(void* ptr){
    if(ptr!=NULL){
        free(ptr);
    }
    return 0;
}
