#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

using namespace std;

//Ŭ���̾�Ʈ ������ ������� ����ü
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

		//I/O Completion Port��ü�� ������ �����Ų��.
		if (BindIOCompletionPort(iocpHandle_) == false)
			return false;

		return BindRecv();
	}

	void Close(bool bIsForce = false) {
		struct linger stLinger = { 0, 0 }; //SO_DONTLINGER�� ����

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
		//socket�� pClientInfo�� CompletionPort ��ü�� �����Ų��.
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSock()
			, iocpHandle_
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE) {
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	bool BindRecv() {
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0; //�񵿱⿡���� �ǹ̾���

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

		//socket_error�̸� client socket�� ������ �ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING)) {
			printf("[����] WSARecv()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// 1���� �����忡���� ȣ���ؾ� �Ѵ�!
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
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);

		lock_guard<mutex> guard(mSendLock);

		delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		delete mSendDataqueue.front();
		mSendDataqueue.pop();

		if (!mSendDataqueue.empty())
			SendIO();
	}


private:
	//�׻� mutex lock�� ��Ȳ�� ȣ��ǹǷ� ���⼱ mutex �ʿ� ����
	bool SendIO() {
		auto front = mSendDataqueue.front();

		DWORD dwRecvNumBytes = 0; //�񵿱⿡���� �ǹ̾���
		int nRet = WSASend(mSock,
			&(front->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)front,
			NULL);

		//socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSASend()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	INT32 mIndex = 0;
	SOCKET mSock; //Client�� ����Ǵ� ����
	stOverlappedEx mRecvOverlappedEx; //RECV Overlapped I/O �۾��� ���� ����

	char mRecvBuf[MAX_SOCKBUF]; //������ ����? �̰� ��������� ��� ���Ѱǰ� ����

	mutex mSendLock;
	queue<stOverlappedEx*> mSendDataqueue;
};

