// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeExit.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMazeExit::AMazeExit()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	// 통과 가능한 비콘: 충돌 끔(닿는 판정은 Tick 거리로).
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 기본 메시 = 엔진 실린더(기둥). ExitMesh로 교체 가능.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Mesh->SetStaticMesh(Cyl.Object);
	}

	// 멀리서도 보이는 등대 빛.
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetLightColor(FLinearColor(0.3f, 0.9f, 1.0f)); // 청록빛.
	Light->SetIntensity(20000.f);
	Light->SetAttenuationRadius(3000.f);
	Light->CastShadows = false;
}

void AMazeExit::BeginPlay()
{
	Super::BeginPlay();

	if (ExitMesh)
	{
		Mesh->SetStaticMesh(ExitMesh);
	}
	ApplyBeaconShape();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("탈출 출구(빛기둥)가 나타났다!"));
	}
}

void AMazeExit::ApplyBeaconShape()
{
	// 엔진 실린더는 약 100cm 높이/지름, 피벗 중앙. 높게 세우고 바닥(Z=0)에서 솟게 위로 오프셋.
	const float ZScale = FMath::Max(0.01f, BeaconHeight / 100.f);
	Mesh->SetRelativeScale3D(FVector(BeaconThickness, BeaconThickness, ZScale));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, BeaconHeight * 0.5f));
}

void AMazeExit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;

	// 천천히 회전(눈에 띄게).
	if (SpinSpeed != 0.f)
	{
		AddActorLocalRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));
	}

	// 빛 맥동(등대처럼).
	if (Light)
	{
		Light->SetIntensity(20000.f * (0.75f + 0.25f * FMath::Sin(Age * 3.f)));
	}

	if (bReached)
	{
		return;
	}

	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	// 수평 거리로 판정(기둥이 높아 수직 거리는 무시).
	const FVector A = GetActorLocation();
	const FVector B = Pawn->GetActorLocation();
	const float DistXY = FVector::Dist2D(A, B);
	if (DistXY <= ReachRadius)
	{
		bReached = true;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Green, TEXT("=== 탈출 성공! ESCAPED ==="));
		}
		OnEscaped.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("AMazeExit: 플레이어 탈출 성공."));
	}
}
