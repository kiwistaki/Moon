#include "dx_utils.h"
#include <mutex>

namespace Moon
{
    class CommandQueue
    {
    public:
        CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType);
        ~CommandQueue();

        bool IsFenceComplete(uint64_t fenceValue);
        void InsertWait(uint64_t fenceValue);
        void InsertWaitForQueueFence(CommandQueue* otherQueue, uint64_t fenceValue);
        void InsertWaitForQueue(CommandQueue* otherQueue);

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

    class CommandQueueManager
    {
    public:
        CommandQueueManager(ID3D12Device* device);
        ~CommandQueueManager();

        CommandQueue* GetGraphicsQueue() { return mGraphicsQueue; }
        CommandQueue* GetComputeQueue() { return mComputeQueue; }
        CommandQueue* GetCopyQueue() { return mCopyQueue; }

        CommandQueue* GetQueue(D3D12_COMMAND_LIST_TYPE commandType);

        bool IsFenceComplete(uint64_t fenceValue);
        void WaitForFenceCPUBlocking(uint64_t fenceValue);
        void WaitForAllIdle();

    private:
        CommandQueue* mGraphicsQueue;
        CommandQueue* mComputeQueue;
        CommandQueue* mCopyQueue;
    };
}