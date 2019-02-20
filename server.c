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

#define SERVER_PORT 2243

extern int errno;

struct multiargs
{
	int comfd;
	int index;
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

void list_create(void);//列表创建函数
void list_insert(char* client_ip, int client_port, int comfd);//列表插入函数
void receive(void* args);//数据包接收函数
int send_packet(int sockfd, char* type, void* packet);//发送数据包的函数
int receive_packet(int sockfd, void* ptr, int len);//接收数据包的函数

int main()
{
	int sockfd, comfd;//句柄
	int ret;
	struct sockaddr_in serverAddr, clientAddr;
	int iClientSize;
	pthread_t threadid;//子线程id
	struct multiargs args;

	list_create();//创建列表
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)//申请socket句柄
	{
		printf("socket() failed! code:%d\n", errno);
		return -1;
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);//填写服务器的端口号
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bzero(&(serverAddr.sin_zero), 8);

	if(bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)//绑定
	{
		printf("bind() failed! code:%d\n", errno);
		close(sockfd);
		return -1;
	}

	if(listen(sockfd, 5) == -1)//监听
	{
		printf("listen() failed! code:%d\n", errno);
		close(sockfd);
		return -1;
	}

	printf("Waiting for client connecting!\n");
	iClientSize = sizeof(struct sockaddr_in);
	while(1)
	{
		if((comfd = accept(sockfd, (struct sockaddr*)&clientAddr, (socklen_t *) &iClientSize)) == -1)//接收服务器
		{
			printf("accept() failed! code:%d\n", errno);
			close(sockfd);
			return -1;
		}
		args.comfd = comfd;//记录客户端句柄
		args.index = client_table.size;//记录编号
		ret = pthread_create(&threadid, NULL, (void*)receive, &args);//创建子线程服务于该客户端
		if(ret != 0)
		{
			printf("Create pthread error\n");
			return -1;
		}
		list_insert(inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), comfd);
		printf("Accepted client: %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
	}

	return 0;
}

//创建链表以保存客户端信息
void list_create(void)
{
	client_table.L = (client_list)malloc(sizeof(struct client_node));
	(client_table.L)->next = NULL;
	client_table.size = 0;
}

//链表的插入函数
void list_insert(char* client_ip, int client_port, int comfd)
{
	client_list new_client;
	client_list p;
	p = client_table.L;
	(client_table.size)++;
	while(p->next != NULL)
		p = p->next;

	//复制客户端信息于客户端节点中
	new_client = (client_list)malloc(sizeof(struct client_node));
	new_client->comfd = comfd;
	new_client->connect_status = 1;
	strcpy(new_client->client_ip, client_ip);
	new_client->client_port= client_port;
	new_client->next = p->next;
	p->next = new_client;
}

