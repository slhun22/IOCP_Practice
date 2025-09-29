#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

const UINT32 MAX_SOCKBUF = 256; //��Ŷ ũ��
const UINT32 MAX_SOCK_SENDBUF = 4096; //���� ������ ũ��
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3;

enum class IOOperation {
	ACCEPT,
	RECV,
	SEND
};

//WSAOVERLAPPED����ü�� Ȯ�� ���Ѽ� �ʿ��� ������ �� ���� ��
struct stOverlappedEx {
	WSAOVERLAPPED m_wsaOverlapped;	//Overlapped I/O����ü
	WSABUF m_wsaBuf;	//Overlapped I/O�۾� ����
	IOOperation m_eOperation;	//�۾� ���� ����
	UINT32 SessionIndex = 0; //�񵿱� Accept�� �� ���� ���� Ŭ���̾�Ʈ�� ���� ������ IOCP ��ü�� ����ϱ� ���̹Ƿ� 
							//CompletionKey���� �ƿ� Overlapped����ü�� ���� ������ �����ϰ��� �ߴ��� �˾Ƴ�����.
};