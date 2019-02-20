#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <pwd.h>
#include <time.h>
#include <sys/ipc.h>
#include <signal.h>

#define _CONNECTED "已连接"
#define _DISCONNECTED "未连接"

extern int errno;

struct multiargs
{
	int sockfd;
};

//请求结构
struct request
{
	int type;
};

//响应结构
struct response
{
	int type;
	int size;
};

//名字类型响应包结构
struct name_packet
{
	char name[20];
};

//时间类型响应包结构
struct time_packet
{
	int year;//年
	int month;//月
	int day;//日
	int hour;//小时
	int minute;//分钟
	int second;//秒
};

//消息内容请求包结构
struct message_request
{
	int index;//接收方编号
	char text[512];//消息内容
};

//消息响应包结构
struct message_response
{
	char text[512];//消息内容
};

//消息内容响应包结构
struct message_process
{
	char text[512];//消息内容
	int index;//发送方编号
	char client_ip[16];//发送方的IP
	int client_port;//发送方端口
};

typedef struct client_node* client_list;

//客户端列表存储结构
struct client_node
{
	int comfd;//客户端socket句柄
	int connect_status;//客户端连接状态
	char client_ip[16];//客户端IP地址
	int client_port;//客户端端口号
	client_list next;
};

//客户端列表
struct Clist
{
	int size;//列表长度
	client_list L;//列表指针
};

struct Clist client_table;//客户端列表

int send_packet(int sockfd, char* type, void* packet);//发送数据包的函数
int receive_packet(int sockfd, void* ptr, int len);//接收数据包的函数
void print_menu(void);//打印菜单函数
void receive(void* args);//循环接收包的子线程函数
void sig_handler(int sig);//信号处理函数,处理主线程关闭向子线程发起的关闭通知
int isConnected(int connect_flag);//判断客户端连接情况函数
void close_thread(pthread_t id, int* flag_ptr, int sockfd);//关闭子线程

