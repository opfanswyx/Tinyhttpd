/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

/*****************************
 * 检查参数c是否为空格字符
 * 也就是判断是否为空格('')、定位字符('\t')、CR('\r')、换行('\n')、垂直定位字符('\v')、或翻页('\f')的情况。
 * 参数为空白字符返回非0，否则返回0。
 * ***************************/
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n" //定义server名称
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

// Http请求，后续主要是处理这个头
//
// GET / HTTP/1.1
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5
// 

// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data
// color=gray


/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  
 * Process the request appropriately.
 * Parameters: the socket connected to the client 
 * 
 * 参数：连接到客户端的套接字
 * */
/**********************************************************************/
void accept_request(void *arg)
{
    //intptr_t在不同的平台是不一样的，始终与地址位数相同
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st; //文件结构体，用于获取文件shux
    int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
    char *query_string = NULL;

    /****************************
     * 接收数据放入buff缓冲区
     * 返回字节数
     * **************************/
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;

    /************************************
     * 举例:"GET / HTTP/1.1\n"
     * 提取字符串GET
     * ISspace参数为空白字符返回非0，否则返回0。
     * **********************************/
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';
    /************************************
     * 函数说明：strcasecmp()用来比较参数s1 和s2 字符串，比较时会自动忽略大小写的差异。
     * 返回值：若参数s1 和s2 字符串相同则返回0。
     *        s1 长度大于s2 长度则返回大于0 的值，
     *        s1 长度若小于s2 长度则返回小于0 的值。
     * *********************************/
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        //如果接收到的数据中既有GET方法与POST方法，则向客户端返回该web方法尚未实现
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)
        cgi = 1;    //cgi为标志位，置1说明开启cgi解析

    i = 0;
    while (ISspace(buf[j]) && (j < numchars))   //如果为空白字符则跳过
        j++;
    /*********************************
     * //得到 "/"   
     * 注意：如果你的http的网址为http://x.x.x.x:8888/index.html 
     * 那么你得到的第一条http信息为GET /index.html HTTP/1.1，那么 
     * 解析得到的就是/index.html 
     * ********************************/
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        //如果是GET请求，url可能会带有?,有查询参数
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            //将解析参数截取下来
            *query_string = '\0';
            query_string++;
        }
    }

    //url中的路径格式化到path
    sprintf(path, "htdocs%s", url);
    //如果path只是一个目录，默认设置为首页index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    /**************************************
     * 函数定义: int stat(const char *file_name, struct stat *buf);
     * 函数说明: 通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
     * 返回值: 执行成功则返回0，失败返回-1，错误代码存于errno（需要include <errno.h>）
     * ***********************************/
    if (stat(path, &st) == -1) {
        //假如访问的网页不存在，则不断的读取剩下的请求头信息，并丢弃即可
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        //最后返回网页不存在
        not_found(client);
    }
    else
    {
        //st_mode是用特征位来表示文件类型的
        //S_IFMT      0170000     文件类型的位遮罩
        //如果路径是个目录，那就将主页进行显示
        if ((st.st_mode & S_IFMT) == S_IFDIR) //目录
            strcat(path, "/index.html");
        //如果你的文件默认是有执行权限的，自动解析成cgi程序，如果有执行权限但是不能执行，会接受到报错信号
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
                //S_IXUSR:文件所有者具可执行权限
                //S_IXGRP:用户组具可执行权限
                //S_IXOTH:其他用户具可读取权限 
            cgi = 1;
        if (!cgi)
            //读取静态文件返回给请求的http客户端
            serve_file(client, path);
        else
            //执行cgi动态解析
            execute_cgi(client, path, method, query_string);
    }
    //执行完毕后，关闭socket
    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    //循环读取
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
//执行cgi动态解析
void execute_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    //读写管道的声明
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    //如果是GET请求
    //读取并且丢弃头信息
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            //循环读取头信息找到Content-Length字段的值
            //"Content-Length: 15"
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);    //错误请求
            return;
        }
    }
    else/*HEAD or other*/
    {
    }
    /****************************************
     * #include<unistd.h>
     * int pipe(int filedes[2]);
     * 返回值：成功，返回0，否则返回-1。参数数组包含pipe使用的两个文件的描述符。fd[0]:读管道，fd[1]:写管道。
     * 必须在fork()中调用pipe()，否则子进程不会继承文件描述符。
     * 两个进程不共享祖先进程，就不能使用pipe。但是可以使用命名管道。
     * pipe(cgi_output)执行成功后，cgi_output[0]:读通道 cgi_output[1]:写通道
     * **************************************/
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }

    //返回正确响应码200
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //1代表着stdout，0代表着stdin，将系统标准输出重定向为cgi_output[1]
        dup2(cgi_output[1], STDOUT);
        //将系统标准输入重定向为cgi_input[0]，这一点非常关键，
        //cgi程序中用的是标准输入输出进行交互
        dup2(cgi_input[0], STDIN);
        //关闭了cgi_output中的读通道
        close(cgi_output[0]);
        //关闭了cgi_input中的写通道
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /**************************************************
         * 表头文件#include<unistd.h>
         * 定义函数
         * int execl(const char * path,const char * arg,....);
         * 函数说明
         * execl()用来执行参数path字符串所代表的文件路径，接下来的参数代表执行该文件时传递过去的argv(0)、argv[1]……，最后一个参数必须用空指针(NULL)作结束。
         * 返回值
         * 如果执行成功则函数不会返回，执行失败则直接返回-1，失败原因存于errno中。 
         * ***********************************************/
        execl(path, NULL);
        exit(0);
    } else {    /* parent */
        //关闭了cgi_output中的写通道，注意这是父进程中cgi_output变量和子进程要区分开
        close(cgi_output[1]);
        //关闭了cgi_input中的读通道
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        /***************************************
         * 定义函数：pid_t waitpid(pid_t pid, int * status, int options);
         * 函数说明：waitpid()会暂时停止目前进程的执行, 直到有信号来到或子进程结束.
         * 如果在调用wait()时子进程已经结束, 则wait()会立即返回子进程结束状态值. 子进程的结束状态值会由参数status 返回,
         * 而子进程的进程识别码也会一快返回.
         * 如果不在意结束状态值, 则参数status 可以设成NULL. 参数pid 为欲等待的子进程识别码, 其他数值意义如下：
         * 1、pid<-1 等待进程组识别码为pid 绝对值的任何子进程.
         * 2、pid=-1 等待任何子进程, 相当于wait().
         * 3、pid=0 等待进程组识别码与目前进程相同的任何子进程.
         * 4、pid>0 等待任何子进程识别码为pid 的子进程.
         * ************************************/
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket 
 * 此函数启动在指定端口上侦听Web连接的过程。
 * 如果端口为0，则动态分配端口并修改原始端口变量以反映实际端口。
 * 参数：指向包含连接端口的变量的指针
 * 返回：套接字
 * */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;
    /**********************************
     * socket系统调用创建一个套接字
     * 返回：一个文件描述符，该描述符可以用来访问该套接字
     * int socket(int domain, int type, int protocol);
     * domain指定协议族，最常用的是AF_UNIX(unix和linux文件系统实现的本地套接字)和AF_INET(UNIX网络套接字)
     * type指定套接字的通信类型，SOCK_STREAM(有序，可靠，面向连接的双向字节流)和SOCK_DGRAM(数据报服务)
     * protocol指定使用的协议，一般由套接字类型和套接字域来决定，一般默认0 
     * *******************************/
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name)); //结构体清空置零
    name.sin_family = AF_INET;
    //host to network, short 短整数从主机字节序到网络字节序转换
    name.sin_port = htons(*port);
    /********************************
     * INADDR_ANY转换过来就是0.0.0.0，泛指本机的意思，也就是表示本机的所有IP
     * 因为有些机子不止一块网卡，多网卡的情况下，这个就表示所有网卡ip地址的意思。
     * 
     * 比如一台电脑有3块网卡，分别连接三个网络，那么这台电脑就有3个ip地址了，如果某个应用程序需要监听某个端口，那他要监听哪个网卡地址的端口呢？
     * 
     * 如果绑定某个具体的ip地址，你只能监听你所设置的ip地址所在的网卡的端口，其它两块网卡无法监听端口，
     * 如果我需要三个网卡都监听，那就需要绑定3个ip，也就等于需要管理3个套接字进行数据交换，这样岂不是很繁琐？
     * 
     * 所以出现INADDR_ANY，你只需绑定INADDR_ANY，管理一个套接字就行，不管数据是从哪个网卡过来的，只要是绑定的端口号过来的数据，都可以接收到。
     * ******************************/
    name.sin_addr.s_addr = htonl(INADDR_ANY);   //host to network, long
    /******************************************
     * 套接字选项？？？？SO_REUSEADDR？？？？需要更多的研究，可写blog
     * int setsockopt(int socket, int level, int option_name,
     *      const void *option_value, size_t option_len);
     * 如果在套接字级别设置选项，level参数设置为SOL_SOCKET
     * option_name参数指定要设置的选项，SO_REUSEADDR:防止服务器在发生意外时，端口未被释放，可以重新使用
     * option_value参数的长度为option_len字节，用于设定选项的新值，它被传递给底层协议的处理函数，并且不能被修改
     * 成功返回0，失败返回-1
     * ****************************************/
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    /******************************************
     * 命名套接字，AF_UNIX套接字会关联到一个文件系统的路径名，AF_INET套接字关联到一个IP端口号
     * int bind(int socket, const struct sockaddr *address, size_t address_len);
     * 成功返回0，失败返回-1
     * ***************************************/
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port 如果动态分配端口号*/
    {
        socklen_t namelen = sizeof(name);
        /*****************************************
         * ?????????不明白？？？？？？？？？？？
         * int PASCAL FAR getsockname( SOCKET s, struct sockaddr FAR* name, int FAR* namelen);
         * s：标识一个已捆绑套接口的描述字。
         * name：接收套接口的地址（名字）。
         * namelen：名字缓冲区长度。
         * ***************************************/
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    /****************************************
     * 套接字队列
     * *************************************/
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
//开始源码阅读之路
int main(void)
{
    int server_sock = -1;
    u_short port = 4000;    //unsigned short类型
    int client_sock = -1;
    /****************************
     * struct sockaddr_in{
     *  short int           sin_family; //AF_INET
     *  unsigned short int  sin_port;   //Port_number
     *  struct in_addr      sin_addr;   //Internet address
     * };
     *  struct in_addr{
     *      unsigned long int s_addr;
     * };
     * **************************/
    struct sockaddr_in client_name;
    /****************************
     * 猜测typedef int socklen_t 
     * /usr/include/arpa/inet.h
     * #ifndef __socklen_t_defined
     * typedef __socklen_t socklen_t;  
     * # define __socklen_t_defined
     * #endif
     * **************************/
    socklen_t  client_name_len = sizeof(client_name); 
    pthread_t newthread;

    /***************
     *  startup: 初始化 httpd 服务
     * 包括建立套接字，绑定端口，进行监听等。
     * *************/
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        /*****************************************
         * int accept(int socket, struct sockaddr *address, size_t *address_len);
         * 当有连接时，accept函数返回一个新的套接字文件描述符。发送错误时，返回-1。
         * ***************************************/
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(&client_sock); */
        /***************************************
         * int pthread_create(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg);
         * 
         * *************************************/
        if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}
