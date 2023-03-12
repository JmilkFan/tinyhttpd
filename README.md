# tinyhttpd

Tiny HTTP Server

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
[《用 C 语言开发一个轻用 C 语言开发一个》](https://blog.csdn.net/Jmilk/article/details/107193674)
