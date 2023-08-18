#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
	
#define BUF_SIZE 1024
#define NAME_SIZE 20
#define MAX_CLNT 256
	
void * send_msg(void * arg);
void error_handling(char * msg);

int clnt_cnt=0;
int clnt_socks[MAX_CLNT];

char name[NAME_SIZE]="[DEFAULT]";
char msg[BUF_SIZE];
char ip[100];

int r_clnt_cnt=0;
int r_clnt_socks[MAX_CLNT];
pthread_mutex_t mutx;

int con_socks[MAX_CLNT] = {0};

char data_name[NAME_SIZE];
	
int main(int argc, char *argv[])
{
	int sock, str_len, serv_sock, clnt_sock, clnt_adr_sz;
	struct sockaddr_in serv_addr, m_serv_adr, con_serv_adr, serv_adr;
	pthread_t snd_thread, t_id;
	void * thread_return;
    int read_cnt;

	FILE *fp;
	char buf[BUF_SIZE], peer_buf[BUF_SIZE];
	int len=0;

	FILE *data_fp;
	
	if(argc!=4 && argc!=3) {
		printf("Sending peer Usage : %s <port>, <client num>, Receive peer Usage : %s <IP> <port> <file name> \n", argv[0], argv[0]);
		exit(1);
	 }
	
	if(argc == 3){  // argc의 숫자로 sending peer과 receiving peer를 구분한다.
        int clnt_num = atoi(argv[2]);
        struct sockaddr_in clnt_adr[clnt_num];
    
        pthread_mutex_init(&mutx, NULL);
        serv_sock=socket(PF_INET, SOCK_STREAM, 0);

        memset(&serv_adr, 0, sizeof(serv_adr));
        serv_adr.sin_family=AF_INET; 
        serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
        serv_adr.sin_port=htons(atoi(argv[1]));
        
        if(bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr))==-1)
            error_handling("bind() error");
        if(listen(serv_sock, 5)==-1)
            error_handling("listen() error");
        
        for(int i = 0; i < clnt_num; i++)
        {
            clnt_adr_sz=sizeof(clnt_adr[i]);
            clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_adr[i],&clnt_adr_sz);
            
            pthread_mutex_lock(&mutx);
            clnt_socks[clnt_cnt++]=clnt_sock;
            pthread_mutex_unlock(&mutx);

            printf("Connected client IP: %s \n", inet_ntoa(clnt_adr[i].sin_addr));
        }

        // 모든 receiving peer들에게 자신의 정보와 다른 receiving peer들의 정보를 제공한다.
        char ipport[100];
        int j;
        pthread_mutex_lock(&mutx);
        for(int i=0; i<clnt_cnt; i++){
            sprintf(ipport, "%d %d\n", i+1, clnt_num);
            write(clnt_socks[i], ipport, 100);
            for(j=i+1; j < clnt_num; j++){
                sprintf(ipport, "%s\n", inet_ntoa(clnt_adr[j].sin_addr));
                write(clnt_socks[i], ipport, 100);
            }
        }	
        // sprintf(ipport, "your num : %d\n", clnt_num);
        // write(clnt_socks[clnt_num-1], ipport, 100);

        // 모든 receiving peer들에게로 부터 모든 연결이 정상적으로 완료되었음을 받는다.
        for(int i=0; i < clnt_cnt; i++){
            read(clnt_socks[i], ipport, 100);
            printf("%s\n", ipport);
        }
        pthread_mutex_unlock(&mutx);

        // 파일 읽어서 보내기
        int size;
        fp = fopen("receive.mp4", "rb");    //  receiving peer에게 보낼 파일
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        printf("total : %d\n", size);

        pthread_mutex_lock(&mutx);
        clock_t sending_start = clock();
        for(int i = 0; i < clnt_cnt; i++){
            if(i == clnt_cnt-1){	// 마지막 receiving peer은 남은 데이터를 모두 전송함
                while(1){
                    read_cnt=fread((void*)buf, 1, BUF_SIZE, fp);
                    if(read_cnt<BUF_SIZE)
                    {
                        write(clnt_socks[i], buf, read_cnt);
                        len += read_cnt;
                        printf("Send %d: %d\n", i+1, len);
                        shutdown(clnt_socks[i], SHUT_WR);	// eof를 보내기 위해 shutdown을 사용한다.
                        break;
                    }
                    write(clnt_socks[i], buf, BUF_SIZE);
                    len += read_cnt;
                }
            }
            else{
                for(int a = 0; a < (size/BUF_SIZE)/clnt_cnt; a++){
                    read_cnt=fread((void*)buf, 1, BUF_SIZE, fp);
                    write(clnt_socks[i], buf, BUF_SIZE);
                    len += read_cnt;
                }
                printf("Send %d: %d\n", i+1, len);
                len =0;
                shutdown(clnt_socks[i], SHUT_WR);	// eof를 보내기 위해 shutdown을 사용한다.
            }
            // sleep(5);		
        }
        clock_t sending_end = clock();
        pthread_mutex_unlock(&mutx);

        printf("elapsed : %f s\n", (double)(sending_end-sending_start)/CLOCKS_PER_SEC);
        close(serv_sock);
        return 0;
    }

    else if(argc == 4){
        sock=socket(PF_INET, SOCK_STREAM, 0);
        
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family=AF_INET;
        serv_addr.sin_addr.s_addr=inet_addr(argv[1]);
        serv_addr.sin_port=htons(atoi(argv[2]));
        
        if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
            error_handling("connect() error");
        
        // sending peer로 부터 나의 고유번호와 전제 receiving peer의 수를 받습니다.
        int mynum, clnt_num;	
        read(sock, ip, 100);
        char *ptr = strtok(ip, " ");
        mynum = atoi(ptr);
        ptr = strtok(NULL, " ");
        clnt_num = atoi(ptr);
        printf("your num : %d\ntotal client num : %d\nyour port num : %d\n", mynum, clnt_num, atoi(argv[2])+mynum);

        // 고유번호가 1인 receiving peer은 모든 receiving peer들에게 connection 요구
        // 고유번호가 2인 receiving peer은 1의 요청을 accept, 나머지 receiving peer들에게 connection 요구
        // 고유번호가 3인 receiving peer은 1, 2의 요청을 accept, 나머지 receiving peer들에게 connection 요구
        // ...

        // 고유번호가 1인 아닌 모든 receiving peer은 accept을 할 수 있어야 한다.
        if(mynum != 1){
            struct sockaddr_in clnt_adr[clnt_num];
            pthread_mutex_init(&mutx, NULL);
            serv_sock=socket(PF_INET, SOCK_STREAM, 0);

            memset(&m_serv_adr, 0, sizeof(m_serv_adr));
            m_serv_adr.sin_family=AF_INET; 
            m_serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
            m_serv_adr.sin_port=htons(atoi(argv[2])+mynum); // port번호는 sending peer에 자신의 고유번호를 더한 값으로 지정했다.
            
            if(bind(serv_sock, (struct sockaddr*) &m_serv_adr, sizeof(m_serv_adr))==-1)
                error_handling("bind() error");
            if(listen(serv_sock, 5)==-1)
                error_handling("listen() error");
            
            // 고유번호가 자신보다 낮은 receiving peer들을 accept해준다.
            for(int i = 0; i < mynum-1; i++)
            {	
                clnt_adr_sz=sizeof(clnt_adr[i]);
                clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_adr[i],&clnt_adr_sz);
                
                pthread_mutex_lock(&mutx);
                r_clnt_socks[r_clnt_cnt++]=clnt_sock;
                pthread_mutex_unlock(&mutx);
            
                printf("Accepted client Port : %d, IP : %s\n", clnt_adr[i].sin_port, inet_ntoa(clnt_adr[i].sin_addr));
            }

        }
        
        // 고유번호가 자신보다 높은 receiving peer들에게 connection을 요구한다.
        sleep(0.5);
        for(int k = 0; k < clnt_num - mynum; k++){
            read(sock, ip, 100);
            con_socks[k]=socket(PF_INET, SOCK_STREAM, 0);
            
            memset(&con_serv_adr, 0, sizeof(con_serv_adr));
            con_serv_adr.sin_family=AF_INET;
            con_serv_adr.sin_addr.s_addr=inet_addr(ip);
            con_serv_adr.sin_port=htons(atoi(argv[2])+mynum+k+1);

            if(connect(con_socks[k], (struct sockaddr*)&con_serv_adr, sizeof(con_serv_adr))==-1)
                error_handling("connect() error");
            else
                printf("Connected client Port : %d, IP : %s", atoi(argv[2])+mynum+k+1, ip);
        }

        // 모든 연결이 정상적으로 완료되었음을 sending peer에게 알린다.
        sprintf(ip, "num %d, clearly connected", mynum);
        printf("%s\n", ip);
        write(sock, ip, 100);

        // sending peer에게로 부터 바이너리 파일의 일부분을 받는다.
        clock_t receiving_start = clock();
        sprintf(data_name, "ex%d", mynum);
        data_fp = fopen(data_name, "wb");	// 새파일을 만들어 sending peer로 부터 받은 정보를 저장한다.
        while((str_len=read(sock, buf, sizeof(buf)))!=0){
            fwrite((void*)buf, 1, str_len, data_fp);
            len += str_len;
        }
        printf("receive : %d\n", len);
        fclose(data_fp);

        // 자신의 고유번호를 thread에 보내어 자신과 연결된 모든 receive peer에게 자신이 받은 파일을 보낸다. 
        pthread_create(&snd_thread, NULL, send_msg, (void*)&mynum);
        // pthread_join(snd_thread, &thread_return);

        sprintf(name, "%d_%s", mynum, argv[3]);
        fp = fopen(name, "wb");
        if(mynum == 1){
            data_fp = fopen(data_name, "rb");
            while(feof(data_fp)==0){
                str_len = fread((void*)peer_buf, 1, BUF_SIZE, data_fp);
                fwrite((void*)peer_buf, 1, str_len, fp);
            }
        
            for(int k = 0; k < clnt_num - mynum; k++){
                while((str_len=read(con_socks[k], peer_buf, sizeof(peer_buf)))!=0){
                    fwrite((void*)peer_buf, 1, str_len, fp);
                }
            }
            fclose(data_fp);
            fclose(fp);
        }
        else{
            //고유번호가 나보다 낮은 데이터.
            for(int i = 0; i < mynum-1; i++){
                while((str_len=read(r_clnt_socks[i], peer_buf, sizeof(peer_buf)))!=0){
                    fwrite((void*)peer_buf, 1, str_len, fp);
                }
            }

            //나의 데이터.
            data_fp = fopen(data_name, "rb");
            while(feof(data_fp)==0){
                str_len = fread((void*)peer_buf, 1, BUF_SIZE, data_fp);
                fwrite((void*)peer_buf, 1, str_len, fp);
            }

            //고유번호가 나보다 높은 데이터.
            for(int k = 0; k < clnt_num - mynum; k++){
                while((str_len=read(con_socks[k], peer_buf, sizeof(peer_buf)))!=0){
                    fwrite((void*)peer_buf, 1, str_len, fp);
                }
            }
            fclose(data_fp);
            fclose(fp);
        }
        clock_t receiving_end = clock();
        printf("elapsed : %f s\n", (double)(receiving_end-receiving_start)/CLOCKS_PER_SEC);

        printf("complete\n");
            
        pthread_join(snd_thread, &thread_return);    
        close(sock);
        return 0;
    }
}
	
