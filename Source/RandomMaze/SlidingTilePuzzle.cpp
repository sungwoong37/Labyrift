// Fill out your copyright notice in the Description page of Project Settings.


#include "SlidingTilePuzzle.h"

#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "MazeGenerator.h"
#include "KeyPickup.h"
#include "UObject/ConstructorHelpers.h"

ASlidingTilePuzzle::ASlidingTilePuzzle()
{
	// 매 프레임 조준 트레이스/입력 폴링/슬라이드 보간을 처리해야 하므로 Tick 활성화.
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// 기본 타일 메시 = 엔진 큐브(평평하게 스케일해서 사용). Fab 타일 메시로 교체 가능.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		TileMesh = Cube.Object;
	}

	// 완성 시 스폰할 기본 열쇠 클래스.
	KeyClass = AKeyPickup::StaticClass();
}

void ASlidingTilePuzzle::BeginPlay()
{
	Super::BeginPlay();

	// 방 배치는 미로(AMazeGenerator::DistributeRoomActors)가 중앙에서 담당한다.
	// 타일은 루트에 로컬 부착이라, 미로가 액터를 옮겨도 같이 따라간다.
	BuildTiles();
	if (ShuffleMoves > 0)
	{
		ShufflePuzzle();
	}
}

void ASlidingTilePuzzle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyTiles();
	Super::EndPlay(EndPlayReason);
}

FVector ASlidingTilePuzzle::SlotLocalLocationStep(int32 Slot, float Step) const
{
	const int32 N = GridSize;
	const int32 Col = Slot % N;
	const int32 Row = Slot / N;
	const float Offset = (N - 1) * Step * 0.5f; // 액터 원점이 판 중앙이 되도록.
	return FVector(Col * Step - Offset, Row * Step - Offset, TileThickness * 0.5f);
}

FVector ASlidingTilePuzzle::SlotLocalLocation(int32 Slot) const
{
	return SlotLocalLocationStep(Slot, TileSize + TileGap);
}

UMaterialInterface* ASlidingTilePuzzle::CreateTileMaterial(int32 TileId)
{
	// 그림이 지정돼 있으면 타일마다 다이내믹 인스턴스로 정답 칸의 UV 구역을 지정.
	if (TilePictureMaterial && PictureTexture)
	{
		const int32 N = GridSize;
		const int32 Col = TileId % N;
		const int32 Row = TileId / N;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TilePictureMaterial, this);
		MID->SetTextureParameterValue(TEXT("Picture"), PictureTexture);
		MID->SetScalarParameterValue(TEXT("UVScale"), 1.f / N);
		MID->SetVectorParameterValue(TEXT("UVOffset"),
			FLinearColor(static_cast<float>(Col) / N, static_cast<float>(Row) / N, 0.f, 0.f));
		return MID;
	}
	return TileMaterial; // null일 수 있음(메시 기본 머티리얼 사용).
}

void ASlidingTilePuzzle::BuildTiles()
{
	if (!TileMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("SlidingTilePuzzle: TileMesh가 없어 타일을 만들 수 없습니다."));
		return;
	}

	// 재빌드 안전: 기존 타일이 있으면 먼저 정리.
	if (Tiles.Num() > 0)
	{
		DestroyTiles();
	}

	const int32 N = GridSize;
	const int32 Total = N * N;
	const int32 NumTiles = Total - 1; // 마지막 한 칸은 빈칸.

	Slots.Init(-1, Total);
	TileSlot.Init(INDEX_NONE, NumTiles);
	Tiles.Reset(NumTiles);
	CompToTile.Reset();

	const FVector TileScale(TileSize / 100.f, TileSize / 100.f, TileThickness / 100.f); // 엔진 큐브 100³ 기준.

	for (int32 Slot = 0; Slot < NumTiles; ++Slot)
	{
		const int32 TileId = Slot; // 시작은 정렬 상태(타일ID == 슬롯).
		Slots[Slot] = TileId;
		TileSlot[TileId] = Slot;

		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(TileMesh);
		Comp->SetupAttachment(Root);
		Comp->RegisterComponent(); // 런타임 생성 컴포넌트는 반드시 등록해야 보이고 충돌이 생긴다.
		Comp->SetRelativeLocation(SlotLocalLocation(Slot));
		Comp->SetRelativeScale3D(TileScale);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionProfileName(TEXT("BlockAll")); // 트레이스(Visibility)도 막아 조준이 맞도록.

		// 그림/베이스 머티리얼 지정(그림이 있으면 타일별 UV 구역 다이내믹 인스턴스).
		if (UMaterialInterface* Mat = CreateTileMaterial(TileId))
		{
			Comp->SetMaterial(0, Mat);
		}

		Tiles.Add(Comp);
		CompToTile.Add(Comp, TileId);
	}

	EmptyIndex = Total - 1;
	HighlightedTile = INDEX_NONE;
}

