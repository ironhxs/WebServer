/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 * ====================================================================
 * 中文说明:
 * WebBench 是一个简单的 Web 服务器压力测试工具
 * 
 * 工作原理:
 * 1. 父进程创建多个子进程 (fork)
 * 2. 每个子进程并发发送HTTP请求
 * 3. 使用 alarm() 定时，到时停止测试
 * 4. 子进程通过管道(pipe)将结果返回父进程
 * 5. 父进程汇总统计并输出结果
 * 
 * 核心指标:
 * - Speed: 每分钟完成的请求数 (pages/min) = QPS * 60
 * - bytes/sec: 每秒传输的字节数
 * - susceed: 成功请求数
 * - failed: 失败请求数
 * 
 * 使用示例:
 *   ./webbench -c 1000 -t 30 http://localhost:9006/
 *   含义: 1000并发连接，持续30秒
 * ====================================================================
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* ==================== 全局变量 ====================  */

/* 测试结果统计变量 */
volatile int timerexpired=0;  /* 定时器是否超时标志 (volatile防止编译器优化) */
int speed=0;   /* 成功请求数 */
int failed=0;  /* 失败请求数 */
int bytes=0;   /* 接收的总字节数 */

/* HTTP协议版本: 0=HTTP/0.9, 1=HTTP/1.0, 2=HTTP/1.1 */
int http10=1;
/* HTTP请求方法定义 */
#define METHOD_GET 0      /* GET方法 - 获取资源 */
#define METHOD_HEAD 1     /* HEAD方法 - 只获取头部 */
#define METHOD_OPTIONS 2  /* OPTIONS方法 - 查询支持的方法 */
#define METHOD_TRACE 3    /* TRACE方法 - 路径追踪 */
#define PROGRAM_VERSION "1.5"

/* ==================== 配置变量 ====================  */
int method=METHOD_GET;    /* 默认使用GET方法 */
int clients=1;            /* 并发客户端数量(子进程数) */
int force=0;              /* 是否强制模式(不等待服务器响应) */
int force_reload=0;       /* 是否发送无缓存请求(Pragma: no-cache) */
int proxyport=80;         /* 代理服务器端口 */
char *proxyhost=NULL;     /* 代理服务器主机 */
int benchtime=30;         /* 测试持续时间(秒) */

/* ==================== 内部变量 ====================  */
int mypipe[2];            /* 父子进程通信管道 [0]=读端, [1]=写端 */
char host[MAXHOSTNAMELEN];/* 目标服务器主机名 */
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];/* HTTP请求报文缓冲区 */

static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* 函数声明 */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

/**
 * @brief SIGALRM信号处理函数
 * @param signal 信号编号 (未使用)
 * 
 * 工作机制:
 * - alarm(benchtime)设置定时器
 * - benchtime秒后触发SIGALRM信号
 * - 此函数被调用，设置timerexpired=1
 * - benchcore()检测到该标志后停止发送请求
 */
static void alarm_handler(int signal)
{
   (void)signal;  /* 消除未使用参数警告 */
   timerexpired=1;
}	

static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)
 {
	  usage();
          return 2;
 } 

 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   case 't': benchtime=atoi(optarg);break;	     
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');
	     proxyhost=optarg;
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}

void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");

  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   bzero(tmp,10);
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else
   {
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // printf("Host=%s\n",host);
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}

