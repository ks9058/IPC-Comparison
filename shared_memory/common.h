#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <time.h>

#define REQQKEY (key_t)60080
#define QPERM 0600
//#define PAYLOAD_SIZE 4096

extern int queue_id;

// 데이터 구조 정의
typedef struct {
    long mtype;
	int client_id;
    char command[16]; // "deposit", "withdraw", "check"
    int amount;

    //덩치를 키우기 위한 더미 데이터
    //char dummy_data[PAYLOAD_SIZE];
} Request;

typedef struct {
	long mtype;
    int status;       // 0: 실패, 1: 성공
    int balance;
    char message[64];

    //덩치를 키우기 위한 더미 데이터
    //char dummy_data[PAYLOAD_SIZE];
} Response;

typedef struct {
    int id;
    int balance;
} Account;

#define MAX_ACCOUNTS 10
#define LOG_FILE "server_log.txt"

// IPC 관련 함수 원형 (팀별로 구현)
void ipc_init();                         // IPC 초기화
void ipc_cleanup();                      // IPC 해제
void send_request(Request *req);         // 클라이언트 -> 서버 요청 전송
void receive_request(Request *req);      // 서버 <- 클라이언트 요청 수신
void send_response(Response *res);       // 서버 -> 클라이언트 응답 전송
void receive_response(Response *res, int client_id);    // 클라이언트 <- 서버 응답 수신

#endif

