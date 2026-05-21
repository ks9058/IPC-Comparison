#include "common.h"
#include <signal.h>


Account accounts[MAX_ACCOUNTS];
pthread_mutex_t account_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

#define DATA_FILE "accounts_data.txt"

// 파일에서 계좌 정보 읽어오기 (Load)
void load_accounts() {
    FILE *fp = fopen(DATA_FILE, "r");
    if (fp == NULL) {
        printf("[INFO] 기존 데이터 파일이 없습니다. 새로 시작합니다.\n");
        // 파일이 없으면 기본 초기화 (기존 main에 있던 초기화 로직)
        for (int i = 0; i < MAX_ACCOUNTS; i++) {
            accounts[i].id = i;
            accounts[i].balance = 10000; // 초기 잔액
        }
        return;
    }

    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        // ID와 잔액을 읽어옴
        if (fscanf(fp, "%d %d", &accounts[i].id, &accounts[i].balance) == EOF) {
            break;
        }
    }
    printf("[INFO] 데이터 파일에서 계좌 정보를 복구했습니다.\n");
    fclose(fp);
}

// 계좌 정보를 파일에 저장하기 (Save)
void save_accounts() {
    // 동기화 중요: 여러 스레드가 동시에 파일을 쓰면 깨질 수 있음
    // 하지만 현재 구조상 worker_thread 안에서 account_mutex를 잡고 호출할 것이므로 안전함
    
    FILE *fp = fopen(DATA_FILE, "w"); // "w" 모드는 내용을 싹 지우고 새로 씀
    if (fp == NULL) {
        perror("파일 저장 실패");
        return;
    }

    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        fprintf(fp, "%d %d\n", accounts[i].id, accounts[i].balance);
    }
    // 디스크에 확실히 쓰도록 버퍼 비우기
    fflush(fp); 
    fclose(fp);
}

void *log_thread(void *arg);
// 요청 처리 스레드 (각 요청마다 개별 실행)
void *worker_thread(void *arg) {
    Request *req = (Request *)arg;
    Response res;

    pthread_mutex_lock(&account_mutex);
    int id = req->client_id;
	int data_changed = 0;

    if (strcmp(req->command, "deposit") == 0) {
        accounts[id].balance += req->amount;
        res.status = 1;
        sprintf(res.message, "Deposit %d success", req->amount);
    	data_changed = 1;
	} else if (strcmp(req->command, "withdraw") == 0) {
        if (accounts[id].balance >= req->amount) {
            accounts[id].balance -= req->amount;
            res.status = 1;
            sprintf(res.message, "Withdraw %d success", req->amount);
        	data_changed  = 1;
		} else {
            res.status = 0;
            sprintf(res.message, "Not enough balance");
        }
    } else if (strcmp(req->command, "check") == 0) {
        res.status = 1;
        sprintf(res.message, "Balance check success");
    } else {
        res.status = 0;
        sprintf(res.message, "Unknown command");
    }

	// 주의: save_accounts()는 mutex 안에서 호출되어야 안전함 (현재 mutex 안이라 OK)
	if(data_changed)
			save_accounts();

    res.balance = accounts[id].balance;
    pthread_mutex_unlock(&account_mutex);

    // 응답 전송 (pipe, message queue, shared memory 중 하나 사용)
	//client_id = 1 인 경우 요청 채널과 겹치므로 +10 진행
    res.mtype = req->client_id + 10;
	send_response(&res);


    pthread_t tid;
    pthread_create(&tid, NULL, log_thread, (void *)req);
    pthread_detach(tid);  // 자동 자원 해제
    
    return NULL;
}

// I/O 스레드 (요청 수신 + worker 스레드 생성)
void *io_thread(void *arg) {
    while (1) {
        Request *req = malloc(sizeof(Request));

        // IPC로 클라이언트 요청 수신
        receive_request(req);
        pthread_t tid;
        pthread_create(&tid, NULL, worker_thread, (void *)req);
        pthread_detach(tid);  // 자동 자원 해제
    }
    return NULL;
}

// 로그 관리 스레드 (옵션)
void *log_thread(void *arg) {
		Request *req = (Request *)arg;
		int id = req->client_id;
		// 로그 기록
		pthread_mutex_lock(&log_mutex);
		FILE *fp = fopen(LOG_FILE, "a");
		if (fp) {
				time_t now = time(NULL);
				fprintf(fp, "[%s] Client %d %s %d -> Balance: %d\n",
								strtok(ctime(&now), "\n"),
								id, req->command, req->amount, accounts[id].balance);
				fclose(fp);
		}
		pthread_mutex_unlock(&log_mutex);
		free(req);
		return NULL;
}

int main() {
    ipc_init();
	printf("은행 서버 시작...\n");


	static struct sigaction act;
	act.sa_handler = ipc_cleanup;
	sigaction(SIGINT, &act, NULL);

    // 계좌 초기화
	load_accounts();

    pthread_t t_io;
    pthread_create(&t_io, NULL, io_thread, NULL);

    pthread_join(t_io, NULL);

    return 0;
}
