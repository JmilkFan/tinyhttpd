#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <fcntl.h>
#include <netdb.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define SUCCESS 0
#define FAIL -1

/* epoll 并发规格参数。 */
#define MAX_EVENTS 10


/**
 * <ctype.h>
 * 判断给定的 Char 是否为空白字符（e.g. 空格符、制表符、换行符等）。
 *  返回 non-0 表示是空白字符;
 *  返回 0 表示不是空白字符。 
 */
#define IS_SPACE(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN  0
#define STDOUT 1
#define STDERR 2

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_msg(const char *str)
{
    fprintf(stderr, "ERROR code: %d \n", errno);
    perror(str);
    exit(EXIT_FAILURE);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup_tcp_socket(u_short port)
{
    assert(port != 0);

    /* 创建 Server Socket fd 实例。*/
    int srv_socket_fd = 0;
    if (-1 == (srv_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)))
        error_msg("Create socket failed");

    /* 配置 Server Sock 信息。*/
    struct sockaddr_in srv_sock_addr;
    memset(&srv_sock_addr, 0, sizeof(srv_sock_addr));
    srv_sock_addr.sin_family = AF_INET;
    srv_sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // INADDR_ANY 即 0.0.0.0 表示监听本机所有的 IP 地址。
                                                        // htonl() 将字符串类型 IP 转换为 unsigned long 类型，以符合 unsigned long a_addr 类型定义。
    srv_sock_addr.sin_port = htons(port);               // htons() 将字符串类型 Port 转换为 unsigned short 类型，以符合 unsigned short int sin_port 类型定义。

    /* 设置 Server Socket fd 选项。*/
    int optval = 1;
    if (setsockopt(srv_socket_fd,
                   SOL_SOCKET,    // 表示套接字选项的协议层。
                   SO_REUSEADDR,  // 表示在绑定地址时允许重用本地地址。这样做的好处是，当服务器进程崩溃或被关闭时，可以更快地重新启动服务器，而不必等待一段时间来释放之前使用的套接字。
                   &optval,
                   sizeof(optval)) < 0)
    {
        error_msg("Set sock options failed");
    }

    /* 绑定 Server Socket fd 与 Sock Address 信息。*/
    if (-1 == bind(srv_socket_fd,
                   (struct sockaddr *)&srv_sock_addr,
                   sizeof(srv_sock_addr)))
    {
        error_msg("Bind socket failed");
    }

    /* Server Socket fd 开始监听 Client 发出的连接请求。*/
    if (-1 == listen(srv_socket_fd, 10))
    {
        error_msg("Listen socket failed");
    }

    return srv_socket_fd;
}

/**********************************************************************/
/* 从 Socket Buffer 中读取 HTTP Requeset 中的一行数据。
 * 每调用一次就读取一行，以 \n、\r 或 \r\n 表示 EOL（End of Line）。
 *   - 如果 socket buffer 中读取到 EOL，则转化为 \n\0 结尾。
 *   - 如果 socket buffer 结束但未找到 EOL，则 buff String 以 \0 结尾。
 * Parameters：
 *   - socket fd、buff string、line size。
 * Returns：
 *   - buff string 的长度，不包括 \0。    
 **********************************************************************/
int get_line(int socket_fd, char *buff, int size)
{
    int i = 0, n = 0;
    char c = '\0';

    /**
     * 接收指定 Client Socket 发出的数据。
     * HTTP request start line 总是以 /r/n 结尾。示例：
normal char: G
normal char: E
normal char: T
normal char:
normal char: /
normal char:
normal char: H
normal char: T
normal char: T
normal char: P
normal char: /
normal char: 1
normal char: .
normal char: 1
normal char:
CLRF char: 0A
     */
    while ((c != '\n') && (i < size-1))
    {
        n = recv(socket_fd, &c, 1, 0);  // 读取一个字符
        if (n > 0)
        {
            /* CLRF \r\n 组合检测 */
            if (c == '\r')  // 回车检测，如果是 \r，就继续看看紧跟的是不是 \n？
            {
                n = recv(socket_fd, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))  // 换行检测，如果是 \n，那就取出丢弃。
                    recv(socket_fd, &c, 1, 0);
                else
                    c = '\n';
            }

            /* 不是 \n 或 \r\n，那就是有效数据。*/
            buff[i] = c;
            i++;
        }
        else
        {
            /* 没数据了，退出读取。*/
            c = '\n';
        }
    }

    buff[i] = '\0';  // Buff String 以 \0 作为读取字符串的结束。
    return i;
}

