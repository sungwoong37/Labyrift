// Fill out your copyright notice in the Description page of Project Settings.


#include "FlyDebugSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UFlyDebugSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld())
	{
		return; // 에디터(비플레이) 월드에선 무동작.
	}

	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	APawn* P = PC->GetPawn();
	if (!P)
	{
		return;
	}

	// 상승/하강 키 폴링(Space=위, LeftCtrl=아래).
	float Up = 0.f;
	if (PC->IsInputKeyDown(EKeys::SpaceBar))
	{
		Up += 1.f;
	}
	if (PC->IsInputKeyDown(EKeys::LeftControl))
	{
		Up -= 1.f;
	}

	// 수직 입력은 비행 모드에서만 실제 이동으로 반영된다(걷기 모드에선 무시됨).
	if (Up != 0.f)
	{
		P->AddMovementInput(FVector::UpVector, Up);
	}
}

TStatId UFlyDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlyDebugSubsystem, STATGROUP_Tickables);
}
