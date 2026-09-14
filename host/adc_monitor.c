/*
 * adc_monitor.c —— Linux 上位机第三步：pthread 双线程实时监控 + ASCII 条形图
 *
 * 线程A（reader） ：读串口 + 6 字节帧解析，更新共享数据
 * 线程B（display）：每 2 秒清屏重绘一次条形图 + 状态统计
 * 串口拔插       ：自动尝试重连（30 秒内每秒试一次），不崩溃
 *
 * 帧格式（6 字节大端）：AA 01 VolH VolL Stat SUM
 * 编译： gcc -Wall -o adc_monitor adc_monitor.c -lpthread
 * 运行： ./adc_monitor /dev/ttyUSB0
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
#include <pthread.h>

#define FRAME_HEAD  0xAA     /* 帧头 */
#define FRAME_LEN   6        /* 整帧长度 */
#define STAT_FAULT  0x01     /* Stat 的超阈报警位 */
#define MAX_BARS    40       /* 条形图列数 */
#define VOLT_FULL   330      /* 满量程 3.30V（×100） */

static int g_fd = -1;
static volatile int g_stop = 0;

/* —— 线程间共享数据（reader 写，display 读，互斥锁保护） —— */
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static unsigned int  g_vx100 = 0;   /* 最新电压×100 */
static unsigned char g_stat = 0;    /* 最新状态位 */
static unsigned long g_ok = 0;      /* 好帧累计 */
static unsigned long g_bad = 0;     /* 坏帧累计 */
static unsigned long g_resync = 0;  /* 失步字节累计 */

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;   /* 只置标志，不 close，避免和 reader 线程竞争 */
}

/* 打开并配置串口 115200/8N1/raw（和 serial_read.c / frame_parse.c 相同） */
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

/* 线程A：读串口 + 帧解析，更新共享数据 */
static void *reader_thread(void *arg)
{
    const char *dev = (const char *)arg;
    struct pollfd pfd;
    unsigned char buf[64];
    unsigned char frame[FRAME_LEN];
    int idx = 0;                 /* 帧内字节下标，0 表示等帧头 */
    unsigned long ok = 0;
    unsigned long bad = 0;
    unsigned long resync = 0;

    pfd.fd = g_fd;
    pfd.events = POLLIN;

    while (!g_stop) {
        int pr = poll(&pfd, 1, 500);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (pr == 0) continue;

        /* 串口异常（拔线/掉线）：自动重连 */
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            printf("\n!! 串口断开，尝试重连 ...\n");
            fflush(stdout);
            close(g_fd);
            g_fd = -1;

            int tried;
            for (tried = 0; tried < 30 && !g_stop; tried++) {
                sleep(1);
                int fd = serial_open(dev);
                if (fd >= 0) {
                    g_fd = fd;
                    pfd.fd = fd;
                    idx = 0;   /* 重连后重新同步帧 */
                    printf("已重新连接 %s，继续监控\n", dev);
                    fflush(stdout);
                    break;
                }
            }
            if (g_fd < 0) { printf("重连失败，退出\n"); g_stop = 1; break; }
            continue;
        }

        if (!(pfd.revents & POLLIN)) continue;

        int n = read(g_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno != EINTR) {
                if (errno == EIO) {   /* USB 转串口拔出时的常见错误 */
                    printf("\n!! read 返回 EIO，串口可能已拔出，尝试重连 ...\n");
                    fflush(stdout);
                    close(g_fd);
                    g_fd = -1;

                    int tried;
                    for (tried = 0; tried < 30 && !g_stop; tried++) {
                        sleep(1);
                        int fd = serial_open(dev);
                        if (fd >= 0) {
                            g_fd = fd;
                            pfd.fd = fd;
                            idx = 0;
                            printf("已重新连接 %s，继续监控\n", dev);
                            fflush(stdout);
                            break;
                        }
                    }
                    if (g_fd < 0) { printf("重连失败，退出\n"); g_stop = 1; break; }
                    continue;
                }
                perror("read");
                break;
            }
            continue;
        }

        /* —— 帧解析状态机（和 frame_parse.c 相同） —— */
        int i;
        for (i = 0; i < n; i++) {
            unsigned char b = buf[i];

            if (idx == 0) {
                if (b == FRAME_HEAD) {
                    frame[0] = b;
                    idx = 1;
                } else {
                    resync++;   /* 杂散字节，稳态下应一直为 0 */
                }
            } else {
                frame[idx++] = b;
                if (idx == FRAME_LEN) {
                    unsigned int sum = 0;
                    int k;
                    for (k = 0; k < FRAME_LEN - 1; k++) sum += frame[k];
                    if ((sum & 0xFF) == frame[5]) {
                        unsigned int vx100 = ((unsigned int)frame[2] << 8) | frame[3];
                        unsigned char stat = frame[4];
                        ok++;
                        /* 好帧：更新电压/状态/计数 */
                        pthread_mutex_lock(&g_mtx);
                        g_vx100 = vx100;
                        g_stat = stat;
                        g_ok = ok;
                        g_bad = bad;
                        g_resync = resync;
                        pthread_mutex_unlock(&g_mtx);
                    } else {
                        bad++;
                        /* 坏帧：只更新计数 */
                        pthread_mutex_lock(&g_mtx);
                        g_ok = ok;
                        g_bad = bad;
                        g_resync = resync;
                        pthread_mutex_unlock(&g_mtx);
                    }
                    idx = 0;
                }
            }
        }
    }
    return NULL;
}

