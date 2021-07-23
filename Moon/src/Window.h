#pragma once
#include <string>
#include <Windows.h>

namespace Moon
{
	class Window
	{
	public:
		Window();
		~Window();

		HWND GetWindowHandle() const { return mhMainWnd; }
		int GetWidth() const { return mClientWidth; }
		int GetHeight() const { return mClientHeight; }

		static LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		void SetFullscreen(bool fullscreen);
		void OnMouseDown(WPARAM btnState, int x, int y);
		void OnMouseUp(WPARAM btnState, int x, int y);
		void OnMouseMove(WPARAM btnState, int x, int y);
		void OnMouseScroll(short x, short y);
		void OnKeyUp(const WPARAM key);
		void OnKeyDown(const WPARAM key, const uint16_t repeatCount);
		void OnResize();

	private:
		//Win32 stuff
		HINSTANCE		mhAppInst = nullptr; // application instance handle
		HWND			mhMainWnd = nullptr; // main window handle
		RECT			mRect;
		bool			mRunning = true;
		bool			mMinimized = false;
		bool			mAppPaused = false;  // is the application paused?
		bool			mMaximized = false;  // is the application maximized?
		bool			mResizing = false;   // are the resize bars being dragged?
		bool			mFullscreenState = false;// fullscreen enabled
		int				mClientWidth = 1600;
		int				mPreviousClientWidth = 1600;
		int				mClientHeight = 900;
		int				mPreviousClientHeight = 900;
		std::wstring	mMainWndCaption = L"Moon";
	};
}	