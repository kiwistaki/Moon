#pragma once
#include <windows.h>

namespace Moon
{
	class Timer
	{
	public:
		Timer()
			: mSecondsPerCount(0.0), mDeltaTime(-1.0), mBaseTime(0),
			mPausedTime(0), mPrevTime(0), mCurrTime(0), mStopped(false), mStopTime(0)
		{
			__int64 countsPerSec;
			QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
			mSecondsPerCount = 1.0 / (double)countsPerSec;
		}

		float TotalTime()const
		{
			if (mStopped)
			{
				return (float)(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
			}
			else
			{
				return (float)(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
			}
		}

		float DeltaTime()const //In sec
		{
			return (float)mDeltaTime;
		}

		void Reset()
		{
			__int64 currTime;
			QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

			mBaseTime = currTime;
			mPrevTime = currTime;
			mStopTime = 0;
			mStopped = false;
		}

		void Start()
		{
			__int64 startTime;
			QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

			if (mStopped)
			{
				mPausedTime += (startTime - mStopTime);

				mPrevTime = startTime;
				mStopTime = 0;
				mStopped = false;
			}
		}

		void Stop()
		{
			if (!mStopped)
			{
				__int64 currTime;
				QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

				mStopTime = currTime;
				mStopped = true;
			}
		}

		void Tick()
		{
			if (mStopped)
			{
				mDeltaTime = 0.0;
				return;
			}

			__int64 currTime;
			QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
			mCurrTime = currTime;

			mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;
			mPrevTime = mCurrTime;

			if (mDeltaTime < 0.0)
			{
				mDeltaTime = 0.0;
			}
		}

	private:
		double mSecondsPerCount;
		double mDeltaTime;

		__int64 mBaseTime;
		__int64 mPausedTime;
		__int64 mStopTime;
		__int64 mPrevTime;
		__int64 mCurrTime;

		bool mStopped;
	};
}
