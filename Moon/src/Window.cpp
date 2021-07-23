#include "mnpch.h"
#include "Window.h"
#include "Application.h"

namespace Moon
{
	Moon::Window* gWindow = nullptr;

	Window::Window()
		:mRect{0,0,0,0}
	{
		gWindow = this;

		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = MsgProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = mhAppInst;
		wc.hIcon = (HICON)LoadImage(
			NULL,
			L"../assets/icon/icon.ico",
			IMAGE_ICON,
			0,
			0,
			LR_LOADFROMFILE |
			LR_DEFAULTSIZE |
			LR_SHARED
		);
		wc.hCursor = LoadCursor(0, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		wc.lpszMenuName = 0;
		wc.lpszClassName = L"MainWnd";

		if (!RegisterClassExW(&wc))
		{
			MessageBox(0, L"RegisterClass Failed.", 0, 0);
		}

		int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
		int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

		mRect = { 0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight) };
		::AdjustWindowRect(&mRect, WS_OVERLAPPEDWINDOW, FALSE);

		int windowWidth = mRect.right - mRect.left;
		int windowHeight = mRect.bottom - mRect.top;

		int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
		int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

		mhMainWnd = CreateWindowExW(NULL, L"MainWnd", mMainWndCaption.c_str(),
			WS_OVERLAPPEDWINDOW, windowX, windowY, windowWidth, windowHeight, 0, 0, mhAppInst, 0);
		if (!mhMainWnd)
		{
			MessageBox(0, L"CreateWindow Failed.", 0, 0);
		}

		ShowWindow(mhMainWnd, SW_SHOW);
		UpdateWindow(mhMainWnd);
	}

	Window::~Window()
	{
	}

	LRESULT Window::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto app = Application::GetInstance();

		switch (msg)
		{
		case WM_ACTIVATE:
			if (LOWORD(wParam) == WA_INACTIVE)
			{
				app->PauseApp(true);
				app->PauseTimer(true);
			}
			else
			{
				app->PauseApp(false);
				app->PauseTimer(false);
			}
			return 0;

		case WM_SIZE:
			// Save the new client area dimensions.
			gWindow->mClientWidth = LOWORD(lParam);
			gWindow->mClientHeight = HIWORD(lParam);
			if (app->IsD3D12Initialized())
			{
				if (wParam == SIZE_MINIMIZED)
				{
					app->PauseApp(true);
					gWindow->mMinimized = true;
					gWindow->mMaximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					app->PauseApp(false);
					gWindow->mMinimized = false;
					gWindow->mMaximized = true;
					gWindow->OnResize();
				}
				else if (wParam == SIZE_RESTORED)
				{

					// Restoring from minimized state?
					if (gWindow->mMinimized)
					{
						app->PauseApp(false);
						gWindow->mMinimized = false;
						gWindow->OnResize();
					}

					// Restoring from maximized state?
					else if (gWindow->mMaximized)
					{
						app->PauseApp(false);
						gWindow->mMaximized = false;
						gWindow->OnResize();
					}
					else if (gWindow->mResizing)
					{
					}
					else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						gWindow->OnResize();
					}
				}
			}
			return 0;

		case WM_ENTERSIZEMOVE:
			app->PauseApp(true);
			gWindow->mResizing = true;
			app->PauseTimer(true);
			return 0;

		case WM_EXITSIZEMOVE:
			app->PauseApp(false);
			gWindow->mResizing = false;
			app->PauseTimer(false);
			gWindow->OnResize();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_MENUCHAR:
			// Don't beep when we alt-enter.
			return MAKELRESULT(0, MNC_CLOSE);

