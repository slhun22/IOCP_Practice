#pragma once
#pragma comment(lib, "ws2_32")

#include "ClientInfo.h"
#include "Define.h"
#include <thread>
#include <vector>

class IOCPServer {
public:
	IOCPServer(void) {}

	virtual ~IOCPServer(void) {
		//������ ����� ������.
		WSACleanup();
	}

	bool InitSocket() {
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nRet != 0) {
			printf("[����] WSAStartup()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���������� TCP, Overlapped I/O ������ ����
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET) {
			printf("[����] socket()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� �ʱ�ȭ ����\n");
		return true;
	}


	//-------������ �Լ�-------//
	//������ �ּ������� ���ϰ� �����Ű�� ���� ��û�� �ޱ� ����
	//������ ����ϴ� �Լ�
	bool BindandListen(int nBindPort) {
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort); //���� ��Ʈ�� �����Ѵ�.
		//� �ּҿ��� ������ �����̶� �޾Ƶ��̰ڴ�.
		//���� ������� �̷��� �����Ѵ�. ���� �� �����ǿ����� ������ �ް� �ʹٸ�
		//�� �ּҸ� inet_addr�Լ��� �̿��� ������ �ȴ�.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//������ ������ ���� �ּ� ������ cIOCompletionPort ������ �����Ѵ�.
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (nRet != 0) {
			printf("[����] bind()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���� ��û�� �޾Ƶ��̱� ���� cIOCompletionPort������ ����ϰ�
		//���Ӵ��ť�� 5���� �����Ѵ�.
		nRet = listen(mListenSocket, 5);
		if (nRet != 0) {
			printf("[����] listen()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� ��� ����..\n");
		return true;
	}

	//���� ��û�� �����ϰ� �޽����� �޾Ƽ� ó���ϴ� �Լ�
	bool StartServer(const UINT32 maxClientCount) {
		CreateClient(maxClientCount);


		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (mIOCPHandle == NULL) {
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		bool bRet = CreateWorkerThread();
		if (bRet == false) {
			return false;
		}

		bRet = CreateAccepterThread();
		if (bRet == false) {
			return false;
		}

		printf("���� ����\n");
		return true;
	}

	//�����Ǿ��ִ� �����带 �ı��Ѵ�.
	void DestroyThread() {
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);

		for (auto& th : mIOWorkerThreads)
			if (th.joinable())
				th.join();

		//Accepter �����带 �����Ѵ�.
		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
			mAccepterThread.join();
	}

	bool SendMsg(const UINT32 sessionIndex_, const UINT32 dataSize_, char* pData) {
		auto pClient = GetClientInfo(sessionIndex_);
		pClient->SendMsg(dataSize_, pData);
	}


	virtual void OnConnect(const UINT32 clientIdx_) {}
	virtual void OnClose(const UINT32 clientIdx_) {}
	virtual void OnReceive(const UINT32 clientIdx_, const UINT32 size_, char* pData) {}

private:
	void CreateClient(const UINT32 maxClientCount) {
		for (UINT32 i = 0; i < maxClientCount; i++) {
			mClientInfos.emplace_back();
			mClientInfos[i].Init(i);
		}
	}

	//WaitingThread Queue���� ����� ��������� ����
	bool CreateWorkerThread() {
		unsigned int uiThreadId = 0;
		//waitingThread Queue�� ��� ���·� ���� ������� ���� ����Ǵ� ���� : (cpu���� * 2) + 1
		for (int i = 0; i < MAX_WORKERTHREAD; i++) {
			mIOWorkerThreads.emplace_back([this]() { WorkerThread(); });
		}

		printf("WorkerThread ����..\n");
		return true;
	}

	//accept��û�� ó���ϴ� ������ ����
	bool CreateAccepterThread() {
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread ����..\n");
		return true;
	}

	//������� �ʴ� Ŭ���̾�Ʈ ���� ����ü�� ��ȯ�Ѵ�.
	ClientInfo* GetEmptyClientInfo() {
		for (auto& client : mClientInfos) {
			if (client.IsConnected() == false)
				return &client;
		}

		return nullptr;
	}

	ClientInfo* GetClientInfo(const UINT32 sessionIndex) {
		return &mClientInfos[sessionIndex];
	}

	//Overlapped I/O�۾��� ���� �Ϸ� �뺸�� �޾�
	//�׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WorkerThread() {
		//CompletionKey�� ���� ������ ����
		ClientInfo* pClientInfo = NULL;
		//�Լ� ȣ�� ���� ����
		BOOL bSuccess = TRUE;
		//Overlapped I/O�۾����� ���۵� ������ ũ��
		DWORD dwIoSize = 0;
		//I/O�۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun) {
			//////////////////////////////////////////////////////
			//�� �Լ��� ���� ��������� WaitingThread Queue��
			//��� ���·� ���� �ȴ�.
			//�Ϸ�� Overlapped I/O�۾��� �߻��ϸ� IOCP Queue����
			//�Ϸ�� �۾��� ������ �� ó���� �Ѵ�.
			//�׸��� PostQueuedCompletionStatus()�Լ������� �����
			//�޼����� �����Ǹ� �����带 �����Ѵ�.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(mIOCPHandle,
				&dwIoSize,	//������ ���۵� ����Ʈ
				(PULONG_PTR)&pClientInfo, //CompletionKey. ��� Ŭ���̾�Ʈ���� ���� �������ΰ�
				&lpOverlapped, //Overlapped IO ��ü
				INFINITE); //����� �ð�

			//����� ������ ���� �޽��� ó��.. �����δ� PostQueue... ���� �������Ƿ� ���� �ȵ�
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped == NULL) {
				mIsWorkerRun = false;
				continue;
			}

			if (lpOverlapped == NULL) {
				continue;
			}

			//client�� ������ ������ ��..
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE)) {
				//printf("socket(%d) ���� ����\n", (int)pClientInfo->GetSock());
				CloseSocket(pClientInfo);
				continue;
			}


			auto pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			//Overlapped I/O Recv�۾� ��� �� ó��
			if (pOverlappedEx->m_eOperation == IOOperation::RECV) {
				//�� �ּ� �κ��� ���� �κ��� �ƴϹǷ� ���� �и��Ͽ���.
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->RecvBuffer());
				
				pClientInfo->BindRecv();
			}
			//Overlapped I/O Send�۾� ��� �� ó��
			else if (pOverlappedEx->m_eOperation == IOOperation::SEND) {
				delete[] pOverlappedEx->m_wsaBuf.buf;
				delete pOverlappedEx;
				pClientInfo->Sendcompleted(dwIoSize);
			}
			//���� ��Ȳ
			else {
				printf("Client Index(%d)���� ���ܻ�Ȳ\n", (int)pClientInfo->GetIndex());
			}
		}
	}

	//������� ������ �޴� ������
	void AccepterThread() {
		SOCKADDR_IN clientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun) {
			//������ ���� ����ü�� �ε����� ���´�.
			ClientInfo* pClientInfo = GetEmptyClientInfo();
			if (pClientInfo == NULL) {
				printf("[����] Client Full\n");
				return;
			}

			//Ŭ���̾�Ʈ ���� ��û�� ���� ������ ��ٸ���.
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&clientAddr, &nAddrLen);
			if (newSocket == INVALID_SOCKET)
				continue;

			if (pClientInfo->OnConnect(mIOCPHandle, newSocket) == false) {
				pClientInfo->Close(true);
				return;
			}

			//char clientIP[32] = { 0, };
			//inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			//printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->m_socketClient);
			
			OnConnect(pClientInfo->GetIndex());

			//Ŭ���̾�Ʈ ���� ����
			++mClientCnt;
		}
	}

	//������ ������ �����Ų��.
	void CloseSocket(ClientInfo* pClientInfo, bool bIsForce = false) {
		auto clientIndex = pClientInfo->GetIndex();
		pClientInfo->Close(bIsForce);
		OnClose(clientIndex);
	}

	//Ŭ���̾�Ʈ ���� ���� ����ü
	std::vector<ClientInfo> mClientInfos;

	//Ŭ���̾�Ʈ�� ������ �ޱ����� ���� ����
	SOCKET mListenSocket = INVALID_SOCKET;

	//���� �Ǿ��ִ� Ŭ���̾�Ʈ ��
	int mClientCnt = 0;

	//IO Worker ������
	std::vector<std::thread> mIOWorkerThreads;

	//Accept ������
	std::thread	mAccepterThread;

	//CompletionPort��ü �ڵ�
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;

	//�۾� ������ ���� �÷���
	bool mIsWorkerRun = true;

	//���� ������ ���� �÷���
	bool mIsAccepterRun = true;
};
