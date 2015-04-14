#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/inotify.h>
#include <netinet/tcp.h>

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

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

#define ACKSIZE 4
#define MAX_CLIENT_NUM 10



char rack[4];
char sack[4] = "ok";

int flag = 1;

int flags = 0;

mtx_t gMutex;
cnd_t sendFileToAll;
cnd_t receiveClient;


SOCKET soc[MAX_CLIENT_NUM];

int trigger_state = 1; // 1 receiving, 2 file change


struct Node{
	char *name;
	int size;
	struct Node *next;
};
struct Node *head;  // link list of remote peer
struct Node *selfhead;


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


long serialize(Node *file, char *buf){
	long bytes = 0;

	memcpy(buf + bytes, file->name, strlen(file->name)+1);
	bytes += strlen(file->name) + 1;

	char *str;
	sprintf(str, "%d", file->size);

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
} 


static int display_info(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf){

	if(tflag == FTW_F){
		char *temp = strdup(fpath);
		addNode(selfhead, temp, (intmax_t) sb->st_size);
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


int thread_check_dir_change(void *aArg){

	int soc_ref = *(int*)&aArg;

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




int thread_receive_client(void *aArg){

	int iresult;

	int i;
	
	int soc_ref = *(int*)&aArg;

	int count = 0;

	while(1){

		if(count != 0){
			mtx_lock(&gMutex);
			cnd_wait(&receiveClient, &gMutex);
			mtx_unlock(&gMutex);
		}

		if(1){
			do{
			iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);
			printf("loop rack %s\n", rack);
			if(strcmp(rack, "bb")!=0)
				send(soc[soc_ref], "bb", strlen("bb"), 0);
			}while(strcmp(rack, "bb")!=0);
		//sleep(2);
		}else{
			iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);
			printf("%s\n", rack);
		}

		clearLinkedList(selfhead);
		if (nftw(".", display_info, 20, flags) == -1) {
			exit(EXIT_FAILURE);
		}

		int selfListLength = countLength(selfhead); //list length
		
		char startnum[10];//list length
		memset(startnum, 0, 10);
		sprintf(startnum, "%d", selfListLength);
		
		iresult = send(soc[soc_ref], startnum, strlen(startnum), 0);
		if(iresult == -1)
			printf("send list length failed\n");
		
		iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);

		struct Node *list = selfhead;
		while(list){

			if(!list->name) break;

			struct ffile *file = new ffile;
			strcpy(file->name, list->name);
			file->size = list->size;


			iresult = send(soc[soc_ref], (char*)file, sizeof(ffile), 0);
			if(iresult == -1)
				printf("send name size failed\n");

			iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);

			list = list->next;
		}

		display(selfhead);

		char willReceiveNumber[10];
		memset(willReceiveNumber, 0, 10);
		iresult = send(soc[soc_ref], "te", strlen(sack), 0);
		iresult = recv(soc[soc_ref], willReceiveNumber, 10, 0);
		if(iresult == -1)
			printf("receive number fail\n");

		int numlimit = atoi(willReceiveNumber);
		int i;

		if(numlimit == 0 && count==0){
			count++;
			continue;
		}else if(numlimit == 0 && count != 0){
			continue;
		}

		for(i=0; i<numlimit; i++){

			char *newbuf = new char[10240];
			memset(newbuf, 0, 10240);

			struct ffile *neww;// = new ffile ;

			iresult = send(soc[soc_ref], sack, strlen(sack), 0);
			
			iresult = recv(soc[soc_ref], newbuf, 10240, 0);
			if(iresult== -1)
				printf("recv failed\n");

			neww = (ffile*) newbuf;

			char nameBuffer[100];
			strcpy(nameBuffer, neww->name);
			int sizeBuffer = neww->size;

			char *fileBuffer = new char[sizeBuffer];

			iresult = send(soc[soc_ref], sack, strlen(sack), 0);
			iresult = recv(soc[soc_ref], fileBuffer, sizeBuffer, 0);
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
		
		iresult = send(soc[soc_ref], sack, strlen(sack), 0);
		sleep(3);
		cnd_broadcast(&sendFileToAll);

		if(count == 0) count++;
	}
}