int main()
{
	int i;
	int sockfd;//socket句柄
	int connect_flag;//判断客户端连接情况的标志
	int choice;//用户选择
	int ret;
	unsigned int server_port;//服务端端口号
	char server_ip[16];//服务端IP地址
	pthread_t id;//线程标记
	time_t t;
	struct passwd *pwd;
	struct tm *lt;
	struct sockaddr_in severAddr;
	struct multiargs args;
	struct request request_packet;//请求包

	connect_flag = 0;//初始化客户端为断开
	printf("连接状态:%s\n",connect_flag == 0 ? _DISCONNECTED : _CONNECTED);//显示连接情况
	print_menu();//打印菜单

	while(1)
	{
		scanf("%d", &choice);
		switch(choice)//用户选择功能
		{
			case 1://连接
			    if(connect_flag)//处理已经连接客户端的情况
			    {
			    	printf("已连接!\n\n");
			    	break;
			    }
			    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)//向操作系统申请socket句柄
			    {
			    	printf("socket() failed! code:%d\n", errno);
			    	return -1;
			    }
				printf("请输入服务器IP和端口\n");//填写请求服务器的IP和端口	
				scanf("%s%u", server_ip, &server_port);
				//请求的服务器的IP和端口
				severAddr.sin_family = AF_INET;
				severAddr.sin_port = htons(server_port);
				severAddr.sin_addr.s_addr = inet_addr(server_ip);
				bzero(&(severAddr.sin_zero), 8);
				printf("connecting...\n");
				//连接服务器
				if(connect(sockfd, (struct sockaddr *)&severAddr, sizeof(severAddr)) == -1)
				{
					printf("connect() failed! code:%d\n", errno);
					close(sockfd);
					return -1;
				}
				connect_flag = 1;//成功连接后改变连接标志
				printf("Connected!\n");
				printf("连接状态:%s\n\n",connect_flag == 0 ? _DISCONNECTED : _CONNECTED);//显示连接状态
				args.sockfd = sockfd;
				//创建子线程循环接收数据包
				ret = pthread_create(&id, NULL, (void *)receive, &args);
				if(ret != 0)//处理子线程创建失败的情况
				{
					printf("Create pthread error\n");
					return -1;
				}
				break;

			case 2://断开
			    if(isConnected(connect_flag) == -1)//处理客户端未连接的情况
			    	break;
			    close_thread(id, &connect_flag, sockfd);//关闭子线程
			    printf("连接状态:%s\n\n",connect_flag == 0 ? _DISCONNECTED : _CONNECTED);//显示连接状态
				break;

			case 3://获取时间
			    if(isConnected(connect_flag) == -1)//处理客户端未连接的情况
			    	break;
			    request_packet.type = 0;//请求时间
			    if(send_packet(sockfd, "request", &request_packet) == -1)//发送请求数据包
			    	return -1;
			    break;

			case 4://获取机器名称
			    if(isConnected(connect_flag) == -1)//处理客户端未连接的情况
			    	break;
			    request_packet.type = 1;//请求机器名称
			    if(send_packet(sockfd, "request", &request_packet) == -1)//发送请求数据包
			    	return -1;
				break;

			case 5://获取客户端列表
			    if(isConnected(connect_flag) == -1)//处理客户端未连接的情况
			    	break;
			    request_packet.type = 2;//请求客户端列表
			    if(send_packet(sockfd, "request", &request_packet) == -1)//发送请求数据包
			    	return -1;
			    break;

			case 6://向列表中客户端发送消息
			    if(isConnected(connect_flag) == -1)//处理客户端未连接的情况
			    	break;
			    request_packet.type = 3;
			    if(send_packet(sockfd, "message", &request_packet) == -1)//发送请求数据包
			    	return -1;
				break;

			case 7://退出客户端
			    if(connect_flag)//处理客户端已经连接的情况
			    {
			    	close_thread(id, &connect_flag, sockfd);
			    }
			    printf("successful exit!\n");
			    return 0;
				break;

			default://处理用户输入错误的功能序号
			    printf("没有此功能,请重新选择!\n");
				break;
		}
	}

	return 0;
}
//子线程循环接收数据包
//传入:socket句柄
void receive(void* args)
{
	int i;
	struct response response_packet;//响应数据包
	struct name_packet host_name;//名字响应数据包
	struct time_packet host_time;//时间响应数据包
	struct multiargs* multi_args = (struct multiargs*)args;
	int sockfd = multi_args->sockfd;//传入的句柄
	struct client_node client;//客户端节点
	struct message_response response_message;//响应的消息给发送方
	struct message_request message;//请求消息数据包
	struct message_process receive_message;//接收方接收到服务器的消息数据包

	signal(SIGQUIT, sig_handler);//处理主线程的关闭信号
	while(1)
	{
		if(receive_packet(sockfd, &response_packet, sizeof(response_packet)) == -1)//接收数据包
			return;

		switch(response_packet.type)//根据响应数据包的类型接收不同类型数据包
		{
			case 0://接收时间数据包
			    if(receive_packet(sockfd, &host_time, sizeof(host_time)) == -1)
			    	return;
			    //打印时间
			    printf("response:%d/%02d/%02d %02d:%02d:%02d\n\n", host_time.year, host_time.month, 
			    	host_time.day, host_time.hour, host_time.minute, host_time.second);
			    break;

			case 1://接收名字数据包
			    if(receive_packet(sockfd, &host_name, sizeof(host_name)) == -1)
			    	return;
			    //打印名字
			    printf("response:%s\n\n", host_name.name);
			    break;

			case 2://接收客户端列表数据包
			    printf("编号:ip:端口\n");//打印客户端列表
			    //通过响应数据包中的个数决定需要循环接收的数据包个数
			    for(i = 0;i < response_packet.size;i++)
			    {
			    	if(receive_packet(sockfd, &client, sizeof(client)) == -1)
			    		return;
			    	printf("%d:%s:%d\n", i, client.client_ip, client.client_port);//打印客户端列表
			    }
			    printf("\n");
			    break;

			case 3://接收服务器给消息发送方的响应
			    if(receive_packet(sockfd, &response_message, sizeof(response_message)) == -1)
			    	return;
			    printf("%s\n\n", response_message.text);//打印响应信息
			    break;

			case 4://接收方接收服务器转发的消息
			    if(receive_packet(sockfd, &receive_message, sizeof(receive_message)) == -1)
			    	return;
			    //打印发送方的信息
			    printf("From:编号:%d ip:%s 端口:%d\n", receive_message.index, receive_message.client_ip, 
			    	receive_message.client_port);
			    //打印消息内容
			    printf("%s\n\n", receive_message.text);
			    break;
			default:
			    break;
		}
	}
}

