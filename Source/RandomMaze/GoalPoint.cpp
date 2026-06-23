// Fill out your copyright notice in the Description page of Project Settings.


#include "GoalPoint.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "KeyInventory.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AGoalPoint::AGoalPoint()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	// 기본 메시 = 엔진 실린더(받침대 느낌). GoalMesh로 교체 가능.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Mesh->SetStaticMesh(Cyl.Object);
	}
}

void AGoalPoint::BeginPlay()
{
	Super::BeginPlay();

	if (GoalMesh)
	{
		Mesh->SetStaticMesh(GoalMesh);
	}
}

void AGoalPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCleared)
	{
		return;
	}

	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	const bool bNear = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) <= InteractRadius;
	if (!bNear)
	{
		if (GEngine)
		{
			GEngine->RemoveOnScreenDebugMessage(101); // 멀어지면 안내 지움.
		}
		return;
	}

	// 보유 열쇠 수 조회.
	int32 Have = 0;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UKeyInventory* Inv = GI->GetSubsystem<UKeyInventory>())
		{
			Have = Inv->GetNumKeys();
		}
	}

	// 화면 안내(매 프레임 같은 키로 갱신; 0초면 안 그려지므로 양수 시간 사용).
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(101, 2.f, FColor::Yellow,
			FString::Printf(TEXT("[%s] 열쇠 꽂기  %d/%d  (보유 %d)"),
				*InsertKey.GetDisplayName().ToString(), InsertedKeys, RequiredKeys, Have));
	}

	// 입력 폴링.
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->WasInputKeyJustPressed(InsertKey))
		{
			InsertOne();
		}
	}
}

void AGoalPoint::InsertOne()
{
	if (bCleared)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UKeyInventory* Inv = GI ? GI->GetSubsystem<UKeyInventory>() : nullptr;
	if (!Inv || !Inv->TryConsumeKey())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red, TEXT("열쇠가 없습니다."));
		}
		return;
	}

	++InsertedKeys;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Green,
			FString::Printf(TEXT("열쇠 꽂음! %d/%d"), InsertedKeys, RequiredKeys));
	}

	if (InsertedKeys >= RequiredKeys)
	{
		Clear();
	}
}

void AGoalPoint::ResetForRetry()
{
	// 꽂았던 열쇠를 인벤토리에 복구(다시 "들고 있는" 상태).
	if (InsertedKeys > 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UKeyInventory* Inv = GI->GetSubsystem<UKeyInventory>())
			{
				Inv->AddKey(InsertedKeys);
			}
		}
	}
	InsertedKeys = 0;
	bCleared = false;
	UE_LOG(LogTemp, Log, TEXT("AGoalPoint: 재도전 리셋(열쇠 복구, 미클리어)."));
}

void AGoalPoint::Clear()
{
	bCleared = true;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan, TEXT("=== 목표 달성! CLEARED ==="));
	}
	OnGoalCleared.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("AGoalPoint: 목표 클리어(열쇠 %d개)."), RequiredKeys);
}
