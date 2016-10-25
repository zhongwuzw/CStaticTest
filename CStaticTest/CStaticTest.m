//
//  CStaticTest.m
//  CStaticTest
//
//  Created by 钟武 on 2016/10/13.
//  Copyright © 2016年 钟武. All rights reserved.
//

#import "CStaticTest.h"
#import "hello.h"
#import "ots_httpdownload.h"

@implementation CStaticTest

- (void)downloadFileWithFilePath:(NSString *)filePath logPath:(NSString *)logPath{
//    char argv[8][] = {{10},{5},{"http://curl.haxx.se/download/curl-7.47.1.tar.gz"},{"/root/test.file"},{"/root/httpdownloadlog"},{1},{1},{3}};
    NSString *path = @"http://curl.haxx.se/download/curl-7.47.1.tar.gz";
    struct ots_httpdownload_para *httpdownload=ots_httpdownload_init();
    char url[256];
    char downloadpath[256];
    char logpath[256];
    //snprintf(url,sizeof(url),"%s","http://192.168.25.96/150MB.exe");
    snprintf(url,sizeof(url),"%s",[path cStringUsingEncoding:NSUTF8StringEncoding]);
    //snprintf(downloadpath,sizeof(downloadpath),"./test.file");
//    snprintf(downloadpath,sizeof(downloadpath),argv[4]);
    snprintf(downloadpath,sizeof(downloadpath),"%s", [filePath cStringUsingEncoding:NSUTF8StringEncoding]);
    httpdownload->url=url;
    httpdownload->connecttimeout=5;
    httpdownload->timeout=10;
    httpdownload->is_write_log=1;
    httpdownload->is_write_file=1;
    httpdownload->downloadfile_path=downloadpath;
    snprintf(logpath,sizeof(logpath),"%s",[logPath cStringUsingEncoding:NSUTF8StringEncoding]);
    
    httpdownload->log_path=logpath;
//    printf("args is %s,%s,%s,%s,%s,%s,%s,%s\n",argv[1],argv[2],argv[3],argv[4],argv[5],argv[6],argv[7],argv[8]);
    ots_httpdownload_run(httpdownload,3);
}
- (void)justForTest{
    hello();
}

@end
