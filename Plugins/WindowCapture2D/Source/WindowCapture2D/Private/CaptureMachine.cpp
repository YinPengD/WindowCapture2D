// Copyright 2019 ayumax. All Rights Reserved.

#include "CaptureMachine.h"
#include "Engine/Texture2D.h"
#include "Async/Async.h"
#include "Internationalization/Regex.h"
#include "Runtime/Core/Public/HAL/RunnableThread.h"
#include "../Private/Utils/WCWorkerThread.h"

#if PLATFORM_WINDOWS
#include <dwmapi.h>
#endif

UCaptureMachine::UCaptureMachine()
{
}

void UCaptureMachine::Start()
{
#if PLATFORM_WINDOWS
	// 抓取工作进程，调用DoCapture方法
	CaptureWorkerThread = new FWCWorkerThread([this] { return DoCapture(); }, 1.0f / (float)Properties.FrameRate);
	/*创建抓取进程*/
	CaptureThread = FRunnableThread::Create(CaptureWorkerThread, TEXT("UCaptureMachine CaptureThread"));
#endif
}

void UCaptureMachine::Close()
{
#if PLATFORM_WINDOWS

	if (TextureTarget)
	{
		TextureTarget->ReleaseResource();
	}

	if (CaptureThread)
	{
		CaptureThread->Kill(true);
		CaptureThread->WaitForCompletion();

		delete CaptureThread;
		CaptureThread = nullptr;
	}

	if (CaptureWorkerThread)
	{
		delete CaptureWorkerThread;
		CaptureWorkerThread = nullptr;
	}

	if (m_hBmp)
	{
		::DeleteObject(m_hBmp);
		m_BitmapBuffer = nullptr;
	}

	if (m_MemDC)
	{
		::DeleteDC(m_MemDC);
		m_MemDC = nullptr;
	}

	if (m_hOriginalBmp)
	{
		::DeleteObject(m_hOriginalBmp);
		m_hOriginalBmp = nullptr;
	}

	if (m_OriginalMemDC)
	{
		::DeleteDC(m_OriginalMemDC);
		m_OriginalMemDC = nullptr;
	}

#endif

}


bool UCaptureMachine::DoCapture()
{
#if PLATFORM_WINDOWS
	if (!m_TargetWindow) return true;
	if (!TextureTarget) return true;

	if (Properties.CheckWindowSize)  // 如果勾选检查windows尺寸
	{
		FIntVector2D oldWindowSize = m_WindowSize;
		GetWindowSize(m_TargetWindow);
		if (m_WindowSize != oldWindowSize)
		{
			ReCreateTexture();
			ChangeTexture.Broadcast(TextureTarget); //给图片绑定调用，如果图片改变了，就调用
		}

		if (!TextureTarget) return true;
	}


	if (Properties.CutShadow) // 如果勾选阴影
	{
		// 复制指定窗口到目标图像描述HDC句柄
		::PrintWindow(m_TargetWindow, m_OriginalMemDC, 2);
		/* 将指定对象的图像信息拷贝到目标对象上
		m_MemDC:          指定对象句柄
		0：               指定目标矩形区域左上角的X轴逻辑坐标
		0:                指定目标矩形区域左上角的Y轴逻辑坐标。
		m_WindowSize.X:   指定源和目标矩形区域的逻辑宽度
		m_WindowSize.Y:   指定源和目标矩形区域的逻辑高度
		m_OriginalMemDC:  目标对象句柄
		m_WindowOffset.X：指定源矩形区域左上角的X轴逻辑坐标
		m_WindowOffset.Y：指定源矩形区域左上角的Y轴逻辑坐标
		SRCCOPY：         光栅操作，拷贝
		*/
		::BitBlt(m_MemDC, 0, 0, m_WindowSize.X, m_WindowSize.Y, m_OriginalMemDC, m_WindowOffset.X, m_WindowOffset.Y, SRCCOPY);
	}
	else
	{
		// 复制指定窗口到目标图像描述HDC句柄
		::PrintWindow(m_TargetWindow, m_MemDC, 2);
	}

	UpdateTexture();
#endif

	return true;
}

UTexture2D* UCaptureMachine::CreateTexture()
{
#if PLATFORM_WINDOWS
	m_TargetWindow = nullptr;
	/*遍历所有窗口寻找目标窗口*/
	::EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
	{
		UCaptureMachine* my = (UCaptureMachine*)lParam;
		return my->FindTargetWindow(hwnd);
	}, (LPARAM)this);

	if (!m_TargetWindow) return nullptr;
	/*获得窗口尺寸*/
	GetWindowSize(m_TargetWindow);
	/*HDC:窗口的句柄*/
	HDC foundDC = ::GetDC(m_TargetWindow);
	m_MemDC = ::CreateCompatibleDC(foundDC); // 创建一个与显示器兼容的句柄
	if (Properties.CutShadow)
	{
		m_OriginalMemDC = ::CreateCompatibleDC(foundDC);
	}
	/*清除窗口句柄*/
	ReleaseDC(m_TargetWindow, foundDC);
	/*调用重复创建材质*/
	ReCreateTexture();

	return TextureTarget;
