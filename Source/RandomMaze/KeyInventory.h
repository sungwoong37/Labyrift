// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KeyInventory.generated.h"

/** 열쇠 개수가 바뀔 때 방송(나중에 UI가 구독). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeysChanged, int32, NewCount);

/**
 * 플레이어가 모은 열쇠 개수를 보관하는 게임 인스턴스 서브시스템.
 * GameInstance 수명 동안 유지(레벨 전환에도 보존, PIE 중지 시 초기화).
 * 어디서든 `GetGameInstance()->GetSubsystem<UKeyInventory>()`로 접근.
 */
UCLASS()
class RANDOMMAZE_API UKeyInventory : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 열쇠 개수가 바뀌면 방송. */
	UPROPERTY(BlueprintAssignable, Category = "Keys")
	FOnKeysChanged OnKeysChanged;

	/** 열쇠를 Count만큼 추가한다. */
	UFUNCTION(BlueprintCallable, Category = "Keys")
	void AddKey(int32 Count = 1);

	/** 열쇠가 1개 이상이면 하나 소모하고 true. 없으면 false. */
	UFUNCTION(BlueprintCallable, Category = "Keys")
	bool TryConsumeKey();

	/** 현재 열쇠 개수. */
	UFUNCTION(BlueprintCallable, Category = "Keys")
	int32 GetNumKeys() const { return NumKeys; }

private:
	int32 NumKeys = 0;
};
