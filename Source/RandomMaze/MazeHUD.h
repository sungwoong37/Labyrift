// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MazeHUD.generated.h"

class UKeyInventory;

/**
 * 화면 HUD. 플레이어가 보유한 열쇠 수를 항상 우상단에 그린다(UKeyInventory에서 폴링).
 * 개수가 바뀌면 잠깐 커졌다 돌아오는 펄스 연출을 준다.
 *
 * UMG(위젯 블루프린트)가 아닌 순수 C++ Canvas 그리기 방식. 유니티 OnGUI에 가깝지만
 * UE에선 플레이어마다 1개 존재하는 화면 그리기 전용 액터다.
 * 사용하려면 GameMode의 HUD Class를 이 클래스로 지정해야 한다.
 */
UCLASS()
class RANDOMMAZE_API AMazeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** 카운터 글자 크기 배율. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	float TextScale = 1.4f;

	/** 화면 가장자리 여백(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	float Margin = 32.f;

	/** 개수 변경 시 펄스(커졌다 복귀) 지속 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	float PulseDuration = 0.35f;

	/** 펄스 최대 추가 배율(0.4면 1.4배까지 커짐). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	float PulseScale = 0.4f;

private:
	UKeyInventory* GetInventory() const;

	int32 CachedKeys = 0;
	bool bInitialized = false;
	float PulseRemaining = 0.f;
};
