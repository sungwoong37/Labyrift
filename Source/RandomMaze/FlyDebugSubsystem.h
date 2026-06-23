// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlyDebugSubsystem.generated.h"

/**
 * 테스트용 비행 수직 이동 보조 서브시스템.
 * Fly 치트로 비행 모드일 때 Space=상승 / LeftCtrl=하강 입력을 매 프레임 폴링해 플레이어 폰에 넣는다.
 * 1인칭 템플릿 이동은 수평(actor forward)만 처리해 위아래 입력이 없으므로 이걸로 보완.
 *
 * AddMovementInput의 수직 성분은 걷기 모드에선 무시되고 비행 모드에서만 반영되므로,
 * 평소 플레이엔 영향이 없다(Space는 걷기 땐 점프, 비행 땐 상승으로 자연스럽게 공존).
 *
 * UTickableWorldSubsystem이라 월드마다 자동 생성·매 프레임 Tick — 별도 액터 배치/에셋 불필요.
 */
UCLASS()
class RANDOMMAZE_API UFlyDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// FTickableGameObject 인터페이스.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }
};
