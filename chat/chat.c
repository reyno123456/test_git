/*
功能：	在同一台服务器上ssh登录的用户可以群聊（聊天室）
原理：	1、通过roomNo.来区分不同的房间或群组；
		2、以roomNo.作为key来创建一块共享内存，来保存进入到该room的用户列表；
		3、用户以ssh（或其它方式）登录到服务器，在/dev/pts/目录下都会有一个对应文件；
		4、自己的用户名和对应的设备文件名可从环境变量（USER和SSH_TTY）中获取到；
		5、通过向用户对应的设备名中写数据，用户就可以收到；
		6、用户在发送信息时向用户列表中的用户逐个发送，即可实现群聊功能；
		实际上是对“write”命令的加强而已。
遇到的问题：
		1、显示不显示汉字受终端软件影响，自己配置下；
		2、获取自己用户名和对应的设备文件名，寻找了很多方式，最终发现通过环境变量最简单；
		3、没有权限open方式其他用户对应的设备，通过修改/dev/pts/下的文件权限为622解决，
		最好自己根目录的.bashrc脚本文件中加入“chmod 622 $SSH_TTY”，这样每次登录时会自动修
		改；暂没找到其它更好方法；
		4、fgets方式获取的字符串不能backspace，网上朋友说修改VERASE值为0x08，试验发现可以，
		但删汉字时，一个汉字要对应2个backspace；
		5、在自己输入信息到一半，但还没有回车发送便收到其它用户的信息时，自己输入的信息
		被打断，再解决这个问题就更接近完美了；
*/
 
#include <unistd.h>  
#include <stdlib.h>  
#include <stdio.h>  
#include <string.h> 
#include <fcntl.h>
#include <utmp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/shm.h> 
#include <signal.h>
#include <stdbool.h>
#include <termios.h>
#include <curses.h>
 
#ifndef _SHMDATA_H_HEADER  
#define _SHMDATA_H_HEADER  
 
//同时在线的用户最大数量，可更改
#define USER_NUM 20
#define USER_LEN 30
  
struct shared_users_st  
{  
    int written;			//作为一个标志，非0：表示不可写，0表示可读写  
    char users[USER_NUM][USER_LEN];	//记录写入和读取的文本, zhi.wang@/dev/pts/2
};  
  
#endif  
 
void utmpinfo();
void pwentinfo();
char *getuser();
char *gettty();
void msgparser(char *buffer);
 
int shmcreate(key_t key);
int shmdetach(void *shm);
int shmdestroy(int shmid);
int shmuseradd(struct shared_users_st *);
int shmuserclear(struct shared_users_st *);
int shmuserexit(struct shared_users_st *);
int shmuserlist(struct shared_users_st *);
int shmusermsg(struct shared_users_st *, char *);
bool userinutmp(char *);
 
void *shm = NULL;
int shmid;
int mylocal = -1;	//记录自己在shm中的位置，退出时方便删除
 
struct termios new; /* 控制终端状态的数据结构，可 man termios 查看*/
struct termios old;
 
 
//全局开关
//匿名发送消息
bool ishidden = false;
bool running = true;
 
char prompt = '_';
 
static char *help = "\
    :l		List users;\n\
    :c		Clear all users;\n\
    :q		Quit;\n\
    :help	Help;\n\
    :hide	set anonymous;\n\
    :nohide	set noanonymous;\n\
	";
 
void sig_handler( int sig);
void chatroominit();
void chatroomexit();
 
void chatroominit()
{
	signal(SIGHUP, sig_handler);
	signal(SIGINT, sig_handler);
 
    tcgetattr(0,&old); /* 得到当前的终端状态 */
    new = old; 
	//printf("old[VERASE]=0x%x\n\n", old.c_cc[VERASE]);
	new.c_cc[VERASE] = 0x08;	//实现backspace功能
	tcsetattr(0,TCSANOW,&new); /* 应用新的设置*/
}
 
void chatroomexit()
{
	tcsetattr(0,TCSANOW,&old);
	//shmuserclear(shm);
	shmuserexit((struct shared_users_st *)shm);
	//shmdetach(shm);
}
 
//处理ctrl+c 和 终端挂起SIGHUP信号
void sig_handler( int sig)
{
	//printf("%s %d \n", __FUNCTION__, sig);
	if(sig == SIGINT || sig == SIGHUP)
	{
		chatroomexit();
		exit(0);
	}
}
 