#endif
	return nullptr;
}
/*寻找目标窗口*/
bool UCaptureMachine::FindTargetWindow(HWND hWnd)
{
#if PLATFORM_WINDOWS
	__wchar_t windowTitle[1024];
	GetWindowText(hWnd, windowTitle, 1024);
	FString title(windowTitle);

	if (title.IsEmpty()) return true;

	bool isMatch = false;

	switch (Properties.TitleMatchingWindowSearch)
	{
	case ETitleMatchingWindowSearch::PerfectMatch:
		isMatch = title.Equals(Properties.CaptureTargetTitle, ESearchCase::IgnoreCase);
		break;

	case ETitleMatchingWindowSearch::ForwardMatch:
		isMatch = title.StartsWith(Properties.CaptureTargetTitle, ESearchCase::IgnoreCase);
		break;

	case ETitleMatchingWindowSearch::PartialMatch:
		isMatch = title.Contains(Properties.CaptureTargetTitle, ESearchCase::IgnoreCase);
		break;

	case ETitleMatchingWindowSearch::BackwardMatch:
		isMatch = title.EndsWith(Properties.CaptureTargetTitle, ESearchCase::IgnoreCase);
		break;

	case ETitleMatchingWindowSearch::RegularExpression:
	{
		const FRegexPattern pattern = FRegexPattern(Properties.CaptureTargetTitle);
		FRegexMatcher matcher(pattern, title);

		isMatch = matcher.FindNext();
	}
	break;
	}

	if (isMatch)
	{
		m_TargetWindow = hWnd;
		return false;
	}
#endif

	return true;
}

void UCaptureMachine::UpdateTexture()
{
#if PLATFORM_WINDOWS
	if (!TextureTarget) return;

	auto Region = new FUpdateTextureRegion2D(0, 0, 0, 0, TextureTarget->GetSizeX(), TextureTarget->GetSizeY());
	//将windows 的m_BitmapBuffer 数据 转换为 UE4能用的UTexture2D图片
	TextureTarget->UpdateTextureRegions(0, 1, Region, 4 * TextureTarget->GetSizeX(), 4, (uint8*)m_BitmapBuffer);
#endif
}

void UCaptureMachine::GetWindowSize(HWND hWnd)
{
#if PLATFORM_WINDOWS
	if (!::IsWindow(hWnd))
	{
		m_OriginalWindowSize = FIntVector2D(0, 0);
		m_WindowSize = m_OriginalWindowSize;
		m_WindowOffset = FIntVector2D(0, 0);
		return;
	}

	RECT rect;
	::GetWindowRect(hWnd, &rect);

	if (Properties.CutShadow)
	{
		RECT dwmWindowRect;
		::DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwmWindowRect, sizeof(RECT));

		m_OriginalWindowSize = FIntVector2D(rect.right - rect.left, rect.bottom - rect.top);
		m_WindowSize = FIntVector2D(dwmWindowRect.right - dwmWindowRect.left, dwmWindowRect.bottom - dwmWindowRect.top);
		m_WindowOffset = FIntVector2D(dwmWindowRect.left - rect.left, dwmWindowRect.top - rect.top);
	}
	else
	{
		m_OriginalWindowSize = FIntVector2D(rect.right - rect.left, rect.bottom - rect.top);
		m_WindowSize = m_OriginalWindowSize;
		m_WindowOffset = FIntVector2D(0, 0);
	}
#endif
}

void UCaptureMachine::ReCreateTexture()
{
#if PLATFORM_WINDOWS
	if (m_hBmp)
	{
		::DeleteObject(m_hBmp); // 删除位图指针
		m_BitmapBuffer = nullptr;
	}
	/*当目标尺寸为0时，目标为空*/
	if (m_WindowSize.X == 0 || m_WindowSize.Y == 0)
	{
		TextureTarget = nullptr;
		return;
	}
	/*初始化位图缓存大小*/
	m_BitmapBuffer = new char[m_WindowSize.X * m_WindowSize.Y * 4];

	/*初始化材质目标*/
	TextureTarget = UTexture2D::CreateTransient(m_WindowSize.X, m_WindowSize.Y, PF_B8G8R8A8);
	/*刷新窗口*/
	TextureTarget->UpdateResource();

	BITMAPINFO bmpInfo;
	bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmpInfo.bmiHeader.biWidth = m_WindowSize.X;
	bmpInfo.bmiHeader.biHeight = m_WindowSize.Y;
	bmpInfo.bmiHeader.biPlanes = 1;
	bmpInfo.bmiHeader.biBitCount = 32;
	bmpInfo.bmiHeader.biCompression = BI_RGB;
	/*该函数创建应用程序可以直接写入的、与设备无关的位图（DIB）。该函数返回一个位图句柄*/
	m_hBmp = ::CreateDIBSection(NULL, &bmpInfo, DIB_RGB_COLORS, (void**)&m_BitmapBuffer, NULL, 0);
	/*该函数选择一对象到指定的设备上下文环境中，该新对象替换先前的相同类型的对象。*/
	::SelectObject(m_MemDC, m_hBmp);

	if (Properties.CutShadow)
	{
		m_hOriginalBmp = ::CreateCompatibleBitmap(m_MemDC, m_OriginalWindowSize.X, m_OriginalWindowSize.Y);
		::SelectObject(m_OriginalMemDC, m_hOriginalBmp);
	}
#endif
}