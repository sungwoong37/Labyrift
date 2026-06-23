// Fill out your copyright notice in the Description page of Project Settings.


#include "KeyPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "KeyInventory.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AKeyPickup::AKeyPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 통과형 픽업(거리 체크로 획득).

	// 기본 메시 = 엔진 구(球). KeyMesh로 교체 가능.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
	}
}

void AKeyPickup::BeginPlay()
{
	Super::BeginPlay();

	if (KeyMesh)
	{
		Mesh->SetStaticMesh(KeyMesh);
	}

	BaseLocation = GetActorLocation();
	Age = 0.f;

	if (AppearSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, AppearSound, BaseLocation);
	}

	// 팝 인: 0에서 시작해 Tick에서 KeyScale까지 커진다.
	Mesh->SetRelativeScale3D(AppearPopTime > 0.f ? FVector::ZeroVector : FVector(KeyScale));
}

void AKeyPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;

	// 등장 팝 스케일("띠링").
	if (AppearPopTime > 0.f)
	{
		const float P = FMath::Clamp(Age / AppearPopTime, 0.f, 1.f);
		Mesh->SetRelativeScale3D(FVector(KeyScale * P));
	}

	// 둥실 + 회전.
	const float Z = BaseLocation.Z + FMath::Sin(Age * BobSpeed) * BobAmplitude;
	SetActorLocation(FVector(BaseLocation.X, BaseLocation.Y, Z));
	AddActorLocalRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));

	// 근접 자동 획득.
	if (!bCollected)
	{
		if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) <= PickupRadius)
			{
				Collect();
			}
		}
	}
}

void AKeyPickup::Collect()
{
	bCollected = true;

	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	// 인벤토리에 +1.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UKeyInventory* Inv = GI->GetSubsystem<UKeyInventory>())
		{
			Inv->AddKey(1);
		}
	}

	OnKeyCollected.Broadcast(KeyId);
	UE_LOG(LogTemp, Log, TEXT("AKeyPickup: Key %d collected."), KeyId);

	Destroy();
}