/**
 * Inform the client that the requested web method has not been implemented.
 * 
 * HTTP Response Header:
 *  HTTP/1.0 501 Method Not Implemented
 *  Server: jdbhttpd/0.1.0
 *  Content-Type: text/html
 * 
 * HTTP Response Body:
 * <HTML>
 *  <HEAD><TITLE>Method Not Implemented</TITLE></HEAD>
 *  <BODY><P>HTTP request method not supported.</BODY>
 * </HTML>
 */
void unimplemented(intptr_t cli_socket_fd)
{
    char buff[1024];
    
    sprintf(buff, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, SERVER_STRING);
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "Content-Type: text/html\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "</TITLE></HEAD>\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<BODY><P>HTTP request method not supported.\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "</BODY></HTML>\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);
}

/**
 * Give a client a 404 not found status message.
 * 
 * HTTP Response Header:
 *  HTTP/1.0 404 NOT FOUND
 *  Server: jdbhttpd/0.1.0
 *  Content-Type: text/html
 * 
 * HTTP Response Body:
 *  <HTML>
 *   <TITLE>Not Found<TITLE>
 *   <BODY><P>The server could not fulfill
 *            your request because the resource specified
 *            is unavailable or nonexistent.
 *   </BODY>
 *  </HTML>
 */
void not_found(intptr_t cli_socket_fd)
{
    char buff[1024];

    sprintf(buff, "HTTP/1.0 404 NOT FOUND\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, SERVER_STRING);
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "Content-Type: text/html\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<HTML><TITLE>Not Found<TITLE>\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<BODY><P>The server could not fulfill\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "your request because the resource specified\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "is unavailable or nonexistent.\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "</BODY></HTML>\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);
}

/**********************************************************************
 * Return the informational HTTP headers about a file.
 * Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void send_headers(intptr_t cli_socket_fd, const char *filename)
{
    char buff[1024];
    (void)filename;  // could use filename to determine file type

    sprintf(buff, "HTTP/1.0 200 OK\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, SERVER_STRING);
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "Content-Type: text/html\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void send_contents(intptr_t cli_socket_fd, FILE *resource)
{
    char buff[1024];

    fgets(buff, sizeof(buff), resource);
    while (!feof(resource))
    {
        send(cli_socket_fd, buff, strlen(buff), 0);
        fgets(buff, sizeof(buff), resource);
    }
}

/**********************************************************************/
/* 向 Client 响应常规 files 请求，会构造 Server Response Headers。
 * Parameters:
 *  - socket fd；
 *  - Client 请求访问的 file path。
 **********************************************************************/
