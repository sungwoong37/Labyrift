// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeChaser.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMazeChaser::AMazeChaser()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	// 통로를 부드럽게 따라가도록 충돌 끔(추격은 거리 판정으로).
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		ChaserMesh = Sphere.Object;
	}

	// 붉은 빛(모퉁이 너머로 다가옴을 예고). 비활성 동안은 꺼둔다.
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetLightColor(FLinearColor(1.0f, 0.1f, 0.05f));
	Light->SetAttenuationRadius(2000.f);
	Light->CastShadows = false;
	Light->SetVisibility(false);
}

void AMazeChaser::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisual();
}

void AMazeChaser::ApplyVisual()
{
	if (ChaserMesh)
	{
		Mesh->SetStaticMesh(ChaserMesh);
	}
	Mesh->SetRelativeScale3D(FVector(ChaserScale));
}

void AMazeChaser::Init(const TArray<FVector>& InWaypoints, float InSpeed, float InCatchRadius, int32 InActivateAfterCells)
{
	Waypoints = InWaypoints;
	Speed = InSpeed;
	CatchRadius = InCatchRadius;
	ActivateAfterCells = InActivateAfterCells;
	TargetIndex = 0;
	bActive = false;
	bCaught = false;

	if (Waypoints.Num() > 0)
	{
		SetActorLocation(Waypoints[0]);
	}
	SetActorHiddenInGame(true); // Activate 전까지 숨김 → 키 꽂자마자 안 보이게.
}

void AMazeChaser::Activate()
{
	bActive = true;
	SetActorHiddenInGame(false);
	if (Light)
	{
		Light->SetVisibility(true);
	}
}

void AMazeChaser::ResetToStart()
{
	TargetIndex = 0;
	bActive = false;
	bCaught = false;
	if (Waypoints.Num() > 0)
	{
		SetActorLocation(Waypoints[0]);
	}
	SetActorHiddenInGame(true);
	if (Light)
	{
		Light->SetVisibility(false);
	}
}

namespace
{
	// 점 P에 가장 가까운 웨이포인트의 인덱스(수평 거리 기준).
	int32 NearestWaypointIndex(const TArray<FVector>& W, const FVector& P)
	{
		int32 Best = 0;
		float BestD = TNumericLimits<float>::Max();
		for (int32 i = 0; i < W.Num(); ++i)
		{
			const float D = FVector::DistSquared2D(W[i], P);
			if (D < BestD)
			{
				BestD = D;
				Best = i;
			}
		}
		return Best;
	}
}

void AMazeChaser::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 살아있는 느낌의 회전.
	if (SpinSpeed != 0.f)
	{
		AddActorLocalRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));
	}

	if (bCaught || Waypoints.Num() == 0)
	{
		return;
	}

	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}
	const FVector PlayerLoc = Pawn->GetActorLocation();

	// 플레이어의 통로상 진행 위치(가장 가까운 웨이포인트).
	const int32 PlayerIdx = NearestWaypointIndex(Waypoints, PlayerLoc);

	// 비활성: 플레이어가 충분히(ActivateAfterCells칸) 전진하면 등장. 그 전엔 시작점에 숨어 대기.
	if (!bActive)
	{
		if (PlayerIdx >= ActivateAfterCells)
		{
			Activate(); // 이때 플레이어는 이미 여러 칸 앞 → 시작점의 추격자와 안 겹침 → 즉사 없음.
		}
		else
		{
			return;
		}
	}

	// 통로(웨이포인트)를 한 칸씩 따라가되, 진행 방향을 플레이어 쪽으로 향한다.
	// (벽을 가로질러 직진하지 않음 → 항상 길을 따라 쫓아오는 느낌.)
	const FVector Cur = GetActorLocation();
	const float StepDist = Speed * DeltaSeconds;
	TargetIndex = FMath::Clamp(TargetIndex, 0, Waypoints.Num() - 1);

	if (TargetIndex == PlayerIdx)
	{
		// 바로 근처(같은 셀, 열린 영역) → 플레이어 실제 위치로 곧장 달려들어 잡는다.
		const FVector To = FVector(PlayerLoc.X, PlayerLoc.Y, Cur.Z) - Cur;
		const float D = To.Size();
		if (D > KINDA_SMALL_NUMBER)
		{
			SetActorLocation(Cur + To / D * FMath::Min(StepDist, D));
		}
	}
	else
	{
		// 플레이어 인덱스 쪽으로 통로를 따라 한 웨이포인트씩 이동.
		const FVector WP = Waypoints[TargetIndex];
		const FVector To = WP - Cur;
		const float D = To.Size();
		if (D <= StepDist)
		{
			SetActorLocation(WP);
			TargetIndex += (TargetIndex < PlayerIdx) ? 1 : -1; // 플레이어 쪽으로 진행.
		}
		else
		{
			SetActorLocation(Cur + To / D * StepDist);
		}
	}

	// 붉은 빛: 가까울수록 강하고 빠르게 맥동(모퉁이 너머로 다가옴을 예고).
	const float DistToPlayer = FVector::Dist(GetActorLocation(), PlayerLoc);
	if (Light)
	{
		const float Near = 1.f - FMath::Clamp(DistToPlayer / 2500.f, 0.f, 1.f); // 0(멀)~1(가까움).
		const float PulseHz = 2.f + 6.f * Near;
		const float Pulse = 0.7f + 0.3f * FMath::Sin(GetWorld()->GetTimeSeconds() * PulseHz);
		Light->SetIntensity((4000.f + 30000.f * Near) * Pulse);
	}

	// 플레이어 포획 판정(3D 거리 — 비행으로 위로 뜨면 높이 차로 안 잡힘).
	if (DistToPlayer <= CatchRadius)
	{
		bCaught = true;
		OnPlayerCaught.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("AMazeChaser: 플레이어 포획."));
	}
}
