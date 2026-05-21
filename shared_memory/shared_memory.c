#include "common.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

// ---------------------------------------------------------------------
// 상수 및 데이터 구조 정의
// ---------------------------------------------------------------------

#define QUEUE_SIZE 10      // 원형 큐의 크기
#define SEM_KEY (REQQKEY + 1) // 세마포어 키 (공유메모리 키 + 1)

// 세마포어 인덱스 정의 (총 6개 사용)
enum {
    SEM_MUTEX_REQ = 0, // 요청 큐 접근 제어 (Binary Semaphore)
    SEM_EMPTY_REQ,     // 요청 큐의 빈 공간 수 (Counting Semaphore)
    SEM_FULL_REQ,      // 요청 큐의 채워진 수 (Counting Semaphore)
    
    SEM_MUTEX_RES,     // 응답 큐 접근 제어
    SEM_EMPTY_RES,     // 응답 큐의 빈 공간 수
    SEM_FULL_RES,      // 응답 큐의 채워진 수
    NUM_SEMS
};

// 공유 메모리에 저장될 구조체 (요청 큐 + 응답 큐)
typedef struct {
    // 요청 큐 (Client -> Server)
    Request req_queue[QUEUE_SIZE];
    int req_front;
    int req_rear;

    // 응답 큐 (Server -> Client)
    Response res_queue[QUEUE_SIZE];
    int res_front;
    int res_rear;
} SharedData;

// 세마포어 제어를 위한 공용체
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// ---------------------------------------------------------------------
// 전역 변수
// ---------------------------------------------------------------------
static int shm_id = -1;
static int sem_id = -1;
static SharedData *shm_ptr = NULL;

// ---------------------------------------------------------------------
// 내부 유틸리티 함수 (P/V 연산)
// ---------------------------------------------------------------------
void p_op(int sem_idx) {
    struct sembuf buf;
    buf.sem_num = sem_idx;
    buf.sem_op = -1; // Wait (감소)
    buf.sem_flg = 0;
    if (semop(sem_id, &buf, 1) == -1) {
        if (errno != EINTR) perror("semop p_op failed");
    }
}

void v_op(int sem_idx) {
    struct sembuf buf;
    buf.sem_num = sem_idx;
    buf.sem_op = 1;  // Signal (증가)
    buf.sem_flg = 0;
    if (semop(sem_id, &buf, 1) == -1) {
        perror("semop v_op failed");
    }
}

// ---------------------------------------------------------------------
// IPC 함수 구현
// ---------------------------------------------------------------------

void ipc_init() {
    // 1. 공유 메모리 생성 (없으면 생성, 있으면 연결)
    // IPC_CREAT | IPC_EXCL로 "내가 처음 만드는 것인가?" 확인
    int is_creator = 0;
    shm_id = shmget(REQQKEY, sizeof(SharedData), 0666 | IPC_CREAT | IPC_EXCL);
    
    if (shm_id == -1) {
        if (errno == EEXIST) {
            // 이미 존재하면 연결만 수행
            shm_id = shmget(REQQKEY, sizeof(SharedData), 0666);
        } else {
            perror("shmget failed");
            exit(1);
        }
    } else {
        is_creator = 1; // 생성자임
    }

    // 2. 프로세스 메모리에 Attach
    shm_ptr = (SharedData *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }

    // 3. 세마포어 생성
    sem_id = semget(SEM_KEY, NUM_SEMS, 0666 | IPC_CREAT);
    if (sem_id == -1) {
        perror("semget failed");
        exit(1);
    }

    // 4. 최초 생성자(Server)일 경우에만 초기화 수행
    if (is_creator) {
        // 큐 인덱스 초기화
        shm_ptr->req_front = 0;
        shm_ptr->req_rear = 0;
        shm_ptr->res_front = 0;
        shm_ptr->res_rear = 0;

        // 세마포어 값 초기화
        union semun arg;
        unsigned short values[NUM_SEMS];
        
        // Mutex: 1 (Lock 가능 상태)
        values[SEM_MUTEX_REQ] = 1;
        values[SEM_MUTEX_RES] = 1;
        
        // Empty: 큐 크기 (비어있는 공간 수)
        values[SEM_EMPTY_REQ] = QUEUE_SIZE;
        values[SEM_EMPTY_RES] = QUEUE_SIZE;
        
        // Full: 0 (채워진 데이터 수)
        values[SEM_FULL_REQ] = 0;
        values[SEM_FULL_RES] = 0;

        arg.array = values;
        if (semctl(sem_id, 0, SETALL, arg) == -1) {
            perror("semctl init failed");
            exit(1);
        }
    }
}

void ipc_cleanup() {
    // Shared Memory Detach (모든 프로세스 공통)
    if (shm_ptr) {
        shmdt(shm_ptr);
    }

    // 자원 삭제는 보통 Server가 종료될 때 수행
    // server.c의 핸들러 구조상 이 함수가 호출되면 삭제 시도
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
         if (errno != EINVAL && errno != EIDRM) perror("shmctl remove failed");
    } else {
        printf("Shared Memory Removed.\n");
    }

    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        if (errno != EINVAL && errno != EIDRM) perror("semctl remove failed");
    } else {
        printf("Semaphores Removed.\n");
        
    }
    exit(0);
}

void send_request(Request *req) {
    // 1. 빈 공간 대기 (Empty 감소)
    p_op(SEM_EMPTY_REQ);
    
    // 2. Lock (Mutex 감소)
    p_op(SEM_MUTEX_REQ);
    
    // 3. 쓰기 (Critical Section)
    shm_ptr->req_queue[shm_ptr->req_rear] = *req;
    shm_ptr->req_rear = (shm_ptr->req_rear + 1) % QUEUE_SIZE;
    
    // 4. Unlock (Mutex 증가)
    v_op(SEM_MUTEX_REQ);
    
    // 5. 데이터 있음 알림 (Full 증가)
    v_op(SEM_FULL_REQ);
}

void receive_request(Request *req) {
    // 1. 데이터 대기 (Full 감소)
    p_op(SEM_FULL_REQ);
    
    // 2. Lock
    p_op(SEM_MUTEX_REQ);
    
    // 3. 읽기
    *req = shm_ptr->req_queue[shm_ptr->req_front];
    shm_ptr->req_front = (shm_ptr->req_front + 1) % QUEUE_SIZE;
    
    // 4. Unlock
    v_op(SEM_MUTEX_REQ);
    
    // 5. 빈 공간 생김 알림 (Empty 증가)
    v_op(SEM_EMPTY_REQ);
}

void send_response(Response *res) {
    // Request와 동일한 로직으로 Response Queue 사용
    p_op(SEM_EMPTY_RES);
    p_op(SEM_MUTEX_RES);
    
    shm_ptr->res_queue[shm_ptr->res_rear] = *res;
    shm_ptr->res_rear = (shm_ptr->res_rear + 1) % QUEUE_SIZE;
    
    v_op(SEM_MUTEX_RES);
    v_op(SEM_FULL_RES);
}

void receive_response(Response *res, int client_id) {
    p_op(SEM_FULL_RES);
    p_op(SEM_MUTEX_RES);
    
    *res = shm_ptr->res_queue[shm_ptr->res_front];
    shm_ptr->res_front = (shm_ptr->res_front + 1) % QUEUE_SIZE;
    
    v_op(SEM_MUTEX_RES);
    v_op(SEM_EMPTY_RES);
}
