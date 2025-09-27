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
		printf("[OnConnect] 클라이언트: Index(%d)\n", clientIdx_);
	}

	void OnClose(const UINT32 clientIdx_) override {
		printf("[OnClose] 클라이언트: Index(%d)\n", clientIdx_);
	}

	//이 함수는 WorkerThread에서 호출되므로 queue는 멀티스레드 환경에 노출되었기 때문에 반드시 lock을 걸어서 사용한다.
	void OnReceive(const UINT32 clientIdx_, const UINT32 size_, char* pData) override {
		printf("[OnReceive] 클라이언트: Index(%d), dataSize(%d)\n", clientIdx_, size_);

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
				SendMsg(packetData.SessionIndex, packetData.DataSize, packetData.pPacketData); //받은 패킷을 처리하는 코드가 들어가는 자리.
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
	std::deque<PacketData> mPacketDataQueue; //스레드 동기화 필요하므로 mutex사용
};