//接收数据包函数
//sockfd: 句柄
//ptr: 接收包的指针
//len: 接收包的长度
int receive_packet(int sockfd, void* ptr, int len)
{
	int ret;
	//通过循环接收指定接收包长度的接收包
	while(len > 0)
	{
		ret = recv(sockfd, ptr, len, 0);
		if(ret <= 0)//处理接收失败的情况
		{
			printf("recv() failed!\n");
			close(sockfd);
			return -1;
		}
		len -= ret;
		ptr = (char*)ptr + ret;//指针向后移动
	}
	return 0;
}

/*************************************************
 *接收数据包函数                                   *
 *sockfd: 句柄                                   *
 *ptr: 接收包的指针                                *
 *len: 接收包的长度                                *
 *************************************************/
int send_packet(int sockfd, char* type, void* packet)
{
	struct message_request message;
	if(strcmp(type, "request") == 0)//请求类型的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct request), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
		printf("request has been sent\n");

	}

	else if(strcmp(type, "response") == 0)//响应类型的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct response), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "time") == 0)//时间响应的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct time_packet), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "name") == 0)//名字响应的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct name_packet), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "list") == 0)//列表响应的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct client_node), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "message_response") == 0)//响应信息的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct message_response), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "message_process") == 0)//接收的消息的数据包
	{
		if(send(sockfd, (char*)packet, sizeof(struct message_process), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	else if(strcmp(type, "message") == 0)//发送的消息数据包
	{
		printf("请输入将要发送的客户端编号\n");//填写发送客户端编号
		scanf("%d", &(message.index));
		printf("请输入发送的消息内容\n");//填写消息内容
		scanf("%s", message.text);
		if(send(sockfd, (char*)packet, sizeof(struct request), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}

		if(send(sockfd, (char*)&message, sizeof(message), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
		printf("request has been sent\n");
	}

	else if(strcmp(type, "close") == 0)
	{
		if(send(sockfd, (char*)packet, sizeof(struct request), 0) == -1)//发送数据包
		{
			printf("send() failed!\n");
			close(sockfd);
			return -1;
		}
	}

	return 0;
}

//打印菜单函数
void print_menu(void)
{
	printf("菜单:(选择功能请输入下列序号 hint:未连接时仅能使用连接和退出功能)\n");
	printf("(1)连接\n");
	printf("(2)断开\n");
	printf("(3)获取时间\n");
	printf("(4)获取机器名字\n");
	printf("(5)获取客户端列表\n");
	printf("(6)发送信息\n");
	printf("(7)退出\n");
}

//判断连接情况函数
int isConnected(int connect_flag)
{
	if(!connect_flag)//通过标志来判断
	{
		printf("未连接!\n\n");
		return -1;
	}
	return 0;
}

//信号处理函数
void sig_handler(int sig)
{
	pthread_exit(0);
}

//关闭子线程函数
void close_thread(pthread_t id, int* flag_ptr, int sockfd)
{
	int ret;
	struct request request_packet;
	*flag_ptr = 0;

	//向服务端发送断开连接的请求，告知服务端
	request_packet.type = 4;
	if(send_packet(sockfd, "close", &request_packet) == -1)
		return;
	pthread_kill(id, SIGQUIT);//向子进程发送信号
	pthread_join(id, NULL);
	ret = pthread_kill(id, 0);
	if(ret == ESRCH)
		printf("thread has been closed\n");
	close(sockfd);
}