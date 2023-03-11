#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

#include <pthread.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>

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

void error_msg(const char *str)
{
    perror(str);
    exit(EXIT_FAILURE);
}

int startup_tcp_socket(u_short port)
{
    assert(port != 0);

    /* 创建 Server Socket。*/
    int srv_socket_fd = 0;
    if (-1 == (srv_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))) {
        error_msg("Create socket failed");
    }

    /* 配置 Server Sock 信息。*/
    struct sockaddr_in srv_sock_addr;
    memset(&srv_sock_addr, 0, sizeof(srv_sock_addr));
    srv_sock_addr.sin_family = AF_INET;
    srv_sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 即 0.0.0.0 表示监听本机所有的 IP 地址。
    srv_sock_addr.sin_port = htons(port);

    /* 设置 Server Socket 选项。*/
    int optval = 1;
    if (setsockopt(srv_socket_fd,
                   SOL_SOCKET,    // 表示套接字选项的协议层。
                   SO_REUSEADDR,  // 表示在绑定地址时允许重用本地地址。这样做的好处是，当服务器进程崩溃或被关闭时，可以更快地重新启动服务器，而不必等待一段时间来释放之前使用的套接字。
                   &optval,
                   sizeof(optval)) < 0)
    {
        error_msg("Set sock options failed");
    }

    /* 绑定 Socket 与 Sock Address 信息。*/
    if (-1 == bind(srv_socket_fd,
                   (struct sockaddr *)&srv_sock_addr,
                   sizeof(srv_sock_addr)))
    {
        error_msg("Bind socket failed");
    }

    /* 开始监听 Client 发出的连接请求。*/
    if (-1 == listen(srv_socket_fd, 10))
    {
        error_msg("Listen socket failed");
    }

    return srv_socket_fd;
}

/**********************************************************************/
/* 从 Socket 中读取一行数据，以 \n 换行、\r 回车、CLRF 组合、\0 空字符表示 EOF。
 *   - 如果 socket buffer 中读取到 EOF，则 buff String 以 \n\0 结尾。
 *   - 如果 socket buffer 结束但未找到 EOF，则 buff String 以 \0 结尾。
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
     * 接收指定 Client Socket 发出的数据。示例：
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
            if (c == '\r')  // 回车检测
            {
                n = recv(socket_fd, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))  // 换行检测
                {
                    recv(socket_fd, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buff[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buff[i] = '\0';  // 以 \0 作为读取字符串的结束。
    return i;
}

/**
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
    int cgi_input[2];   // 1：输入端，0：输出端。
    int cgi_output[2];  // 1：输入端，0：输出端。
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
        dup2(cgi_input[0], STDIN);    // cgi_input 输出端重定向到 STDIN
        dup2(cgi_output[1], STDOUT);  // cgi_output 输入端重定向到 STDOUT
        close(cgi_input[1]);          // 关闭 cgi_input 输入端
        close(cgi_output[0]);         // 关闭 cgi_output 输出端

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

        execl(path, NULL);
        exit(0);
    }
    else
    {  // 父进程
        char c = '\0';
        close(cgi_output[1]);
        close(cgi_input[0]);

        /* 将 POST request 通过 Pipe 传递到子进程。*/
        if (0 == strcasecmp(method, "POST"))
        {
            for (int i=0; i < content_len; i++)
            {
                recv(cli_socket_fd, &c, 1, 0);
                write(cgi_input[1], &c, 1);
#ifdef DEBUG
                printf("content char: %c", c);
#endif
            }
        }

        /* 从 cgi_output Pipe 输出端口读取子进程的处理结构，然后响应到 Client。*/
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
    /* 测试：curl localhost:6666
buff: GET / HTTP/1.1
num_chars: 15
     */
#endif

    /* 将 HTTP Method 存入 method 中。*/
    char method[255];
    size_t method_part = 0;  // e.g. `GET`
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
    char *query_str = NULL;  // e.g. /index.html?color=red
    if (0 == strcasecmp(method, "GET"))
    {
        query_str = url;  // 传递 URL 字符串入口。
        while ((*query_str != '?') && (*query_str != '\0'))
        {
            query_str++;
        }
        if (*query_str == '?')
        {
            cgi_on = 1;
            *query_str = '\0';
            query_str++;
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
        if (S_IFDIR == (st.st_mode & S_IFMT))
        {
            strcat(path, "/index.html");
        }
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
        {
            cgi_on = 1;
        }

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

    close(cli_socket_fd);
}

int main(void)
{
    u_short port = 4001;
    int srv_socket_fd = startup_tcp_socket(port);
    printf("httpd running on port %d\n", port);

    /* 初始化 Client Sock 信息存储器变量。*/
    struct sockaddr cli_sock_addr;
    memset(&cli_sock_addr, 0, sizeof(cli_sock_addr));
    int cli_sockaddr_len = sizeof(cli_sock_addr);

    int cli_socket_fd = 0;
    pthread_t newthread;
    while (1)
    {
        if (-1 == (cli_socket_fd = accept(srv_socket_fd,
                                          (struct sockaddr *)(&cli_sock_addr),  // 填充 Client Sock 信息。
                                          (socklen_t *)&cli_sockaddr_len)))
        {
            error_msg("Accept connection from client failed");
        }

        if (pthread_create(&newthread,
                           NULL,
                           (void *)request_handle,
                           (void *)(intptr_t)cli_socket_fd) != 0)
        {
            error_msg("pthread create failed");
        }
    }

    close(srv_socket_fd);
    return EXIT_SUCCESS;
}