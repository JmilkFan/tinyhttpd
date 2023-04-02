# tinyhttpd

1. 实现了基于 epoll 和非阻塞 I/O 的 TCP Socket。
2. 实现了 HTTP/1 协议，支持 GET 和 PUT 方式。
3. 实现了 Perl CGI 应用程序。
4. 使用了 fork 子进程。
5. 使用了 pthread 线程处理 Client 请求。

# Use Guide

- 操作系统：CentOS 7


```bash
$ yum install perl-core -y
$ perl -e shell -MCPAN
<cpan> install CGI

$ git clone https://github.com/JmilkFan/tinyhttpd
$ cd tinyhttpd
$ make
#or $ make httpd-debug

$ cd tinyhttpd
$ chmod +x htdocs/*.cgi
$ chmod 600 htdocs/index.html

$ ./httpd
$ curl http://localhost:8086/
```

# Documents & Blog
[《用 C 语言开发一个轻量级 HTTP 服务器》](https://blog.csdn.net/Jmilk/article/details/107193674)