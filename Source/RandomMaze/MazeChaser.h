// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeChaser.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;

/** 추격자가 플레이어를 잡았을 때 방송. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerCaught);

/**
 * 피날레 추격자. 열린 통로의 웨이포인트(경로 셀 월드 좌표)를 따라 플레이어를 쫓는다.
 * 미로가 단일 통로라 NavMesh 없이 웨이포인트 추종으로 공정하게 추격(키네매틱 이동, 충돌 없음).
 * 플레이어와 CatchRadius 안으로 좁혀지면 OnPlayerCaught를 1회 방송한다.
 */
UCLASS()
class RANDOMMAZE_API AMazeChaser : public AActor
{
	GENERATED_BODY()

public:
	AMazeChaser();

	virtual void Tick(float DeltaSeconds) override;

	/** 플레이어를 잡았을 때 방송(MazeGenerator가 구독해 암전·리스폰 처리). */
	UPROPERTY(BlueprintAssignable, Category = "Chaser")
	FOnPlayerCaught OnPlayerCaught;

	/** 추격자 메시(비우면 엔진 구). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaser")
	TObjectPtr<UStaticMesh> ChaserMesh;

	/** 메시 스케일(엔진 구 100cm 기준). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaser", meta = (ClampMin = "0.05"))
	float ChaserScale = 0.9f;

	/** 회전 속도(도/초) — 살아있는 느낌. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaser")
	float SpinSpeed = 120.f;

	/** 웨이포인트/속도/포획반경/활성화 칸수를 받아 준비한다(숨김 대기; 플레이어가 InActivateAfterCells칸 전진하면 자동 등장). */
	void Init(const TArray<FVector>& InWaypoints, float InSpeed, float InCatchRadius, int32 InActivateAfterCells);

	/** 추격자를 등장·활성화한다(보통 Tick이 플레이어 전진을 보고 자동 호출). */
	void Activate();

	/** 첫 웨이포인트로 되돌리고 다시 숨김·비활성 상태로(잡힘 후 재시작용). */
	void ResetToStart();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Chaser")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 다가올수록 강해지는 붉은 빛(모퉁이 너머 예고). */
	UPROPERTY(VisibleAnywhere, Category = "Chaser")
	TObjectPtr<UPointLightComponent> Light;

	TArray<FVector> Waypoints;
	int32 TargetIndex = 0;
	float Speed = 500.f;
	float CatchRadius = 150.f;
	int32 ActivateAfterCells = 3; // 플레이어가 이만큼 경로를 전진하면 등장(시작점 겹침 즉사 방지).
	bool bActive = false;
	bool bCaught = false;

	void ApplyVisual();
};
