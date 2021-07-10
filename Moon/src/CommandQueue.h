#include "dx_utils.h"
#include <mutex>

namespace Moon
{
    class Direct3DQueue
    {
    public:
        Direct3DQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType);
        ~Direct3DQueue();

        bool IsFenceComplete(uint64_t fenceValue);
        void InsertWait(uint64_t fenceValue);
        void InsertWaitForQueueFence(Direct3DQueue* otherQueue, uint64_t fenceValue);
        void InsertWaitForQueue(Direct3DQueue* otherQueue);

        void WaitForFenceCPUBlocking(uint64_t fenceValue);
        void WaitForIdle();

        ID3D12CommandQueue* GetCommandQueue() { return mCommandQueue; }

        uint64_t PollCurrentFenceValue();
        uint64_t GetLastCompletedFence() { return mLastCompletedFenceValue; }
        uint64_t GetNextFenceValue() { return mNextFenceValue; }
        ID3D12Fence* GetFence() { return mFence; }

        uint64_t ExecuteCommandList(ID3D12CommandList* List);

    private:
        ID3D12CommandQueue* mCommandQueue;
        D3D12_COMMAND_LIST_TYPE mQueueType;

        std::mutex mFenceMutex;
        std::mutex mEventMutex;

        ID3D12Fence* mFence;
        uint64_t mNextFenceValue;
        uint64_t mLastCompletedFenceValue;
        HANDLE mFenceEventHandle;
    };

    class Direct3DQueueManager
    {
    public:
        Direct3DQueueManager(ID3D12Device* device);
        ~Direct3DQueueManager();

        Direct3DQueue* GetGraphicsQueue() { return mGraphicsQueue; }
        Direct3DQueue* GetComputeQueue() { return mComputeQueue; }
        Direct3DQueue* GetCopyQueue() { return mCopyQueue; }

        Direct3DQueue* GetQueue(D3D12_COMMAND_LIST_TYPE commandType);

        bool IsFenceComplete(uint64_t fenceValue);
        void WaitForFenceCPUBlocking(uint64_t fenceValue);
        void WaitForAllIdle();

    private:
        Direct3DQueue* mGraphicsQueue;
        Direct3DQueue* mComputeQueue;
        Direct3DQueue* mCopyQueue;
    };
}