/**
 * @brief 执行压力测试的主函数
 * @return 系统错误码 (0=成功, 1=连接失败, 3=fork失败)
 * 
 * 核心流程:
 * 1. 首先测试连接服务器是否可用
 * 2. 创建管道(pipe)用于父子进程通信
 * 3. fork()创建多个子进程
 * 4. 子进程: 执行benchcore()发送HTTP请求
 * 5. 父进程: 从pipe读取汇总结果
 * 6. 输出最终统计信息
 * 
 * 关键系统调用:
 * - pipe(): 创建匿名管道
 * - fork(): 创建子进程
 * - fdopen(): 将fd转为FILE*
 * - fscanf(): 从管道读取数据
 */
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;
  FILE *f;

  /* 步骤1: 检查目标服务器是否可达 */
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);
  /* create pipe */
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }

  if( pid< (pid_t) 0)
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)
  {
    /**
     * 子进程处理逻辑:
     * 1. 调用benchcore()循环发送HTTP请求
     * 2. 直到alarm信号到达 (timerexpired=1)
     * 3. 将结果写入管道返回给父进程
     */
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

    /* 将结果写入管道: "成功数 失败数 字节数" */
    f=fdopen(mypipe[1],"w");  /* 将fd转为FILE*便于fprintf */
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
	 fclose(f);
	 return 0;
  } else
  {
	  /**
	   * 父进程处理逻辑:
	   * 1. 从管道读取所有子进程的结果
	   * 2. 累加统计数据
	   * 3. 输出最终报告
	   */
	  f=fdopen(mypipe[0],"r");  /* 打开管道读端 */
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);
	  speed=0;
          failed=0;
          bytes=0;

	  while(1)
	  {
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);

  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}

/**
 * @brief 核心压测函数 - 循环发送HTTP请求
 * @param host 目标主机
 * @param port 目标端口
 * @param req HTTP请求报文
 * 
 * 工作流程:
 * 1. 设置alarm定时器，到时触发timerexpired标志
 * 2. 循环执行:
 *    a. 创建TCP连接
 *    b. 发送HTTP请求
 *    c. 读取响应(如果force=0)
 *    d. 关闭连接，统计结果
 * 3. 直到timerexpired=1才退出
 * 
 * 关键系统调用:
 * - alarm(): 设置定时信号
 * - sigaction(): 注册信号处理函数
 * - write(): 发送HTTP请求
 * - read(): 接收HTTP响应
 * - shutdown(): 半关闭连接(HTTP/0.9时用)
 */
void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];  /* 接收缓冲区 */
 int s,i;
 struct sigaction sa;

 /**
  * sigaction() - 设置信号处理函数
  * 注册SIGALRM的处理函数为alarm_handler
  */
 sa.sa_handler=alarm_handler;
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);

 /**
  * alarm() - 设置定时器
  * @param seconds: benchtime秒后触发SIGALRM
  * 到时alarm_handler被调用，设置timerexpired=1
  */
 alarm(benchtime);

 rlen=strlen(req);

 /**
  * 主循环: 不断发送HTTP请求直到定时器超时
  * 
  * 每次循环:
  * 1. 检查timerexpired标志
  * 2. 创建socket连接
  * 3. 发送请求(write)
  * 4. 读取响应(read) - 如果force=0
  * 5. 关闭连接
  * 6. 更新统计计数
  */
 nexttry:while(1)
 {
    /* 检查定时器是否超时 */
    if(timerexpired)
    {
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;  /* 修正: 最后一次失败可能是因为超时导致的 */
       }
       return;
    }
    
    /* 创建TCP连接 */
    s=Socket(host,port);                          
    if(s<0) { failed++;continue;}  /* 连接失败，计入失败数 */
    
    /* 发送HTTP请求 */
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
    
    /* HTTP/0.9时需要半关闭写端，通知服务器请求发送完毕 */
    if(http10==0) 
	    if(shutdown(s,1)) { failed++;close(s);continue;}
    
    /* 读取服务器响应 (force=0时才读) */
    if(force==0) 
    {
            /* read all available data from socket */
	    while(1)
	    {
              if(timerexpired) break; 
	      i=read(s,buf,1500);
              /* fprintf(stderr,"%d\n",i); */
	      if(i<0) 
              { 
                 failed++;
                 close(s);
                 goto nexttry;
              }
	       else
		       if(i==0) break;
		       else
			       bytes+=i;
	    }
    }
    if(close(s)) {failed++;continue;}
    speed++;
 }
}
