#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

// 1. 파이프 경로 정의
#define FIFO_REQ "/tmp/bank_req_fifo"       // 모든 클라이언트가 공유하는 요청 파이프
#define FIFO_RES_PRE "/tmp/bank_res_fifo_"  // 클라이언트별 응답 파이프 접두사

// 2. 파이프 경로를 저장할 배열 (최대 계좌 수만큼 생성)
// 예: res_fifo_paths[0] -> "/tmp/bank_res_fifo_0"
char res_fifo_paths[MAX_ACCOUNTS][64];

void ipc_init() {
    // umask 0 설정 (권한 문제 방지)
    mode_t old_umask = umask(0);

    // 1) 공용 요청(Request) 파이프 생성
    if (mkfifo(FIFO_REQ, 0666) == -1) {
        if (errno != EEXIST) {
            perror("[IPC] mkfifo request pipe failed");
            exit(1);
        }
    }

    // 2) 클라이언트별 응답(Response) 파이프 생성 (배열 사용)
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        // 경로 문자열 생성 및 배열에 저장
        sprintf(res_fifo_paths[i], "%s%d", FIFO_RES_PRE, i);

        // 네임드 파이프 생성
        if (mkfifo(res_fifo_paths[i], 0666) == -1) {
            if (errno != EEXIST) {
                perror("[IPC] mkfifo response pipe failed");
                exit(1);
            }
        }
    }

    umask(old_umask);
    // printf("[IPC] Pipes initialized using array.\n");
}

void ipc_cleanup() {
    // 1) 요청 파이프 삭제
    unlink(FIFO_REQ);

    // 2) 모든 응답 파이프 삭제 (배열 순회)
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        unlink(res_fifo_paths[i]);
    }
    exit(0);
}

void send_request(Request* req) {
    int fd;
    // 공용 요청 파이프로 전송
    if ((fd = open(FIFO_REQ, O_WRONLY)) == -1) {
        perror("send_request open error");
        return;
    }

    if (write(fd, req, sizeof(Request)) == -1) {
        perror("send_request write error");
    }

    close(fd);
}

void receive_request(Request* req) {
    int fd;
    // 공용 요청 파이프에서 수신
    // (서버는 여기서 대기하다가 누군가 요청을 보내면 읽음)
    if ((fd = open(FIFO_REQ, O_RDONLY)) == -1) {
        perror("receive_request open error");
        return;
    }

    int n = read(fd, req, sizeof(Request));
    if (n == -1) {
        perror("receive_request read error");
    }

    close(fd);
}

void send_response(Response* res) {
    int fd;

    // Server.c 로직: res.mtype = req->client_id + 10
    // 따라서 client_id를 복원하려면 10을 빼야 함
    int target_id = res->mtype - 10;

    // 예외 처리: 유효하지 않은 ID일 경우
    if (target_id < 0 || target_id >= MAX_ACCOUNTS) {
        fprintf(stderr, "[Error] Invalid client ID: %d\n", target_id);
        return;
    }

    // *** 핵심 변경: 배열을 사용하여 해당 클라이언트의 전용 파이프 열기 ***
    if ((fd = open(res_fifo_paths[target_id], O_WRONLY)) == -1) {
        perror("send_response open error");
        return;
    }

    if (write(fd, res, sizeof(Response)) == -1) {
        perror("send_response write error");
    }

    close(fd);
}

// common.h 선언에 맞춰 인자 수정 (Response* res, int client_id)
void receive_response(Response* res, int client_id) {
    int fd;

    // 예외 처리
    if (client_id < 0 || client_id >= MAX_ACCOUNTS) {
        fprintf(stderr, "[Error] Invalid client ID for receive: %d\n", client_id);
        return;
    }

    // *** 핵심 변경: 내 ID(client_id)에 해당하는 배열의 파이프 경로 사용 ***
    // 배열에 미리 저장해둔 경로: res_fifo_paths[client_id]
    if ((fd = open(res_fifo_paths[client_id], O_RDONLY)) == -1) {
        perror("receive_response open error");
        return;
    }

    int n = read(fd, res, sizeof(Response));
    if (n == -1) {
        perror("receive_response read error");
    }

    close(fd);
}