		case WM_GETMINMAXINFO:
			// Catch this message so to prevent the window from becoming too small.
			((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
			((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
			return 0;

		case WM_LBUTTONDOWN:
			gWindow->OnMouseDown(MK_LBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MBUTTONDOWN:
			gWindow->OnMouseDown(MK_MBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_RBUTTONDOWN:
			gWindow->OnMouseDown(MK_RBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_LBUTTONUP:
			gWindow->OnMouseUp(MK_LBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MBUTTONUP:
			gWindow->OnMouseUp(MK_MBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_RBUTTONUP:
			gWindow->OnMouseUp(MK_RBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MOUSEMOVE:
			gWindow->OnMouseMove(wParam, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MOUSEWHEEL:
			gWindow->OnMouseScroll(0, GET_WHEEL_DELTA_WPARAM(wParam));
			return 0;
		case WM_MOUSEHWHEEL:
			gWindow->OnMouseScroll(GET_WHEEL_DELTA_WPARAM(wParam), 0);
			return 0;
		case WM_KEYUP:
			if (wParam == VK_ESCAPE)
			{
				PostQuitMessage(0);
			}
			else if ((int)wParam == VK_F11)
			{
				gWindow->SetFullscreen(!gWindow->mFullscreenState);
			}
			else if ((int)wParam == VK_F1)
			{
				app->ToggleWireframeRendering();
			}
			else if ((int)wParam == VK_F2)
			{
				app->ToggleVSync();
			}
			gWindow->OnKeyUp(wParam);
			return 0;
		case WM_KEYDOWN:
			gWindow->OnKeyDown(wParam, 0);
			return 0;
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void Window::SetFullscreen(bool fullscreen)
	{
		if (mFullscreenState != fullscreen)
		{
			mFullscreenState = fullscreen;
			if (mFullscreenState) // Switching to fullscreen.
			{
				// Store the current window dimensions so they can be restored 
				// when switching out of fullscreen state.
				::GetWindowRect(mhMainWnd, &mRect);

				mPreviousClientWidth = mClientWidth;
				mPreviousClientHeight = mClientHeight;

				// Set the window style to a borderless window so the client area fills
				// the entire screen.
				UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
				::SetWindowLongW(mhMainWnd, GWL_STYLE, windowStyle);

				// Query the name of the nearest display device for the window.
				// This is required to set the fullscreen dimensions of the window
				// when using a multi-monitor setup.
				HMONITOR hMonitor = ::MonitorFromWindow(mhMainWnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFOEX monitorInfo = {};
				monitorInfo.cbSize = sizeof(MONITORINFOEX);
				::GetMonitorInfo(hMonitor, &monitorInfo);
				::SetWindowPos(mhMainWnd, HWND_TOPMOST,
					monitorInfo.rcMonitor.left,
					monitorInfo.rcMonitor.top,
					monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
					monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
					SWP_FRAMECHANGED | SWP_NOACTIVATE);
				::ShowWindow(mhMainWnd, SW_MAXIMIZE);

				//Resize Swapchain to match fullscreen
				mClientWidth = monitorInfo.rcMonitor.right;
				mClientHeight = monitorInfo.rcMonitor.bottom;
				OnResize();
			}
			else
			{
				// Restore all the window decorators.
				::SetWindowLong(mhMainWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
				::SetWindowPos(mhMainWnd, HWND_NOTOPMOST,
					mRect.left,
					mRect.top,
					mRect.right - mRect.left,
					mRect.bottom - mRect.top,
					SWP_FRAMECHANGED | SWP_NOACTIVATE);
				::ShowWindow(mhMainWnd, SW_NORMAL);

				//Resize Swapchain to match windowed
				mClientWidth = mPreviousClientWidth;
				mClientHeight = mPreviousClientHeight;
				OnResize();
			}
		}
	}

	void Window::OnMouseDown(WPARAM btnState, int x, int y)
	{
		MouseButtonPressedEvent e(btnState, x, y);
		Application::GetInstance()->OnEvent(e);
	}

	void Window::OnMouseUp(WPARAM btnState, int x, int y)
	{
		MouseButtonReleasedEvent e(btnState, x, y);
		Application::GetInstance()->OnEvent(e);
	}

	void Window::OnMouseMove(WPARAM btnState, int x, int y)
	{
		MouseMovedEvent event(btnState, x, y);
		Application::GetInstance()->OnEvent(event);
	}

	void Window::OnMouseScroll(short x, short y)
	{
		MouseScrolledEvent event((const float)x, (const float)y);
		Application::GetInstance()->OnEvent(event);
	}

	void Window::OnKeyUp(const WPARAM key)
	{
		KeyReleasedEvent event(key);
		Application::GetInstance()->OnEvent(event);
	}

	void Window::OnKeyDown(const WPARAM key, const uint16_t repeatCount)
	{
		KeyPressedEvent event(key, repeatCount);
		Application::GetInstance()->OnEvent(event);
	}

	void Window::OnResize()
	{
		WindowResizeEvent event(mClientWidth, mClientHeight);

		if (mClientWidth == 0 || mClientHeight == 0)
			mMinimized = true;
		else
			mMinimized = false;

		Application::GetInstance()->OnEvent(event);
	}
}