int main()  
{	
	char buffer[BUFSIZ + 1];//用于保存输入的文本
	char *roomno;
	
	//printf(" ** room NO.: %d\n", sizeof(struct shared_users_st));
	roomno = getpass("Please enter room No.:");
	//printf("  ** room NO. is : %s\n", roomno);
	
	chatroominit();
	//setuid(0);
 
	shmid = shmcreate((key_t)atoi(roomno));
	// shmid = shmcreate((key_t)2);
	shmuseradd((struct shared_users_st *)shm);
	shmuserlist((struct shared_users_st *)shm);
	
	while (running)
	{
		printf("%c", prompt);
		memset(buffer, 0, BUFSIZ + 1);
		fgets(buffer, BUFSIZ, stdin);
 
		msgparser(buffer);
		
	}
 
	chatroomexit();
	exit(EXIT_SUCCESS);  
}  
 
//分析输入内容
void msgparser(char *buffer)
{
	char c;
		
	switch (c = buffer[0])
	{
	case ':':
		if (strncmp(buffer+1, "q", 1) == 0)
		{
			running = false;
			//检查是否还有在线用户，如没有则销毁
		}
		else if (strncmp(buffer+1, "help", 4) == 0)
		{
			printf("%s\n", help);
		}
		else if (strncmp(buffer+1, "clear", 5) == 0)
		{
			shmuserclear((struct shared_users_st *)shm);
		}
		else if (strncmp(buffer+1, "l", 1) == 0)
		{
			shmuserlist((struct shared_users_st *)shm);
		}
		else if (strncmp(buffer+1, "hide", 4) == 0)
		{
			ishidden = true;
		}
		else if (strncmp(buffer+1, "nohide", 6) == 0)
		{
			ishidden = false;
		}
		break;
	case '?':
		printf("%s\n", help);
		break;
	default:
		//printf("%d %d\n", strlen(buffer), buffer[0]);
		//空格和换行不发送
		if ((strlen(buffer) <= 2 && buffer[0] == 32) || 
			(strlen(buffer) <= 1 && buffer[0] == 10))
		{
			break;
		}
		//printf("%s\n", buffer);
		shmusermsg((struct shared_users_st *)shm, buffer);
	}
}
 
char *getuser()
{
	return getenv("USER");
}
 
char *gettty()
{
	return getenv("SSH_TTY");
}
 
static void waitwrite(struct shared_users_st *shared)
{
	//数据还没有被读取，则等待数据被读取,不能向共享内存中写入文本 
	//shared->written = 0;
	while(shared->written == 1)  
	{  
		sleep(1);  
		printf("Waiting...\n");  
	} 
}
 
int shmcreate(key_t key)
{
    char buffer[BUFSIZ + 1];//用于保存输入的文本  
     
    //创建共享内存  
    shmid = shmget((key_t)key, sizeof(struct shared_users_st), 0666|IPC_CREAT);  
    if(shmid == -1)  
    {  
        fprintf(stderr, "shmget failed\n");  
        //exit(EXIT_FAILURE);  
    }  
    //将共享内存连接到当前进程的地址空间  
    shm = shmat(shmid, (void*)0, 0);  
    if(shm == (void*)-1)  
    {  
        fprintf(stderr, "shmat failed\n");  
        //exit(EXIT_FAILURE);  
    }  
    //printf("Memory attached at %X\n", (int)shm); 
	
	return shmid;
 
}
 
int shmdetach(void *shm) 
{
    //把共享内存从当前进程中分离  
    if(shmdt(shm) == -1)  
    {  
        fprintf(stderr, "shmdt failed\n");  
        exit(EXIT_FAILURE);  
    }  
 
	return 0;
}
 
int shmdestroy(int shmid) 
{
    //printf(" ** %s\n", __FUNCTION__); 
 
	//删除共享内存  SHM_DEST
    if(shmctl(shmid, IPC_RMID, 0) == -1)  
    {  
        fprintf(stderr, "shmctl(IPC_RMID) failed(%d).\n", shmid);  
        exit(EXIT_FAILURE);  
    }  
 
	return 0;
}
 
