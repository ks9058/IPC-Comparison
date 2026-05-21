# IPC 기법별 성능 분석 프로젝트

## 1. 프로젝트 소개

본 프로젝트는 UNIX/Linux 환경에서 대표적인 IPC(Inter-Process Communication) 기법인 **Named Pipe**, **Message Queue**, **Shared Memory**를 직접 구현하고, 각 방식의 성능을 비교 분석한 시스템 프로그래밍 프로젝트입니다.

은행 서버-클라이언트 구조를 기반으로 입금, 출금, 잔액 조회 기능을 구현했으며, 다수의 클라이언트가 동시에 하나의 계좌 자원에 접근하는 상황에서 **Mutex, Condition Variable, Semaphore**를 활용하여 데이터 무결성을 보장하도록 설계했습니다.

또한 각 IPC 방식별 RTT(Round Trip Time)를 측정하여, 데이터 크기와 동기화 방식에 따른 통신 오버헤드 차이를 분석했습니다.

---

## 2. 프로젝트 목표

본 프로젝트의 주요 목표는 다음과 같습니다.

- Named Pipe, Message Queue, Shared Memory 기반 IPC 구현
- 멀티스레드 기반 은행 서버-클라이언트 구조 설계
- Mutex와 Semaphore를 이용한 동시성 제어
- Condition Variable을 활용한 Busy Waiting 방지
- IPC 방식별 RTT 측정 및 성능 비교
- 데이터 크기 변화에 따른 통신 오버헤드 분석
- 파일 기반 계좌 데이터 영속성 보장

---

## 3. 개발 환경

| 항목 | 내용 |
|---|---|
| OS | Linux / UNIX 계열 환경 |
| Language | C |
| Build Tool | Makefile |
| Thread Library | pthread |
| IPC | Named Pipe, System V Message Queue, Shared Memory |
| Synchronization | Mutex, Condition Variable, System V Semaphore |

---

## 4. 주요 기능

### 4.1 은행 트랜잭션 처리

클라이언트는 서버에 요청을 보내 다음 기능을 수행할 수 있습니다.

| 명령어 | 기능 |
|---|---|
| `deposit` | 입금 |
| `withdraw` | 출금 |
| `check` | 잔액 조회 |
| `exit` | 클라이언트 종료 |

서버는 클라이언트 요청을 수신한 뒤 계좌 데이터를 처리하고, 처리 결과와 현재 잔액을 응답으로 전송합니다.

---

### 4.2 계좌 데이터 영속성

계좌 정보는 메모리의 `accounts[MAX_ACCOUNTS]` 배열뿐 아니라 `accounts_data.txt` 파일에도 저장됩니다.

입금 또는 출금으로 잔액이 변경되면 서버는 즉시 파일에 데이터를 반영합니다.

이를 통해 서버가 재시작되더라도 기존 계좌 데이터를 복구할 수 있도록 구현했습니다.

---

### 4.3 비동기 로그 기록

서버와 클라이언트는 별도의 로그 스레드를 통해 거래 내역을 기록합니다.

| 로그 파일 | 설명 |
|---|---|
| `server_log.txt` | 서버 측 거래 처리 로그 |
| `client_N_log.txt` | 클라이언트별 응답 로그 |

로그 기록을 메인 처리 로직과 분리하여, 트랜잭션 처리 지연을 줄이도록 설계했습니다.

---

## 5. 시스템 구조

본 프로젝트는 서버와 클라이언트가 각각 여러 개의 스레드로 동작하는 구조입니다.

### 5.1 서버 스레드 구조

| Thread | 역할 |
|---|---|
| `io_thread` | IPC 채널을 통해 클라이언트 요청 수신 |
| `worker_thread` | 입금, 출금, 조회 등 비즈니스 로직 처리 |
| `log_thread` | 처리 결과를 서버 로그 파일에 기록 |

서버는 클라이언트 요청을 수신하면 요청별로 `worker_thread`를 생성하여 병렬 처리를 수행합니다.

---

### 5.2 클라이언트 스레드 구조

| Thread | 역할 |
|---|---|
| `user_input_thread` | 사용자 명령 입력 처리 |
| `io_thread` | 서버 요청 전송, 응답 수신, RTT 측정 |
| `log_thread` | 클라이언트 로그 기록 |

