// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeExit.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;

/** 플레이어가 출구에 도달(탈출)했을 때 방송. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEscaped);

/**
 * 피날레 탈출 출구. 미로가 재배열되는 동안 먼 곳에 솟는 키 큰 빛기둥(길찾기 비콘)으로,
 * 플레이어가 ReachRadius 안으로 들어오면 탈출 성공(OnEscaped 방송).
 * 보통 AMazeGenerator가 클리어 시 스폰한다. 충돌은 끈다(플레이어가 통과해 닿기만 하면 됨).
 */
UCLASS()
class RANDOMMAZE_API AMazeExit : public AActor
{
	GENERATED_BODY()

public:
	AMazeExit();

	virtual void Tick(float DeltaSeconds) override;

	/** 플레이어가 도달하면 방송. */
	UPROPERTY(BlueprintAssignable, Category = "Exit")
	FOnEscaped OnEscaped;

	/** 이 반경(cm) 안에 플레이어가 오면 탈출 처리. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit", meta = (ClampMin = "1.0"))
	float ReachRadius = 250.f;

	/** 비콘 기둥 높이(cm). 벽(WallHeight)보다 충분히 높게 잡아 멀리서도 보이게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit", meta = (ClampMin = "1.0"))
	float BeaconHeight = 1200.f;

	/** 비콘 기둥 굵기 배율(엔진 실린더 기준 1.0=100cm 지름). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit", meta = (ClampMin = "0.05"))
	float BeaconThickness = 0.4f;

	/** 회전 속도(도/초) — 눈에 띄게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit")
	float SpinSpeed = 60.f;

	/** 비콘 메시(비우면 엔진 실린더). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit")
	TObjectPtr<UStaticMesh> ExitMesh;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Exit")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 멀리서도 보이는 등대 빛(맥동). */
	UPROPERTY(VisibleAnywhere, Category = "Exit")
	TObjectPtr<UPointLightComponent> Light;

	bool bReached = false;
	float Age = 0.f;

	void ApplyBeaconShape();
};