void ASlidingTilePuzzle::DestroyTiles()
{
	for (UStaticMeshComponent* Comp : Tiles)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	if (NinthTile)
	{
		NinthTile->DestroyComponent();
		NinthTile = nullptr;
	}
	Tiles.Reset();
	CompToTile.Reset();
	Slots.Reset();
	TileSlot.Reset();
	HighlightedTile = INDEX_NONE;
	bSliding = false;
	SlidingComp = nullptr;
	SolvePhase = ESolvePhase::None;
	SolvePhaseElapsed = 0.f;
}

int32 ASlidingTilePuzzle::MoveTileLogical(int32 TileId)
{
	const int32 FromSlot = TileSlot[TileId];
	const int32 TargetSlot = EmptyIndex; // 타일이 차지할 칸 = 현재 빈칸.

	Slots[TargetSlot] = TileId;
	Slots[FromSlot] = -1;
	TileSlot[TileId] = TargetSlot;
	EmptyIndex = FromSlot; // 빈칸은 타일이 떠난 자리로 이동.

	return TargetSlot;
}

void ASlidingTilePuzzle::ShufflePuzzle()
{
	// 시드 결정론. 항상 '유효 이동'만 적용하므로 결과 보드는 반드시 풀이 가능.
	FRandomStream Stream(Seed);

	for (int32 Move = 0; Move < ShuffleMoves; ++Move)
	{
		const int32 N = GridSize;
		const int32 EC = EmptyIndex % N;
		const int32 ER = EmptyIndex / N;

		// 빈칸과 직교 인접한 슬롯(=옮길 수 있는 타일이 있는 칸) 수집.
		TArray<int32, TInlineAllocator<4>> Cands;
		if (ER + 1 < N) { Cands.Add(Idx(EC, ER + 1)); }
		if (ER - 1 >= 0) { Cands.Add(Idx(EC, ER - 1)); }
		if (EC + 1 < N) { Cands.Add(Idx(EC + 1, ER)); }
		if (EC - 1 >= 0) { Cands.Add(Idx(EC - 1, ER)); }
		if (Cands.Num() == 0)
		{
			break;
		}

		const int32 PickSlot = Cands[Stream.RandRange(0, Cands.Num() - 1)];
		const int32 TileId = Slots[PickSlot];
		const int32 Landed = MoveTileLogical(TileId);
		if (Tiles.IsValidIndex(TileId) && Tiles[TileId])
		{
			Tiles[TileId]->SetRelativeLocation(SlotLocalLocation(Landed)); // 셔플은 즉시 스냅.
		}
	}
}

bool ASlidingTilePuzzle::TraceForTile(int32& OutTileId, int32& OutSlot) const
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !GetWorld())
	{
		return false;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector End = ViewLoc + ViewRot.Vector() * TraceDistance;

	// 자기 자신(this)은 무시목록에 넣지 않는다 — 타일이 this의 컴포넌트라 같이 무시돼버린다.
	// 대신 플레이어 폰만 무시해 카메라 앞 캡슐에 막히지 않게 한다.
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
	if (const APawn* Pawn = PC->GetPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}

	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params))
	{
		if (const int32* FoundTile = CompToTile.Find(Hit.GetComponent()))
		{
			OutTileId = *FoundTile;
			OutSlot = TileSlot[*FoundTile];
			return true;
		}
	}
	return false;
}

bool ASlidingTilePuzzle::IsSlotAdjacentToEmpty(int32 Slot) const
{
	const int32 N = GridSize;
	const int32 C1 = Slot % N, R1 = Slot / N;
	const int32 C2 = EmptyIndex % N, R2 = EmptyIndex / N;
	return (FMath::Abs(C1 - C2) + FMath::Abs(R1 - R2)) == 1;
}

void ASlidingTilePuzzle::SlideTileToEmpty(int32 TileId, int32 /*FromSlot*/)
{
	if (!Tiles.IsValidIndex(TileId) || !Tiles[TileId])
	{
		return;
	}

	const int32 TargetSlot = MoveTileLogical(TileId); // 논리 모델 즉시 갱신.
	UStaticMeshComponent* Comp = Tiles[TileId];
	const FVector Target = SlotLocalLocation(TargetSlot);

	if (SlideDuration > 0.f)
	{
		// Tick에서 보간.
		bSliding = true;
		SlidingComp = Comp;
		SlideFrom = Comp->GetRelativeLocation();
		SlideTo = Target;
		SlideElapsed = 0.f;
	}
	else
	{
		Comp->SetRelativeLocation(Target);
	}

	if (IsSolved())
	{
		StartSolveSequence();
	}
}