클라이언트는 사용자 입력 처리, 서버 통신, 로그 기록을 분리하여 동작합니다.

---

## 6. IPC 구현 방식

본 프로젝트에서는 동일한 서버-클라이언트 은행 시스템을 세 가지 IPC 방식으로 각각 구현했습니다.

---

### 6.1 Named Pipe

Named Pipe는 파일 시스템에 생성되는 FIFO 파일을 통해 프로세스 간 통신을 수행합니다.

요청과 응답이 섞이지 않도록 다음과 같이 채널을 분리했습니다.

```text
요청 채널: /tmp/bank_req_fifo
응답 채널: /tmp/bank_res_fifo_[client_id]
```

모든 클라이언트는 공용 요청 파이프에 요청을 전송하고, 서버는 클라이언트별 응답 파이프로 처리 결과를 전송합니다.

#### 특징

- 구현이 비교적 단순함
- 파일 시스템 기반 FIFO 사용
- 요청 채널과 응답 채널 분리를 통해 응답 혼선 방지
- Pipe 내부에서 커널 버퍼를 사용하므로 데이터 복사 비용 발생

---

### 6.2 Message Queue

Message Queue는 System V 메시지 큐를 이용하여 데이터를 주고받는 방식입니다.

요청과 응답을 구분하기 위해 메시지 구조체의 `mtype` 필드를 활용했습니다.

```text
요청 메시지 타입: 1
응답 메시지 타입: client_id + 10
```

서버는 `mtype = 1`인 요청을 수신하고, 클라이언트는 자신의 `client_id + 10`에 해당하는 응답만 선택적으로 수신합니다.

#### 특징

- 커널이 메시지 큐를 관리
- 메시지 타입 기반 라우팅 가능
- 클라이언트별 응답 구분이 쉬움
- 작은 데이터 전송에서 우수한 성능 확인

---

### 6.3 Shared Memory

Shared Memory는 프로세스들이 동일한 메모리 영역을 공유하여 데이터를 주고받는 방식입니다.

데이터 복사 비용을 줄일 수 있지만, 커널이 자동으로 동기화를 제공하지 않기 때문에 직접 동기화 로직을 구현해야 합니다.

본 프로젝트에서는 원형 큐 구조와 System V Semaphore를 사용했습니다.

```text
Empty 대기 → Mutex 획득 → 데이터 쓰기 → Mutex 해제 → Full 알림
```

#### 특징

- 프로세스 간 메모리 직접 공유
- 데이터 복사 비용이 적음
- 대용량 데이터 전송에 유리
- 직접 세마포어 동기화 구현 필요

---

## 7. 동기화 설계

### 7.1 서버 측 계좌 데이터 보호

여러 클라이언트가 동시에 동일한 계좌에 입금 또는 출금을 요청하면 Race Condition이 발생할 수 있습니다.

이를 방지하기 위해 서버에서는 계좌 데이터와 파일 저장 로직을 하나의 Mutex로 보호했습니다.

```c
pthread_mutex_lock(&account_mutex);

/*
    계좌 잔액 변경
    accounts_data.txt 저장
*/

pthread_mutex_unlock(&account_mutex);
```

잔액 변경과 파일 저장을 하나의 임계 영역으로 묶어, 메모리 데이터와 파일 데이터 간 불일치가 발생하지 않도록 했습니다.

---

### 7.2 서버 로그 파일 보호

여러 스레드가 동시에 로그 파일에 접근하면 로그 메시지가 섞일 수 있습니다.

따라서 서버 로그 파일 접근에는 별도의 `log_mutex`를 사용했습니다.

```c
pthread_mutex_lock(&log_mutex);

/*
    server_log.txt 기록
*/

pthread_mutex_unlock(&log_mutex);
```

---

### 7.3 클라이언트 요청 버퍼 보호

클라이언트 내부에서는 사용자 입력 스레드와 통신 스레드가 공유 요청 버퍼에 접근합니다.

이를 안전하게 처리하기 위해 Mutex와 Condition Variable을 사용했습니다.

