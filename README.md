# tinyhttpd

Tiny HTTP Server 适用于学习并理解 TCP 协议、HTTP 协议以及 HTTP 服务器的设计与实现。

# Use Guide

```bash
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