void receive(void* args)
{
	int i;
	void* ptr;
	struct passwd *pwd;
	time_t t;
	struct tm *lt;
	int ret;
	int request_size;
	struct multiargs* multi_args = (struct multiargs*)args;
	int comfd = multi_args->comfd;
	int index = multi_args->index;
	struct request request_packet;//请求数据包
	struct response response_packet;//响应数据包
	struct name_packet host_name;//名字响应数据包
	struct time_packet host_time;//时间响应数据包
	struct message_request message;//请求消息数据包
	struct message_response response_message;//响应的消息给发送方
	struct message_process transmit_message;//服务端接收到的消息转发给接收方的数据包
	client_list p;
	client_list q;
	while(1)
	{
		if(receive_packet(comfd, &request_packet, sizeof(request_packet)) == -1)//接收请求数据包
			return;

		response_packet.type = request_packet.type;//打包响应数据包
		response_packet.size = 0;
		switch(request_packet.type)//根据请求类型打包不同类型的响应数据包
		{
			case 0://处理时间请求
			    if(send_packet(comfd, "response", &response_packet) == -1)
			    	return;

			    //通过时间的函数读取当前时间并存储于时间的数据包中
			    time(&t);
			    lt = localtime(&t);
			    host_time.year = lt->tm_year + 1900;
			    host_time.month = lt->tm_mon+1;
			    host_time.day = lt->tm_mday;
			    host_time.hour = lt->tm_hour;
			    host_time.minute = lt->tm_min;
			    host_time.second = lt->tm_sec;
			    if(send_packet(comfd, "time", &host_time) == -1)//发送时间响应数据包
			    	return;
			    printf("time info has been sent!\n");
			    break;

			case 1://处理名字请求
			    if(send_packet(comfd, "response", &response_packet) == -1)//发送响应头
			    	return;
			    pwd = getpwuid(getuid());//获取主机名字
			    strcpy(host_name.name, pwd->pw_name);
			    if(send_packet(comfd, "name", &host_name) == -1)//发送名字响应数据包
			    	return;
			    printf("hostname info has been sent!\n");
			    break;

			case 2://处理客户端列表请求
			    response_packet.size = client_table.size;//存储将要发送的客户端节点的个数
			    if(send_packet(comfd, "response", &response_packet) == -1)//发送响应头
			    	return;
			    p = client_table.L->next;
			    for(i = 0;i < client_table.size;i++)//将链表中的每一个客户端节点都循环发送至请求客户端
			    {
			    	if(send_packet(comfd, "list", p) == -1)
			    		return;
			    	p = p->next;
			    }
			    printf("clientlist info has been sent!\n");
			    break;

			case 3://处理消息转发请求
			    if(receive_packet(comfd, &message, sizeof(message)) == -1)
			    	return;

			    if(message.index >= client_table.size || message.index < 0)//判断接收方编号是否存在
			    {
			    	if(send_packet(comfd, "response", &response_packet) == -1)
			    		return;
			    	strcpy(response_message.text, "没有该编号的客户端!");
			    	if(send_packet(comfd, "message_response", &response_message) == -1)//发送没有该编号客户端的响应信息
			    		return;
			    	break;
			    }

			    p = client_table.L->next;
			    for(i = 0;i < message.index;i++)
			    	p = p->next;
			    if(!p->connect_status)//判断接收方是否在线
			    {
			    	if(send_packet(comfd, "response", &response_packet) == -1)
			    		return;
			    	strcpy(response_message.text, "该编号的客户端已下线!");
			    	if(send_packet(comfd, "message_response", &response_message) == -1)//发送客户端不在线的响应信息
			    		return;
			    	break;
			    }
			    else
			    {
			    	response_packet.type = 4;//打包指示数据包
			    	if(send(p->comfd, (char*)&response_packet, sizeof(response_packet), 0) == -1)
			    	{
			    		response_packet.type = 3;
			    		if(send_packet(comfd, "response", &response_packet) == -1)
			    			return;
			    		strcpy(response_message.text, "服务器发送请求失败!");
			    		if(send_packet(comfd, "message_response", &response_message) == -1)
			    			return;
			    		printf("send() failed!\n");
			    		break;
			    	}

			    	//转发消息中加入发送方的信息:发送方的客户端编号和ip端口信息
			    	strcpy(transmit_message.text, message.text);
			    	transmit_message.index = index;
			    	q = client_table.L->next;
			    	for(i = 0;i < index;i++)
			    		q = q->next;
			    	strcpy(transmit_message.client_ip, q->client_ip);
			    	transmit_message.client_port = q->client_port;
			    	if(send(p->comfd, (char*)&transmit_message, sizeof(transmit_message), 0) == -1)//发送处理后转发消息的数据包
			    	{
			    		response_packet.type = 3;
			    		if(send_packet(comfd, "response", &response_packet) == -1)
			    			return;
			    		strcpy(response_message.text, "服务器发送消息失败!");
			    		if(send(comfd, (char*)&response_message, sizeof(response_message), 0) == -1)
			    		{
			    			printf("send() failed!\n");
			    			close(comfd);
			    			return;
			    		}
			    		printf("send() failed!\n");
			    		break;
			    	}
			    	response_packet.type = 3;
			    	if(send(comfd, (char*)&response_packet, sizeof(response_packet), 0) == -1)
			    	{
			    		printf("send() failed!\n");
			    		close(comfd);
			    		return;
			    	}
			    	strcpy(response_message.text, "成功发送至对方客户端!");
			    	if(send_packet(comfd, "message_response", &response_message) == -1)//发送成功发送的响应信息至发送方
			    		return;
			    	printf("transmit message successful!\n");
			    break;
			case 4:
			    p = client_table.L->next;
			    for(i = 0;i < index;i++)
			    	p = p->next;
			    p->connect_status = 0;
			    pthread_exit(0);
			    break;
			default:
			    break;
		    }
	    }
    }
}

/*************************************************
 *接收数据包函数                                   *
 *sockfd: 句柄                                   *
 *ptr: 接收包的指针                                *
 *len: 接收包的长度                                *
 *************************************************/
int receive_packet(int sockfd, void* ptr, int len)
{
	int ret;
	//通过循环接收指定接收包长度的接收包
	while(len > 0)
	{
		ret = recv(sockfd, ptr, len, 0);
		if(ret <= 0)
		{
			printf("recv() failed!\n");
			close(sockfd);
			return -1;
		}
		len -= ret;
		ptr = (char*)ptr + ret;
	}
	return 0;
}

/****************************************************
 *发送数据包函数                                       *
 *sockfd:句柄                                        *
 *type: 发送包的类型                                  *
 *packet:指向发送的数据包的指针                         *
 ****************************************************/
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
		printf("请输入将要发送的客户端编号\n");
		scanf("%d", &(message.index));
		printf("请输入发送的消息内容\n");
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

