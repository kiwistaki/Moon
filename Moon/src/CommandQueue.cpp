#include "mnpch.h"
#include "CommandQueue.h"

namespace Moon
{
    CommandQueue::CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandType)
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

    CommandQueue::~CommandQueue()
    {
        CloseHandle(mFenceEventHandle);

        mFence->Release();
        mFence = NULL;

        mCommandQueue->Release();
        mCommandQueue = NULL;
    }

    uint64_t CommandQueue::PollCurrentFenceValue()
    {
        mLastCompletedFenceValue = std::max(mLastCompletedFenceValue, mFence->GetCompletedValue());
        return mLastCompletedFenceValue;
    }

    bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
    {
        if (fenceValue > mLastCompletedFenceValue)
        {
            PollCurrentFenceValue();
        }

        return fenceValue <= mLastCompletedFenceValue;
    }

    void CommandQueue::InsertWait(uint64_t fenceValue)
    {
        mCommandQueue->Wait(mFence, fenceValue);
    }

    void CommandQueue::InsertWaitForQueueFence(CommandQueue* otherQueue, uint64_t fenceValue)
    {
        mCommandQueue->Wait(otherQueue->GetFence(), fenceValue);
    }

    void CommandQueue::InsertWaitForQueue(CommandQueue* otherQueue)
    {
        mCommandQueue->Wait(otherQueue->GetFence(), otherQueue->GetNextFenceValue() - 1);
    }

    void CommandQueue::WaitForFenceCPUBlocking(uint64_t fenceValue)
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

    void CommandQueue::WaitForIdle()
    {
        DX_CHECK(mCommandQueue->Signal(mFence, mNextFenceValue));
        mNextFenceValue++;
        WaitForFenceCPUBlocking(mNextFenceValue - 1);
    }

    uint64_t CommandQueue::ExecuteCommandList(ID3D12CommandList* commandList)
    {
        DX_CHECK(((ID3D12GraphicsCommandList*)commandList)->Close());
        mCommandQueue->ExecuteCommandLists(1, &commandList);

        std::lock_guard<std::mutex> lockGuard(mFenceMutex);

        mCommandQueue->Signal(mFence, mNextFenceValue);

        return mNextFenceValue++;
    }

    CommandQueueManager::CommandQueueManager(ID3D12Device* device)
    {
        mGraphicsQueue = new CommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        mComputeQueue = new CommandQueue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
        mCopyQueue = new CommandQueue(device, D3D12_COMMAND_LIST_TYPE_COPY);
    }

    CommandQueueManager::~CommandQueueManager()
    {
        delete mGraphicsQueue;
        delete mComputeQueue;
        delete mCopyQueue;
    }

    CommandQueue* CommandQueueManager::GetQueue(D3D12_COMMAND_LIST_TYPE commandType)
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

    bool CommandQueueManager::IsFenceComplete(uint64_t fenceValue)
    {
        return GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56))->IsFenceComplete(fenceValue);
    }

    void CommandQueueManager::WaitForFenceCPUBlocking(uint64_t fenceValue)
    {
        CommandQueue* commandQueue = GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56));
        commandQueue->WaitForFenceCPUBlocking(fenceValue);
    }

    void CommandQueueManager::WaitForAllIdle()
    {
        mGraphicsQueue->WaitForIdle();
        mComputeQueue->WaitForIdle();
        mCopyQueue->WaitForIdle();
    }
}