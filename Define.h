#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

const UINT32 MAX_SOCKBUF = 256; //패킷 크기
const UINT32 MAX_SOCK_SENDBUF = 4096; //소켓 버퍼의 크기
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3;

enum class IOOperation {
	ACCEPT,
	RECV,
	SEND
};

//WSAOVERLAPPED구조체를 확장 시켜서 필요한 정보를 더 넣은 것
struct stOverlappedEx {
	WSAOVERLAPPED m_wsaOverlapped;	//Overlapped I/O구조체
	WSABUF m_wsaBuf;	//Overlapped I/O작업 버퍼
	IOOperation m_eOperation;	//작업 동작 종류
	UINT32 SessionIndex = 0; //비동기 Accept을 할 때는 아직 클라이언트에 대한 소켓을 IOCP 객체에 등록하기 전이므로 
							//CompletionKey말고 아예 Overlapped구조체로 누가 서버와 접속하고자 했는지 알아내야함.
};