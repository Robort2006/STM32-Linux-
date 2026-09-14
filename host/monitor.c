/*
 * monitor.c —— Linux 上位机第三步：pthread 双线程 + 终端 ASCII 条形图
 *
 * 架构（双线程，这是本步核心）：
 *   线程A reader：poll+read+状态机 解析串口，把最新电压/状态写入共享变量
 *   线程B display：每 2 秒读共享变量，在终端原地画一条条形图
 *   两个线程都碰同一份数据，所以用 pthread_mutex 加锁保护
 *
 * 帧格式（6 字节，大端）：AA 01 VolH VolL Stat SUM
 *
 * 编译： gcc -Wall -o monitor monitor.c -pthread      ← 注意要加 -pthread
 * 运行： ./monitor /dev/ttyUSB0
 * 退出： Ctrl + C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <pthread.h>

#define FRAME_HEAD  0xAA     // 帧头
#define FRAME_LEN   6        // 整帧长度
#define STAT_FAULT  0x01     // Stat 超阈位
#define BAR_WIDTH   20       // 条形图长度（格）
#define MAX_X100    330      // 满量程 3.30V（volt_x100 单位）

/* —— 线程间共享的数据 —— */
typedef struct {
	pthread_mutex_t lock;        // 访问下面字段前必须先加锁
	uint16_t volt_x100;          // 最新电压×100
	uint8_t  stat;               // 最新状态位
	unsigned long ok, bad, resync;
} shared_t;

static shared_t g_sh;
static int g_fd = -1;

static void on_sigint(int sig)
{
	(void)sig;
	if (g_fd >= 0) close(g_fd);
	printf("\n已退出，串口已关闭\n");
	exit(0);
}

/* 打开并配置串口 115200/8N1/raw */
static int serial_open(const char *dev)
{
	int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) { perror("open 串口失败"); return -1; }
	fcntl(fd, F_SETFL, 0);

	struct termios opt;
	if (tcgetattr(fd, &opt) != 0) { perror("tcgetattr 失败"); close(fd); return -1; }
	cfmakeraw(&opt);
	cfsetispeed(&opt, B115200);
	cfsetospeed(&opt, B115200);
	opt.c_cflag |= (CLOCAL | CREAD);
	opt.c_cflag &= ~PARENB;
	opt.c_cflag &= ~CSTOPB;
	opt.c_cflag &= ~CSIZE;
	opt.c_cflag |= CS8;
	opt.c_cflag &= ~CRTSCTS;
	opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	opt.c_oflag &= ~OPOST;
	opt.c_cc[VMIN]  = 0;
	opt.c_cc[VTIME] = 0;
	tcflush(fd, TCIOFLUSH);
	if (tcsetattr(fd, TCSANOW, &opt) != 0) { perror("tcsetattr 失败"); close(fd); return -1; }
	return fd;
}

/* 线程A：一直读串口、解析帧，更新共享变量 */
static void *reader_thread(void *arg)
{
	(void)arg;
	struct pollfd pfd = { g_fd, POLLIN, 0 };
	unsigned char buf[64];
	unsigned char frame[FRAME_LEN];
	int idx = 0;

	while (1) {
		int pr = poll(&pfd, 1, 1000);
		if (pr < 0) { if (errno == EINTR) continue; break; }
		if (pr == 0) continue;

		int n = read(g_fd, buf, sizeof(buf));
		if (n <= 0) {                 // 读不到数据（多半是设备被拔出），直接结束线程
			printf("\n[串口断开] 读线程退出\n");
			break;
		}

		int i;
		for (i = 0; i < n; i++) {
			unsigned char b = buf[i];
			if (idx == 0) {
				if (b == FRAME_HEAD) { frame[0] = b; idx = 1; }
				else {
					pthread_mutex_lock(&g_sh.lock);
					g_sh.resync++;
					pthread_mutex_unlock(&g_sh.lock);
				}
			} else {
				frame[idx++] = b;
				if (idx == FRAME_LEN) {
					unsigned int sum = 0;
					int k;
					for (k = 0; k < FRAME_LEN - 1; k++) sum += frame[k];
					pthread_mutex_lock(&g_sh.lock);
					if ((sum & 0xFF) == frame[5]) {
						g_sh.volt_x100 = ((unsigned int)frame[2] << 8) | frame[3];
						g_sh.stat      = frame[4];
						g_sh.ok++;
					} else {
						g_sh.bad++;
					}
					pthread_mutex_unlock(&g_sh.lock);
					idx = 0;
				}
			}
		}
	}
	return NULL;
}

/* 线程B：每 2 秒画一次条形图 */
static void *display_thread(void *arg)
{
	(void)arg;
	while (1) {
		sleep(2);

		/* 加锁拷贝一份快照，锁外画图（不要拿着锁做 I/O） */
		pthread_mutex_lock(&g_sh.lock);
		uint16_t v   = g_sh.volt_x100;
		uint8_t  s   = g_sh.stat;
		unsigned long ok = g_sh.ok, bad = g_sh.bad, rs = g_sh.resync;
		pthread_mutex_unlock(&g_sh.lock);

		int filled = (int)v * BAR_WIDTH / MAX_X100;   // 0~3.30V 映射到 0~20 格
		if (filled > BAR_WIDTH) filled = BAR_WIDTH;

		printf("\rVolt: [");
		int i;
		for (i = 0; i < filled; i++)  putchar('#');
		for (i = filled; i < BAR_WIDTH; i++) putchar(' ');
		printf("] %u.%02uV %-9s | 好%lu 坏%lu 失步%lu   ",
		       v / 100, v % 100,
		       (s & STAT_FAULT) ? "ALARM!" : "OK",
		       ok, bad, rs);
		fflush(stdout);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	const char *dev = (argc >= 2) ? argv[1] : "/dev/ttyUSB0";
	signal(SIGINT, on_sigint);

	g_fd = serial_open(dev);
	if (g_fd < 0) {
		printf("打开 %s 失败：检查路径/权限/是否被占用\n", dev);
		return 1;
	}

	memset(&g_sh, 0, sizeof(g_sh));
	pthread_mutex_init(&g_sh.lock, NULL);

	printf("已打开 %s，开始监控（Ctrl+C 退出）...\n\n", dev);

	pthread_t tr, td;
	pthread_create(&tr, NULL, reader_thread, NULL);    // 线程A：读串口
	pthread_create(&td, NULL, display_thread, NULL);   // 线程B：画图

	pthread_join(tr, NULL);      // 读线程退出（串口断）就结束
	pthread_cancel(td);          // 结束画图线程
	pthread_join(td, NULL);

	pthread_mutex_destroy(&g_sh.lock);
	close(g_fd);
	return 0;
}