int shmuseradd(struct shared_users_st *shared)  
{  
	//struct shared_users_st *shared = NULL;
	char buffer[BUFSIZ + 1];//用于保存输入的文本
	int i = 0;
	char *p = NULL;
 
 
    //设置共享内存  
    //shared = (struct shared_users_st*)shm;
 
	//向共享内存中写入数据  
	strcat(buffer, getuser());
	strcat(buffer, "@");
	strcat(buffer, gettty());
	//printf("buffer:%s\n", buffer);
	
	waitwrite(shared);
	//向共享内存中写入数据前先置1，其它进程不可写
	shared->written = 1;
 
	do
	{	
		//没有找到@时,或者找到自己对应的tty时则替换
		if ((p = strchr(shared->users[i],'@')) == 0)
		{
 
			if (mylocal == -1)
			{
				//printf("%s %s %d\n", __FUNCTION__, buffer, i);
				strncpy(shared->users[i], buffer, USER_LEN); 
				//记录下自己所在位置，退出时自己删除
				mylocal = i;
			}
			else
			{
				//printf("%s %s %d reset.\n", __FUNCTION__, shared->users[i], i);
				memset(shared->users[i], 0, USER_LEN);		
			}
		} 
		else 
		{
			//printf("\n\nbuffer: %s != %s %d=%d\n\n", p+1, gettty(), strlen(p+1) ,strlen(gettty()));
			if (!strncasecmp(p+1, gettty(), strlen(p+1) > strlen(gettty()) ? strlen(p+1) : strlen(gettty()) ))
			{
				if (mylocal == -1)
				{
					//printf("%s %s %d\n", __FUNCTION__, buffer, i);
					strncpy(shared->users[i], buffer, USER_LEN); 
					//记录下自己所在位置，退出时自己删除
					mylocal = i;
				}
				else
				{
					//printf("%s %s %d reset.\n", __FUNCTION__, shared->users[i], i);
					memset(shared->users[i], 0, USER_LEN);		
				}
			}
			//continue;
 
			//检查user和tty跟当前系统登录的是否匹配（参考w命令）
			if (userinutmp(shared->users[i]) == false)
			{
				printf("  ## %s %s %d autodelete.\n", __FUNCTION__, shared->users[i], i);
				memset(shared->users[i], 0, USER_LEN);
			}
		}
		//memset(shared->users[i], 0, USER_LEN);
	} while (++i < USER_NUM);
 
	//写完数据，设置written使共享内存段可读  
	shared->written = 0;  
 
	if (mylocal != -1)
	{
		//告诉在线用户，我已加入
		memset(buffer, 0, BUFSIZ + 1);
		sprintf(buffer, "\n  ## Welcome %s to our room.\n", shared->users[mylocal]);
		shmusermsg(shared, buffer);
	}
 
	return 0;
}  
 
//清空所有用户
int shmuserclear(struct shared_users_st *shared)  
{  
	//struct shared_users_st *shared = NULL;
	int i = 0;
	char buffer[BUFSIZ + 1];//用于保存输入的文本
 
    //设置共享内存  
    //shared = (struct shared_users_st*)shm;
	
	//数据还没有被读取，则等待数据被读取,不能向共享内存中写入文本  
	waitwrite(shared);	
	//向共享内存中写入数据前先置1，其它进程不可写
	shared->written = 1;
 
	do
	{	
		//找到@时
		if (rindex(shared->users[i],'@') != 0)
		{
			memset(buffer, 0, BUFSIZ + 1);
			sprintf(buffer, "\n  ## %s has been kicked out of the room.\n\n\n", 
				shared->users[i]);
			shmusermsg(shared, buffer);
 
			memset(shared->users[i], 0, USER_LEN);			
			//break;
		} 
	} while (++i < USER_NUM);
	
	//写完数据，设置written使共享内存段可读  
	shared->written = 0;  
 
	return 0;
} 
 
//退出时只删除自己即mylocal所标记位置
//如果退出时没有在线用户，则销毁shm
int shmuserexit(struct shared_users_st *shared)  
{  
	//struct shared_users_st *shared = NULL;  
	char buffer[BUFSIZ + 1];//用于保存输入的文本
	int i = 0;
 
    //设置共享内存  
    //shared = (struct shared_users_st*)shm;
 
	//printf("%s %s %d\n", __FUNCTION__, shared->users[mylocal], mylocal);
	//告诉在线用户，我已离开
	memset(buffer, 0, BUFSIZ + 1);
	sprintf(buffer, "\n  ## %s exit the room.\n", shared->users[mylocal]);
	shmusermsg(shared, buffer);
	
	//数据还没有被读取，则等待数据被读取,不能向共享内存中写入文本  
	waitwrite(shared);  
	
	//向共享内存中写入数据前先置1，其它进程不可写
	shared->written = 1;
 
	memset(shared->users[mylocal], 0, USER_LEN);
 
	do
	{	
		//找到@时
		if (rindex(shared->users[i],'@') != 0)
		{
			//memset(shared->users[i], 0, USER_LEN);			
			break;
		} 
	} while (++i < USER_NUM);
 
	//写完数据，设置written使共享内存段可读  
	shared->written = 0;  
 
	shmdetach((void *)shared);
 
	//如果已没有用户在线，则销毁
	if (i == USER_NUM)
	{
		//此处暂未解决“不能其他用户创建的共享内存”的问题，所有注释掉
		//shmdestroy(shmid);
	}
	
	return 0;
}  
 
