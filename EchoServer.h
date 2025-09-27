#pragma once
#include "IOCPServer.h"
#include "Packet.h"

#include<vector>
#include<deque>
#include<thread>
#include<mutex>

class EchoServer : public IOCPServer {
public :
	EchoServer() = default;
	virtual ~EchoServer() = default;

	void OnConnect(const UINT32 clientIdx_) override {
		printf("[OnConnect] Ŭ���̾�Ʈ: Index(%d)\n", clientIdx_);
	}

	void OnClose(const UINT32 clientIdx_) override {
		printf("[OnClose] Ŭ���̾�Ʈ: Index(%d)\n", clientIdx_);
	}

	//�� �Լ��� WorkerThread���� ȣ��ǹǷ� queue�� ��Ƽ������ ȯ�濡 ����Ǿ��� ������ �ݵ�� lock�� �ɾ ����Ѵ�.
	void OnReceive(const UINT32 clientIdx_, const UINT32 size_, char* pData) override {
		printf("[OnReceive] Ŭ���̾�Ʈ: Index(%d), dataSize(%d)\n", clientIdx_, size_);

		PacketData packet;
		packet.Set(clientIdx_, size_, pData);
		
		std::lock_guard<std::mutex> guard(mLock);
		mPacketDataQueue.push_back(packet);
	}

	void Run(const UINT32 maxClient) {
		mIsRunProcessThread = true;
		mProcessThread = std::thread([this]() { ProcessPacket(); });

		StartServer(maxClient);
	}

	void End() {
		mIsRunProcessThread = false;
		
		if (mProcessThread.joinable())
			mProcessThread.join();

		DestroyThread();
	}

private:
	void ProcessPacket() {
		while (mIsRunProcessThread) {
			auto packetData = DequePacketData();
			if (packetData.DataSize != 0)
				SendMsg(packetData.SessionIndex, packetData.DataSize, packetData.pPacketData); //���� ��Ŷ�� ó���ϴ� �ڵ尡 ���� �ڸ�.
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	PacketData DequePacketData() {
		PacketData packetData;

		std::lock_guard<std::mutex> guard(mLock);
		if (mPacketDataQueue.empty())
			return PacketData();

		//packetData = mPacketDataQueue.front();
		packetData.Set(mPacketDataQueue.front());

		mPacketDataQueue.front().Release();
		mPacketDataQueue.pop_front();

		return packetData;
	}

	bool mIsRunProcessThread = false;
	std::thread mProcessThread;
	std::mutex mLock;
	std::deque<PacketData> mPacketDataQueue; //������ ����ȭ �ʿ��ϹǷ� mutex���
};