#pragma once

#include "Define.h"
#include <MSWSock.h>
#include <stdio.h>
#include <mutex>
#include <queue>

using namespace std;

//클라이언트 정보를 담기위한 구조체
class ClientInfo {
public:
	ClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		mSock = INVALID_SOCKET;
	}

	~ClientInfo() = default;

	void Init(const UINT32 index, HANDLE iocpHandle_) {
		mIndex = index;
		mIOCPHandle = iocpHandle_;
	}

	UINT32 GetIndex() { return mIndex; }

	bool IsConnected() { return mIsConnect; }

	SOCKET GetSock() { return mSock; }

	UINT64 GetLatestClosedTimeSec() { return mLatestClosedTimeSec; }

	char* RecvBuffer() { return mRecvBuf; }



	bool OnConnect() {
		//mSock = socket_;
		mIsConnect = true;

		Clear();

		//I/O Completion Port객체와 소켓을 연결시킨다.
		if (!BindIOCompletionPort())
			return false;

		return BindRecv();
	}

	void Close(bool bIsForce = false) {
		struct linger stLinger = { 0, 0 }; //SO_DONTLINGER로 설정

		if (bIsForce == true)
			stLinger.l_onoff = 1;

		shutdown(mSock, SD_BOTH);

		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		mIsConnect = false;
		mLatestClosedTimeSec = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now().time_since_epoch()).count();
		
		closesocket(mSock);
		mSock = INVALID_SOCKET;
	}

	void Clear() {
	}

	bool PostAccept(SOCKET listenSock_, const UINT64 curTimeSec_) { //Accept 예약
		printf_s("PostAccept. client Index: %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;

		mSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED); //accept()은 리턴값으로 통신할 소켓을 줬지만, 비동기 방식은 아니므로 각 ClientInfo에서 통신할 소켓을 미리 만들어둔다.

		if (mSock == INVALID_SOCKET) {
			printf_s("client Socket WSASocket Error : %d\n", GetLastError());
			return false;
		}

		//mAcceptContext는 멤버 변수이므로 그냥 ZeroMemory랑 Overlapped구조체 값 초기화는 초기에 한 번만 해도 될 것 같은데, 일단 냅둠.
		ZeroMemory(&mAcceptContext, sizeof(stOverlappedEx));

		DWORD bytes = 0;
		DWORD flags = 0;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
		mAcceptContext.SessionIndex = mIndex;

		//나중에 listenSocket이 accept요청을 받아서 mSock과 클라이언트를 "!연결 완료하면!", Overlapped구조체 형태로 IOCP큐로 갈거임
		//WSASend처럼 완료하고 난 이후에 알려주는 의미로 Overlapped구조체를 쓴다.
		int nRet = AcceptEx(listenSock_, mSock,
			mAcceptBuf,
			0,
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&bytes,
			(LPWSAOVERLAPPED)&(mAcceptContext));

		if (nRet == FALSE && WSAGetLastError() != WSA_IO_PENDING) {
			printf_s("AcceptEx Error : %d\n", GetLastError());
			return false;
		}

		return true;
	}

	bool AcceptCompletion() {
		printf_s("AcceptCompletion : SessionIndex(%d)\n", mIndex);

		if (!OnConnect())
			return false;

		SOCKADDR_IN stClientAddr; //이거 아직 안채워서 IP 제대로 안 들어감. GetAcceptExSockaddrs()로 주소 받아야함
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
		printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)mSock);

		return true;
	}

	bool BindIOCompletionPort() {
		//socket과 pClientInfo를 CompletionPort 객체와 연결시킨다.
		auto hIOCP = CreateIoCompletionPort((HANDLE)mSock
			, mIOCPHandle
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE) {
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	bool BindRecv() {
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0; //비동기에서는 의미없음

		mRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		mRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(mSock,
			&(mRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) &(mRecvOverlappedEx),
			NULL);

		//socket_error이면 client socket이 끊어진 걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING)) {
			printf("[에러] WSARecv()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// 1개의 스레드에서만 호출해야 한다!
	bool SendMsg(const UINT32 dataSize_, char* pMsg_) {
		auto pSendOverlappedEx = new stOverlappedEx;
		ZeroMemory(pSendOverlappedEx, sizeof(stOverlappedEx));
		pSendOverlappedEx->m_eOperation = IOOperation::SEND;
		pSendOverlappedEx->m_wsaBuf.len = dataSize_;
		pSendOverlappedEx->m_wsaBuf.buf = new char[dataSize_];
		CopyMemory(pSendOverlappedEx->m_wsaBuf.buf, pMsg_, dataSize_);

		lock_guard<mutex> guard(mSendLock);

		mSendDataqueue.push(pSendOverlappedEx);
		if (mSendDataqueue.size() == 1)
			SendIO();

		return true;
	}

	void Sendcompleted(const UINT32 dataSize_) {
		printf("[송신 완료] bytes : %d\n", dataSize_);

		lock_guard<mutex> guard(mSendLock);

		delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		delete mSendDataqueue.front();
		mSendDataqueue.pop();

		if (!mSendDataqueue.empty())
			SendIO();
	}


private:
	//항상 mutex lock된 상황에 호출되므로 여기선 mutex 필요 없음
	bool SendIO() {
		auto front = mSendDataqueue.front();

		DWORD dwRecvNumBytes = 0; //비동기에서는 의미없음
		int nRet = WSASend(mSock,
			&(front->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)front,
			NULL);

		//socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool SetSocketOption() { //아직 안씀
		/*if (SOCKET_ERROR == setsockopt(mSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)GIocpManager->GetListenSocket(), sizeof(SOCKET)))
		{
			printf_s("[DEBUG] SO_UPDATE_ACCEPT_CONTEXT error: %d\n", GetLastError());
			return false;
		}*/

		int opt = 1;
		if (setsockopt(mSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int)) == SOCKET_ERROR) {
			printf_s("[DEBUG] TCP_NODELAY error: %d\n", GetLastError());
			return false;
		}

		opt = 0;
		if (setsockopt(mSock, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int)) == SOCKET_ERROR) {
			printf_s("[DEBUG] SO_RCVBUF change error: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	INT32 mIndex = 0;
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;

	bool mIsConnect = 0;
	UINT64 mLatestClosedTimeSec = 0;

	SOCKET mSock; //Client와 연결되는 소켓

	stOverlappedEx mAcceptContext;
	char mAcceptBuf[64];

	stOverlappedEx mRecvOverlappedEx; //RECV Overlapped I/O 작업을 위한 변수
	char mRecvBuf[MAX_SOCKBUF]; //데이터 버퍼? 이건 덮어씌워지기 고려 안한건가 아직

	mutex mSendLock;
	queue<stOverlappedEx*> mSendDataqueue;
};

