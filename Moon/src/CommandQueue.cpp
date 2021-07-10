#include "mnpch.h"
#include "CommandQueue.h"

namespace Moon
{
    Direct3DQueue::Direct3DQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType)
    {
        mQueueType = commandType;
        mCommandQueue = NULL;
        mFence = NULL;
        mNextFenceValue = ((uint64_t)mQueueType << 56) + 1;
        mLastCompletedFenceValue = ((uint64_t)mQueueType << 56);

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = mQueueType;
        queueDesc.NodeMask = 0;
        device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue));

        DX_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

        mFence->Signal(mLastCompletedFenceValue);

        mFenceEventHandle = CreateEventEx(NULL, false, false, EVENT_ALL_ACCESS);
        //MN_ENGINE_ASSERT(mFenceEventHandle != INVALID_HANDLE_VALUE);
        if(mFenceEventHandle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Fence handle is invalid.");
    }

    Direct3DQueue::~Direct3DQueue()
    {
        CloseHandle(mFenceEventHandle);

        mFence->Release();
        mFence = NULL;

        mCommandQueue->Release();
        mCommandQueue = NULL;
    }

    uint64_t Direct3DQueue::PollCurrentFenceValue()
    {
        mLastCompletedFenceValue = std::max(mLastCompletedFenceValue, mFence->GetCompletedValue());
        return mLastCompletedFenceValue;
    }

    bool Direct3DQueue::IsFenceComplete(uint64_t fenceValue)
    {
        if (fenceValue > mLastCompletedFenceValue)
        {
            PollCurrentFenceValue();
        }

        return fenceValue <= mLastCompletedFenceValue;
    }

    void Direct3DQueue::InsertWait(uint64_t fenceValue)
    {
        mCommandQueue->Wait(mFence, fenceValue);
    }

    void Direct3DQueue::InsertWaitForQueueFence(Direct3DQueue* otherQueue, uint64_t fenceValue)
    {
        mCommandQueue->Wait(otherQueue->GetFence(), fenceValue);
    }

    void Direct3DQueue::InsertWaitForQueue(Direct3DQueue* otherQueue)
    {
        mCommandQueue->Wait(otherQueue->GetFence(), otherQueue->GetNextFenceValue() - 1);
    }

    void Direct3DQueue::WaitForFenceCPUBlocking(uint64_t fenceValue)
    {
        if (IsFenceComplete(fenceValue))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lockGuard(mEventMutex);

            mFence->SetEventOnCompletion(fenceValue, mFenceEventHandle);
            WaitForSingleObjectEx(mFenceEventHandle, INFINITE, false);
            mLastCompletedFenceValue = fenceValue;
        }
    }

    void Direct3DQueue::WaitForIdle()
    {
        DX_CHECK(mCommandQueue->Signal(mFence, mNextFenceValue));
        mNextFenceValue++;
        WaitForFenceCPUBlocking(mNextFenceValue - 1);
    }

    uint64_t Direct3DQueue::ExecuteCommandList(ID3D12CommandList* commandList)
    {
        DX_CHECK(((ID3D12GraphicsCommandList*)commandList)->Close());
        mCommandQueue->ExecuteCommandLists(1, &commandList);

        std::lock_guard<std::mutex> lockGuard(mFenceMutex);

        mCommandQueue->Signal(mFence, mNextFenceValue);

        return mNextFenceValue++;
    }

    Direct3DQueueManager::Direct3DQueueManager(ID3D12Device* device)
    {
        mGraphicsQueue = new Direct3DQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        mComputeQueue = new Direct3DQueue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
        mCopyQueue = new Direct3DQueue(device, D3D12_COMMAND_LIST_TYPE_COPY);
    }

    Direct3DQueueManager::~Direct3DQueueManager()
    {
        delete mGraphicsQueue;
        delete mComputeQueue;
        delete mCopyQueue;
    }

    Direct3DQueue* Direct3DQueueManager::GetQueue(D3D12_COMMAND_LIST_TYPE commandType)
    {
        switch (commandType)
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return mGraphicsQueue;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return mComputeQueue;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return mCopyQueue;
        default:
            throw std::runtime_error("Bad command type lookup in queue manager.");
        }

        return NULL;
    }

    bool Direct3DQueueManager::IsFenceComplete(uint64_t fenceValue)
    {
        return GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56))->IsFenceComplete(fenceValue);
    }

    void Direct3DQueueManager::WaitForFenceCPUBlocking(uint64_t fenceValue)
    {
        Direct3DQueue* commandQueue = GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56));
        commandQueue->WaitForFenceCPUBlocking(fenceValue);
    }

    void Direct3DQueueManager::WaitForAllIdle()
    {
        mGraphicsQueue->WaitForIdle();
        mComputeQueue->WaitForIdle();
        mCopyQueue->WaitForIdle();
    }
}