/* 线程B：每 2 秒画一次 ASCII 条形图 */
static void *display_thread(void *arg)
{
    (void)arg;
    while (!g_stop) {
        sleep(2);

        unsigned int v;
        unsigned char s;
        unsigned long ok, bad, resync;
        pthread_mutex_lock(&g_mtx);
        v = g_vx100;
        s = g_stat;
        ok = g_ok;
        bad = g_bad;
        resync = g_resync;
        pthread_mutex_unlock(&g_mtx);

        /* 0 ~ 3.30V 映射到 MAX_BARS 列 */
        int bars = (int)((unsigned long long)v * MAX_BARS / VOLT_FULL);
        if (bars > MAX_BARS) bars = MAX_BARS;

        printf("\033[2J\033[H");   /* 清屏回到左上角 */
        printf("========== STM32 ADC 实时监控 ==========\n");
        printf("电压: %u.%02uV  %s\n", v / 100, v % 100,
               (s & STAT_FAULT) ? "*** FAULT 超阈 ***" : "OK");
        printf("[");
        int i;
        for (i = 0; i < MAX_BARS; i++) putchar(i < bars ? '#' : '-');
        printf("]\n");
        printf("   0V                         3.30V\n");
        printf("----------------------------------------\n");
        printf("好帧=%lu  坏帧=%lu  失步字节=%lu\n", ok, bad, resync);
        if (s & STAT_FAULT) printf("!!! 电压超过 3.0V 阈值，请检查电位器 !!!\n");
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
        printf("打开 %s 失败：检查路径/权限(sudo chmod 666 %s)/是否被占用\n", dev, dev);
        return 1;
    }
    printf("已打开 %s，启动双线程监控（Ctrl+C 退出）...\n", dev);

    pthread_t tid_r, tid_d;
    if (pthread_create(&tid_r, NULL, reader_thread, (void *)dev) != 0) {
        perror("pthread_create reader");
        close(g_fd);
        return 1;
    }
    if (pthread_create(&tid_d, NULL, display_thread, NULL) != 0) {
        perror("pthread_create display");
        g_stop = 1;
        pthread_join(tid_r, NULL);
        close(g_fd);
        return 1;
    }

    /* 主线程只等退出信号，不碰 g_fd，避免和 reader 线程竞争 */
    while (!g_stop) usleep(200000);

    pthread_join(tid_r, NULL);
    pthread_join(tid_d, NULL);

    if (g_fd >= 0) close(g_fd);
    printf("\n已退出，串口已关闭\n");
    return 0;
}
