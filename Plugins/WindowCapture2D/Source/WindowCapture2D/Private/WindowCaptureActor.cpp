// Copyright 2019 ayumax. All Rights Reserved.

#include "WindowCaptureActor.h"
#include "Engine/Texture2D.h"


AWindowCaptureActor::AWindowCaptureActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AWindowCaptureActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CaptureMachine)
	{
		CaptureMachine->Close();
		CaptureMachine = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}
/*会每帧被蓝图调用*/
UTexture2D* AWindowCaptureActor::Start()
{
	if (CaptureMachine)
	{
		CaptureMachine->Close(); // 清除之前的资源
	}
	/*创建UCaptureMachine类*/
	CaptureMachine = NewObject<UCaptureMachine>(this);
	/*初始化属性*/
	CaptureMachine->Properties = Properties;
	/*当对象调用重叠时，指定调用*/
	CaptureMachine->ChangeTexture.AddDynamic(this, &AWindowCaptureActor::OnChangeTexture);
	/*实时抓取对应进程*/
	CaptureMachine->Start();
	/*实时获取图片*/
	return CaptureMachine->CreateTexture();
}

void AWindowCaptureActor::OnChangeTexture(UTexture2D* _NewTexture)
{
	ChangeTexture.Broadcast(_NewTexture);
}