```c
pthread_mutex_lock(&req_mutex);

while (!request_ready) {
    pthread_cond_wait(&req_cond, &req_mutex);
}

/*
    요청 처리
*/

pthread_mutex_unlock(&req_mutex);
```

Condition Variable을 사용하여 요청이 없을 때 통신 스레드가 불필요하게 CPU를 점유하지 않도록 했습니다.

---

### 7.4 Shared Memory 동기화

Shared Memory는 여러 프로세스가 같은 메모리 영역에 접근하기 때문에 동기화가 필수입니다.

본 프로젝트에서는 System V Semaphore를 사용하여 다음 세 가지 역할을 수행했습니다.

| Semaphore | 역할 |
|---|---|
| Mutex | 공유 메모리 접근 상호 배제 |
| Empty | 큐에 빈 공간이 있는지 확인 |
| Full | 큐에 읽을 데이터가 있는지 확인 |

이를 통해 여러 프로세스가 동시에 공유 메모리에 접근하더라도 데이터 충돌이 발생하지 않도록 구현했습니다.

---

## 8. 프로젝트 구조

```text
IPC/
├── README.md
├── message_passing/
│   ├── client.c
│   ├── common.h
│   ├── Makefile
│   ├── message_passing.c
│   └── server.c
├── pipe/
│   ├── client.c
│   ├── common.h
│   ├── Makefile
│   ├── pipe.c
│   └── server.c
└── shared_memory/
    ├── client.c
    ├── common.h
    ├── Makefile
    ├── server.c
    └── shared_memory.c
```

### 주요 디렉터리 설명

| 디렉터리 | 설명 |
|---|---|
| `message_passing/` | System V Message Queue 기반 IPC 구현 |
| `pipe/` | Named Pipe 기반 IPC 구현 |
| `shared_memory/` | Shared Memory 기반 IPC 구현 |

### 주요 파일 설명

| 파일 | 설명 |
|---|---|
| `common.h` | 공통 구조체, 상수, IPC 함수 원형 정의 |
| `client.c` | 클라이언트 메인 로직 및 스레드 구현 |
| `server.c` | 서버 메인 로직 및 트랜잭션 처리 |
| `message_passing.c` | Message Queue 기반 IPC 구현 |
| `pipe.c` | Named Pipe 기반 IPC 구현 |
| `shared_memory.c` | Shared Memory 기반 IPC 구현 |
| `Makefile` | 각 IPC 방식별 빌드 자동화 파일 |


## 9. 실행 방법

### 9.1 빌드

```bash
make
```

### 9.2 서버 실행

```bash
./server
```

### 9.3 클라이언트 실행

```bash
./client 1
```

클라이언트 실행 시 인자로 클라이언트 ID를 전달합니다.

예시:

```bash
./client 1
./client 2
./client 3
```

---

## 10. 사용 예시

### 10.1 입금

```bash
명령 입력 (deposit / withdraw / check / exit): deposit
금액 입력: 1000
```

예상 응답:

```text
>>> [IPC 성능] RTT: 0.000541364 sec
[서버 응답] Deposit 1000 success (잔액: 11000)
```

---

### 10.2 출금

```bash
명령 입력 (deposit / withdraw / check / exit): withdraw
금액 입력: 500
```

예상 응답:

```text
>>> [IPC 성능] RTT: 0.000466198 sec
[서버 응답] Withdraw 500 success (잔액: 10500)
```

---

### 10.3 잔액 조회

```bash
명령 입력 (deposit / withdraw / check / exit): check
```

예상 응답:

```text
>>> [IPC 성능] RTT: 0.000223839 sec
[서버 응답] Balance check success (잔액: 10500)
```

---

## 11. 성능 측정 방법

성능 측정은 클라이언트가 서버에 요청을 전송한 시점부터 서버 응답을 수신한 시점까지의 시간을 RTT로 계산했습니다.

```text
RTT = 응답 수신 시간 - 요청 전송 시간
```

측정 대상 명령어는 다음과 같습니다.

- `deposit`
- `withdraw`
- `check`

데이터 크기에 따른 성능 변화를 확인하기 위해 두 가지 조건에서 실험했습니다.

| 실험 | 설명 |
|---|---|
| 소량 데이터 | 기본 Request / Response 구조체 사용 |
| 대량 데이터 | `PAYLOAD_SIZE`를 4096으로 설정하여 4KB 더미 데이터 포함 |

