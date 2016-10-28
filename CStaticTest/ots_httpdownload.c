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
#define CONNECTTIMEOUT 15
#define TIMEOUT 30
/**************define global variable****************/
double download_sum = 0;
int start_calc_flag = 0;
int exit_calc_flag = 0;
//读写互斥锁
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//内存释放方法
int ots_mem_free(void* ptr);
int write_data_record(char* filename,char* content);

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
int ots_curl_get(struct ots_httpdownload_para* data){
    CURL* curl;
    CURLcode res;
    
    struct curl_download_writedata_struct *callback_userdata;
    callback_userdata = (struct curl_download_writedata_struct *)malloc(sizeof(struct curl_download_writedata_struct));
    memset(callback_userdata, 0, sizeof(struct curl_download_writedata_struct));
    callback_userdata->curldata=(struct curl_data_struct *)malloc(sizeof(struct curl_data_struct));
    memset(callback_userdata->curldata, 0, sizeof(struct curl_data_struct));
    
    if(data->is_write_file==1){
        if(data->downloadfile_path!=NULL)
            callback_userdata->fp=fopen(data->downloadfile_path,"w+");
        else//设置默认存储文件名
            callback_userdata->fp=fopen("./httpdownload.file","w+");
        if(callback_userdata->fp==NULL){//文件打开失败也会执行下载过程，只是不写文件
            
            printf("%s——%d——%s:downloadfile open failed!\n",__FILE__,__LINE__,__FUNCTION__);
        };
    }
    //curl_global_init(CURL_GLOBAL_ALL);
    
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
    
    if (res != CURLE_OK){
        printf("%s——%d——%s:curl request %s failed:%s!\n",__FILE__,__LINE__,__FUNCTION__,data->url,curl_easy_strerror(res));
        
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &data->code);
        
        if(callback_userdata->fp!=NULL){
            fclose(callback_userdata->fp);
        }
        if(callback_userdata->curldata->ptr!=NULL){
            ots_mem_free(callback_userdata->curldata->ptr);
        }
        ots_mem_free(callback_userdata->curldata);
        ots_mem_free(callback_userdata);
        curl_easy_cleanup(curl);
        //curl_global_cleanup();
        return -1;
    }else{
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,&data->code);
        //data check
        
        if(callback_userdata->fp!=NULL){
            fclose(callback_userdata->fp);
        }
        if(callback_userdata->curldata->ptr!=NULL){
            ots_mem_free(callback_userdata->curldata->ptr);
        }
        ots_mem_free(callback_userdata->curldata);
        ots_mem_free(callback_userdata);
        curl_easy_cleanup(curl);
        //curl_global_cleanup();
        return 0;
    }
}


//下载线程
void *download_thread(void* curl_data){
    struct ots_httpdownload_para* data=(struct ots_httpdownload_para*)curl_data;
    printf("%s——%d——%s:child thread id is %lu!\n",__FILE__,__LINE__,__FUNCTION__,pthread_self());
    
    pthread_mutex_lock(&mutex);
    start_calc_flag ++;
    pthread_mutex_unlock(&mutex);
    
    int res=ots_curl_get(data);
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
    
    printf("%s——%d——%s:enter calc thread\n",__FILE__,__LINE__,__FUNCTION__);
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
        printf("%s——%d——%s:speed:%.2lfMb/s\n",__FILE__,__LINE__,__FUNCTION__,_speed/1024/1024);
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
    
    printf("%s——%d——%s:enter write_data_record\n",__FILE__,__LINE__,__FUNCTION__);
    get_current_time(time);
    if(filename==NULL)
        snprintf(filename_tmp,sizeof(filename_tmp),"./httpdownload_%d",getpid());
    else
        snprintf(filename_tmp,strlen(filename)+1,"%s",filename);
    FILE* fp=fopen(filename_tmp,"a+");
    if(fp==NULL){
        
        printf("%s——%d——%s:file %s open failed\n",__FILE__,__LINE__,__FUNCTION__,filename_tmp);
        return errno;}
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
    httpdownload->url=NULL;
    httpdownload->downloadfile_path="./test.file";
    return httpdownload;
}
//下载执行函数
int ots_httpdownload_run(struct ots_httpdownload_para* data,int thread_num){
    int i;
    start_calc_flag=0;
    exit_calc_flag=0;
    download_sum=0;
    pthread_t t_calc;
    pthread_t thread[THREADNUM];
    pthread_create(&t_calc, NULL, calc_thread, data);
    printf("%s——%d——%s:download thread_num is %d\n",__FILE__,__LINE__,__FUNCTION__,thread_num);
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
        ots_mem_free(data); 
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