void * send_msg(void * arg)
{
	int mynum=*((int*)arg);
	FILE *fp;
	char data[BUF_SIZE];
	int read_cnt, len =0;

	// sending peer로 부터 받은 나만의 데이터를 BUF_SIZE만큼 읽어 나와 연결된 모든 receive peer에게 보낸다.
	fp = fopen(data_name, "rb");
	while(1){
		read_cnt=fread((void*)data, 1, BUF_SIZE, fp);
		if(read_cnt<BUF_SIZE){
			for(int i = 0; i < mynum-1; i++){
				write(r_clnt_socks[i], data, read_cnt);
				shutdown(r_clnt_socks[i], SHUT_WR);	// eof를 보내기 위해 shutdown을 사용한다.
			}
			for(int k = 0; con_socks[k] != 0; k++){
				write(con_socks[k], data, read_cnt);
				shutdown(con_socks[k], SHUT_WR);	// eof를 보내기 위해 shutdown을 사용한다.
			}
			len += read_cnt;
			printf("Thread Send : %d\n", len);
			break;
		}
		else{
			for(int i = 0; i < mynum-1; i++){
				write(r_clnt_socks[i], data, BUF_SIZE);
			}
			for(int k = 0; con_socks[k] != 0; k++){
				write(con_socks[k], data, BUF_SIZE);
			}
		}
		len += read_cnt;
	}

	fclose(fp);
	return NULL;
}

void error_handling(char *msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
