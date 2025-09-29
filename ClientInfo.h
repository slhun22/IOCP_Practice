#pragma once

#include "Define.h"
#include <MSWSock.h>
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

		//I/O Completion Port��ü�� ������ �����Ų��.
		if (!BindIOCompletionPort())
			return false;

		return BindRecv();
	}

	void Close(bool bIsForce = false) {
		struct linger stLinger = { 0, 0 }; //SO_DONTLINGER�� ����

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

	bool PostAccept(SOCKET listenSock_, const UINT64 curTimeSec_) { //Accept ����
		printf_s("PostAccept. client Index: %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;

		mSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED); //accept()�� ���ϰ����� ����� ������ ������, �񵿱� ����� �ƴϹǷ� �� ClientInfo���� ����� ������ �̸� �����д�.

		if (mSock == INVALID_SOCKET) {
			printf_s("client Socket WSASocket Error : %d\n", GetLastError());
			return false;
		}

		//mAcceptContext�� ��� �����̹Ƿ� �׳� ZeroMemory�� Overlapped����ü �� �ʱ�ȭ�� �ʱ⿡ �� ���� �ص� �� �� ������, �ϴ� ����.
		ZeroMemory(&mAcceptContext, sizeof(stOverlappedEx));

		DWORD bytes = 0;
		DWORD flags = 0;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
		mAcceptContext.SessionIndex = mIndex;

		//���߿� listenSocket�� accept��û�� �޾Ƽ� mSock�� Ŭ���̾�Ʈ�� "!���� �Ϸ��ϸ�!", Overlapped����ü ���·� IOCPť�� ������
		//WSASendó�� �Ϸ��ϰ� �� ���Ŀ� �˷��ִ� �ǹ̷� Overlapped����ü�� ����.
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

		SOCKADDR_IN stClientAddr; //�̰� ���� ��ä���� IP ����� �� ��. GetAcceptExSockaddrs()�� �ּ� �޾ƾ���
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
		printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", clientIP, (int)mSock);

		return true;
	}

	bool BindIOCompletionPort() {
		//socket�� pClientInfo�� CompletionPort ��ü�� �����Ų��.
		auto hIOCP = CreateIoCompletionPort((HANDLE)mSock
			, mIOCPHandle
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

	bool SetSocketOption() { //���� �Ⱦ�
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

	SOCKET mSock; //Client�� ����Ǵ� ����

	stOverlappedEx mAcceptContext;
	char mAcceptBuf[64];

	stOverlappedEx mRecvOverlappedEx; //RECV Overlapped I/O �۾��� ���� ����
	char mRecvBuf[MAX_SOCKBUF]; //������ ����? �̰� ��������� ��� ���Ѱǰ� ����

	mutex mSendLock;
	queue<stOverlappedEx*> mSendDataqueue;
};

