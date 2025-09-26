#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>

//클라이언트 정보를 담기위한 구조체
class ClientInfo {
public:
	ClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&mSendOverlappedEx, sizeof(stOverlappedEx));
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
		mSendPos = 0;
		mIsSending = false;
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
		std::lock_guard<std::mutex> guard(mSendLock);

		//야매 링버퍼?
		if ((mSendPos + dataSize_) > MAX_SOCK_SENDBUF)
			mSendPos = 0;

		auto pSendBuf = &mSendBuf[mSendPos];

		//전송될 메시지를 복사
		CopyMemory(pSendBuf, pMsg_, dataSize_);
		mSendPos += dataSize_;

		return true;
	}

	bool SendIO() {
		//전송할 메시지가 버퍼에 없거나, 이미 메시지를 전송 중인 과정이면 바로 종료
		if (mSendPos <= 0 || mIsSending)
			return true;

		std::lock_guard<std::mutex> guard(mSendLock);

		mIsSending = true;

		CopyMemory(mSendingBuf, &mSendBuf[0], mSendPos);

		//Overlapped I/O를 위해 각 정보를 세팅해준다.
		mSendOverlappedEx.m_wsaBuf.len = mSendPos;
		mSendOverlappedEx.m_wsaBuf.buf = &mSendingBuf[0];
		mSendOverlappedEx.m_eOperation = IOOperation::SEND;

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(mSock,
			&(mSendOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED) & (mSendOverlappedEx),
			NULL);

		//socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING)) {
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		mSendPos = 0; //다 보냈으니 버퍼 포인터 맨 앞자리에 위치시키기
		return true;
	}

	void Sendcompleted(const UINT32 dataSize_) {
		mIsSending = false;
		printf("[송신 완료] bytes : %d\n", dataSize_);
	}


private:
	INT32 mIndex = 0;
	SOCKET mSock; //Client와 연결되는 소켓
	stOverlappedEx mRecvOverlappedEx; //RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx mSendOverlappedEx; //SEND Overlapped I/O 작업을 위한 변수

	char mRecvBuf[MAX_SOCKBUF]; //데이터 버퍼? 이건 덮어씌워지기 고려 안한건가 아직

	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0;
	char mSendBuf[MAX_SOCK_SENDBUF]; //전송할 데이터들 쌓는 버퍼
	char mSendingBuf[MAX_SOCK_SENDBUF]; //지금 당장 전송할 예정인 데이터 버퍼
};