---

## 12. 성능 측정 결과

### 12.1 소량 데이터 평균 RTT

| 구분 | Pipe | Shared Memory | Message Passing |
|---|---:|---:|---:|
| Deposit | 0.000697 sec | 0.000650 sec | 0.000499 sec |
| Withdraw | 0.000614 sec | 0.000626 sec | 0.000481 sec |
| Check | 0.000349 sec | 0.000361 sec | 0.000241 sec |

소량 데이터 기준으로는 **Message Passing**이 가장 빠른 성능을 보였습니다.

특히 파일 I/O 영향이 적은 `check` 명령 기준 성능 순위는 다음과 같습니다.

```text
Message Passing > Named Pipe > Shared Memory
```

---

### 12.2 대량 데이터 평균 RTT

| 구분 | Pipe | Shared Memory | Message Passing |
|---|---:|---:|---:|
| Deposit | 0.000666 sec | 0.000699 sec | 0.000577 sec |
| Withdraw | 0.000617 sec | 0.000636 sec | 0.000542 sec |
| Check | 0.000416 sec | 0.000366 sec | 0.000295 sec |

대량 데이터 기준으로는 **Shared Memory가 Named Pipe보다 빠른 결과**를 보였습니다.

파일 I/O 영향이 적은 `check` 명령 기준 성능 순위는 다음과 같습니다.

```text
Message Passing > Shared Memory > Named Pipe
```

---

## 13. 성능 분석

### 13.1 소량 데이터 분석

소량 데이터에서는 Message Passing이 가장 빠른 성능을 보였습니다.

Message Queue는 커널 내부에서 메시지 큐를 관리하며, 작은 데이터 전달에 최적화되어 있어 낮은 RTT를 기록했습니다.

반면 Shared Memory는 데이터 복사 비용을 줄일 수 있다는 장점이 있지만, 본 실험처럼 작은 데이터를 전송하는 경우에는 Semaphore 동기화 비용이 더 크게 작용했습니다.

즉, 소량 데이터에서는 Shared Memory의 Zero-Copy 장점보다 세마포어 연산에 따른 오버헤드가 더 크게 나타났습니다.

---

### 13.2 대량 데이터 분석

4KB 더미 데이터를 포함한 대량 데이터 실험에서는 Shared Memory가 Named Pipe보다 더 좋은 성능을 보였습니다.

Pipe는 user space와 kernel space 사이에서 데이터 복사가 발생하기 때문에, 데이터 크기가 커질수록 복사 비용이 증가합니다.

반면 Shared Memory는 초기 설정 이후 공유 메모리 영역을 직접 참조하기 때문에 데이터 크기가 커져도 성능 저하폭이 상대적으로 작았습니다.

이를 통해 데이터 크기가 증가할수록 Shared Memory의 장점이 나타난다는 것을 확인했습니다.

---

### 13.3 Message Passing 성능 분석

Message Passing은 소량 데이터와 대량 데이터 모두에서 가장 좋은 성능을 보였습니다.

본 프로젝트의 데이터 크기인 4KB 수준에서는 Message Queue의 커널 내부 처리 비용이 Shared Memory의 Semaphore 동기화 비용보다 낮게 나타났습니다.

하지만 데이터 크기가 더 커질 경우, Shared Memory의 Zero-Copy 구조가 더 유리해질 가능성이 있습니다.

---

### 13.4 파일 I/O 병목 분석

`deposit`과 `withdraw` 명령은 계좌 잔액 변경 후 `accounts_data.txt` 파일 저장을 수행합니다.

반면 `check` 명령은 단순 조회만 수행합니다.

실험 결과 `deposit`, `withdraw`의 RTT가 `check`보다 높게 나타났으며, 이를 통해 IPC 방식뿐 아니라 파일 I/O가 전체 시스템 성능에 큰 영향을 미칠 수 있음을 확인했습니다.

---

## 14. 동시성 제어 검증

동일한 계좌에 대해 두 개의 클라이언트가 동시에 입금을 요청하는 상황을 테스트했습니다.

### 테스트 조건

