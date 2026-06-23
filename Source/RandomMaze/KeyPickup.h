// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KeyPickup.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USoundBase;

/** 열쇠 획득 시 방송. KeyId로 어느 열쇠인지 구분(나중에 목표지점/인벤토리가 구독). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeyCollected, int32, KeyId);

/**
 * 공중에 떠서 회전하는 열쇠 픽업. 플레이어가 PickupRadius 안으로 들어오면 자동 획득.
 * 메시/사운드는 비워두면 엔진 기본 구(球) + 무음으로 동작(임시 골격). 나중에 진짜 에셋으로 교체.
 */
UCLASS()
class RANDOMMAZE_API AKeyPickup : public AActor
{
	GENERATED_BODY()

public:
	AKeyPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 획득됐을 때 방송(목표지점/카운터가 구독). */
	UPROPERTY(BlueprintAssignable, Category = "Key")
	FOnKeyCollected OnKeyCollected;

	/** 열쇠 메시(비우면 엔진 기본 구). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	TObjectPtr<UStaticMesh> KeyMesh;

	/** 이 반경(cm) 안에 플레이어가 오면 자동 획득. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ClampMin = "1.0"))
	float PickupRadius = 200.f;

	/** 회전 속도(도/초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	float SpinSpeed = 90.f;

	/** 위아래 둥실거림 진폭(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	float BobAmplitude = 20.f;

	/** 둥실거림 속도. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	float BobSpeed = 2.f;

	/** 등장 시 0→1로 커지는 팝("띠링") 시간(초). 0이면 즉시 등장. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ClampMin = "0.0"))
	float AppearPopTime = 0.3f;

	/** 메시 기본 스케일(엔진 구는 100cm라 0.5면 50cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ClampMin = "0.01"))
	float KeyScale = 0.5f;

	/** (선택) 등장 사운드("띠링"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	TObjectPtr<USoundBase> AppearSound;

	/** (선택) 획득 사운드. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	TObjectPtr<USoundBase> PickupSound;

	/** 이 열쇠의 식별 번호(여러 열쇠 구분용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	int32 KeyId = 0;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector BaseLocation = FVector::ZeroVector;
	float Age = 0.f;
	bool bCollected = false;

	void Collect();
};
