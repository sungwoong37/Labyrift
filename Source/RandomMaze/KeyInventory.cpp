// Fill out your copyright notice in the Description page of Project Settings.


#include "KeyInventory.h"

void UKeyInventory::AddKey(int32 Count)
{
	if (Count == 0)
	{
		return;
	}
	NumKeys = FMath::Max(0, NumKeys + Count);
	OnKeysChanged.Broadcast(NumKeys);
	UE_LOG(LogTemp, Log, TEXT("KeyInventory: 열쇠 %+d → 총 %d개."), Count, NumKeys);
}

bool UKeyInventory::TryConsumeKey()
{
	if (NumKeys <= 0)
	{
		return false;
	}
	--NumKeys;
	OnKeysChanged.Broadcast(NumKeys);
	return true;
}
