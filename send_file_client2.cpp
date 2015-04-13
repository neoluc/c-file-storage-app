#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <netinet/tcp.h>
#include <time.h>


#include "tinycthread.h"



#ifdef WIN32 // Windows
# include <winsock2.h>
# include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#else // Assume Linux
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <netdb.h>
# include <string.h>
# include <stdlib.h>
# include <unistd.h>
# include <errno.h>
#define SOCKET int
#define SOCKET_ERROR ‐1
#define WSAGetLastError() (errno)
#define closesocket(s) close(s)
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK
// There are other WSAExxxx constants which may also need to be #define'd to the Exxxx versions.
#define Sleep(x) usleep((x)*1000)
#endif



#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>


#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

#define ACKSIZE 4



int flag =1;
int flags = 0;

char sack[4] = "ok";
char rack[4];

int trigger_state = 1; // 1 = file change, 2 = receive from server

mtx_t gMutex;
cnd_t gCond;
cnd_t re_ser;
cnd_t listen_ser;

struct Node{
	char *name;
	int size;
	struct Node *next;
};
struct Node *head;


struct ffile{
	char name[100];
	int size;
};


void addNode(struct Node *head, char *name, int size){
	if(!head->name){
		head->name = name;
		head->size = size;
		head->next = NULL;	
	}else{
		Node *newNode = new Node;
		newNode->name = name;
		newNode->size = size;
		newNode->next = NULL;

		Node *cur = head;
		while(cur){
			if(cur->next == NULL){
				cur->next = newNode;
				return;
			}
			cur = cur->next;
		}

	}

}


void insertFront(struct Node **head, char *name, int size){
	Node *newNode = new Node;
	newNode->name = name;
	newNode->size = size;
	newNode->next = *head;
	*head = newNode;
}

struct Node *searchNode(struct Node *head, char *name){
	Node *cur = head;
	while(cur){
		if(!cur->name) break;
		if(strcmp(cur->name, name)==0) return cur;
		cur = cur->next;
	}
	struct Node *noo = new Node;
	noo->name;
	noo->size = NULL;
	noo->size = NULL;
	return noo;
}

int deleteNode(struct Node **head, Node *ptrDel){
	Node *cur = *head;
	if(ptrDel == *head){
		*head = cur->next;
		delete ptrDel;
		return 1;
	}
	while(cur){
		if(cur->next == ptrDel){
			cur->next = ptrDel->next;
			delete ptrDel;
			return 1;
		}
		cur = cur->next;
	}
	return 0;
}




void deleteLinkedList(struct Node **node){
	struct Node *tmpNode;
	while(*node){
		tmpNode = *node;
		*node = tmpNode->next;
		delete tmpNode;
	}
}


void clearLinkedList(struct Node *head){
	Node *temp = head;

	Node *temp2 = temp->next;

	head->name = NULL;
	head->size = NULL;
	head->next = NULL;

	if(temp2)
		deleteLinkedList(&temp2);
}



void display(struct Node *head){
	Node *list = head;
	while(list){
		printf("%s %d\n", list->name, list->size);
		list = list->next;
	}
}


int countLength(struct Node *head){
	int length = 0;
	Node *list = head;
	while(list){
		if(list->name)
			length++;
		list = list->next;
	}
	return length;
}



static int display_info(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf){

	if(tflag == FTW_F){
		char *temp = strdup(fpath);
		addNode(&head[0], temp, (intmax_t) sb->st_size);
	}

	printf("%-3s %2d %7jd   %-40s %d %s\n",
		(tflag == FTW_D) ?   "d"   : (tflag == FTW_DNR) ? "dnr" :
		(tflag == FTW_DP) ?  "dp"  : (tflag == FTW_F) ?   "f" :
		(tflag == FTW_NS) ?  "ns"  : (tflag == FTW_SL) ?  "sl" :
		(tflag == FTW_SLN) ? "sln" : "???",
		ftwbuf->level, (intmax_t) sb->st_size,
		fpath, ftwbuf->base, fpath + ftwbuf->base);
	return 0;           /* To tell nftw() to continue */
}







long serialize(Node *file, char *buf){
	long bytes = 0;

	memcpy(buf + bytes, file->name, strlen(file->name)+1);
	bytes += strlen(file->name) + 1;

	char *str;
	sprintf(str, "%d", file->size);
	printf("after sprintf %s\n", str);

	memcpy(buf + bytes, str, strlen(str)+1);
	bytes += strlen(str) + 1;

	return bytes;
}


