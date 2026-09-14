/*
 * serial_read.c —— Linux 串口读取第一步：打开 /dev/ttyUSB0，termio 配 115200/8N1/raw，
 *                  用 poll() 非阻塞等数据，读到字节按 HEX 打印，验证整条链路。
 *
 * 编译： gcc -Wall -o serial_read serial_read.c
 * 运行： ./serial_read /dev/ttyUSB0        （不传参数默认就是 /dev/ttyUSB0）
 * 退出： Ctrl + C
 *
 * 预期现象：板子每 500ms 发一帧 6 字节，屏幕上每 0.5 秒刷一行，形如
 *           AA 01 01 2C 00 D9
 *           能稳定看到帧头 AA，就说明 Linux 侧收数完全正常。
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

static int g_fd = -1;          // 串口文件描述符，信号处理函数里要用来关闭

/* Ctrl+C 退出时把串口关好，避免设备被占用 */
static void on_sigint(int sig)
{
	(void)sig;
	if (g_fd >= 0) close(g_fd);
	printf("\n已退出，串口已关闭\n");
	exit(0);
}

/*
 * 打开并配置串口。
 * 参数 dev：设备路径，如 "/dev/ttyUSB0"
 * 返回：成功返回文件描述符(>=0)，失败返回 -1
 */
static int serial_open(const char *dev)
{
	// O_RDWR 读写；O_NOCTTY 不让串口成为控制终端；O_NDELAY 非阻塞打开
	int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		perror("open 串口失败");
		return -1;
	}
	// 打开后恢复阻塞标志，阻塞与否交给 poll 控制
	fcntl(fd, F_SETFL, 0);

	struct termios opt;
	if (tcgetattr(fd, &opt) != 0) {        // 读出当前串口属性
		perror("tcgetattr 失败");
		close(fd);
		return -1;
	}

	cfmakeraw(&opt);                       // 一步进入 raw 模式：关闭特殊字符处理/回显/规范模式
	cfsetispeed(&opt, B115200);            // 输入波特率 115200
	cfsetospeed(&opt, B115200);            // 输出波特率 115200

	opt.c_cflag |= (CLOCAL | CREAD);       // 忽略调制解调器控制线；使能接收
	opt.c_cflag &= ~PARENB;                // 无校验位
	opt.c_cflag &= ~CSTOPB;                // 1 个停止位
	opt.c_cflag &= ~CSIZE;
	opt.c_cflag |= CS8;                    // 8 个数据位
	opt.c_cflag &= ~CRTSCTS;               // 关闭硬件流控（CH340 没接 RTS/CTS）

	opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 非规范模式、不回显、不把 Ctrl+C 当信号交给串口
	opt.c_oflag &= ~OPOST;                 // 输出不做特殊处理（原始输出）

	// VMIN=0 VTIME=0：read 有多少读多少、不等待，配合 poll 使用
	opt.c_cc[VMIN]  = 0;
	opt.c_cc[VTIME] = 0;

	tcflush(fd, TCIOFLUSH);                // 清空收发缓冲区里的残留数据
	if (tcsetattr(fd, TCSANOW, &opt) != 0) {   // TCSANOW：立即生效
		perror("tcsetattr 失败");
		close(fd);
		return -1;
	}
	return fd;
}

int main(int argc, char **argv)
{
	const char *dev = (argc >= 2) ? argv[1] : "/dev/ttyUSB0";

	signal(SIGINT, on_sigint);             // 注册 Ctrl+C 处理

	g_fd = serial_open(dev);
	if (g_fd < 0) {
		printf("打开 %s 失败：检查设备路径、权限(sudo chmod 666 %s)、是否被其他程序占用\n", dev, dev);
		return 1;
	}
	printf("已打开 %s，115200/8N1/raw，开始接收（Ctrl+C 退出）...\n", dev);

	struct pollfd pfd;
	pfd.fd = g_fd;
	pfd.events = POLLIN;                   // 只关心“可读”事件

	unsigned char buf[64];
	while (1) {
		// poll 等串口可读，超时 1000ms；超时返回 0，循环继续，不忙等、不卡死
		int pr = poll(&pfd, 1, 1000);
		if (pr < 0) {
			if (errno == EINTR) continue;  // 被信号打断就重来
			perror("poll 失败");
			break;
		}
		if (pr == 0) continue;             // 1 秒内没数据，继续等

		if (pfd.revents & POLLIN) {
			int n = read(g_fd, buf, sizeof(buf));   // 一次最多读 64 字节
			if (n > 0) {
				int i;
				for (i = 0; i < n; i++)
					printf("%02X ", buf[i]);         // 每个字节打成两位大写十六进制
				printf("\n");
				fflush(stdout);                      // 立刻刷出，不等缓冲区
			} else if (n < 0 && errno != EINTR) {
				perror("read 失败");
				break;
			}
		}
	}

	close(g_fd);
	return 0;
}