void serve_regular_file(intptr_t cli_socket_fd, const char *filename)
{
    /* 从 Recv buffer 中消耗完 Client Request Headers。*/
    char buff[1024];
    buff[0] = 'A';
    buff[1] = '\0';
    int num_chars = 1;
    while ((num_chars > 0) && strcmp(buff, "\n"))  
    {
        num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
    }

    /* 获取文件内容。*/
    FILE *resource = fopen(filename, "r");
    if (NULL == resource)
    {
        not_found(cli_socket_fd);
    }
    else
    {
        // 响应 Header
        send_headers(cli_socket_fd, filename);
        // 响应 File Content
        send_contents(cli_socket_fd, resource);
    }

    fclose(resource);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(intptr_t cli_socket_fd)
{
    char buff[1024];

    sprintf(buff, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "Content-Type: text/html\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<P>Your browser sent a bad request, ");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "such as a POST without a Content-Length.\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(intptr_t cli_socket_fd)
{
    char buff[1024];

    sprintf(buff, "HTTP/1.0 500 Internal Server Error\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "Content-Type: text/html\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    sprintf(buff, "<P>Error prohibited CGI execution.\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(intptr_t cli_socket_fd, const char *path,
                 const char *method, const char *query_str)
{
    char buff[1024];
    buff[0] = 'A';
    buff[1] = '\0';
    int num_chars = 0;
    int content_len = -1;

    if (0 == strcasecmp(method, "GET"))
    {  // GET

        /* 从 Recv buffer 中消耗完 Client Request Headers。*/
        while ((num_chars > 0) && strcmp(buff, "\n"))
        {
            num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
        }
    }
    else if (0 == strcasecmp(method, "POST"))
    {  // POST

        /* 从 Recv buffer 中的 Headers 中取出 Request Body，主要是 Content-Length。*/
        num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
        while ((num_chars > 0) && strcmp(buff,"\n"))
        {
            buff[15] = '\0';  // `Content-Length:` 的长度截止到 15。
            if (0 == strcasecmp(buff, "Content-Length:"))
            {
                content_len = atoi(&(buff[16])); // 17 开始为具体的数值。
#ifdef DEBUG
                printf("content length: %d", content_len);
#endif
            }

            /* 从 Recv buffer 中消耗完 Client Request Headers。*/
            num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
        }
        if (-1 == content_len)
        {
            bad_request(cli_socket_fd);  // HTTP Request Body 中没有 Content-Length 字段
            return;
        }
    }

    /* 创建 In/Out 两个 Pipe，用于父子进程间通信。*/
    int cgi_input[2];   // 0：输出端，1：输入端。
    int cgi_output[2];  // 0：输出端，1：输入端。
    if (pipe(cgi_input) < 0)   // Input Pipe
    {
        cannot_execute(cli_socket_fd);
        return;
    }
    if (pipe(cgi_output) < 0)  // Output Pipe
    {
        cannot_execute(cli_socket_fd);
        return;
    }

    /* 创建子进程，用于执行 CGI 程序。*/
    pid_t pid;
    if ((pid = fork()) < 0)
    {
        cannot_execute(cli_socket_fd);
        return;
    }

    sprintf(buff, "HTTP/1.0 200 OK\r\n");
    send(cli_socket_fd, buff, strlen(buff), 0);

    /*** 
     * Pipeline 数据流：
     *  client ->cgi_input[1] -> cgi_input[0] -> STDIN -> STDOUT -> cgi_output[1] -> cgi_output[0] -> client
     */
    if (0 == pid)
    {  // 子进程

        /* 管道重定向：cgi_input[0] -> STDIN -> STDOUT -> cgi_output[1] */
        dup2(cgi_input[0], STDIN);    // cgi_input 管道的输出端重定向到 STDIN；
        dup2(cgi_output[1], STDOUT);  // cgi_output 管道的输入端重定向到 STDOUT；

        /* 子进程不需要 cgi_input 输入端，和 cgi_output 输出端，这两端在父进程处理。*/
        close(cgi_input[1]);          // 关闭 cgi_input 输入端；
        close(cgi_output[0]);         // 关闭 cgi_output 输出端；

        /* 设置 CGI 程序的运行时环境变量。*/
        char meth_env[255];
        char query_env[255];
        char length_env[255];
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (0 == strcasecmp(method, "GET"))
        {
            sprintf(query_env, "QUERY_STRING=%s", query_str);
            putenv(query_env);
        }
        else
        {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_len);
            putenv(length_env);
        }

        // 子进程调用 path 指定的 Perl CGI 脚本程序。
        execl(path, NULL);
        exit(0);
    }
    else
    {  // 父进程
        char c = '\0';

        close(cgi_input[0]);
        close(cgi_output[1]);

        /* 将 POST request 通过 Pipe 传递到子进程。*/
        if (0 == strcasecmp(method, "POST"))
        {
            int i;
            for (i=0; i < content_len; i++)
            {
                /* 将 Client Request 以 Byte steam 的形式写入子进程。*/
                recv(cli_socket_fd, &c, 1, 0);
                write(cgi_input[1], &c, 1);  
#ifdef DEBUG
                printf("content char: %c", c);
#endif
            }
        }

        /* 从 cgi_output 输出端口读取子进程的处理结果，然后响应到 Client。*/
        while (read(cgi_output[0], &c, 1) > 0)
        {
            send(cli_socket_fd, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);

        int status;
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************
 * 处理 Client Request。支持 GET 和 POST Methods。
 * 
 * e.g.
 *  GET / HTTP/1.1
 *  Host: localhost:8086
 *  ...
 * 
 * or
 *  POST /color1.cgi HTTP/1.1
 *  Host: localhost:8086
 *  Content-Length: 10
 *  ...
 *  Form Data
 *  color=yellow
 **********************************************************************/
void request_handle(void *arg)
{
    intptr_t cli_socket_fd = (intptr_t)arg;
    char buff[1024];

    /* 获取 HTTP Request 的第一行。*/ 
    size_t num_chars;  // 第一行的字符数量
    num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
#ifdef DEBUG
    printf("Recevice message from client:\n");
    printf("buff: %s", buff);
    printf("num_chars: %zu\n", num_chars);
    /* 测试：curl localhost:8086
buff: GET / HTTP/1.1
num_chars: 15
     */
#endif

    /* 将 HTTP Method 存入 method 中。*/
    char method[255];
    size_t method_part = 0;
    while (!IS_SPACE(buff[method_part]) && (method_part < sizeof(method)-1))
    {
        method[method_part] = buff[method_part];
        method_part++;
    }
    method[method_part] = '\0';
#ifdef DEBUG
    printf("method: %s\n", method);
    /*
method: GET
     */
#endif

    /* 如果不是 GET 也不是 POST，返回未实现。*/
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(cli_socket_fd);
        return;
    }

    /* 将 HTTP URL 存入 url 中。*/
    size_t url_part = method_part;  // e.g. `/index.html`
    while (IS_SPACE(buff[url_part]) && (url_part < num_chars))  // 跳过 method 后紧跟的空字符。
    {
        url_part++;
    }
    size_t i = 0;
    char url[255];
    while (!IS_SPACE(buff[url_part]) && (i < sizeof(url) -1) && (url_part < num_chars))
    {
        url[i] = buff[url_part];
        i++;
        url_part++;
    }
    url[i] = '\0';
#ifdef DEBUG
    printf("url: %s\n", url);
    /*
url: /index.html
     */
#endif

    /* 针对 POST，需要开启 Perl CGI。*/
    int cgi_on = 0;
    if (0 == strcasecmp(method, "POST"))
    {
        cgi_on = 1;
    }

    /* 处理 GET 请求的 Query String，如果存在，还需要开启 Perl CGI。*/
    char *query_str = NULL;  // query string 入口指针
    if (0 == strcasecmp(method, "GET"))
    {
        query_str = url;  // 初始指向 URL 字符串入口
        while ((*query_str != '?') && (*query_str != '\0'))
        {
            query_str++;
        }

        if (*query_str == '?')
        {
            cgi_on = 1;
            *query_str = '\0';  // 将 ? 替换为空字符
            query_str++;  // query_str 指向 ? 后的参数
        }
    }

    /* 将 HTTP File Path 存入 path 中。*/
    char path[512];
    sprintf(path, "htdocs%s", url);     // index.html 存放在 ./htdocs/ 目录下
    if (path[strlen(path) - 1] == '/')  // e.g. htdocs/
    {
        strcat(path, "index.html");     // e.g. htdocs/index.html
    }
#ifdef DEBUG
    printf("path: %s\n", path);
    /*
path: htdocs/index.html
     */
#endif

    /* 获取 path 指定文件的元数据信息，并存储到 st buf 中。*/
    struct stat st;
    if (-1 == stat(path, &st)) 
    {   /* 没有找到文件，返回 404。*/

        /* 从 Recv buffer 中消耗完 Client Request Headers。*/
        while ((num_chars > 0) && strcmp("\n", buff))
        {
            num_chars = get_line(cli_socket_fd, buff, sizeof(buff));
        }
        /* 返回 404。*/
        not_found(cli_socket_fd);
    }
    else
    {
        /* 判断 path 是否为目录。*/
        if (S_IFDIR == (st.st_mode & S_IFMT))
        {
            strcat(path, "/index.html");
        }
        /* 判断文件是否具有执行权限。*/
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
        {
            cgi_on = 1;
        }

        /* 是否需要执行 CGI 程序。*/
        if (!cgi_on)
        {
#ifdef DEBUG
            printf("Serve regular file: %s\n", path);
#endif
            serve_regular_file(cli_socket_fd, path);
        }
        else
        {
#ifdef DEBUG
            printf("Execute CGI: %s\n.", path);
#endif
            execute_cgi(cli_socket_fd, path, method, query_str);
        }
    }

    /* 关闭 Client Socket fd，epoll 实例也会从监听 fds 列表中删除这个 fd。*/
    close(cli_socket_fd);
}



/*************************
 * MAIN
 *************************/

/* 设置 Socket 为非阻塞 I/O 模式。*/
static int set_sock_non_blocking(int sock_fd)
{
    int flags;

    if (FAIL == (flags = fcntl(sock_fd, F_GETFL, 0)))
    {
        error_msg("fcntl1");
        return FAIL;
    }

    flags |= O_NONBLOCK;
    if (FAIL == fcntl(sock_fd, F_SETFL, flags))
    {
        error_msg("fcntl2");
        return FAIL;
    }

    return SUCCESS;
}

int main(void)
{
    
    u_short port = 8086;  // 自定义 Socket 端口。
    int srv_socket_fd = startup_tcp_socket(port);
    
    /* 设置 Server Socket fd 为非阻塞模式。*/
    if (FAIL == set_sock_non_blocking(srv_socket_fd))
    {
        error_msg("set_sock_non_blocking");
        close(srv_socket_fd);
    }

    /* 创建一个 epoll 实例。*/
    int epoll_fd = -1;
    if (FAIL == (epoll_fd = epoll_create1(0)))
    {
        error_msg("epoll_create");
    }

    /* 定义 epoll ctrl event 和 callback events 实例 */
    struct epoll_event event, events[MAX_EVENTS];
    // 将 Server Socket fd 添加到 epoll 实例的监听列表中
    event.data.fd = srv_socket_fd;
    // 设置 epoll 的 Events 类型为 EPOLLIN（可读事件）和 EPOLLET（采用 ET 模式）
    event.events = EPOLLIN | EPOLLET;
    // 将 Server Socket fd 添加（EPOLL_CTL_ADD）到 epoll 实例的监听列表中，并设定监听事件类型。
    if (FAIL == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, srv_socket_fd, &event))
    {
        error_msg("epoll_ctl");
    }

    printf("httpd running on port %d\n", port);
    int i, event_cnt;
    while (1)
    {
        /* epoll 实例开始等待事件，一次最多可返回 MAX_EVENTS 个事件，并存放到 events 容器中。*/
        event_cnt = epoll_wait(epoll_fd, events, 64, -1);
        for (i = 0; i < event_cnt; ++i)
        {
            /* Server Socket fd 有可读事件，表示有 Client 发起了连接请求。*/
            if (srv_socket_fd == events[i].data.fd)
            {
                printf("Accepted client connection request.\n");
                for ( ;; )
                {
                    /* 初始化 Client Sock 信息存储器变量。*/
                    struct sockaddr cli_sock_addr;
                    memset(&cli_sock_addr, 0, sizeof(cli_sock_addr));
                    int cli_sockaddr_len = sizeof(cli_sock_addr);

                    int cli_socket_fd = 0;
                    if (FAIL == (cli_socket_fd = accept(srv_socket_fd,
                                                        (struct sockaddr *)(&cli_sock_addr),  // 填充 Client Sock 信息。
                                                        (socklen_t *)&cli_sockaddr_len)))
                    {
                        /* 如果是 EAGAIN（Try again ）错误或非阻塞 I/O 的 EWOULDBLOCK（Operation would block）错误通知，则直接 break，继续循环，直到 “数据就绪” 为止。*/
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                            break;
                        else
                            error_msg("Accept connection from client failed");
                            break;
                    }

                    /* 将一个 socket addr 转换为对应的 Hostname 和 Service name */
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
                    if (SUCCESS == getnameinfo(&cli_sock_addr, cli_sockaddr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NAMEREQD | NI_NUMERICHOST))
                        printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", cli_socket_fd, hbuf, sbuf);
                    else
                        printf("Accepted connection on descriptor %d\n", cli_socket_fd);

                    /* 设置 Client Socket 为非阻塞 I/O 模式。*/
                    if (FAIL == set_sock_non_blocking(cli_socket_fd))
                    {
                        error_msg("set_sock_non_blocking");
                        close(cli_socket_fd);
                        break;
                    }

                    /* 将 Client Socket fd 添加到 epoll 实例的监听列表中 */
                    event.data.fd = cli_socket_fd;
                    event.events = EPOLLIN | EPOLLET;  // 设定可读监听事件，并采用 ET 模式。
                    if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cli_socket_fd, &event))  // 添加 Client Socket fd 及其监听事件。
                    {
                        error_msg("epoll_ctl");
                        close(cli_socket_fd);
                        break;
                    }
                }
            }

            /* 发生了数据等待读取事件。因为 epoll 实例正在使用 ET 模式，所以必须完全读取所有可用数据，否则不会再次收到相同数据的通知。*/
            else if (events[i].events & EPOLLIN)
            {
                pthread_t newthread;
                int cli_socket_fd = events[i].data.fd;

                if (pthread_create(&newthread,
                                   NULL,
                                   (void *)request_handle,
                                   (void *)(intptr_t)cli_socket_fd) != 0)
                {
                    error_msg("pthread create failed");
                }
            }
            /* 发生了 epoll 异常事件，直接关闭 Client Socket fd。*/
            else if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
                error_msg("epoll error.");
                close(events[i].data.fd);
            }
        }
    }

    close(srv_socket_fd);
    return EXIT_SUCCESS;
}