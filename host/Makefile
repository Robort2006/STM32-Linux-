# Makefile —— 一键编译 Linux 上位机
# 用法：
#   make          编译全部三个程序
#   make monitor  只编译 monitor（最终成品，双线程条形图）
#   make clean    删除所有可执行文件
# 注意：下面每条命令行必须用 Tab 缩进（不是空格），否则 make 报错

CC      = gcc
CFLAGS  = -Wall
PTHREAD = -pthread          # monitor 用了 pthread，必须链接线程库

all: serial_read frame_parse monitor

# 第一步：裸 HEX 打印，验证链路
serial_read: serial_read.c
	$(CC) $(CFLAGS) -o $@ $<

# 第二步：帧解析，好/坏帧计数
frame_parse: frame_parse.c
	$(CC) $(CFLAGS) -o $@ $<

# 第三步：双线程 + ASCII 条形图（最终成品）
monitor: monitor.c
	$(CC) $(CFLAGS) -o $@ $< $(PTHREAD)

clean:
	rm -f serial_read frame_parse monitor

.PHONY: all clean
