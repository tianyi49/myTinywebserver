# 测试记录1 使用链表的定时器
**LT+ET**

>yy@yy-server:~/code_project/myTinyWebServer/test_pressure/webbench-1.5$ ./webbench -c 8000 -t 5 http://192.168.100.128:9006/
>Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.
>
>Benchmarking: GET http://192.168.100.128:9006/
8500 clients, running 5 sec.
>
>Speed=762204 pages/min, 1422780 bytes/sec.
Requests: 63517 susceed, 0 failed.

1万+QPS

**ET+ET**

>yy@yy-server:~/code_project/myTinyWebServer/test_pressure/webbench-1.5$ ./webbench -c 8500 -t 5 http://192.168.100.128:9006/
>Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.
>
>Benchmarking: GET http://192.168.100.128:9006/
8500 clients, running 5 sec.
>
>Speed=656880 pages/min, 1226131 bytes/sec.
Requests: 54740 susceed, 0 failed.

**LT+LT**
>
>yy@yy-server:~/code_project/myTinyWebServer/test_pressure/webbench-1.5$ ./webbench -c 8500 -t 5 http://192.168.100.128:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.
>
>Benchmarking: GET http://192.168.100.128:9006/
8500 clients, running 5 sec.
>
>Speed=651012 pages/min, 1215222 bytes/sec.
Requests: 54251 susceed, 0 failed.

# 测试记录2 小根堆
**LT+ET**

运行命令：./server -m 1 -t 11 -c 1
yy@yy-server:~/code_project/myTinyWebServer/test_pressure/webbench-1.5$ ./webbench -c 8500 -t 5 http://192.168.100.128:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.100.128:9006/
8500 clients, running 5 sec.

Speed=1267272 pages/min, 2365574 bytes/sec.
Requests: 105606 susceed, 0 failed.

2万QPS+

## 小根堆提前分配
**LT+ET**

运行命令：./server -m 1 -t 11 -c 1

yy@yy-server:~/code_project/myTinyWebServer/test_pressure/webbench-1.5$ ./webbench -c 8500 -t 5 http://192.168.100.128:9006/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.100.128:9006/
8500 clients, running 5 sec.

Speed=1317204 pages/min, 2458758 bytes/sec.
Requests: 109767 susceed, 0 failed.
提升不明显
