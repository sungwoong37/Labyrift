// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "GoalPoint.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** 목표지점이 모두 채워져 클리어됐을 때 방송(피날레 훅). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGoalCleared);

/**
 * 목표지점. 플레이어가 InteractRadius 안에서 InsertKey를 누르면 인벤토리의 열쇠를 하나씩 꽂는다.
 * RequiredKeys개를 다 꽂으면 클리어(OnGoalCleared 방송). 피드백은 화면 디버그 텍스트.
 */
UCLASS()
class RANDOMMAZE_API AGoalPoint : public AActor
{
	GENERATED_BODY()

public:
	AGoalPoint();

	virtual void Tick(float DeltaSeconds) override;

	/** 모두 꽂아 클리어됐을 때 방송. */
	UPROPERTY(BlueprintAssignable, Category = "Goal")
	FOnGoalCleared OnGoalCleared;

	/** 클리어에 필요한 열쇠 수. bAutoRequiredKeys면 미로가 퍼즐 수로 덮어쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal", meta = (ClampMin = "1"))
	int32 RequiredKeys = 1;

	/** 켜면 미로가 이 목표를 '가장 먼 방'에 배치한다(AMazeGenerator::DistributeRoomActors가 읽음). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
	bool bPlaceInMazeRoom = true;

	/** 켜면 미로가 RequiredKeys를 배치된 퍼즐 수로 자동 설정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
	bool bAutoRequiredKeys = true;

	/** 이 반경(cm) 안에서 상호작용 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal", meta = (ClampMin = "1.0"))
	float InteractRadius = 250.f;

	/** 열쇠를 꽂는 입력 키(폴링). 기본 E. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
	FKey InsertKey = EKeys::E;

	/** 목표지점 메시(비우면 엔진 실린더). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
	TObjectPtr<UStaticMesh> GoalMesh;

	/** 꽂았던 열쇠를 인벤토리에 되돌리고 미클리어 상태로 리셋(피날레 잡힘 후 재도전용). */
	void ResetForRetry();

	/** 이미 클리어됐는지. */
	bool IsCleared() const { return bCleared; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Goal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	int32 InsertedKeys = 0;
	bool bCleared = false;

	void InsertOne();
	void Clear();
};
