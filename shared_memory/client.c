#include "common.h"

#define BILLION 1000000000L

pthread_mutex_t req_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t req_cond = PTHREAD_COND_INITIALIZER;

Request shared_request;
Response shared_response;
int request_ready = 0;   // 요청 큐가 비었는지 표시
int response_ready = 0;  // 응답이 도착했는지 표시

double get_time_diff(struct timespec start, struct timespec end){
		return (end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec)/BILLION;
}

// 사용자 입력 스레드
void *user_input_thread(void *arg) {
    int client_id = *(int *)arg;
    while (1) {
        Request req;
        printf("명령 입력 (deposit / withdraw / check / exit): ");
        scanf("%s", req.command);
        if (strcmp(req.command, "exit") == 0) break;

        req.client_id = client_id;
        req.amount = 0;
        //입금이나 출금시 금액을 입력, 통장 잔액 확인 시 req의 amount =0
        if (strcmp(req.command, "check") != 0) {
            printf("금액 입력: ");
            scanf("%d", &req.amount);
        }

        // 요청을 공유 변수에 저장
        pthread_mutex_lock(&req_mutex);
        shared_request = req;
        request_ready = 1;
        pthread_cond_signal(&req_cond); // I/O 스레드에게 알림
        pthread_mutex_unlock(&req_mutex);
		sleep(1);
    }
    return NULL;
}

// 통신 스레드 (I/O 역할)
void *io_thread(void *arg) {
	struct timespec start_time, end_time;
    int client_id = *(int *)arg;
    while (1) {
        
        pthread_mutex_lock(&req_mutex);
        
        //while(!request_readt)를 빼도 정상 동작할 것 같은데?
        while (!request_ready) { // 요청이 들어올 때까지 대기
            pthread_cond_wait(&req_cond, &req_mutex);
        }
        //pthread_cond_wait(&req_cond, &req_mutex);
        

		//shared_request : user input thread에서 만든 요청 구조체
        Request req = shared_request;
        req.mtype = 1;
		request_ready = 0;
        pthread_mutex_unlock(&req_mutex);



		clock_gettime(CLOCK_MONOTONIC, &start_time);
        // IPC로 서버에 요청 전송
        send_request(&req);

        // 서버 응답 수신
        Response res;
        receive_response(&res, client_id);

		clock_gettime(CLOCK_MONOTONIC, &end_time);

		double elapsed = get_time_diff(start_time, end_time);
		printf(" >>> [IPC 성능] RTT: %.9f sec \n", elapsed);

        // 응답 저장(log 관리 쓰레드 동작 조건)
        pthread_mutex_lock(&req_mutex);
        shared_response = res;
        response_ready = 1;
        pthread_mutex_unlock(&req_mutex);

        // 콘솔 출력
        printf("[서버 응답] %s (잔액: %d)\n", res.message, res.balance);
    }
    return NULL;
}

// 로그 관리 스레드
void *log_thread(void *arg) {
	
	//client id로 된 log파일 생성
	int client_id = *(int *)arg;
	char filename[50];
    sprintf(filename, "client_%d_log.txt", client_id);

	while (1) {
		pthread_mutex_lock(&req_mutex);
        // 서버로 부터 응답이 온 경우
        if (response_ready) {
			//두 개의 클라이언트가 file에 접근할 때 문제 발생 -> 우리의 경우 id마다 서로 다른 파일 오픈		
            FILE *fp = fopen(filename, "a");
            if (fp) {
                time_t now = time(NULL);
                fprintf(fp, "[%s] %s - Balance: %d\n",
                        strtok(ctime(&now), "\n"),
                        shared_response.message,
                        shared_response.balance);
                fclose(fp);
            }
            response_ready = 0;
        }
        pthread_mutex_unlock(&req_mutex);
        sleep(1); // 로그 기록 주기
    }
    return NULL;
}

int main(int argc,char *argv[]) {
	ipc_init();

	int *client_id = malloc(sizeof(int));
	if(argc!=2)
			perror("인자 개수 오류");
	else
			*client_id = atoi(argv[1]);

	printf("클라이언트 시작...\n");

	pthread_t t_input, t_io, t_log;
    pthread_create(&t_input, NULL, user_input_thread, client_id);
    pthread_create(&t_io, NULL, io_thread, client_id);
    pthread_create(&t_log, NULL, log_thread, client_id);

    pthread_join(t_input, NULL);
    pthread_cancel(t_io);  // 입력 종료 시 나머지 스레드 종료
    pthread_cancel(t_log);

    printf("클라이언트 종료.\n");
    return 0;
}
