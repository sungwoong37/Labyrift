// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Shakes/LegacyCameraShake.h"
#include "FinaleCameraShake.generated.h"

/**
 * 피날레 타격감용 카메라 흔들림(오실레이션). 벽 구간이 열릴 때(작게)·추격자에게 잡힐 때(크게) 재생.
 * ClientStartCameraShake(StaticClass(), Scale)의 Scale로 강약 조절.
 * (ULegacyCameraShake = 구 UMatineeCameraShake, EngineCameras 플러그인 제공.)
 */
UCLASS()
class RANDOMMAZE_API UFinaleCameraShake : public ULegacyCameraShake
{
	GENERATED_BODY()

public:
	UFinaleCameraShake();
};
