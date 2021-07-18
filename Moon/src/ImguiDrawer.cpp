#include "mnpch.h"
#include "ImguiDrawer.h"
#include <imgui.h>
#include <examples/imgui_impl_dx12.h>
#include <examples/imgui_impl_dx12.cpp>
#include <examples/imgui_impl_win32.h>
#include <examples/imgui_impl_win32.cpp>

namespace Moon
{
	ImguiDrawer::ImguiDrawer(HWND window, Microsoft::WRL::ComPtr<ID3D12Device8> device, DXGI_FORMAT backbufferFormat)
	{
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
        io.KeyMap[ImGuiKey_Tab] = VK_TAB;
        io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
        io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
        io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
        io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
        io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
        io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
        io.KeyMap[ImGuiKey_Home] = VK_HOME;
        io.KeyMap[ImGuiKey_End] = VK_END;
        io.KeyMap[ImGuiKey_Insert] = VK_INSERT;
        io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
        io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
        io.KeyMap[ImGuiKey_Space] = VK_SPACE;
        io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
        io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
        io.KeyMap[ImGuiKey_KeyPadEnter] = VK_RETURN;
        io.KeyMap[ImGuiKey_A] = 0x41;
        io.KeyMap[ImGuiKey_C] = 0x43;
        io.KeyMap[ImGuiKey_V] = 0x56;
        io.KeyMap[ImGuiKey_X] = 0x58;
        io.KeyMap[ImGuiKey_Y] = 0x59;
        io.KeyMap[ImGuiKey_Z] = 0x5A;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        // Headers
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        // Buttons
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Setup Platform/Renderer bindings
        ImGui_ImplWin32_Init(window);

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mImguiHeap)));
		ImGui_ImplDX12_Init(device.Get(), BACKBUFFER_COUNT, backbufferFormat, mImguiHeap.Get(), mImguiHeap->GetCPUDescriptorHandleForHeapStart(), mImguiHeap->GetGPUDescriptorHandleForHeapStart());
	}

	ImguiDrawer::~ImguiDrawer()
	{
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
	}

    void ImguiDrawer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                //if (ImGui::MenuItem("New")) {}
                //if (ImGui::MenuItem("Open", "Ctrl+O")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Show ImGui Demo", "", &showDemo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void ImguiDrawer::DrawImgui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList, int width, int height)
	{
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);

        //Actual imgui drawing
        {
            if (showDemo)
                ImGui::ShowDemoWindow(&showDemo);

            DrawMenuBar();
        }

        // Rendering
        ImGui::Render();

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault(NULL, (void*)cmdList.Get());
        }

		ID3D12DescriptorHeap* descriptorHeaps[] = { mImguiHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
	}

    void ImguiDrawer::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseButtonPressedEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnMouseButtonPressedEvent));
        dispatcher.Dispatch<MouseButtonReleasedEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnMouseButtonReleasedEvent));
        dispatcher.Dispatch<MouseMovedEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnMouseMovedEvent));
        dispatcher.Dispatch<MouseScrolledEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnMouseScrolledEvent));
        dispatcher.Dispatch<KeyPressedEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnKeyPressedEvent));
        dispatcher.Dispatch<KeyReleasedEvent>(MN_BIND_EVENT_FN(ImguiDrawer::OnKeyReleasedEvent));
    }

    bool ImguiDrawer::OnMouseButtonPressedEvent(MouseButtonPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
        {
            io.MouseDown[e.GetBtnState() - 1] = true;
            return true;
        }
        else
            return false;
    }

    bool ImguiDrawer::OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDown[e.GetBtnState() - 1] = false;

        return false;
    }

    bool ImguiDrawer::OnMouseMovedEvent(MouseMovedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
        {
            io.MousePos = ImVec2((float)e.GetX(), (float)e.GetY());
            return true;
        }
        else
            return false;
    }

    bool ImguiDrawer::OnMouseScrolledEvent(MouseScrolledEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
        {
            io.MouseWheelH += e.GetXOffset() / 120.0f;
            io.MouseWheel += e.GetYOffset() / 120.0f;
            return true;
        }
        return false;
    }

    bool ImguiDrawer::OnKeyPressedEvent(KeyPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.KeysDown[e.GetKey()] = true;

        io.KeyCtrl = GetAsyncKeyState(VK_LCONTROL) & 0x8000;
        io.KeyShift = GetAsyncKeyState(VK_LSHIFT) & 0x8000;
        io.KeyAlt = GetAsyncKeyState(VK_LMENU) & 0x8000;
        io.KeySuper = false;

        if (io.WantTextInput)
        {
            io.AddInputCharacterUTF16((unsigned short)e.GetKey());
            return true;
        }

        return false;
    }

    bool ImguiDrawer::OnKeyReleasedEvent(KeyReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.KeysDown[e.GetKey()] = false;

        return false;
    }
}