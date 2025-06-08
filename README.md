统计代码行数
```
find . -type d -name tests -prune -o -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.py"  -type f | xargs wc -l
```

我们几乎不处理任务结束、退出的情况


正常cocotb启动并附加打印debug信息的命令：
```
make COCOTB_LOG_LEVEL=trace COCOTB_SCHEDULER_DEBUG=1
```

替代telnet localhost 4000的命令：
```
rlwrap nc localhost 4000
```