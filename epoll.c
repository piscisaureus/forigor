
#include <WinSock2.h>
#include <Windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "winsock.h"
#include "winapi.h"

#define AFD_POLL_RECEIVE_BIT            0
#define AFD_POLL_RECEIVE                (1 << AFD_POLL_RECEIVE_BIT)
#define AFD_POLL_RECEIVE_EXPEDITED_BIT  1
#define AFD_POLL_RECEIVE_EXPEDITED      (1 << AFD_POLL_RECEIVE_EXPEDITED_BIT)
#define AFD_POLL_SEND_BIT               2
#define AFD_POLL_SEND                   (1 << AFD_POLL_SEND_BIT)
#define AFD_POLL_DISCONNECT_BIT         3
#define AFD_POLL_DISCONNECT             (1 << AFD_POLL_DISCONNECT_BIT)
#define AFD_POLL_ABORT_BIT              4
#define AFD_POLL_ABORT                  (1 << AFD_POLL_ABORT_BIT)
#define AFD_POLL_LOCAL_CLOSE_BIT        5
#define AFD_POLL_LOCAL_CLOSE            (1 << AFD_POLL_LOCAL_CLOSE_BIT)
#define AFD_POLL_CONNECT_BIT            6
#define AFD_POLL_CONNECT                (1 << AFD_POLL_CONNECT_BIT)
#define AFD_POLL_ACCEPT_BIT             7
#define AFD_POLL_ACCEPT                 (1 << AFD_POLL_ACCEPT_BIT)
#define AFD_POLL_CONNECT_FAIL_BIT       8
#define AFD_POLL_CONNECT_FAIL           (1 << AFD_POLL_CONNECT_FAIL_BIT)
#define AFD_POLL_QOS_BIT                9
#define AFD_POLL_QOS                    (1 << AFD_POLL_QOS_BIT)
#define AFD_POLL_GROUP_QOS_BIT          10
#define AFD_POLL_GROUP_QOS              (1 << AFD_POLL_GROUP_QOS_BIT)

#define AFD_NUM_POLL_EVENTS             11
#define AFD_POLL_ALL                    ((1 << AFD_NUM_POLL_EVENTS) - 1)


typedef struct _AFD_POLL_HANDLE_INFO {
  HANDLE Handle;
  ULONG Events;
  NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;
	 
typedef struct _AFD_POLL_INFO {
  LARGE_INTEGER Timeout;
  ULONG NumberOfHandles;
  ULONG Exclusive;
  AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

sNtDeviceIoControlFile pNtDeviceIoControlFile;

void init_winsock() {
  WORD version = MAKEWORD(2, 2);
  WSADATA wsa_data;
  DWORD r = WSAStartup(version, &wsa_data);
  assert(r == 0);

  {
    HMODULE hm = LoadLibraryA("ntdll.dll");
    assert(hm != NULL);
    pNtDeviceIoControlFile = GetProcAddress(hm, "NtDeviceIoControlFile");
    assert(pNtDeviceIoControlFile != NULL);
  }
}

#define AFD_POLL                    9
#define IOCTL_AFD_POLL                    _AFD_CONTROL_CODE( AFD_POLL, METHOD_BUFFERED )


char fd_read = 0, fd_write = 0;
OVERLAPPED overlapped;
AFD_POLL_INFO info;
HANDLE iocp;

void epoll_ctl(HANDLE iocp, SOCKET sock) {
  IO_STATUS_BLOCK* iosb = (IO_STATUS_BLOCK*) &overlapped.Internal;
  NTSTATUS status;

  info.Timeout.QuadPart = INT64_MAX;
  info.Exclusive = FALSE;
  info.NumberOfHandles = 1;
  info.Handles[0].Handle = (HANDLE) sock;

  info.Handles[0].Events = AFD_POLL_LOCAL_CLOSE;
  if (fd_read) {
    info.Handles[0].Events |= AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT;
  }
  if (fd_write) {
    info.Handles[0].Events |= AFD_POLL_SEND;
  } 



  status = pNtDeviceIoControlFile(
                 (HANDLE) sock,
                 NULL,
                 NULL,                   // APC Routine
                 &overlapped,                   // APC Context
                 iosb,
                 IOCTL_AFD_POLL,
                 &info,
                 sizeof info,
                 &info,
                 sizeof info
                 );

  assert(status == STATUS_SUCCESS || status == STATUS_PENDING);
}


void epoll_wait(HANDLE iocp, char* readable, char* writable) {
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* overlapped;
  DWORD result;

  result = GetQueuedCompletionStatus(iocp, &bytes, &key, &overlapped, INFINITE);
  assert(result != 0);

  assert(info.NumberOfHandles == 1);

  if (info.Handles[0].Events & AFD_POLL_LOCAL_CLOSE) {
    printf("local close. exiting");
    _exit(0);
  }

  *readable = (info.Handles[0].Events & (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT)) != 0;
  *writable = (info.Handles[0].Events & (AFD_POLL_SEND | AFD_POLL_CONNECT | AFD_POLL_CONNECT_FAIL)) != 0;
}

char read_buf[8192];
char write_buf[65536];


void poll_loop(SOCKET sock) {
  char readable;
  char writable;

  fd_read = 1;
  fd_write = 1;
  epoll_ctl(iocp, sock);

  for (;;) {
    int rcnt = (DWORD) ((uint64_t) rand() * sizeof(read_buf) / RAND_MAX);
    int wcnt = (DWORD) ((uint64_t) rand() * sizeof(write_buf) / RAND_MAX);
    int r;

    epoll_wait(iocp, &readable, &writable);

    printf("r: %d, w: %d\n", (int) readable, (int) writable);
    
    if (readable) {
      r = recv(sock, (char*) read_buf, rcnt, 0);
      if (r == rcnt) {
        printf("Read %d, socket buffer not empty\n", r);
      } else if (r > 0) {
        printf("Read %d, socket buffer empty\n", r);
      } else if (r == 0) {
        printf("Read FIN, closing\n", r);
        fd_read = fd_write = 0;
        epoll_ctl(iocp, sock);
        closesocket(sock);
        continue;
      } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        printf("Read 0, WSAEWOULDBLOCK\n");
      } else {
        assert(0);
      }
    }

    if (writable) {
      r = send(sock, (char*) write_buf, rcnt, 0);
      if (r == wcnt) {
        printf("Written %d, socket buffer not full\n", r);
      } else if (r >= 0) {
        printf("Written %d, socket buffer full\n", r);
      } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        printf("Written 0, WSAEWOULDBLOCK\n");
      } else {
        assert(0);
      }
    }
    epoll_ctl(iocp, sock);
  }
}


int main(int argc, char** argv) {
  SOCKET sock;
  int r;
  u_long on = 1;
  struct sockaddr_in addr;
  
  iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  assert(iocp != NULL);

  init_winsock();
  sock = socket(AF_INET, SOCK_STREAM, 0);
  assert(sock != SOCKET_ERROR);

  r = CreateIoCompletionPort((HANDLE) sock, iocp, 0, 1) != NULL;
  assert(r != 0);

  r = ioctlsocket(sock, FIONBIO, &on);
  assert(r == 0);

  addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8000);
  
  r = connect(sock, (const struct sockaddr*) &addr, sizeof addr);
  assert(r == 0 || WSAGetLastError() == WSAEWOULDBLOCK);
  
  poll_loop(sock);  

  return 0;
}