bool ASlidingTilePuzzle::IsSolved() const
{
	for (int32 TileId = 0; TileId < TileSlot.Num(); ++TileId)
	{
		if (TileSlot[TileId] != TileId)
		{
			return false;
		}
	}
	return true;
}

void ASlidingTilePuzzle::UpdateHighlight(int32 DesiredTile)
{
	if (DesiredTile == HighlightedTile)
	{
		return;
	}

	// 커스텀 뎁스 스텐실로 외곽선 표시 — 표면을 덮지 않고 포스트프로세스가 가장자리만 그린다.
	// 이전 하이라이트 해제(안 하면 여러 타일이 동시에 외곽선이 남는다).
	if (HighlightedTile != INDEX_NONE && Tiles.IsValidIndex(HighlightedTile) && Tiles[HighlightedTile])
	{
		Tiles[HighlightedTile]->SetRenderCustomDepth(false);
	}
	if (DesiredTile != INDEX_NONE && Tiles.IsValidIndex(DesiredTile) && Tiles[DesiredTile])
	{
		Tiles[DesiredTile]->SetCustomDepthStencilValue(OutlineStencilValue);
		Tiles[DesiredTile]->SetRenderCustomDepth(true);
	}
	HighlightedTile = DesiredTile;
}

void ASlidingTilePuzzle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 완성 보상 연출 중이면 일반 조준/입력은 멈추고 연출만 진행.
	if (SolvePhase != ESolvePhase::None)
	{
		AdvanceSolveSequence(DeltaSeconds);
		return;
	}

	// 슬라이드 애니메이션 진행.
	if (bSliding && SlidingComp)
	{
		SlideElapsed += DeltaSeconds;
		const float Alpha = (SlideDuration > 0.f) ? FMath::Clamp(SlideElapsed / SlideDuration, 0.f, 1.f) : 1.f;
		SlidingComp->SetRelativeLocation(FMath::Lerp(SlideFrom, SlideTo, Alpha));
		if (Alpha >= 1.f)
		{
			bSliding = false;
			SlidingComp = nullptr;
		}
	}

	// 디버그: 즉시 풀기 — 가장 가까운 퍼즐만 반응.
	if (bEnableSolveCheat)
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->WasInputKeyJustPressed(SolveCheatKey) && IsNearestPuzzleToPlayer())
			{
				SolveInstant();
				return;
			}
		}
	}

	// 조준 트레이스는 프레임당 한 번만.
	int32 HitTile = INDEX_NONE;
	int32 HitSlot = INDEX_NONE;
	const bool bHit = TraceForTile(HitTile, HitSlot);
	const bool bMovable = bHit && IsSlotAdjacentToEmpty(HitSlot);

	UpdateHighlight(bMovable ? HitTile : INDEX_NONE);

	// 입력 폴링(바인딩 테이블 안 거침 → 에셋 0개, 무기 발사 바인딩과 무관).
	if (!bSliding && bMovable)
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->WasInputKeyJustPressed(InteractKey))
			{
				SlideTileToEmpty(HitTile, HitSlot);
			}
		}
	}
}

void ASlidingTilePuzzle::StartSolveSequence()
{
	if (SolvePhase != ESolvePhase::None)
	{
		return;
	}

	// 마지막 슬라이드가 진행 중이면 즉시 끝내 보드를 정돈한 뒤 연출 시작.
	if (bSliding && SlidingComp)
	{
		SlidingComp->SetRelativeLocation(SlideTo);
		bSliding = false;
		SlidingComp = nullptr;
	}

	// 조준 외곽선 정리.
	UpdateHighlight(INDEX_NONE);

	// 빠졌던 9번째(마지막) 조각을 빈칸 위치에, 바닥 아래에서부터 생성.
	if (TileMesh)
	{
		const int32 NinthId = GridSize * GridSize - 1;
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(TileMesh);
		Comp->SetupAttachment(Root);
		Comp->RegisterComponent();
		Comp->SetRelativeScale3D(FVector(TileSize / 100.f, TileSize / 100.f, TileThickness / 100.f));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 연출용.
		if (UMaterialInterface* Mat = CreateTileMaterial(NinthId))
		{
			Comp->SetMaterial(0, Mat);
		}
		const FVector Slot = SlotLocalLocation(EmptyIndex); // 완성 시 빈칸 = 마지막 칸.
		Comp->SetRelativeLocation(FVector(Slot.X, Slot.Y, -150.f)); // 바닥 아래에서 시작.
		NinthTile = Comp;
	}

	SolvePhase = ESolvePhase::Rising;
	SolvePhaseElapsed = 0.f;
	UE_LOG(LogTemp, Log, TEXT("SlidingTilePuzzle: 완성! 보상 연출 시작."));
}