int shmuserlist(struct shared_users_st *shared)
{  
	//struct shared_users_st *shared = NULL; 
	int i = 0;
 
    //设置共享内存  
    //shared = (struct shared_users_st*)shm;
	
	//数据还没有被读取，则等待数据被读取,不能向共享内存中写入文本  
	//waitwrite(shared); 
	
	//向共享内存中写入数据前先置1，其它进程不可写
	//shared->written = 1;
 
	printf("  zz%s\n", "zzzzzzzzzzzzzzzzzzzzzzzzzzzz");
 
	do
	{	
		//找到@时
		if (rindex(shared->users[i],'@') != 0)
		{
			printf("  zz %d %s\n", i, shared->users[i]);
			
			//break;
		} 
	} while (++i < USER_NUM);
 
	printf("  zz%s\n", "zzzzzzzzzzzzzzzzzzzzzzzzzzzz");
	
	//写完数据，设置written使共享内存段可读  
	//shared->written = 0;  
 
	return 0;
}  
 
int shmusermsg(struct shared_users_st *shared, char *buffer)
{  
	//struct shared_users_st *shared = NULL; 
	int i = 0;
	char buff[BUFSIZ + 1];//用于保存输入的文本
 
    //设置共享内存  
    //shared = (struct shared_users_st*)shm;
	
	//数据还没有被读取，则等待数据被读取,不能向共享内存中写入文本  
	//waitwrite(shared);  
	
	//向共享内存中写入数据前先置1，其它进程不可写
	//shared->written = 1;
 
	if (!ishidden)
	{	
		sprintf( buff, "  ** %s: %s", getuser(), buffer);
		strcpy(buffer, buff);
	}
 
 
	do
	{	
		//找到@时
		if (rindex(shared->users[i],'@') != 0 ) //&& i != mylocal
		{
			char *p;
			int fd;
 
			//发送给其它用户时，换行\n后输出
			if (i != mylocal)
			{
				sprintf( buff, "%s", buffer);
			}
			else
			{
				//\033[1A 先回到上一行 //\033[K 清除该行
				sprintf( buff, "\033[1A\033[K%s", buffer);
			}
			//strcpy(buffer, buff);
 
			//printf("%s %s %d\n", __FUNCTION__, strchr(shared->users[i],'@')+1, i);
 
			if ((p = strchr(shared->users[i],'@')) == 0)
			{
				//perror("open error");
				continue;
			}
 
			//发送消息之前先判断该用户是否在线
			if ((fd = open(p+1, O_WRONLY)) < 0)
			{
				perror("open error");
				printf("%s\n", shared->users[i]);
				//exit(EXIT_FAILURE);
			}
 
			if (write(fd, buff, strlen(buff)+1) != strlen(buff)+1)
			{
				perror("write error");
				//exit(EXIT_FAILURE);
			}
 
			close(fd);
			
			//break;
		} 
	} while (++i < USER_NUM);
	
	//写完数据，设置written使共享内存段可读  
	//shared->written = 0;  
 
	return 0;
}  
 
//在utmp文件（当前在线用户，即w命令展示结果）中找到返回非0，未找到返回0
bool userinutmp(char *userinfo)
{
	struct utmp *u;
	char *p, *tty, user[USER_LEN];
 
	memset(user, 0, USER_LEN);
	strncpy(user, userinfo, strlen(userinfo));
 
	p = strtok(user, "@");
	//user = p;
	p = strtok(NULL, "@");
	tty = p;
	//printf("%s %s %s.\n", user, tty, userinfo);
 
	struct utmp ut;
	strcpy (ut.ut_line,tty+5);
	while ((u=getutline(&ut)))
	{
		//printf("-%d %s %s %s \n",u->ut_type,u->ut_user,u->ut_line,u->ut_host);
		//如果找到匹配
		if (!strcmp(u->ut_user, user))
		{
			endutent();
			return true;
		}
	}
 
	endutent();
 
	return false;
 
}
 
#if 1
 
void pwentinfo()
{
	struct passwd *user;
 
	if((user = getpwuid(geteuid()))!=0)
	{
		printf("%s:%d:%d:%s:%s:%s\n",user->pw_name,user->pw_uid,user->pw_gid,
		user->pw_gecos,user->pw_dir,user->pw_shell);
	}
 
	endpwent();
	
	printf("uid is %d\n",getpgid(getegid()));
	printf("egid is %d\n",getegid());
	printf("gid is %d\n",getpgrp());
 
}
#endif