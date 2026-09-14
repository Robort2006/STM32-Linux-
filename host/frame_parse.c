/*
 * frame_parse.c —— Linux 上位机第二步：在串口字节流上做定长帧解析
 *
 * 板子帧格式（6 字节，大端）：
 *   [0] 0xAA 帧头
 *   [1] 0x01 通道号
 *   [2] 电压×100 的高字节
 *   [3] 电压×100 的低字节
 *   [4] Stat 状态位（bit0=1 表示超阈报警）
 *   [5] 校验和 = 前 5 字节累加结果的低 8 位
 *
 * 本程序做三件事：
 *   1) 状态机逐字节找帧头 0xAA，收满 6 字节
 *   2) 用校验和判断这一帧有没有在传输中出错/错位
 *   3) 解析出电压、状态，并统计 好帧/坏帧/失步字节 三个计数
 *
 * 编译： gcc -Wall -o frame_parse frame_parse.c
 * 运行： ./frame_parse /dev/ttyUSB0
 * 退出： Ctrl + C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>

#define FRAME_HEAD  0xAA     // 帧头
#define FRAME_LEN   6        // 整帧长度
#define STAT_FAULT  0x01     // Stat 的超阈报警位

static int g_fd = -1;

static void on_sigint(int sig)
{
	(void)sig;
	if (g_fd >= 0) close(g_fd);
	printf("\n已退出，串口已关闭\n");
	exit(0);
}

/* 打开并配置串口 115200/8N1/raw（和 serial_read.c 相同） */
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

int main(int argc, char **argv)
{
	const char *dev = (argc >= 2) ? argv[1] : "/dev/ttyUSB0";
	signal(SIGINT, on_sigint);

	g_fd = serial_open(dev);
	if (g_fd < 0) {
		printf("打开 %s 失败：检查路径/权限(sudo chmod 666 %s)/是否被占用\n", dev, dev);
		return 1;
	}
	printf("已打开 %s，开始解析帧（Ctrl+C 退出）...\n\n", dev);

	struct pollfd pfd;
	pfd.fd = g_fd;
	pfd.events = POLLIN;

	unsigned char buf[64];

	/* —— 帧解析状态机 —— */
	unsigned char frame[FRAME_LEN];
	int idx = 0;                 // 当前已收到帧内第几个字节，0 表示正在等帧头
	unsigned long ok = 0;        // 校验通过的好帧
	unsigned long bad = 0;       // 校验失败的坏帧（传输错位/丢字节）
	unsigned long resync = 0;    // 在“等帧头”状态丢掉的杂散字节数（反映失步）

	while (1) {
		int pr = poll(&pfd, 1, 1000);
		if (pr < 0) { if (errno == EINTR) continue; perror("poll"); break; }
		if (pr == 0) continue;
		if (!(pfd.revents & POLLIN)) continue;

		int n = read(g_fd, buf, sizeof(buf));
		if (n <= 0) { if (n < 0 && errno != EINTR) { perror("read"); break; } continue; }

		int i;
		for (i = 0; i < n; i++) {
			unsigned char b = buf[i];

			if (idx == 0) {                 // 状态0：只认帧头
				if (b == FRAME_HEAD) {
					frame[0] = b;
					idx = 1;
				} else {
					resync++;               // 不是帧头，丢弃并计数（正常稳态下应一直为0）
				}
			} else {                        // 状态1：依次装后续字节
				frame[idx++] = b;
				if (idx == FRAME_LEN) {      // 收满 6 字节，校验
					unsigned int sum = 0;
					int k;
					for (k = 0; k < FRAME_LEN - 1; k++) sum += frame[k];
					if ((sum & 0xFF) == frame[5]) {
						/* —— 校验通过，解析字段 —— */
						unsigned int vx100 = ((unsigned int)frame[2] << 8) | frame[3];
						unsigned char stat = frame[4];
						ok++;
						printf("[#%lu] %u.%02uV  Stat=%s  好帧=%lu 坏帧=%lu 失步字节=%lu\n",
						       ok,
						       vx100 / 100, vx100 % 100,
						       (stat & STAT_FAULT) ? "FAULT(超阈)" : "OK ",
						       ok, bad, resync);
						fflush(stdout);
					} else {
						bad++;
						printf("!! 校验和不符，丢弃一帧（期望%02X 实得%02X）坏帧=%lu\n",
						       sum & 0xFF, frame[5], bad);
						fflush(stdout);
					}
					idx = 0;                // 一帧处理完，回到等帧头状态
				}
			}
		}
	}

	close(g_fd);
	return 0;
}