void deserialize(char *buf, Node *file){
	long offset = 0;

	file->name = strdup(buf + offset);
	offset += strlen(buf + offset) + 1;

	printf("-----------%s\n", strdup(buf + offset));

	file->size = atoi(strdup(buf + offset));
	//offset += strlen(buf + offset + 1);
} 




int thread_check_dir_change(void *aArg){

    int length, i = 0;
    int fd;
    int wd;
    char buffer[BUF_LEN];

    fd = inotify_init();

    if (fd < 0) {
        perror("inotify_init");
    }

    wd = inotify_add_watch(fd, ".",
        IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO);

    while(1){
        length = 0;
        i = 0;
        length = read(fd, buffer, BUF_LEN);

        if (length < 0) {
            perror("read");
        }

        trigger_state = 1;
        cnd_signal(&gCond);

        while (i < length) {
            struct inotify_event *event =
                (struct inotify_event *) &buffer[i];
            if (event->len) {
                if (event->mask & IN_CREATE) {
                    printf("The file %s was created.\n", event->name);
                } else if (event->mask & IN_DELETE) {
                    printf("The file %s was deleted.\n", event->name);
                } else if (event->mask & IN_MODIFY) {
                    printf("The file %s was modified.\n", event->name);
                } else if (event->mask & IN_ATTRIB) {
                    printf("The metadata %s was modified.\n", event->name);
                } else if (event->mask & IN_MOVED_FROM) {
                    printf("The file %s was moved out.\n", event->name);
                } else if (event->mask & IN_MOVED_TO) {
                    printf("The file %s was moved in.\n", event->name);
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }

    (void) inotify_rm_watch(fd, wd);
    (void) close(fd);

    return 0;
}

int thread_listen_server(void *aArg){

	SOCKET soc = *(SOCKET*)&aArg;

	int iresult;

	while(1){
		mtx_lock(&gMutex);
		cnd_wait(&listen_ser, &gMutex);
		mtx_unlock(&gMutex);

		iresult = recv(soc, rack, ACKSIZE, 0);
		if(strcmp(rack, "01")==0){
			trigger_state = 2;
			cnd_signal(&re_ser);
		}else{
			cnd_signal(&gCond);
			iresult = send(soc, "bb", strlen("bb"), 0);
		}
	}

}




int thread_send_server_file(void *aArg){

	int iresult;
	SOCKET soc = *(SOCKET*)&aArg;


	while(1){

		printf("waiting for the condinious signal\n");
		mtx_lock(&gMutex);
		cnd_wait(&gCond, &gMutex);
		mtx_unlock(&gMutex);
		printf("folder has some change!!! update the server!!! \n");

		clearLinkedList(&head[1]);
		clearLinkedList(&head[0]);
		if (nftw(".", display_info, 20, flags) == -1) {
			exit(EXIT_FAILURE);
		}

		

		char startnum[100];
		memset(startnum, 0, 100);
		
		iresult = send(soc, "aa", strlen(sack), 0);
		do{
			iresult = recv(soc, startnum, 100, 0);
			if(strcmp(startnum, "bb")==0){
				memset(startnum, 0, 100);
				iresult = send(soc, "cc", strlen("cc"), 0);
			}
		}while(strcmp(startnum, "bb")==0);

		if(iresult == -1)
			printf("receive startnum failed\n");

		int listSize = atoi(startnum);



		iresult = send(soc, "ee", strlen(sack), 0);




		int loopNum = 0; // number of files server has

		while(loopNum < listSize){


			char *newbuf = new char[10240];

			struct ffile *neww;// = new ffile ;

			iresult = recv(soc, newbuf, 10240, 0);
			if(iresult== -1)
				printf("recv failed\n");
			else if(iresult == 0)
				break;
			
			neww = (ffile*) newbuf;

			addNode(&head[1], neww->name, neww->size);
			
			iresult = send(soc, "sa", strlen(sack), 0);

			loopNum++; // number of server files

		}
		iresult = recv(soc, rack, ACKSIZE, 0);
		display(&head[1]);

		struct Node *list = &head[0];

		
		int diffnum = 0; //diff files with server
		
		while(list){ // find how many diff from server

			struct Node *sn = searchNode(&head[1], list->name);
			if(!sn->name) { diffnum++; list = list->next; continue;}
			if(strcmp(sn->name, list->name) != 0){
				diffnum++;
			}

			list = list->next;
		}

		char sendFileNum[10];//how many file send to server
		memset(sendFileNum, 0, 10);
		sprintf(sendFileNum, "%d", diffnum);
		iresult = send(soc, sendFileNum, strlen(sendFileNum), 0);
		if(iresult == -1)
			printf("send number fail %d\n", iresult);


		if(diffnum == 0){ //no file send to server
			sleep(2);
			cnd_signal(&re_ser);
			continue;
		}


		iresult = recv(soc, rack, ACKSIZE, 0);



		list = &head[0];
		while(list){

			//printf("1. %s \n", list->name);
			//printf("2. %s\n\n", searchNode(list->name)->name);		
			struct Node *sn = searchNode(&head[1], list->name);
			//printf("2. %s\n", sn->name);
			if(strcmp(sn->name, list->name) != 0){
				

				printf("preparing to send file :: %s\n", list->name);

				struct ffile *file = new ffile;
				memset(file->name, 0, 100);
				strcpy(file->name, list->name);
				file->size = list->size;

				iresult = send(soc, (char*)file, sizeof(ffile), 0);
				if(iresult == -1)
					printf("send file info failed\n");
				
				iresult = recv(soc, rack, ACKSIZE, 0);
				
				char *string;
				long fsize;

				FILE *f = fopen(list->name, "rb");
				if(f != NULL){
					fseek(f, 0, SEEK_END);
					fsize = ftell(f);
					fseek(f, 0, SEEK_SET);

					string = (char *)malloc(fsize+1);
					fread(string, fsize, 1, f);
					fclose(f);

				}


				iresult = send(soc, string, fsize, 0);
				if(iresult == -1){
					printf("send file failed\n");
				}

				iresult = recv(soc, rack, ACKSIZE, 0);

			}

			list = list->next;
		}
		sleep(2);
		cnd_signal(&re_ser);
	}

}



int thread_receive_server_file(void *aArg){

	int iresult;
	SOCKET soc = *(SOCKET*)&aArg;


	int count = 0;
	while(1){

		if(count != 0){
			mtx_lock(&gMutex);
			cnd_wait(&re_ser, &gMutex);
			mtx_unlock(&gMutex);
		}

		if(trigger_state == 1)
			iresult = recv(soc, rack, ACKSIZE, 0); //01


		clearLinkedList(&head[0]);
		if (nftw(".", display_info, 20, flags) == -1) {
			perror("nftw");
			exit(EXIT_FAILURE);
		}
		display(&head[0]);



		int selfListLength = countLength(&head[0]);

		char startnum[10];
		memset(startnum, 0, 10);
		sprintf(startnum, "%d", selfListLength);

		iresult = send(soc, startnum, strlen(startnum), 0);//02
		if(iresult == -1)
			printf("send list length failed\n");

		iresult = recv(soc, rack, ACKSIZE, 0);//03
		if(strcmp(rack, "no")==0){
			if(count==0){
				thrd_t t;
				thrd_create(&t, thread_send_server_file, (void*)soc);

				thrd_t t1;
				thrd_create(&t1, thread_check_dir_change, (void*)0);
				count++;
				continue;
			}else{
				continue;
			}
		}


		struct Node *list = &head[0];
		while(list){

			if(!list->name) break;

			struct ffile *file = new ffile;
			strcpy(file->name, list->name);
			file->size = list->size;

			iresult = send(soc, (char*)file, sizeof(ffile), 0);//04
			if(iresult == -1)
				printf("send name size failed\n");
			printf("iresult %d\n", iresult);
			iresult = recv(soc, rack, ACKSIZE, 0);//05

			list = list->next;
		}
		display(&head[0]);

		char willReceiveNumber[10];
		memset(willReceiveNumber, 0, 10);
		
		iresult = send(soc, sack, strlen(sack), 0);//06
		iresult = recv(soc, willReceiveNumber, 10, 0);//07
		if(iresult == -1)
			printf("receive number fail\n");

		int numlimit = atoi(willReceiveNumber);
		int i;

		if(numlimit == 0 && count==0){
			thrd_t t;
			thrd_create(&t, thread_send_server_file, (void*)soc);

			thrd_t t1;
			thrd_create(&t1, thread_check_dir_change, (void*)0);
			count++;
			sleep(1);
			cnd_signal(&listen_ser);
			trigger_state = 1;
			continue;
		}else if(numlimit == 0 && count!=0){
			cnd_signal(&listen_ser);
			trigger_state = 1;
			continue;
		}

		for(i=0; i<numlimit; i++){

			char *newbuf = new char[10240];
			memset(newbuf, 0, 10240);

			struct ffile *neww;// = new ffile ;

			iresult = send(soc, sack, strlen(sack), 0);//08 or 12
			
			iresult = recv(soc, newbuf, 10240, 0);//09
			if(iresult== -1)
				printf("recv failed\n");

			neww = (ffile*) newbuf;

			char nameBuffer[100];
			strcpy(nameBuffer, neww->name);
			int sizeBuffer = neww->size;

			char *fileBuffer = new char[sizeBuffer];

			iresult = send(soc, sack, strlen(sack), 0);//10
			iresult = recv(soc, fileBuffer, sizeBuffer, 0);//11
			if(iresult == -1)
				printf("receive file error\n");

			
			if(access(nameBuffer, F_OK) == -1){  // file not exist

				char *test = strdup(nameBuffer);

				char part[50][50];
				char *temp;

				int i=0;
				if((temp = strtok(test, "/"))){
					strcpy(part[i++], temp);
					while((temp = strtok(NULL, "/"))){
						strcpy(part[i++], temp);
					}
				}

				char cwd[1024];
				getcwd(cwd, 1024);

				int j;
				for(j=1; j<i-1; j++){
					printf("%s\n", part[j]);
					mkdir(part[j], S_IRWXU);
					char tempstring[50]="./";
					strcat(tempstring, part[j]);
					chdir(tempstring);
				}
				chdir(cwd);


				FILE *newfile = fopen(nameBuffer, "wb");
				if(newfile){
					fwrite(fileBuffer, sizeBuffer+1, 1, newfile);
					puts("wrote to file");
					fclose(newfile);
				}
			}


			free(newbuf);
			free(fileBuffer);


		}

		iresult = send(soc, sack, strlen(sack), 0);//12

		iresult = recv(soc, rack, ACKSIZE, 0);//13

		if(count == 0){
			thrd_t t;
			thrd_create(&t, thread_send_server_file, (void*)soc);

			thrd_t t1;
			thrd_create(&t1, thread_check_dir_change, (void*)0);
			count++;

			thrd_t t2;
			thrd_create(&t2, thread_listen_server, (void*)soc);

		}
		sleep(1);
		cnd_signal(&listen_ser);
		trigger_state = 1;
	}

}




int main(int argc, char *argv[]){

	mtx_init(&gMutex, mtx_plain);
	cnd_init(&gCond);
	cnd_init(&re_ser);
	cnd_init(&listen_ser);

	head = new Node[2];

	head[0].name = NULL;
	head[1].name = NULL;

	if (argc > 2 && strchr(argv[2], 'd') != NULL)
		flags |= FTW_DEPTH;
	if (argc > 2 && strchr(argv[2], 'p') != NULL)
		flags |= FTW_PHYS;

	if(argc>=2){
		chdir(argv[1]);	
		argc =1;
	}


	char *port = "12345";
	int iresult;

	#ifdef WIN32
	WSADATA wsadata;
	printf("wsastartup\n");
	iresult = WSAStartup(MAKEWORD(2,2), &wsadata);

	if(iresult!=0){
		printf("WSAStartup failed. error code: %d.\n", WSAGetLastError());
		getchar();
		return 0;
	}
	printf("initialised winsock.\n");
	#endif



	SOCKET soc = socket(AF_INET, SOCK_STREAM, 0);
	if(soc == -1){
		printf("socket() error:%d\n", errno);
		getchar();
		return 0;
	}
	printf("socket() success\n");

	// get address info
	struct addrinfo hints, *result=NULL, *ptr=NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;

	iresult = getaddrinfo("localhost", port, &hints, &result);
	if(iresult!=0){
		printf("getaddrinfo failed: %d\n", iresult);
		getchar();
		return 0;
	}
	printf("getaddrinfo success.\n");

	sockaddr tempsockaddr;
	tempsockaddr = *result->ai_addr;
	freeaddrinfo(result);


	iresult = connect(soc, &tempsockaddr, sizeof(tempsockaddr));
	if(iresult!=0){
		printf("connect() failed: %d\n", WSAGetLastError());
		getchar();
		return 0;
	}
	printf("connect() success.\n");


	thrd_t t;
	thrd_create(&t, thread_receive_server_file, (void*)soc);


	sleep(10000);

	mtx_destroy(&gMutex);
	cnd_destroy(&gCond);
	cnd_destroy(&re_ser);
	cnd_destroy(&listen_ser);


	return 0;
	

}