```text
초기 잔액: 10000
Client A: deposit 1000
Client B: deposit 1000
예상 최종 잔액: 12000
```

서버의 임계 영역에 `sleep(3)`을 삽입하여 첫 번째 요청이 Lock을 점유하는 동안 두 번째 요청이 대기하는지 확인했습니다.

### 테스트 결과

```text
Client A RTT: 약 3.0 sec
Client B RTT: 약 5.5 sec
최종 잔액: 12000
```

두 번째 클라이언트의 RTT가 더 길게 측정된 것은 첫 번째 요청이 Lock을 해제할 때까지 대기했다는 것을 의미합니다.

최종 잔액 또한 12,000원으로 정상 반영되어, Mutex 기반 동시성 제어가 정상적으로 작동함을 확인했습니다.

---

## 15. 핵심 코드 설명

### 15.1 공통 구조체

```c
typedef struct {
    long mtype;
    int client_id;
    char command[16];
    int amount;
} Request;

typedef struct {
    long mtype;
    int status;
    int balance;
    char message[64];
} Response;
```

`Request`는 클라이언트가 서버에 보내는 요청이며, `Response`는 서버가 클라이언트에 보내는 응답입니다.

---

### 15.2 서버 트랜잭션 처리

```c
pthread_mutex_lock(&account_mutex);

if (strcmp(req->command, "deposit") == 0) {
    accounts[id].balance += req->amount;
    data_changed = 1;
} else if (strcmp(req->command, "withdraw") == 0) {
    if (accounts[id].balance >= req->amount) {
        accounts[id].balance -= req->amount;
        data_changed = 1;
    }
}

if (data_changed) {
    save_accounts();
}

res.balance = accounts[id].balance;

pthread_mutex_unlock(&account_mutex);
```

계좌 잔액 변경과 파일 저장을 같은 Mutex 영역 안에서 수행하여 데이터 무결성을 보장했습니다.

---

### 15.3 클라이언트 Condition Variable

```c
pthread_mutex_lock(&req_mutex);

while (!request_ready) {
    pthread_cond_wait(&req_cond, &req_mutex);
}

Request req = shared_request;
request_ready = 0;

pthread_mutex_unlock(&req_mutex);
```

요청이 없을 때 통신 스레드가 대기 상태로 들어가도록 하여 Busy Waiting을 방지했습니다.

---

### 15.4 Message Queue 응답 라우팅

```c
res.mtype = req->client_id + 10;
```

클라이언트는 자신의 ID에 해당하는 메시지 타입만 수신하여, 다른 클라이언트의 응답과 섞이지 않도록 했습니다.

---

## 16. 프로젝트를 통해 배운 점

본 프로젝트를 통해 다음과 같은 내용을 학습했습니다.

- UNIX/Linux 환경에서 다양한 IPC 기법을 직접 구현하는 방법
- Named Pipe, Message Queue, Shared Memory의 구조적 차이
- 멀티스레드 환경에서 Race Condition이 발생하는 원인
- Mutex, Condition Variable, Semaphore의 사용 목적과 차이
- Shared Memory에서 직접 동기화가 필요한 이유
- IPC 성능이 데이터 크기, 동기화 비용, 커널 복사 비용에 따라 달라진다는 점
- 고성능 시스템 설계 시 IPC뿐 아니라 파일 I/O 병목까지 고려해야 한다는 점

---

## 17. 결론

본 프로젝트는 단순한 은행 서버 구현을 넘어, UNIX 시스템 프로그래밍의 핵심 요소인 IPC, 멀티스레드, 동기화, 성능 측정을 종합적으로 다룬 프로젝트입니다.

실험 결과, 작은 데이터에서는 Message Queue가 가장 효율적이었고, 데이터 크기가 증가할수록 Shared Memory의 장점이 나타났습니다.

또한 동일 계좌에 대한 동시 접근 테스트를 통해 Mutex 기반 동시성 제어가 데이터 무결성을 보장한다는 것을 확인했습니다.

결론적으로 IPC 기법은 특정 방식이 항상 우월한 것이 아니라, 데이터 크기, 동기화 비용, 구현 복잡도, 파일 I/O 병목 등을 함께 고려하여 선택해야 한다는 점을 확인할 수 있었습니다.