void ASlidingTilePuzzle::AdvanceSolveSequence(float DeltaSeconds)
{
	SolvePhaseElapsed += DeltaSeconds;

	if (SolvePhase == ESolvePhase::Rising)
	{
		const float A = (RiseDuration > 0.f) ? FMath::Clamp(SolvePhaseElapsed / RiseDuration, 0.f, 1.f) : 1.f;
		if (NinthTile)
		{
			const FVector Slot = SlotLocalLocation(EmptyIndex);
			const float Z = FMath::Lerp(-150.f, TileThickness * 0.5f, A);
			NinthTile->SetRelativeLocation(FVector(Slot.X, Slot.Y, Z));
		}
		if (A >= 1.f)
		{
			SolvePhase = ESolvePhase::Merging;
			SolvePhaseElapsed = 0.f;
		}
	}
	else if (SolvePhase == ESolvePhase::Merging)
	{
		const float A = (MergeDuration > 0.f) ? FMath::Clamp(SolvePhaseElapsed / MergeDuration, 0.f, 1.f) : 1.f;
		const float GappedStep = TileSize + TileGap;

		// 모든 타일을 간격 있는 위치(현재) → 간격 0 위치로 보간해 한 그림으로 합친다.
		// 완성 상태라 각 타일은 정답 슬롯(= 타일ID)에 있다.
		for (int32 t = 0; t < Tiles.Num(); ++t)
		{
			if (Tiles[t])
			{
				const FVector From = SlotLocalLocationStep(t, GappedStep);
				const FVector To = SlotLocalLocationStep(t, TileSize);
				Tiles[t]->SetRelativeLocation(FMath::Lerp(From, To, A));
			}
		}
		if (NinthTile)
		{
			const int32 NinthId = GridSize * GridSize - 1;
			const FVector From = SlotLocalLocationStep(NinthId, GappedStep);
			const FVector To = SlotLocalLocationStep(NinthId, TileSize);
			NinthTile->SetRelativeLocation(FMath::Lerp(From, To, A));
		}

		if (A >= 1.f)
		{
			SpawnKey();
			SolvePhase = ESolvePhase::Done;
		}
	}
}

void ASlidingTilePuzzle::SpawnKey()
{
	if (!bSpawnKeyOnSolve || !KeyClass)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Loc = GetActorLocation() + FVector(0.f, 0.f, KeyHoverHeight);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<AActor>(KeyClass, Loc, FRotator::ZeroRotator, SpawnParams);

	UE_LOG(LogTemp, Log, TEXT("SlidingTilePuzzle: 열쇠 스폰."));
}

void ASlidingTilePuzzle::SolveInstant()
{
	if (SolvePhase != ESolvePhase::None || Tiles.Num() == 0)
	{
		return;
	}

	// 모든 타일을 정답 슬롯으로 즉시 정렬.
	const int32 N = GridSize;
	const int32 Total = N * N;
	for (int32 TileId = 0; TileId < Total - 1; ++TileId)
	{
		Slots[TileId] = TileId;
		TileSlot[TileId] = TileId;
		if (Tiles.IsValidIndex(TileId) && Tiles[TileId])
		{
			Tiles[TileId]->SetRelativeLocation(SlotLocalLocation(TileId));
		}
	}
	Slots[Total - 1] = -1;
	EmptyIndex = Total - 1;
	bSliding = false;
	SlidingComp = nullptr;

	StartSolveSequence();
}

bool ASlidingTilePuzzle::IsNearestPuzzleToPlayer() const
{
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn || !GetWorld())
	{
		return true; // 비교 불가하면 그냥 허용.
	}

	const FVector PlayerLoc = Pawn->GetActorLocation();
	const float MyDistSq = FVector::DistSquared(GetActorLocation(), PlayerLoc);

	for (TActorIterator<ASlidingTilePuzzle> It(GetWorld()); It; ++It)
	{
		const ASlidingTilePuzzle* Other = *It;
		if (Other == this)
		{
			continue;
		}
		if (FVector::DistSquared(Other->GetActorLocation(), PlayerLoc) < MyDistSq)
		{
			return false; // 더 가까운 퍼즐이 있음.
		}
	}
	return true;
}
