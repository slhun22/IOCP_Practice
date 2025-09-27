#pragma once

#include "Define.h"
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

	void Init(const UINT32 index) {
		mIndex = index;
	}

	UINT32 GetIndex() { return mIndex; }

	bool IsConnected() { return mSock != INVALID_SOCKET; }

	SOCKET GetSock() { return mSock; }

	char* RecvBuffer() { return mRecvBuf; }

	bool OnConnect(HANDLE iocpHandle_, SOCKET socket_) {
		mSock = socket_;

		Clear();

		//I/O Completion Port객체와 소켓을 연결시킨다.
		if (BindIOCompletionPort(iocpHandle_) == false)
			return false;

		return BindRecv();
	}

	void Close(bool bIsForce = false) {
		struct linger stLinger = { 0, 0 }; //SO_DONTLINGER로 설정

		if (bIsForce == true)
			stLinger.l_onoff = 1;

		shutdown(mSock, SD_BOTH);

		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(mSock);
		mSock = INVALID_SOCKET;
	}

	void Clear() {
	}

	bool BindIOCompletionPort(HANDLE iocpHandle_) {
		//socket과 pClientInfo를 CompletionPort 객체와 연결시킨다.
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSock()
			, iocpHandle_
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

	INT32 mIndex = 0;
	SOCKET mSock; //Client와 연결되는 소켓
	stOverlappedEx mRecvOverlappedEx; //RECV Overlapped I/O 작업을 위한 변수

	char mRecvBuf[MAX_SOCKBUF]; //데이터 버퍼? 이건 덮어씌워지기 고려 안한건가 아직

	mutex mSendLock;
	queue<stOverlappedEx*> mSendDataqueue;
};