int thread_send_client(void *aArg){
	

	int soc_ref = *(int*)&aArg;

	int iresult;


	long count = 0;
	while(1){

		if(count!=0){
			cnd_signal(&receiveClient);
			mtx_lock(&gMutex);
			cnd_wait(&sendFileToAll, &gMutex);
			mtx_unlock(&gMutex);
		}

		clearLinkedList(&head[soc_ref]);
		clearLinkedList(selfhead);
		if (nftw(".", display_info, 20, flags) == -1) {
			perror("nftw");
			exit(EXIT_FAILURE);
		}
		display(selfhead);

		char startnum[10];
		memset(startnum, 0, 10);
		iresult = send(soc[soc_ref], "01", strlen(sack), 0);//01
		iresult = recv(soc[soc_ref], startnum, 10, 0); //02
		if(iresult == -1)
			printf("receive startnum failed\n");

		int listSize = atoi(startnum);

		if(countLength(selfhead)==0 && listSize==0){
			iresult = send(soc[soc_ref], "no", strlen(sack), 0);
			if(count==0){
				thrd_t t;
				thrd_create(&t, thread_receive_client, (void*)soc_ref);
				count++;
				continue;
			}else{
				continue;
			}
		}


		int loopNum = 0;

		while(loopNum < listSize){


			char *newbuf = new char[10240];

			struct ffile *neww;// = new ffile ;

			iresult = send(soc[soc_ref], "03", strlen(sack), 0);//03
			iresult = recv(soc[soc_ref], newbuf, 10240, 0);//04
			if(iresult== -1)
				printf("recv failed\n");
			else if(iresult == 0)
				return 0;

			neww = (ffile*) newbuf;

			addNode(&head[soc_ref], neww->name, neww->size);

			loopNum++;
		}

		display(&head[soc_ref]);
		printf("received file info number%d\n", loopNum);


		iresult = send(soc[soc_ref], sack, strlen(sack), 0);//05
		iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);//06

		struct Node *list = selfhead;
		
		int diffnum = 0;
		
		while(list){
			struct Node *sn = searchNode(&head[soc_ref], list->name);
			if(!sn->name) { diffnum++; list = list->next; continue;}
			if(strcmp(sn->name, list->name) != 0){
				diffnum++;
			}

			list = list->next;
		}


		char sendFileNum[10];
		memset(sendFileNum, 0, 10);
		sprintf(sendFileNum, "%d", diffnum);

		iresult = send(soc[soc_ref], sendFileNum, strlen(sendFileNum), 0);//07
		if(iresult == -1)
			printf("send number fail %d\n", iresult);

		if(diffnum == 0 && count == 0){
			thrd_t t;
			thrd_create(&t, thread_receive_client, (void*)soc_ref);
			count++;
			continue;
		}else if(diffnum==0 && count!=0){
			continue;
		}

		iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);//08


		list = selfhead;
		while(list){

			//printf("1. %s \n", list->name);
			//printf("2. %s\n\n", searchNode(list->name)->name);		
			struct Node *sn = searchNode(&head[soc_ref], list->name);
			//printf("2. %s\n", sn->name);
			if(strcmp(sn->name, list->name) != 0){
				
				struct ffile *file = new ffile;
				memset(file->name, 0, 100);
				strcpy(file->name, list->name);
				file->size = list->size;

				iresult = send(soc[soc_ref], (char*)file, sizeof(ffile), 0);//09
				if(iresult == -1)
					printf("send file info failed\n");
				

				iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);//10

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


				iresult = send(soc[soc_ref], string, fsize, 0);//11
				if(iresult == -1){
					printf("send file failed\n");
				}

				iresult = recv(soc[soc_ref], rack, ACKSIZE, 0);//12

			}

			list = list->next;
		}

		if(count == 0){
			thrd_t t;
			thrd_create(&t, thread_receive_client, (void*)soc_ref);
			count++;
		}
		iresult = send(soc[soc_ref], sack, strlen(sack), 0);//13
	

	}

}





int main(int argc, char *argv[]){


	mtx_init(&gMutex, mtx_plain);
	cnd_init(&sendFileToAll);
	cnd_init(&receiveClient);

	head = new Node[MAX_CLIENT_NUM];
	int i;
	for(i=0; i<MAX_CLIENT_NUM; i++){
		head[i].name = NULL;
	}
	selfhead = new Node;
	selfhead->name = NULL;

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
	iresult = WSAStartup(MAKEWORD(2,2), &wsadata);

	if(iresult!=0){
		printf("WSAStartup failed. error code: %d.\n", WSAGetLastError());
		getchar();
		return 0;
	}
	#endif



	SOCKET server_soc = socket(AF_INET, SOCK_STREAM, 0);
	if(server_soc == -1){
		printf("socket() error:%d\n", errno);
		getchar();
		return 0;
	}

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

	sockaddr tempsockaddr;
	tempsockaddr = *result->ai_addr;
	freeaddrinfo(result);

	do{
		iresult = bind(server_soc, &tempsockaddr, sizeof(struct sockaddr_in));
		if(iresult!=0){
			printf("bind() failed: %d\n", errno);
		}
		sleep(1);
	}while(iresult!=0);

	iresult = listen(server_soc, 5);
	if(iresult != 0){
		printf("listen() error: %d\n", errno);
	}
	printf("listen() success\n");

	sockaddr client_sockaddr;
	int addrlen = sizeof(struct sockaddr);


	int soc_ref = 0;
	while(soc_ref<10){

		soc[soc_ref] = accept(server_soc, &client_sockaddr, (socklen_t*)&addrlen);
		if(soc[soc_ref] == -1){
			printf("accept() error: %d\n", errno);
		}


		thrd_t t;
		thrd_create(&t, thread_send_client, (void*)soc_ref);

		soc_ref++;
	}

sleep(10000);

	mtx_destroy(&gMutex);
	cnd_destroy(&sendFileToAll);
	cnd_destroy(&receiveClient);

	return 0;
}




