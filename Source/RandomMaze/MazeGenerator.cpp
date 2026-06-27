// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeGenerator.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "FinaleCameraShake.h"
#include "GoalPoint.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/RandomStream.h"
#include "MazeChaser.h"
#include "MazeExit.h"
#include "SlidingTilePuzzle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 각 셀의 4방향 벽 비트마스크.
	constexpr uint8 Wall_North = 1 << 0; // +Y
	constexpr uint8 Wall_East  = 1 << 1; // +X
	constexpr uint8 Wall_South = 1 << 2; // -Y
	constexpr uint8 Wall_West  = 1 << 3; // -X
	constexpr uint8 Wall_All   = Wall_North | Wall_East | Wall_South | Wall_West;
}

AMazeGenerator::AMazeGenerator()
{
	// 평소엔 Tick 불필요. 피날레(길 열기 애니) 동안에만 SetActorTickEnabled로 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	WallISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallISM"));
	RootComponent = WallISM;

	// 캐릭터가 벽에 부딪히도록 충돌 설정.
	WallISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallISM->SetCollisionProfileName(TEXT("BlockAll"));

	// 기본 벽 메시 = 엔진 큐브 (Fab 벽 메시 임포트 전에도 미로가 즉시 보이도록).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		WallStaticMesh = CubeMesh.Object;
	}

	// 바닥 타일 ISM(시프트 모드 휘는 바닥). 충돌은 숨긴 레벨 바닥이 담당 → 여기선 시각만(NoCollision).
	FloorISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FloorISM"));
	FloorISM->SetupAttachment(WallISM);
	FloorISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 기본 바닥 메시 = 엔진 Plane(정점 4개라 타일마다 통째로 기울어 곡면 근사).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		FloorStaticMesh = PlaneMesh.Object;
	}
}

void AMazeGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 데이터 생성 + (에디터면) 안전 한도 내 미리보기 렌더까지 GenerateMaze가 처리한다.
	// 런타임(Play)에서는 BeginPlay 타이머가 플레이어 추적 윈도우로 렌더한다.
	GenerateMaze();
}

void AMazeGenerator::BeginPlay()
{
	Super::BeginPlay();

	// PIE/런타임에서는 에디터 월드가 복제되며 비-UPROPERTY인 Cells가 따라오지 않는다.
	// 여기서 데이터를 다시 생성해 Cells를 채우고(복제된 미리보기 인스턴스도 ClearMaze로 정리됨),
	// 그 뒤 플레이어 주변 윈도우만 렌더한다.
	GenerateMaze();

	// 레벨의 퍼즐/목표 액터를 방에 분배(퍼즐=서로 다른 방, 목표=가장 먼 방).
	DistributeRoomActors();

	if (UWorld* World = GetWorld())
	{
		// 관측-반응 시프트 모드: 어둠 속에서 보지 않는 경로 벽을 실시간 애니로 앞 열고 뒤 닫는다(헤드라인 메커니즘).
		if (bShiftMode)
		{
			// 시프트는 피날레식 개폐 애니를 쓴다 → 인스턴스 인덱스가 안정해야 한다(윈도우 재빌드 금지).
			// 미로 전체를 1회 렌더하며 (X,Y,Side)→인스턴스 인덱스를 기록하고, 윈도우 타이머는 시작하지 않는다.
			// (대형 미로는 전체 렌더 비용↑ — 헤드라인 데모는 걸어다닐 규모라 무방. 윈도우+애니 병행은 Phase 4.)
			bRecordInstanceMap = true;
			BuildWallsInWindow(FIntPoint(GridWidth / 2, GridHeight / 2), FMath::Max(GridWidth, GridHeight));
			LastWindowCenter = FIntPoint(MIN_int32, MIN_int32);

			// 연결성 가드 목표 셀 = 가장 먼 방 중심(없으면 반대편 코너). 경로 staircase의 도착점이기도 하다.
			if (Rooms.Num() > 0)
			{
				const FIntRect& GR = Rooms[GetGoalRoomIndex()];
				ShiftGoalCell = FIntPoint((GR.Min.X + GR.Max.X - 1) / 2, (GR.Min.Y + GR.Max.Y - 1) / 2);
			}
			else
			{
				ShiftGoalCell = FIntPoint(GridWidth - 1, GridHeight - 1);
			}
			bShiftGoalValid = true;
			ShiftStream.Initialize(Seed ^ 0x9E3779B9);
			// 경로 산출은 플레이어 폰이 준비된 첫 ShiftTick에서(InitShiftRun). 타이머만 건다.
			World->GetTimerManager().SetTimer(ShiftTimer, this, &AMazeGenerator::ShiftTick, ShiftInterval, /*bLoop=*/true);
			UE_LOG(LogTemp, Warning, TEXT("AMazeGenerator: ShiftMode ON, timer=%.2fs, goal=(%d,%d)."), ShiftInterval, ShiftGoalCell.X, ShiftGoalCell.Y);
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Green, TEXT("ShiftMode ON (timer started)")); }

			// 월드 커브: 머티리얼 WPO가 읽는 MPC 'Curvature'에 곡률을 주입(휨은 시각만, 충돌/이동은 평면).
			if (bWorldCurve && CurveMPC)
			{
				UKismetMaterialLibrary::SetScalarParameterValue(World, CurveMPC, TEXT("Curvature"), CurveStrength);
				UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: WorldCurve ON, Curvature=%.5f."), CurveStrength);
			}

			// 시프트 안개: 천장 부재(하늘)와 X방향 먼 벽을 안개로 덮어 시야를 손전등 반경으로 가둔다.
			EnsureShiftFog();

			// 휘는 바닥: 셀 타일 ISM을 깔고 레벨 평면 바닥을 숨긴다(커브는 벽과 같은 머티리얼 WPO가 담당, 충돌은 숨긴 바닥 유지).
			if (bShiftCurvedFloor)
			{
				BuildFloorInWindow(FIntPoint(GridWidth / 2, GridHeight / 2), FMath::Max(GridWidth, GridHeight));
				if (FloorActorToHide)
				{
					FloorActorToHide->SetActorHiddenInGame(true);
				}
			}

			// 선택: 목표 셀에 발광 비콘(어둠 속에서도 멀리 휘어 보이는 목표 마커).
			if (bSpawnGoalBeacon && GoalBeaconClass)
			{
				FActorSpawnParameters BParams;
				BParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector BeaconLoc = GetCellCenterWorld(ShiftGoalCell.X, ShiftGoalCell.Y);
				World->SpawnActor<AActor>(GoalBeaconClass, BeaconLoc, GetActorRotation(), BParams);
			}

			return; // 시프트 모드에선 윈도우 타이머/피날레 구독을 쓰지 않는다(인덱스 보존 + 충돌 방지).
		}

		// 일반/피날레 모드: 플레이어 주변만 렌더하는 윈도우를 주기적으로 갱신.
		UpdateRenderWindow();
		World->GetTimerManager().SetTimer(WindowTimer, this, &AMazeGenerator::UpdateRenderWindow, WindowUpdateInterval, /*bLoop=*/true);

		// 피날레: 레벨의 목표지점들이 클리어되면 미로 재배열 + 출구 생성을 시작하도록 구독.
		if (bEnableFinale)
		{
			for (TActorIterator<AGoalPoint> It(World); It; ++It)
			{
				if (AGoalPoint* Goal = *It)
				{
					Goal->OnGoalCleared.AddDynamic(this, &AMazeGenerator::HandleGoalCleared);
				}
			}
		}
	}
}

int32 AMazeGenerator::GetGoalRoomIndex() const
{
	int32 Best = 0;
	double BestDistSq = -1.0;
	for (int32 i = 0; i < Rooms.Num(); ++i)
	{
		const FIntRect& R = Rooms[i];
		// 방 중심 셀(Max 배타적). 시작점(0,0)에서의 거리².
		const double Cx = (R.Min.X + R.Max.X - 1) * 0.5;
		const double Cy = (R.Min.Y + R.Max.Y - 1) * 0.5;
		const double DistSq = Cx * Cx + Cy * Cy;
		if (DistSq > BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

void AMazeGenerator::DistributeRoomActors()
{
	UWorld* World = GetWorld();
	if (!bDistributeRoomActors || Rooms.Num() == 0 || !World)
	{
		return;
	}

	const int32 GoalRoom = GetGoalRoomIndex();
	const FRotator MazeRot = GetActorRotation();

	// 목표지점들 → 가장 먼 방.
	for (TActorIterator<AGoalPoint> It(World); It; ++It)
	{
		AGoalPoint* Goal = *It;
		if (Goal && Goal->bPlaceInMazeRoom)
		{
			Goal->SetActorLocation(GetRoomCenterWorld(GoalRoom));
			Goal->SetActorRotation(MazeRot);
		}
	}

	// 퍼즐들 → 목표 방을 제외한 방에 하나씩.
	int32 RoomCursor = 0;
	int32 PlacedPuzzles = 0;
	for (TActorIterator<ASlidingTilePuzzle> It(World); It; ++It)
	{
		ASlidingTilePuzzle* Puzzle = *It;
		if (!Puzzle || !Puzzle->bPlaceInMazeRoom)
		{
			continue;
		}

		// 목표 방을 건너뛰고 다음 빈 방 찾기.
		while (RoomCursor < Rooms.Num() && RoomCursor == GoalRoom)
		{
			++RoomCursor;
		}
		if (RoomCursor >= Rooms.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("AMazeGenerator: 방이 부족해 일부 퍼즐 미배치(RoomCount를 늘리세요)."));
			break;
		}

		Puzzle->SetActorLocation(GetRoomCenterWorld(RoomCursor));
		Puzzle->SetActorRotation(MazeRot);
		++RoomCursor;
		++PlacedPuzzles;
	}

	// 목표 필요 열쇠 수 = 배치된 퍼즐 수(자동).
	for (TActorIterator<AGoalPoint> It(World); It; ++It)
	{
		AGoalPoint* Goal = *It;
		if (Goal && Goal->bPlaceInMazeRoom && Goal->bAutoRequiredKeys)
		{
			Goal->RequiredKeys = FMath::Max(1, PlacedPuzzles);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: 퍼즐 %d개 배치, 목표 방 %d(총 방 %d)."),
		PlacedPuzzles, GoalRoom, Rooms.Num());
}

void AMazeGenerator::GenerateMaze()
{
	if (!WallISM)
	{
		return;
	}

	if (!WallStaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("AMazeGenerator: WallStaticMesh가 설정되지 않아 미로를 생성할 수 없습니다."));
		return;
	}

	// 오버플로 방지를 위해 셀 수는 int64로 계산하고 한도로 막는다.
	const int64 NumCells = static_cast<int64>(GridWidth) * static_cast<int64>(GridHeight);
	if (NumCells > MaxCells)
	{
		ClearMaze();
		Cells.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("AMazeGenerator: 셀 수 %lld가 한도 %d 초과 — 생성 거부(크래시 방지)."), NumCells, MaxCells);
		return;
	}

	ClearMaze();
	WallISM->SetStaticMesh(WallStaticMesh);

	// 데이터 산출(완벽한 미로) 후 방을 후처리로 뚫는다. 윈도우 중심 캐시는 무효화.
	CarveMaze();
	CarveRooms();
	LastWindowCenter = FIntPoint(MIN_int32, MIN_int32);

	// 에디터(비-게임 월드)에서는 안전 한도 내에서 원점 주변만 미리보기 렌더.
	// 런타임에서는 BeginPlay 타이머의 UpdateRenderWindow가 플레이어 주변을 렌더한다.
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	if (!bGameWorld)
	{
		const int32 PreviewR = FMath::Max(1, static_cast<int32>(FMath::Sqrt(static_cast<double>(EditorPreviewMaxCells)) / 2.0));
		BuildWallsInWindow(FIntPoint(GridWidth / 2, GridHeight / 2), PreviewR);
	}
}

void AMazeGenerator::ClearMaze()
{
	if (WallISM)
	{
		WallISM->ClearInstances();
	}
	if (FloorISM)
	{
		FloorISM->ClearInstances();
	}
}

void AMazeGenerator::CarveMaze()
{
	const int32 NumCells = GridWidth * GridHeight;

	// 시작: 모든 셀의 벽 4개가 닫힌 상태.
	Cells.Init(Wall_All, NumCells);

	// 방문 여부는 비트셀(1bit/셀)로 — 대형 미로 임시 메모리 절감.
	TBitArray<> Visited(false, NumCells);

	// 결정론적 난수: 같은 Seed면 항상 같은 미로.
	FRandomStream Stream(Seed);

	// 재귀 대신 명시적 스택을 사용 (큰 미로에서 스택오버플로 방지).
	// 좌표 대신 셀 인덱스(int32)만 저장해 메모리 절감. 전체 예약은 하지 않는다.
	TArray<int32> CellStack;

	const FIntPoint Start(0, 0);
	Visited[Index(Start.X, Start.Y)] = true;
	CellStack.Push(Index(Start.X, Start.Y));

	// 4방향: (오프셋, 현재셀 벽, 이웃셀 반대벽)
	struct FDir
	{
		int32 DX;
		int32 DY;
		uint8 Wall;
		uint8 OppositeWall;
	};
	static const FDir Dirs[4] =
	{
		{  0,  1, Wall_North, Wall_South },
		{  1,  0, Wall_East,  Wall_West  },
		{  0, -1, Wall_South, Wall_North },
		{ -1,  0, Wall_West,  Wall_East  },
	};

	while (CellStack.Num() > 0)
	{
		const int32 CurrentIdx = CellStack.Top();
		const FIntPoint Current(CurrentIdx % GridWidth, CurrentIdx / GridWidth);

		// 현재 셀의 미방문 이웃 수집.
		TArray<int32, TInlineAllocator<4>> Unvisited;
		for (int32 D = 0; D < 4; ++D)
		{
			const int32 NX = Current.X + Dirs[D].DX;
			const int32 NY = Current.Y + Dirs[D].DY;
			if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight)
			{
				continue;
			}
			if (!Visited[Index(NX, NY)])
			{
				Unvisited.Add(D);
			}
		}

		if (Unvisited.Num() == 0)
		{
			// 막다른 길 → 백트래킹.
			CellStack.Pop();
			continue;
		}

		// 미방문 이웃 중 하나를 무작위 선택해 벽을 뚫는다.
		const int32 Pick = Unvisited[Stream.RandRange(0, Unvisited.Num() - 1)];
		const FDir& Dir = Dirs[Pick];
		const int32 NX = Current.X + Dir.DX;
		const int32 NY = Current.Y + Dir.DY;

		// 두 셀 사이의 벽 제거.
		Cells[Index(Current.X, Current.Y)] &= ~Dir.Wall;
		Cells[Index(NX, NY)] &= ~Dir.OppositeWall;

		Visited[Index(NX, NY)] = true;
		CellStack.Push(Index(NX, NY));
	}
}

void AMazeGenerator::CarveRooms()
{
	Rooms.Reset();
	if (RoomCount <= 0 || Cells.Num() == 0)
	{
		return;
	}

	const int32 MinSize = FMath::Max(2, FMath::Min(RoomMinSize, RoomMaxSize));
	const int32 MaxSize = FMath::Max(MinSize, RoomMaxSize);

	// Seed 파생: 미로 카브 스트림과 분리해 방 배치만 따로 결정론적으로 정한다.
	const uint32 Mixed = static_cast<uint32>(Seed) * 2654435761u + 0x9E3779B9u;
	FRandomStream RoomStream(static_cast<int32>(Mixed));

	const int32 MaxAttempts = RoomCount * 20;
	for (int32 Attempt = 0; Attempt < MaxAttempts && Rooms.Num() < RoomCount; ++Attempt)
	{
		const int32 W = RoomStream.RandRange(MinSize, MaxSize);
		const int32 H = RoomStream.RandRange(MinSize, MaxSize);

		// 테두리에서 1칸 여유. 격자가 작아 안 들어가면 스킵(크래시 없음).
		if (W > GridWidth - 2 || H > GridHeight - 2)
		{
			continue;
		}
		const int32 X0 = RoomStream.RandRange(1, GridWidth - W - 1);
		const int32 Y0 = RoomStream.RandRange(1, GridHeight - H - 1);
		const FIntRect Rect(X0, Y0, X0 + W, Y0 + H); // Max는 배타적.

		// 기존 방과 1칸 마진을 두고 겹치면 버린다.
		bool bOverlaps = false;
		for (const FIntRect& R : Rooms)
		{
			if (Rect.Min.X < R.Max.X + 1 && Rect.Max.X + 1 > R.Min.X &&
				Rect.Min.Y < R.Max.Y + 1 && Rect.Max.Y + 1 > R.Min.Y)
			{
				bOverlaps = true;
				break;
			}
		}
		if (bOverlaps)
		{
			continue;
		}

		Rooms.Add(Rect);
	}

	// 방 내부 벽 열기 — 이미 연결된 미로에 간선만 추가하므로 연결성은 보존된다.
	for (const FIntRect& R : Rooms)
	{
		for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
		{
			for (int32 X = R.Min.X; X < R.Max.X; ++X)
			{
				// 같은 방 안의 동쪽 이웃과의 벽 제거.
				if (X + 1 < R.Max.X)
				{
					Cells[Index(X, Y)]     &= ~Wall_East;
					Cells[Index(X + 1, Y)] &= ~Wall_West;
				}
				// 같은 방 안의 북쪽 이웃과의 벽 제거.
				if (Y + 1 < R.Max.Y)
				{
					Cells[Index(X, Y)]     &= ~Wall_North;
					Cells[Index(X, Y + 1)] &= ~Wall_South;
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: 방 %d개 배치(요청 %d)."), Rooms.Num(), RoomCount);
}

FVector AMazeGenerator::GetRoomCenterWorld(int32 RoomIndex) const
{
	if (!Rooms.IsValidIndex(RoomIndex))
	{
		return GetActorLocation();
	}
	const FIntRect& R = Rooms[RoomIndex];
	// Max는 배타적이라 마지막 셀은 Max-1 → 중심 셀 = (Min + Max - 1) / 2.
	const float Col = (R.Min.X + R.Max.X - 1) * 0.5f;
	const float Row = (R.Min.Y + R.Max.Y - 1) * 0.5f;
	const FVector Local(Col * CellSize, Row * CellSize, 0.f); // Z=0 = 벽이 서는 바닥 평면.
	return GetActorTransform().TransformPosition(Local);
}

FVector AMazeGenerator::GetCellCenterWorld(int32 X, int32 Y) const
{
	const FVector Local(X * CellSize, Y * CellSize, 0.f); // Z=0 = 바닥 평면.
	return GetActorTransform().TransformPosition(Local);
}

void AMazeGenerator::BuildWallsInWindow(FIntPoint Center, int32 Radius)
{
	if (!WallISM || Cells.Num() == 0)
	{
		return;
	}

	const float HalfCell = CellSize * 0.5f;
	const float HalfHeight = WallHeight * 0.5f;

	// 메시 스케일/피벗 보정은 ComputeWallGeom과 한 곳(GetWallMeshScaling)에서 산출 — 동적 생성 벽과 동일.
	FVector WallScale, ScaledBoundsOffset;
	GetWallMeshScaling(WallScale, ScaledBoundsOffset);

	// 길이 축이 +X가 아닌 메시를 위한 사용자 보정 Yaw.
	const float YawOffset = WallMeshYawOffset;

	// 렌더 윈도우 = [Center-R, Center+R] ∩ [0, Grid).
	const int32 MinX = FMath::Clamp(Center.X - Radius, 0, GridWidth - 1);
	const int32 MaxX = FMath::Clamp(Center.X + Radius, 0, GridWidth - 1);
	const int32 MinY = FMath::Clamp(Center.Y - Radius, 0, GridHeight - 1);
	const int32 MaxY = FMath::Clamp(Center.Y + Radius, 0, GridHeight - 1);

	// 기존 인스턴스를 비우고 윈도우 범위만 다시 채운다.
	WallISM->ClearInstances();
	if (bRecordInstanceMap)
	{
		WallInstanceIndex.Reset();
	}

	TArray<FTransform> Transforms;
	const int32 WinW = MaxX - MinX + 1;
	const int32 WinH = MaxY - MinY + 1;
	Transforms.Reserve(WinW * WinH * 2 + WinW + WinH);

	// 벽 하나를 배치: 바운드 중심이 DesiredCenter에 오도록 피벗 오프셋을 역보정한다.
	// BaseYaw 0=길이를 X로(북/남 벽), 90=길이를 Y로 회전(동/서 벽).
	// 피날레 기록 모드면 (X,Y,Side)→인스턴스 인덱스(=Transforms 추가 위치)를 저장.
	auto EmplaceWall = [&](const FVector& DesiredCenter, float BaseYaw, int32 WX, int32 WY, int32 Side)
	{
		if (bRecordInstanceMap)
		{
			WallInstanceIndex.Add(EncodeWallKey(WX, WY, Side), Transforms.Num());
		}
		const FQuat Rot = FRotator(0.f, BaseYaw + YawOffset, 0.f).Quaternion();
		const FVector Loc = DesiredCenter - Rot.RotateVector(ScaledBoundsOffset);
		Transforms.Emplace(Rot, Loc, WallScale);
	};

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const uint8 Walls = Cells[Index(X, Y)];
			const float CX = X * CellSize;
			const float CY = Y * CellSize;

			// 북쪽 벽 (+Y 모서리) — X 방향으로 누운 벽(길이축 그대로, Yaw 0).
			if (Walls & Wall_North)
			{
				EmplaceWall(FVector(CX, CY + HalfCell, HalfHeight), 0.f, X, Y, 0);
			}

			// 동쪽 벽 (+X 모서리) — Y 방향으로 누운 벽(길이축을 90° 회전).
			if (Walls & Wall_East)
			{
				EmplaceWall(FVector(CX + HalfCell, CY, HalfHeight), 90.f, X, Y, 1);
			}

			// 남쪽 테두리 (맨 아래 행에서만) — 인접 셀의 북쪽 벽과 중복 방지.
			if ((Y == 0) && (Walls & Wall_South))
			{
				EmplaceWall(FVector(CX, CY - HalfCell, HalfHeight), 0.f, X, Y, 2);
			}

			// 서쪽 테두리 (맨 왼쪽 열에서만) — 인접 셀의 동쪽 벽과 중복 방지.
			if ((X == 0) && (Walls & Wall_West))
			{
				EmplaceWall(FVector(CX - HalfCell, CY, HalfHeight), 90.f, X, Y, 3);
			}
		}
	}

	// 한 번에 배치 — 대형 미로에서 개별 AddInstance보다 훨씬 빠르다.
	if (Transforms.Num() > 0)
	{
		WallISM->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/false);
	}

	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: %dx%d (%lld cells), 윈도우[%d..%d, %d..%d], %d wall instances."),
		GridWidth, GridHeight, static_cast<int64>(GridWidth) * GridHeight, MinX, MaxX, MinY, MaxY, WallISM->GetInstanceCount());
}

void AMazeGenerator::BuildFloorInWindow(FIntPoint Center, int32 Radius)
{
	if (!FloorISM || !FloorStaticMesh)
	{
		return;
	}
	FloorISM->SetStaticMesh(FloorStaticMesh);
	if (FloorMaterial)
	{
		FloorISM->SetMaterial(0, FloorMaterial);
	}
	FloorISM->ClearInstances();

	// 평면 메시 바운드 → 셀 크기에 맞춘 균일 스케일(Plane 피벗은 중앙이라 셀 중심에 그대로 놓는다).
	const FBoxSphereBounds B = FloorStaticMesh->GetBounds();
	const float MeshX = B.BoxExtent.X * 2.f;
	const float MeshY = B.BoxExtent.Y * 2.f;
	const FVector FloorScale(
		(MeshX > KINDA_SMALL_NUMBER) ? CellSize / MeshX : 1.f,
		(MeshY > KINDA_SMALL_NUMBER) ? CellSize / MeshY : 1.f,
		1.f);

	const int32 MinX = FMath::Clamp(Center.X - Radius, 0, GridWidth - 1);
	const int32 MaxX = FMath::Clamp(Center.X + Radius, 0, GridWidth - 1);
	const int32 MinY = FMath::Clamp(Center.Y - Radius, 0, GridHeight - 1);
	const int32 MaxY = FMath::Clamp(Center.Y + Radius, 0, GridHeight - 1);

	TArray<FTransform> Transforms;
	Transforms.Reserve((MaxX - MinX + 1) * (MaxY - MinY + 1));
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			// 셀 중심, 바닥면 Z=0. 휨은 머티리얼 WPO가 담당(여기선 평면 타일만 배치).
			Transforms.Emplace(FQuat::Identity, FVector(X * CellSize, Y * CellSize, 0.f), FloorScale);
		}
	}
	if (Transforms.Num() > 0)
	{
		FloorISM->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/false);
	}
	UE_LOG(LogTemp, Warning, TEXT("AMazeGenerator: 바닥 타일 %d개 mesh=%s material=%s hide=%s."),
		FloorISM->GetInstanceCount(),
		*FloorStaticMesh->GetName(),
		FloorMaterial ? *FloorMaterial->GetName() : TEXT("(none→메시기본=평평)"),
		FloorActorToHide ? *FloorActorToHide->GetName() : TEXT("(none)"));

	// [FloorDiag] 휨이 안 보이는 원인 격리: 실제 적용된 머티리얼(SetMaterial 반영) + 곡률 주입값(벽과 공유 MPC).
	UMaterialInterface* AppliedMat = FloorISM->GetMaterial(0);
	float CurMpc = -999.f;
	if (CurveMPC) { CurMpc = UKismetMaterialLibrary::GetScalarParameterValue(GetWorld(), CurveMPC, TEXT("Curvature")); }
	UE_LOG(LogTemp, Warning, TEXT("[FloorDiag] appliedMat(slot0)=%s MPC.Curvature=%.5f CurveStrength=%.5f bWorldCurve=%d"),
		AppliedMat ? *AppliedMat->GetName() : TEXT("null"), CurMpc, CurveStrength, bWorldCurve ? 1 : 0);
}

void AMazeGenerator::UpdateRenderWindow()
{
	if (Cells.Num() == 0 || bFinaleActive)
	{
		return; // 피날레 중에는 윈도우 재빌드 금지(애니 중인 인스턴스를 보존).
	}

	// 플레이어 셀을 윈도우 중심으로.
	const FIntPoint Center = GetPlayerCell();
	const int32 CX = Center.X;
	const int32 CY = Center.Y;

	// 히스테리시스: 매 칸마다 재빌드하면 깜빡이므로, 일정 거리(Step) 이상 움직였을 때만 재빌드.
	// 윈도우 반경에 여유가 있어 그 사이엔 가장자리가 비지 않는다.
	// (첫 호출은 LastWindowCenter가 MIN_int32 센티넬 → 단축평가로 오버플로 없이 항상 빌드.)
	const int32 Step = FMath::Max(1, RenderRadiusCells / 4);
	const bool bFirstBuild = (LastWindowCenter.X == MIN_int32);
	if (bFirstBuild ||
		FMath::Max(FMath::Abs(CX - LastWindowCenter.X), FMath::Abs(CY - LastWindowCenter.Y)) >= Step)
	{
		BuildWallsInWindow(Center, RenderRadiusCells);
		LastWindowCenter = Center;
	}
}

FIntPoint AMazeGenerator::GetPlayerCell() const
{
	FVector PlayerWorld = GetActorLocation();
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				PlayerWorld = Pawn->GetActorLocation();
			}
		}
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(PlayerWorld);
	const int32 CX = FMath::Clamp(FMath::RoundToInt(Local.X / CellSize), 0, GridWidth - 1);
	const int32 CY = FMath::Clamp(FMath::RoundToInt(Local.Y / CellSize), 0, GridHeight - 1);
	return FIntPoint(CX, CY);
}

bool AMazeGenerator::IsPointObserved(const FVector& WorldPoint, const FVector& CamLoc, const FVector& CamFwd) const
{
	const FVector To = WorldPoint - CamLoc;
	const float DistSq = static_cast<float>(To.SizeSquared());
	if (DistSq > ObserveRange * ObserveRange)
	{
		return false; // 사거리 밖.
	}
	if (DistSq < KINDA_SMALL_NUMBER)
	{
		return true; // 카메라와 거의 같은 위치.
	}
	const FVector Dir = To * FMath::InvSqrt(DistSq);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(ObserveConeAngle));
	return FVector::DotProduct(CamFwd, Dir) >= CosHalf; // 원뿔 반각 안 = 관측.
}

bool AMazeGenerator::IsCellReachable(FIntPoint From, FIntPoint To, const TSet<int64>* ExtraClosedEdges) const
{
	if (From == To)
	{
		return true;
	}
	const int32 NumCells = GridWidth * GridHeight;
	if (NumCells <= 0 || !Cells.IsValidIndex(Index(From.X, From.Y)) || !Cells.IsValidIndex(Index(To.X, To.Y)))
	{
		return false;
	}
	// 추가 닫힘 간선(아직 Cells 미반영) 검사: 두 셀 사이가 ExtraClosedEdges에 있으면 벽으로 취급.
	auto BlockedExtra = [&](FIntPoint A, FIntPoint B) -> bool
	{
		if (!ExtraClosedEdges) { return false; }
		const int64 K = EdgeKey(A, B);
		return K >= 0 && ExtraClosedEdges->Contains(K);
	};
	// 현재 '열린 간선'만 따라가는 BFS/DFS (벽이 없는 방향으로만 이동).
	TBitArray<> Visited(false, NumCells);
	TArray<int32> Stack;
	Stack.Reserve(64);
	const int32 Start = Index(From.X, From.Y);
	const int32 Target = Index(To.X, To.Y);
	Visited[Start] = true;
	Stack.Add(Start);
	while (Stack.Num() > 0)
	{
		const int32 Cur = Stack.Pop();
		if (Cur == Target)
		{
			return true;
		}
		const int32 X = Cur % GridWidth;
		const int32 Y = Cur / GridWidth;
		const FIntPoint C(X, Y);
		const uint8 W = Cells[Cur];
		if (!(W & Wall_North) && Y + 1 < GridHeight && !BlockedExtra(C, FIntPoint(X, Y + 1))) { const int32 Ni = Cur + GridWidth; if (!Visited[Ni]) { Visited[Ni] = true; Stack.Add(Ni); } }
		if (!(W & Wall_South) && Y - 1 >= 0         && !BlockedExtra(C, FIntPoint(X, Y - 1))) { const int32 Ni = Cur - GridWidth; if (!Visited[Ni]) { Visited[Ni] = true; Stack.Add(Ni); } }
		if (!(W & Wall_East)  && X + 1 < GridWidth  && !BlockedExtra(C, FIntPoint(X + 1, Y))) { const int32 Ni = Cur + 1;         if (!Visited[Ni]) { Visited[Ni] = true; Stack.Add(Ni); } }
		if (!(W & Wall_West)  && X - 1 >= 0         && !BlockedExtra(C, FIntPoint(X - 1, Y))) { const int32 Ni = Cur - 1;         if (!Visited[Ni]) { Visited[Ni] = true; Stack.Add(Ni); } }
	}
	return false;
}

void AMazeGenerator::ShiftTick()
{
	// 관측-반응 시프트 1사이클: '의도된 경로'를 따라 어둠 속(빛 밖) 앞쪽 닫힌 간선은 열고, 지나온 뒤쪽 비경로
	// 간선은 닫는다(예산제+연결성 가드). 끊기면 RepairConnectivityToGoal로 즉시 복구해 길을 보장한다.
	UWorld* World = GetWorld();
	if (!bShiftMode || bFinaleActive || !World || Cells.Num() == 0)
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	// 카메라 원뿔(관측) origin/forward + 플레이어 위치 + 손전등(최초 1회 부착).
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector CamFwd = CamRot.Vector();
	FVector PlayerLoc = CamLoc;
	if (APawn* Pawn = PC->GetPawn())
	{
		PlayerLoc = Pawn->GetActorLocation();
		EnsureShiftFlashlight(Pawn);
	}

	// 첫 사이클: 플레이어 시작→목표 경로 산출 + Tick 애니 구동 활성(폰이 준비된 지금 안전하게).
	if (!bShiftActive)
	{
		InitShiftRun();
	}
	if (RouteCells.Num() < 2)
	{
		return; // 경로가 없으면(예: 시작==목표) 시프트 대상 없음.
	}

	const FIntPoint P = GetPlayerCell();
	const float DrawZ = WallHeight * 0.5f;
	const float FreezeRSq = FreezeRadius * FreezeRadius;

	// 플레이어의 경로상 진행 인덱스 = 가장 가까운 경로 셀(앞/뒤를 경로 기준으로 가른다).
	int32 PlayerRouteIdx = 0;
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < RouteCells.Num(); ++i)
		{
			const float D = static_cast<float>(FVector::DistSquaredXY(PlayerLoc, GetCellCenterWorld(RouteCells[i].X, RouteCells[i].Y)));
			if (D < Best) { Best = D; PlayerRouteIdx = i; }
		}
	}

	// 어둠 속(=관측 안 됨, 코앞 아님, 얼림 반경 밖, 이미 애니 중 아님)일 때만 자유롭게 바꾼다 — 빛 안이면 얼림.
	auto IsFree = [&](FIntPoint A, FIntPoint B, const FVector& Mid) -> bool
	{
		if (A == P || B == P) { return false; }                                  // 코앞 간선 고정(끼임 방지).
		if (FVector::DistSquaredXY(PlayerLoc, Mid) < FreezeRSq) { return false; } // 얼림 반경 안.
		if (IsPointObserved(Mid, CamLoc, CamFwd)) { return false; }              // 빛 안(관측) = 얼림.
		const int64 K = EdgeKey(A, B);
		if (K < 0 || AnimatedEdges.Contains(K)) { return false; }                // 이미 개폐 애니 중.
		return true;
	};

	// 아직 Cells에 미반영된 '닫힐 간선' 집합 = 진행 중인 닫기 애니. 열기 루프-판정과 닫기 가드가 공유한다.
	// (이번 틱에 확정하는 닫기는 아래 닫기 루프에서 누적 추가.)
	TSet<int64> PendingClosed;
	for (const FWallAnim& An : ActiveAnims)
	{
		if (!An.bOpening)
		{
			const int64 K = EdgeKey(An.CellA, An.CellB);
			if (K >= 0) { PendingClosed.Add(K); }
		}
	}

	// --- 후보 수집: '의도된 경로'만 대상(무작위 근처 개폐 금지) ---
	// 열기: 플레이어 앞 ShiftForwardCells 칸의 '닫힌 경로 간선'(어둠 속) → 앞길이 열림.
	// 단, 두 셀이 이미 다른 경로로 연결돼 있으면 열기 = 루프(도는 구간) 생성이므로 건너뛴다.
	// → 닫기가 앞 경로를 끊은 드문 경우에만 복구용으로 열림(루프 절대 안 생김).
	TArray<TPair<FIntPoint, FIntPoint>> OpenCands;
	{
		const int32 End = FMath::Min(PlayerRouteIdx + ShiftForwardCells, RouteCells.Num() - 1);
		for (int32 i = PlayerRouteIdx; i < End; ++i)
		{
			const FIntPoint A = RouteCells[i];
			const FIntPoint B = RouteCells[i + 1];
			if (!IsWallClosed(A, B)) { continue; } // 이미 열림.
			if (IsCellReachable(A, B, &PendingClosed)) { continue; } // 이미 연결됨 → 열면 루프 → 건너뜀.
			const FVector Mid = EdgeMidWorld(A, B);
			if (!IsFree(A, B, Mid)) { continue; }
			OpenCands.Emplace(A, B);
			if (bShiftDebugDraw) { DrawDebugBox(World, Mid, FVector(20, 20, DrawZ), FColor::Green, false, ShiftInterval * 1.1f, 0, 4.f); }
		}
	}

	// 닫기: 플레이어 뒤 ShiftBackCells 칸의 경로 셀에서 '비경로 옆 간선'(현재 열림, 어둠 속) → 지나온 길 봉인.
	TArray<TPair<FIntPoint, FIntPoint>> CloseCands;
	{
		const FIntPoint Dirs4[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		const int32 Begin = FMath::Max(0, PlayerRouteIdx - ShiftBackCells);
		for (int32 i = Begin; i < PlayerRouteIdx; ++i)
		{
			const FIntPoint C = RouteCells[i];
			// 방(공터/퍼즐 공간) 안 셀은 옆 간선을 닫지 않는다(열린 방 유지).
			bool bInRoom = false;
			for (const FIntRect& R : Rooms)
			{
				if (C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y) { bInRoom = true; break; }
			}
			if (bInRoom) { continue; }
			for (const FIntPoint& D : Dirs4)
			{
				const FIntPoint N = C + D;
				if (N.X < 0 || N.X >= GridWidth || N.Y < 0 || N.Y >= GridHeight) { continue; }
				if (IsWallClosed(C, N)) { continue; } // 이미 벽.
				const int64 K = EdgeKey(C, N);
				if (K < 0 || RouteEdgeSet.Contains(K)) { continue; } // 경로 간선은 닫지 않음(길 보장).
				const FVector Mid = EdgeMidWorld(C, N);
				if (!IsFree(C, N, Mid)) { continue; }
				CloseCands.Emplace(C, N);
				if (bShiftDebugDraw) { DrawDebugBox(World, Mid, FVector(20, 20, DrawZ), FColor::Red, false, ShiftInterval * 1.1f, 0, 4.f); }
			}
		}
	}

	// 후보 섞기 — 매 사이클 다른 곳이 바뀌게.
	auto Shuffle = [&](TArray<TPair<FIntPoint, FIntPoint>>& Arr)
	{
		for (int32 i = Arr.Num() - 1; i > 0; --i) { Arr.Swap(i, ShiftStream.RandRange(0, i)); }
	};
	Shuffle(OpenCands);
	Shuffle(CloseCands);

	// --- 예산제 + 실시간 애니 예약(즉시 팝 없음 — 피날레식 슬라이드/문/솟기 재사용) ---
	// 밀도 유지(공터화 방지): 열기(벽 제거)는 절반 이하, 나머지는 닫기(벽 생성) → 닫기 ≥ 열기.
	int32 OpenDone = 0, CloseDone = 0;
	const int32 OpenBudget = ShiftBudgetPerCycle / 2;
	for (const TPair<FIntPoint, FIntPoint>& E : OpenCands)
	{
		if (OpenDone >= OpenBudget) { break; }
		const EWallAnimStyle Style = static_cast<EWallAnimStyle>(FinaleStream.RandRange(0, 1)); // 열기: Slide/SwingDoor.
		ScheduleOpenEdge(E.Key, E.Value, Style);
		++OpenDone;
	}

	// 닫기 연결성 가드(길 막힘 절대 금지): 후보를 PendingClosed(위에서 진행 중 닫기로 시드)에 누적 반영해
	// 검사한다. 후보를 임시로 넣고 IsCellReachable(P→목표, ExtraClosed=PendingClosed)이 통과할 때만 확정.
	// (개별 검사로는 두 닫기가 '합쳐서' 길을 막는 경우를 못 잡았다 → 누적 검사로 해결.)
	const int32 CloseBudget = ShiftBudgetPerCycle - OpenDone; // 열기가 적으면 닫기가 더 많아져 밀도 유지.
	// 목표가 유효하지 않으면(가드 불가) 닫기 전면 생략 — 안전(길 막힘) 우선.
	if (bShiftGoalValid)
	{
		for (const TPair<FIntPoint, FIntPoint>& E : CloseCands)
		{
			if (CloseDone >= CloseBudget) { break; }
			if (WallWouldHitPlayer(E.Key, E.Value, PlayerLoc)) { continue; } // 솟는 벽이 플레이어와 겹침 → 보류.
			const int64 K = EdgeKey(E.Key, E.Value);
			if (K < 0) { continue; }
			PendingClosed.Add(K); // 후보를 가정 포함.
			if (!IsCellReachable(P, ShiftGoalCell, &PendingClosed))
			{
				PendingClosed.Remove(K); // 길 막힘 유발 → 이 후보 거부(나머지 후보는 계속 검사).
				continue;
			}
			// 통과: 키를 PendingClosed에 유지(다음 후보가 이 닫기를 반영해 누적 검사).
			ScheduleCloseEdge(E.Key, E.Value, EWallAnimStyle::RiseFall); // 닫기는 항상 바닥 솟기(팝 없음).
			++CloseDone;
		}
	}

	// 길 보장: 단절됐으면 어둠 속 벽을 열어 즉시 복구(연 벽 수 반환).
	const int32 RepairedWalls = RepairConnectivityToGoal(P, CamLoc, CamFwd);

	// [진단] 후보/적용 수 + 경로 진행도 + 복구 수.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7777, ShiftInterval * 1.1f, FColor::Cyan,
			FString::Printf(TEXT("ShiftTick: open=%d/%d close=%d/%d repair=%d routeIdx=%d/%d cell=(%d,%d)"),
				OpenDone, OpenCands.Num(), CloseDone, CloseCands.Num(), RepairedWalls, PlayerRouteIdx, RouteCells.Num(), P.X, P.Y));
	}

	// 디버그 시각화(관측 원뿔 + 목표까지 경로선).
	if (bShiftDebugDraw)
	{
		DrawShiftDebug(P, CamLoc, CamFwd);
	}
}

FVector AMazeGenerator::EdgeMidWorld(FIntPoint A, FIntPoint B) const
{
	return (GetCellCenterWorld(A.X, A.Y) + GetCellCenterWorld(B.X, B.Y)) * 0.5f + FVector(0.f, 0.f, WallHeight * 0.5f);
}

void AMazeGenerator::EnsureShiftFlashlight(APawn* Pawn)
{
	// 손전등: 코드로 카메라에 1회 부착(BP 불필요). 원뿔/사거리는 관측 파라미터와 일치시켜 "빛=얼림"이 시각적으로 맞게 한다.
	if (!bShiftFlashlight || Flashlight.IsValid() || !Pawn)
	{
		return;
	}
	UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>();
	if (!Cam)
	{
		return;
	}
	USpotLightComponent* Flash = NewObject<USpotLightComponent>(Pawn);
	if (!Flash)
	{
		return;
	}
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->RegisterComponent();
	Flash->AttachToComponent(Cam, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Flash->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	Flash->SetAttenuationRadius(ObserveRange);
	Flash->SetOuterConeAngle(ObserveConeAngle);
	Flash->SetInnerConeAngle(FMath::Max(1.f, ObserveConeAngle * 0.6f));
	Flash->SetIntensity(FlashlightIntensity);
	Flash->SetCastShadows(true);
	Flashlight = Flash;
}

void AMazeGenerator::EnsureShiftFog()
{
	// 안개: 코드로 ExponentialHeightFog를 1회 스폰(BP 불필요). 손전등과 짝이 되어 어둠 속 시야를 반경 안에 가둔다.
	// 천장(Z): FogHeightFalloff를 작게 줘 위쪽까지 안개가 고르게 차오르고, 하늘(무한 거리)은 MaxOpacity로 완전히 덮인다.
	// 수평(X/Y): 안개가 거리 비례로 누적되므로 StartDistance 너머 먼 벽이 자연스럽게 페이드된다.
	UWorld* World = GetWorld();
	if (!bShiftFog || ShiftFog.IsValid() || !World)
	{
		return;
	}
	// UE는 씬당 ExponentialHeightFog를 사실상 하나만 렌더에 쓴다 → 레벨에 이미 있으면 그걸 조정해야
	// 한다(추가 스폰하면 기존 것에 가려 무시됨). 있으면 재사용, 없을 때만 새로 스폰.
	AExponentialHeightFog* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	if (!Fog)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// 안개 레이어 기준 높이 = 벽 중간. 위/아래로 고르게 덮이도록(충돌 없음, 시각만).
		const FVector FogLoc = GetActorLocation() + FVector(0.f, 0.f, WallHeight * 0.5f);
		Fog = World->SpawnActor<AExponentialHeightFog>(
			AExponentialHeightFog::StaticClass(), FogLoc, FRotator::ZeroRotator, Params);
	}
	if (!Fog)
	{
		return;
	}
	if (UExponentialHeightFogComponent* C = Fog->GetComponent())
	{
		// 모든 파라미터는 디테일 패널 'Maze|Fog'에서 조정 가능(여기선 그 값을 적용만).
		C->SetFogDensity(FogDensity);
		C->SetFogHeightFalloff(FogHeightFalloff);
		C->SetFogInscatteringColor(FogColor);
		C->SetStartDistance(FogStartDistance);
		C->SetFogMaxOpacity(FogMaxOpacity);
		// 방향성 산란(태양빛 줄무늬) 제거 — 어둠 분위기 유지.
		C->SetDirectionalInscatteringColor(FLinearColor::Black);
	}
	ShiftFog = Fog;
	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: ShiftFog ready (density=%.3f falloff=%.3f start=%.0f color=(%.3f,%.3f,%.3f))."),
		FogDensity, FogHeightFalloff, FogStartDistance, FogColor.R, FogColor.G, FogColor.B);
}

int32 AMazeGenerator::RepairConnectivityToGoal(FIntPoint PlayerCell, const FVector& CamLoc, const FVector& CamFwd)
{
	// 닫기 가드(예방)를 넘어선 단절(플레이어가 닫히는 간선을 가로지르는 레이스 등)이 생기면, 어둠 속 벽을
	// 우선 골라 여는 '최소-벽 경로'를 찾아 즉시 복구한다. 닫기는 연결을 만들 수 없으므로 복구는 '열기'로만.
	// 복구 열기는 끊긴 두 컴포넌트를 잇는 것이라 루프도 만들지 않는다. (디버그와 무관하게 항상 호출.)
	if (!bShiftGoalValid || IsCellReachable(PlayerCell, ShiftGoalCell))
	{
		return 0;
	}

	const int32 NumCells = GridWidth * GridHeight;
	const int32 Start = Index(PlayerCell.X, PlayerCell.Y);
	const int32 Target = Index(ShiftGoalCell.X, ShiftGoalCell.Y);

	// Dijkstra(드물게만 실행): 간선 비용 = 열림 0 / 닫힘·어둠 1 / 닫힘·관측 OBS_COST(가능하면 안 건드림).
	// 모든 셀쌍은 벽을 열어 통과 가능하므로 목표 도달은 항상 성공.
	const int32 OBS_COST = 10000;
	TArray<int32> Dist; Dist.Init(MAX_int32, NumCells);
	TArray<int32> Prev; Prev.Init(INDEX_NONE, NumCells);
	TBitArray<> Done(false, NumCells);
	Dist[Start] = 0;
	for (;;)
	{
		int32 Cur = INDEX_NONE, BestD = MAX_int32;
		for (int32 i = 0; i < NumCells; ++i) { if (!Done[i] && Dist[i] < BestD) { BestD = Dist[i]; Cur = i; } }
		if (Cur == INDEX_NONE || Cur == Target) { break; }
		Done[Cur] = true;
		const int32 X = Cur % GridWidth, Y = Cur / GridWidth;
		const FIntPoint C(X, Y);
		auto Relax = [&](int32 Ni, FIntPoint Nb)
		{
			int32 W = 0;
			if (IsWallClosed(C, Nb))
			{
				W = IsPointObserved(EdgeMidWorld(C, Nb), CamLoc, CamFwd) ? OBS_COST : 1;
			}
			const int32 Nd = Dist[Cur] + W;
			if (Nd < Dist[Ni]) { Dist[Ni] = Nd; Prev[Ni] = Cur; }
		};
		if (Y + 1 < GridHeight) { Relax(Cur + GridWidth, FIntPoint(X, Y + 1)); }
		if (Y - 1 >= 0)         { Relax(Cur - GridWidth, FIntPoint(X, Y - 1)); }
		if (X + 1 < GridWidth)  { Relax(Cur + 1,         FIntPoint(X + 1, Y)); }
		if (X - 1 >= 0)         { Relax(Cur - 1,         FIntPoint(X - 1, Y)); }
	}

	// 경로 역추적: 닫힌 간선마다 '즉시' 데이터+물리 동시 개통(애니 없음 → 불일치 0초). 어둠 우선이라 보통 안 보임.
	int32 Repaired = 0;
	for (int32 Cur = Target; Cur != Start && Prev[Cur] != INDEX_NONE; Cur = Prev[Cur])
	{
		const int32 Pr = Prev[Cur];
		const FIntPoint A(Cur % GridWidth, Cur / GridWidth);
		const FIntPoint B(Pr % GridWidth, Pr / GridWidth);
		if (!IsWallClosed(A, B)) { continue; } // 이미 열림.

		const int64 K = EdgeKey(A, B);
		// 이 간선에 진행 중이던 개폐 애니가 있으면 제거(즉시 개통이 우선 — 충돌/재닫힘 방지).
		ActiveAnims.RemoveAll([&](const FWallAnim& An2)
		{
			return (An2.CellA == A && An2.CellB == B) || (An2.CellA == B && An2.CellB == A);
		});
		if (K >= 0) { AnimatedEdges.Remove(K); }

		// 인스턴스를 즉시 바닥 아래로 숨긴다(RiseFall O=1 위치) → 데이터 개통과 동시에 물리 벽도 사라짐.
		int32 Idx = INDEX_NONE, X, Y, S;
		if (WallISM && FindWallInstance(A, B, Idx) && EdgeOwner(A, B, X, Y, S))
		{
			FQuat R; FVector Sc, Ce, Bo;
			if (ComputeWallGeom(X, Y, S, R, Sc, Ce, Bo))
			{
				const FVector GoneCenter = Ce + FVector(0.f, 0.f, -(WallHeight + 50.f));
				const FVector Loc = GoneCenter - R.RotateVector(Bo);
				WallISM->UpdateInstanceTransform(Idx, FTransform(R, Loc, Sc), false, true, true);
			}
			if (K >= 0) { WallInstanceIndex.Remove(K); }
			FreedWallPool.Add(Idx); // 미래 닫기에서 재사용.
		}
		ClearWallBetween(A, B); // 데이터 개통.
		++Repaired;
	}
	return Repaired;
}

void AMazeGenerator::DrawShiftDebug(FIntPoint PlayerCell, const FVector& CamLoc, const FVector& CamFwd) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 관측 원뿔.
	const float AngleRad = FMath::DegreesToRadians(ObserveConeAngle);
	DrawDebugCone(World, CamLoc, CamFwd, ObserveRange, AngleRad, AngleRad, 16, FColor::Yellow, false, ShiftInterval * 1.1f, 0, 1.5f);

	// 목표까지 길이 항상 존재함을 시각 확인: 현재 열린 그래프에서 플레이어→목표 BFS 경로선(청록).
	if (!bShiftGoalValid)
	{
		return;
	}
	const int32 NumCells = GridWidth * GridHeight;
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, NumCells);
	TArray<int32> Queue;
	Queue.Reserve(NumCells);
	const int32 Start = Index(PlayerCell.X, PlayerCell.Y);
	const int32 Target = Index(ShiftGoalCell.X, ShiftGoalCell.Y);
	Parent[Start] = Start;
	Queue.Add(Start);
	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const int32 Cur = Queue[Head++];
		if (Cur == Target) { break; }
		const int32 X = Cur % GridWidth;
		const int32 Y = Cur / GridWidth;
		const uint8 W = Cells[Cur];
		if (!(W & Wall_North) && Y + 1 < GridHeight) { const int32 Ni = Cur + GridWidth; if (Parent[Ni] == INDEX_NONE) { Parent[Ni] = Cur; Queue.Add(Ni); } }
		if (!(W & Wall_South) && Y - 1 >= 0)         { const int32 Ni = Cur - GridWidth; if (Parent[Ni] == INDEX_NONE) { Parent[Ni] = Cur; Queue.Add(Ni); } }
		if (!(W & Wall_East)  && X + 1 < GridWidth)  { const int32 Ni = Cur + 1;         if (Parent[Ni] == INDEX_NONE) { Parent[Ni] = Cur; Queue.Add(Ni); } }
		if (!(W & Wall_West)  && X - 1 >= 0)         { const int32 Ni = Cur - 1;         if (Parent[Ni] == INDEX_NONE) { Parent[Ni] = Cur; Queue.Add(Ni); } }
	}
	const float PathZ = WallHeight * 0.5f;
	if (Parent[Target] != INDEX_NONE)
	{
		for (int32 Cur = Target; Cur != Start; Cur = Parent[Cur])
		{
			const int32 Pr = Parent[Cur];
			const FVector A0 = GetCellCenterWorld(Cur % GridWidth, Cur / GridWidth) + FVector(0, 0, PathZ);
			const FVector B0 = GetCellCenterWorld(Pr % GridWidth, Pr / GridWidth) + FVector(0, 0, PathZ);
			DrawDebugLine(World, A0, B0, FColor::Cyan, false, ShiftInterval * 1.1f, 0, 8.f);
		}
	}
	else if (GEngine)
	{
		// 가드/복구가 정상이면 여기 오면 안 됨 — 오면 길이 끊긴 것이므로 경고.
		GEngine->AddOnScreenDebugMessage(7778, ShiftInterval * 1.1f, FColor::Red, TEXT("WARNING: no path player->goal!"));
	}
}

void AMazeGenerator::InitShiftRun()
{
	// 플레이어 시작 셀 → 목표 셀(ShiftGoalCell)까지의 단조 staircase 경로를 만든다(피날레 경로 산출과 동형).
	// 이 경로가 시프트의 '척추' — 앞쪽 경로 간선을 열고(전진 유도), 지나온 경로 셀의 옆 간선을 닫는다(백트래킹 봉인).
	bShiftActive = true;

	RouteCells.Reset();
	RouteEdgeSet.Reset();
	AnimatedEdges.Reset();
	ActiveAnims.Reset();
	FreedWallPool.Reset();
	OpenedWallCount = 0;
	FinaleStream = FRandomStream(Seed ^ 0x2545F491); // 개폐 방식(Slide/SwingDoor) 선택용.

	const FIntPoint Start = GetPlayerCell();
	const FIntPoint Goal = bShiftGoalValid ? ShiftGoalCell : FIntPoint(GridWidth - 1, GridHeight - 1);

	FRandomStream Stream(Seed ^ 0x5BD1E995);
	FIntPoint Cur = Start;
	RouteCells.Add(Cur);
	int32 Guard = (GridWidth + GridHeight) * 2 + 4;
	while (Cur != Goal && Guard-- > 0)
	{
		const int32 DX = Goal.X - Cur.X;
		const int32 DY = Goal.Y - Cur.Y;
		const bool bCanX = DX != 0;
		const bool bCanY = DY != 0;
		const bool bMoveX = (bCanX && bCanY) ? (Stream.FRand() < 0.5f) : bCanX;
		if (bMoveX) { Cur.X += (DX > 0) ? 1 : -1; }
		else        { Cur.Y += (DY > 0) ? 1 : -1; }
		RouteCells.Add(Cur);
	}

	// 경로 간선 집합 — 이 벽들은 절대 닫지 않는다(플레이어→목표 항상 풀림).
	for (int32 i = 0; i + 1 < RouteCells.Num(); ++i)
	{
		const int64 K = EdgeKey(RouteCells[i], RouteCells[i + 1]);
		if (K >= 0) { RouteEdgeSet.Add(K); }
	}

	SetActorTickEnabled(true); // Tick이 개폐 애니(ActiveAnims)를 매 프레임 구동.
	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: ShiftRun init — route %d cells, start(%d,%d)→goal(%d,%d)."),
		RouteCells.Num(), Start.X, Start.Y, Goal.X, Goal.Y);
}

bool AMazeGenerator::WallWouldHitPlayer(FIntPoint C, FIntPoint N, const FVector& PlayerLoc) const
{
	// 솟아오르는 닫기 벽이 플레이어 캡슐과 겹치면 CharacterMovement depenetration이 플레이어를 밀어낸다.
	// 벽 footprint(셀 경계 슬랩) 중심 기준 across(법선)·along(길이축) 거리로 캡슐 겹침을 근사 판정.
	const float PlayerRadius = 55.f; // 1인칭 템플릿 캡슐 반경(34) + 여유.
	const FVector Fc = (GetCellCenterWorld(C.X, C.Y) + GetCellCenterWorld(N.X, N.Y)) * 0.5f; // 벽 footprint 중심(경계).
	const FVector AcrossN = FVector(static_cast<float>(N.X - C.X), static_cast<float>(N.Y - C.Y), 0.f).GetSafeNormal(); // 경계 법선.
	const FVector Rel2D(PlayerLoc.X - Fc.X, PlayerLoc.Y - Fc.Y, 0.f);
	const float DistAcross = FMath::Abs(FVector::DotProduct(Rel2D, AcrossN));        // 슬랩 면까지 수직 거리.
	const float DistAlong = (Rel2D - AcrossN * FVector::DotProduct(Rel2D, AcrossN)).Size(); // 벽 길이축 거리.
	return DistAcross < (WallThickness * 0.5f + PlayerRadius + 8.f)
		&& DistAlong < (CellSize * 0.5f + PlayerRadius);
}

int64 AMazeGenerator::EncodeWallKey(int32 X, int32 Y, int32 Side) const
{
	return ((static_cast<int64>(Y) * GridWidth + X) << 2) | (Side & 3);
}

bool AMazeGenerator::FindWallInstance(FIntPoint A, FIntPoint B, int32& OutIndex) const
{
	// 두 인접 셀 사이 벽은 항상 '아래/왼쪽 셀'의 North(0) 또는 East(1)로 렌더된다.
	int32 OwnerX, OwnerY, Side;
	if (B.Y == A.Y + 1)      { OwnerX = A.X; OwnerY = A.Y; Side = 0; }
	else if (B.Y == A.Y - 1) { OwnerX = B.X; OwnerY = B.Y; Side = 0; }
	else if (B.X == A.X + 1) { OwnerX = A.X; OwnerY = A.Y; Side = 1; }
	else if (B.X == A.X - 1) { OwnerX = B.X; OwnerY = B.Y; Side = 1; }
	else                     { return false; } // 인접하지 않음.

	if (const int32* Found = WallInstanceIndex.Find(EncodeWallKey(OwnerX, OwnerY, Side)))
	{
		OutIndex = *Found;
		return true;
	}
	return false; // 이미 열린 벽(렌더 안 됨) → 애니 불필요.
}

void AMazeGenerator::ClearWallBetween(FIntPoint A, FIntPoint B)
{
	if (!Cells.IsValidIndex(Index(A.X, A.Y)) || !Cells.IsValidIndex(Index(B.X, B.Y)))
	{
		return;
	}
	if (B.Y == A.Y + 1)      { Cells[Index(A.X, A.Y)] &= ~Wall_North; Cells[Index(B.X, B.Y)] &= ~Wall_South; }
	else if (B.Y == A.Y - 1) { Cells[Index(A.X, A.Y)] &= ~Wall_South; Cells[Index(B.X, B.Y)] &= ~Wall_North; }
	else if (B.X == A.X + 1) { Cells[Index(A.X, A.Y)] &= ~Wall_East;  Cells[Index(B.X, B.Y)] &= ~Wall_West;  }
	else if (B.X == A.X - 1) { Cells[Index(A.X, A.Y)] &= ~Wall_West;  Cells[Index(B.X, B.Y)] &= ~Wall_East;  }
}

void AMazeGenerator::SetWallBetween(FIntPoint A, FIntPoint B)
{
	if (!Cells.IsValidIndex(Index(A.X, A.Y)) || !Cells.IsValidIndex(Index(B.X, B.Y)))
	{
		return;
	}
	if (B.Y == A.Y + 1)      { Cells[Index(A.X, A.Y)] |= Wall_North; Cells[Index(B.X, B.Y)] |= Wall_South; }
	else if (B.Y == A.Y - 1) { Cells[Index(A.X, A.Y)] |= Wall_South; Cells[Index(B.X, B.Y)] |= Wall_North; }
	else if (B.X == A.X + 1) { Cells[Index(A.X, A.Y)] |= Wall_East;  Cells[Index(B.X, B.Y)] |= Wall_West;  }
	else if (B.X == A.X - 1) { Cells[Index(A.X, A.Y)] |= Wall_West;  Cells[Index(B.X, B.Y)] |= Wall_East;  }
}

bool AMazeGenerator::IsWallClosed(FIntPoint A, FIntPoint B) const
{
	if (!Cells.IsValidIndex(Index(A.X, A.Y)))
	{
		return true;
	}
	const uint8 W = Cells[Index(A.X, A.Y)];
	if (B.Y == A.Y + 1) return (W & Wall_North) != 0;
	if (B.Y == A.Y - 1) return (W & Wall_South) != 0;
	if (B.X == A.X + 1) return (W & Wall_East)  != 0;
	if (B.X == A.X - 1) return (W & Wall_West)  != 0;
	return true; // 인접 아님 → 막힘 취급.
}

bool AMazeGenerator::EdgeOwner(FIntPoint A, FIntPoint B, int32& OutX, int32& OutY, int32& OutSide) const
{
	// 두 인접 셀 사이 벽은 항상 '아래/왼쪽 셀'의 North(0) 또는 East(1)로 소유된다.
	if (B.Y == A.Y + 1)      { OutX = A.X; OutY = A.Y; OutSide = 0; }
	else if (B.Y == A.Y - 1) { OutX = B.X; OutY = B.Y; OutSide = 0; }
	else if (B.X == A.X + 1) { OutX = A.X; OutY = A.Y; OutSide = 1; }
	else if (B.X == A.X - 1) { OutX = B.X; OutY = B.Y; OutSide = 1; }
	else                     { return false; }
	return true;
}

int64 AMazeGenerator::EdgeKey(FIntPoint A, FIntPoint B) const
{
	int32 X, Y, S;
	if (!EdgeOwner(A, B, X, Y, S))
	{
		return -1;
	}
	return EncodeWallKey(X, Y, S);
}

void AMazeGenerator::GetWallMeshScaling(FVector& OutScale, FVector& OutBoundsOffset) const
{
	OutScale = FVector::OneVector;
	OutBoundsOffset = FVector::ZeroVector;
	if (!WallStaticMesh)
	{
		return;
	}

	// 메시 바운드에서 실제 크기/피벗을 읽어 스케일한다. 가정: 메시 로컬 X=길이, Y=두께, Z=높이.
	const FBoxSphereBounds MeshBounds = WallStaticMesh->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0; // 전체 크기 = 절반범위 ×2.
	const double SizeX = FMath::Max<double>(MeshSize.X, UE_KINDA_SMALL_NUMBER);
	const double SizeY = FMath::Max<double>(MeshSize.Y, UE_KINDA_SMALL_NUMBER);
	const double SizeZ = FMath::Max<double>(MeshSize.Z, UE_KINDA_SMALL_NUMBER);

	// 수평(X·Y)을 같은 배율(HUniform)로 묶어 균일 스케일 → 단면(두께:길이) 비율 유지.
	const double HUniform = CellSize / SizeX;
	const double RenderedThickness = SizeY * HUniform;
	const double CornerOverlap = FMath::Min<double>(WallThickness, RenderedThickness);

	const float ScaleThickness = static_cast<float>(HUniform);
	const float ScaleLength    = static_cast<float>((CellSize + CornerOverlap) / SizeX);
	const float ScaleHeight    = static_cast<float>(WallHeight / SizeZ);
	OutScale = FVector(ScaleLength, ScaleThickness, ScaleHeight);
	OutBoundsOffset = MeshBounds.Origin * OutScale;
}

bool AMazeGenerator::ComputeWallGeom(int32 X, int32 Y, int32 Side, FQuat& OutRot, FVector& OutScale, FVector& OutCenter, FVector& OutBoundsOffset) const
{
	if (!WallStaticMesh)
	{
		return false;
	}
	GetWallMeshScaling(OutScale, OutBoundsOffset);

	const float HalfCell = CellSize * 0.5f;
	const float HalfHeight = WallHeight * 0.5f;
	const float CX = X * CellSize;
	const float CY = Y * CellSize;

	float BaseYaw = 0.f;
	switch (Side)
	{
	case 0: OutCenter = FVector(CX, CY + HalfCell, HalfHeight); BaseYaw = 0.f;  break; // North
	case 1: OutCenter = FVector(CX + HalfCell, CY, HalfHeight); BaseYaw = 90.f; break; // East
	case 2: OutCenter = FVector(CX, CY - HalfCell, HalfHeight); BaseYaw = 0.f;  break; // South 경계
	case 3: OutCenter = FVector(CX - HalfCell, CY, HalfHeight); BaseYaw = 90.f; break; // West 경계
	default: return false;
	}
	OutRot = FRotator(0.f, BaseYaw + WallMeshYawOffset, 0.f).Quaternion();
	return true;
}

FTransform AMazeGenerator::AnimXform(const FWallAnim& A, float OpenAmount) const
{
	const float O = FMath::Clamp(OpenAmount, 0.f, 1.f);
	FQuat dq = FQuat::Identity;
	FVector Center = A.Center;

	switch (A.Style)
	{
	case EWallAnimStyle::Slide:
		// 옆으로 한 칸 미끄러짐. 회전은 이동 중에만(sin: 양끝 0) → 정지 시 축 정렬되어 통로를 막지 않음.
		dq = FQuat(FVector::UpVector, FMath::DegreesToRadians(WallOpenSpinDegrees * FMath::Sin(PI * O)));
		Center = A.Center + A.SlideDir * (CellSize * O);
		break;

	case EWallAnimStyle::SwingDoor:
	{
		// 한쪽 끝(경첩)을 축으로 문처럼 회전(트인 쪽으로 돌도록 SwingSign 적용).
		dq = FQuat(FVector::UpVector, FMath::DegreesToRadians(WallSwingDegrees * A.SwingSign * O));
		const FVector Pivot = A.Center + A.PivotOffset;
		Center = Pivot + dq.RotateVector(A.Center - Pivot);
		break;
	}

	case EWallAnimStyle::RiseFall:
		// O=1이면 바닥 아래(사라짐), O=0이면 제자리.
		Center = A.Center + FVector(0.f, 0.f, -(WallHeight + 50.f) * O);
		break;
	}

	const FQuat Rot = dq * A.BaseRot;
	const FVector Loc = Center - Rot.RotateVector(A.BoundsOffset);
	return FTransform(Rot, Loc, A.Scale);
}

AMazeGenerator::EWallAnimStyle AMazeGenerator::ResolveAnimStyle(FIntPoint A, FIntPoint B, EWallAnimStyle Desired,
	FVector& OutSlideDir, FVector& OutPivotOffset, float& OutSwingSign,
	FIntPoint& OutRestA, FIntPoint& OutRestB) const
{
	// 간선 기하: N=두 셀을 가르는 수직 스텝, Fc=벽 길이축 스텝(N에 수직).
	const FIntPoint N = B - A;
	const FIntPoint Fc(N.Y, -N.X);
	const FVector FcV(static_cast<float>(Fc.X), static_cast<float>(Fc.Y), 0.f);

	auto InGrid = [&](const FIntPoint& C)
	{
		return C.X >= 0 && C.X < GridWidth && C.Y >= 0 && C.Y < GridHeight;
	};
	// 안착 후보 슬롯이 쓸 수 있는지: 격자 내 + 비어 있고(열림) + 경로 간선이 아님(길 안 막음).
	auto SlotUsable = [&](const FIntPoint& P, const FIntPoint& Q)
	{
		if (!InGrid(P) || !InGrid(Q) || IsWallClosed(P, Q))
		{
			return false;
		}
		const int64 K = EdgeKey(P, Q);
		return K >= 0 && !RouteEdgeSet.Contains(K);
	};

	// 기본값(폴백/RiseFall): 수직으로 바닥에 가라앉아 사라짐 — 안착 슬롯 없음.
	OutSlideDir = FcV;
	OutPivotOffset = FVector::ZeroVector;
	OutSwingSign = 1.f;
	OutRestA = FIntPoint(-1, -1);
	OutRestB = FIntPoint(-1, -1);

	// 슬라이드: 길이축 +Fc/-Fc 중 동일선상 빈 비경로 슬롯으로 한 칸 미끄러져 정렬 안착.
	auto TrySlide = [&]() -> bool
	{
		for (int32 s = 1; s >= -1; s -= 2)
		{
			const FIntPoint nA = A + Fc * s;
			const FIntPoint nB = B + Fc * s;
			if (SlotUsable(nA, nB))
			{
				OutSlideDir = FcV * static_cast<float>(s);
				OutRestA = nA;
				OutRestB = nB;
				return true;
			}
		}
		return false;
	};

	// 문: 끝을 축으로 90° 회전해 수직 슬롯 (sideCell, sideCell+s*Fc)에 플러시 안착.
	auto TrySwing = [&]() -> bool
	{
		struct FSide { FIntPoint Cell; int32 Sigma; };
		const FSide Sides[2] = { { A, -1 }, { B, +1 } };
		for (int32 s = 1; s >= -1; s -= 2)
		{
			for (const FSide& Sd : Sides)
			{
				const FIntPoint Block = Sd.Cell + Fc * s;
				if (SlotUsable(Sd.Cell, Block))
				{
					OutPivotOffset = FcV * (static_cast<float>(s) * CellSize * 0.5f);
					OutSwingSign = -static_cast<float>(s * Sd.Sigma); // 자유단이 대상 셀 쪽으로 회전.
					OutRestA = Sd.Cell;
					OutRestB = Block;
					return true;
				}
			}
		}
		return false;
	};

	// 구간이 원한 방식 우선, 안 되면 다른 방식, 둘 다 안 되면 RiseFall(완전히 막힘 → 바닥으로 꺼짐).
	if (Desired == EWallAnimStyle::SwingDoor)
	{
		if (TrySwing()) return EWallAnimStyle::SwingDoor;
		if (TrySlide()) return EWallAnimStyle::Slide;
	}
	else
	{
		if (TrySlide()) return EWallAnimStyle::Slide;
		if (TrySwing()) return EWallAnimStyle::SwingDoor;
	}
	return EWallAnimStyle::RiseFall;
}

void AMazeGenerator::ScheduleOpenEdge(FIntPoint A, FIntPoint B, EWallAnimStyle Style)
{
	const int64 Key = EdgeKey(A, B);
	if (Key < 0 || AnimatedEdges.Contains(Key))
	{
		return;
	}
	int32 Idx = INDEX_NONE;
	if (!FindWallInstance(A, B, Idx))
	{
		return; // 이미 열린(렌더 안 된) 간선 → 열 것이 없음.
	}
	int32 X, Y, S;
	if (!EdgeOwner(A, B, X, Y, S))
	{
		return;
	}
	FWallAnim An;
	if (!ComputeWallGeom(X, Y, S, An.BaseRot, An.Scale, An.Center, An.BoundsOffset))
	{
		return;
	}
	An.InstanceIndex = Idx;
	An.bOpening = true;
	An.CellA = A;
	An.CellB = B;
	if (bShiftActive)
	{
		// 시프트 모드: 열린 벽은 옆 슬롯에 '주차'하지 않고 바닥으로 가라앉아 완전히 사라진다.
		// (주차는 데이터-개방 슬롯에 벽 인스턴스를 남겨 "경로는 열렸는데 벽이 막는" 데이터/물리 불일치를 만든다.)
		An.Style = EWallAnimStyle::RiseFall;
		An.RestSlotA = FIntPoint(-1, -1);
		An.RestSlotB = FIntPoint(-1, -1);
	}
	else
	{
		// 피날레: 길을 막지 않게 안착할 비경로 빈 슬롯을 정함(슬라이드/문 불가 시 RiseFall=바닥으로 꺼짐).
		An.Style = ResolveAnimStyle(A, B, Style, An.SlideDir, An.PivotOffset, An.SwingSign, An.RestSlotA, An.RestSlotB);
	}
	ActiveAnims.Add(An);
	AnimatedEdges.Add(Key);

	// 벽이 안착할 비경로 슬롯을 지금(스케줄 시점) 봉인 예약 → 애니 도중 닫기가 같은 자리에 이중 벽을 올리지 않게.
	if (An.Style != EWallAnimStyle::RiseFall && An.RestSlotA.X >= 0)
	{
		const int64 RestKey = EdgeKey(An.RestSlotA, An.RestSlotB);
		if (RestKey >= 0)
		{
			AnimatedEdges.Add(RestKey);
			WallInstanceIndex.Add(RestKey, Idx);
		}
	}
	++OpenedWallCount;
}

void AMazeGenerator::ScheduleCloseEdge(FIntPoint A, FIntPoint B, EWallAnimStyle Style)
{
	const int64 Key = EdgeKey(A, B);
	if (Key < 0 || AnimatedEdges.Contains(Key) || !WallISM)
	{
		return;
	}
	int32 X, Y, S;
	if (!EdgeOwner(A, B, X, Y, S))
	{
		return;
	}
	FWallAnim An;
	if (!ComputeWallGeom(X, Y, S, An.BaseRot, An.Scale, An.Center, An.BoundsOffset))
	{
		return;
	}
	An.bOpening = false;
	An.CellA = A;
	An.CellB = B;
	// 닫기는 항상 RiseFall(바닥에서 수직 솟기)로 들어옴 → 방향 결정 결과도 RiseFall.
	An.Style = ResolveAnimStyle(A, B, Style, An.SlideDir, An.PivotOffset, An.SwingSign, An.RestSlotA, An.RestSlotB);

	// 시작 위치 = '완전 개방' 상태 = 바닥 아래(안 보임). 여기서 솟아오르며 닫히므로 팝 없음.
	const FTransform Gone = AnimXform(An, 1.f);
	if (FreedWallPool.Num() > 0)
	{
		// 길을 열며 치운 벽을 재배치(재사용). 바닥 아래끼리 이동이라 보이지 않게 자리만 바뀜.
		An.InstanceIndex = FreedWallPool.Pop();
		WallISM->UpdateInstanceTransform(An.InstanceIndex, Gone, /*bWorldSpace=*/false, true, true);
	}
	else
	{
		// 재사용할 벽이 없으면 새로 추가하되, 역시 바닥 아래에서 시작 → 솟아오름(팝 없음).
		An.InstanceIndex = WallISM->AddInstance(Gone, /*bWorldSpace=*/false);
	}
	WallInstanceIndex.Add(Key, An.InstanceIndex);
	ActiveAnims.Add(An);
	AnimatedEdges.Add(Key);
}

void AMazeGenerator::HandleGoalCleared()
{
	UWorld* World = GetWorld();
	if (bFinaleActive || !World || Cells.Num() == 0)
	{
		return; // 이미 진행 중이거나 월드/데이터 없음.
	}
	bFinaleActive = true;

	// 출구 셀 = 플레이어 현재 셀에서 가장 먼 격자 코너.
	const FIntPoint P = GetPlayerCell();
	const FIntPoint Corners[4] =
	{
		FIntPoint(0, 0),
		FIntPoint(GridWidth - 1, 0),
		FIntPoint(0, GridHeight - 1),
		FIntPoint(GridWidth - 1, GridHeight - 1),
	};
	FIntPoint ExitCell = Corners[0];
	int64 BestDistSq = -1;
	for (const FIntPoint& C : Corners)
	{
		const int64 DX = C.X - P.X;
		const int64 DY = C.Y - P.Y;
		const int64 D = DX * DX + DY * DY;
		if (D > BestDistSq)
		{
			BestDistSq = D;
			ExitCell = C;
		}
	}

	// 출구(빛기둥) 스폰.
	TSubclassOf<AMazeExit> Cls = ExitClass;
	if (!Cls)
	{
		Cls = AMazeExit::StaticClass();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector ExitLoc = GetCellCenterWorld(ExitCell.X, ExitCell.Y);
	if (AMazeExit* Exit = World->SpawnActor<AMazeExit>(Cls, ExitLoc, GetActorRotation(), Params))
	{
		ExitActor = Exit;
		Exit->OnEscaped.AddDynamic(this, &AMazeGenerator::HandleEscaped);
	}

	// 메인 탈출 경로: 플레이어 셀 → 출구 셀 단조 계단형(랜덤 타이브레이크 = 약간 구불, 시도마다 다름).
	TArray<FIntPoint> MainCells;
	FRandomStream Stream(Seed ^ 0x5BD1E995 ^ static_cast<int32>(FinaleAttempt * 2654435761u));
	{
		FIntPoint Cur = P;
		MainCells.Add(Cur);
		int32 Guard = (GridWidth + GridHeight) * 2 + 4;
		while (Cur != ExitCell && Guard-- > 0)
		{
			const int32 DX = ExitCell.X - Cur.X;
			const int32 DY = ExitCell.Y - Cur.Y;
			const bool bCanX = DX != 0;
			const bool bCanY = DY != 0;
			const bool bMoveX = (bCanX && bCanY) ? (Stream.FRand() < 0.5f) : bCanX;
			if (bMoveX) { Cur.X += (DX > 0) ? 1 : -1; }
			else        { Cur.Y += (DY > 0) ? 1 : -1; }
			MainCells.Add(Cur);
		}
	}

	// 미로는 그대로 둔다(전면 폐쇄 금지). 실제 미로 위에서 경로 벽은 '열고', 경로 아닌 옆 통로는 다가오면 '닫는다'.
	// 전체 1회 렌더 + (X,Y,Side)→인스턴스 인덱스 기록(경로의 닫힌 벽은 인스턴스 보유 → 열기 가능).
	World->GetTimerManager().ClearTimer(WindowTimer);
	bRecordInstanceMap = true;
	BuildWallsInWindow(FIntPoint(GridWidth / 2, GridHeight / 2), FMath::Max(GridWidth, GridHeight));
	LastWindowCenter = FIntPoint(MIN_int32, MIN_int32);

	// --- 연출 상태 초기화 ---
	Segments.Reset();
	OpenedWallCount = 0;
	ActiveAnims.Reset();
	RouteEdgeSet.Reset();
	AnimatedEdges.Reset();
	RouteCells = MainCells;
	RouteCellClosed.Init(false, RouteCells.Num());
	FinaleStream = FRandomStream(Seed ^ 0x2545F491 ^ static_cast<int32>(FinaleAttempt * 40503u));

	// 목표 방(시작 공간) 캐시 — 이 안은 옆 통로 닫기에서 제외(열린 시작 공간 유지).
	bFinaleHasGoalRoom = Rooms.Num() > 0;
	if (bFinaleHasGoalRoom)
	{
		FinaleGoalRoom = Rooms[GetGoalRoomIndex()];
	}

	// 메인 경로 간선 집합 — 이 벽들은 절대 닫지 않는다(길 보장).
	for (int32 i = 0; i + 1 < MainCells.Num(); ++i)
	{
		const int64 K = EdgeKey(MainCells[i], MainCells[i + 1]);
		if (K >= 0)
		{
			RouteEdgeSet.Add(K);
		}
	}

	// 메인 경로를 직선 구간으로 쪼개 코너까지 통째로 열리게(구간마다 개방 방식을 다르게: 슬라이드/문).
	{
		int32 RunStart = 0;
		FIntPoint Dir = (MainCells.Num() >= 2) ? (MainCells[1] - MainCells[0]) : FIntPoint::ZeroValue;
		int32 PrevSeg = INDEX_NONE;
		auto FlushRun = [&](int32 EndIdx)
		{
			FRevealSegment Seg;
			for (int32 k = RunStart; k < EndIdx; ++k)
			{
				Seg.Edges.Emplace(MainCells[k], MainCells[k + 1]);
			}
			Seg.TriggerWorld = GetCellCenterWorld(MainCells[RunStart].X, MainCells[RunStart].Y);
			Seg.Parent = PrevSeg;
			Seg.Style = static_cast<EWallAnimStyle>(FinaleStream.RandRange(0, 1)); // 열기: Slide/SwingDoor.
			PrevSeg = Segments.Add(Seg);
			RunStart = EndIdx;
		};
		for (int32 i = 1; i + 1 < MainCells.Num(); ++i)
		{
			const FIntPoint D = MainCells[i + 1] - MainCells[i];
			if (D != Dir) { FlushRun(i); Dir = D; }
		}
		if (MainCells.Num() >= 2)
		{
			FlushRun(MainCells.Num() - 1);
		}
	}

	// 리스폰 지점 = 목표 셀(경로 시작) 약간 위.
	const float SpawnZ = 120.f;
	FinaleStartWorld = GetCellCenterWorld(MainCells[0].X, MainCells[0].Y) + FVector(0.f, 0.f, SpawnZ);

	// 추격자 준비(메인 경로 웨이포인트). 등장은 벽 임계 개수에서 Activate.
	if (bEnableChaser)
	{
		TArray<FVector> Waypoints;
		Waypoints.Reserve(MainCells.Num());
		for (const FIntPoint& C : MainCells)
		{
			Waypoints.Add(GetCellCenterWorld(C.X, C.Y) + FVector(0.f, 0.f, SpawnZ));
		}

		TSubclassOf<AMazeChaser> CCls = ChaserClass;
		if (!CCls)
		{
			CCls = AMazeChaser::StaticClass();
		}
		FActorSpawnParameters CParams;
		CParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AMazeChaser* NewChaser = World->SpawnActor<AMazeChaser>(CCls, Waypoints[0], GetActorRotation(), CParams))
		{
			Chaser = NewChaser;
			NewChaser->Init(Waypoints, ChaserSpeed, ChaserCatchRadius, ChaserStartCells);
			NewChaser->OnPlayerCaught.AddDynamic(this, &AMazeGenerator::HandlePlayerCaught);
		}
	}

	SetActorTickEnabled(true);

	// 피날레 동안 모든 퍼즐 숨김(잡혀 리셋되면 ResetFinaleToPreClear에서 복구).
	for (TActorIterator<ASlidingTilePuzzle> It(World); It; ++It)
	{
		if (ASlidingTilePuzzle* Puzzle = *It)
		{
			Puzzle->SetActorHiddenInGame(true);
			Puzzle->SetActorEnableCollision(false);
			Puzzle->SetActorTickEnabled(false);
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Orange,
			TEXT("벽이 열리고 닫힌다! 빛기둥을 향한 길을 따라 탈출하라!"));
	}
	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: 피날레 시작 — 출구 (%d,%d), 구간 %d개."),
		ExitCell.X, ExitCell.Y, Segments.Num());

	++FinaleAttempt; // 다음 재도전은 다른 길.
}

void AMazeGenerator::OpenRoomInterior(const FIntRect& R)
{
	// 방 내부 인접 셀 사이 벽 제거(= 공터). CarveRooms 내부 로직과 동일.
	for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
	{
		for (int32 X = R.Min.X; X < R.Max.X; ++X)
		{
			if (X + 1 < R.Max.X)
			{
				Cells[Index(X, Y)]     &= ~Wall_East;
				Cells[Index(X + 1, Y)] &= ~Wall_West;
			}
			if (Y + 1 < R.Max.Y)
			{
				Cells[Index(X, Y)]     &= ~Wall_North;
				Cells[Index(X, Y + 1)] &= ~Wall_South;
			}
		}
	}
}

void AMazeGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 피날레(거리 기반 길 열기/닫기) 또는 시프트(관측 기반, ShiftTick이 예약) — 둘 다 개폐 애니를 Tick에서 구동.
	if ((!bFinaleActive && !bShiftActive) || !WallISM)
	{
		return;
	}

	// 플레이어 현재 월드 위치 + (시프트용) 카메라 관측 시점.
	FVector PlayerLoc = GetActorLocation();
	FVector CamLoc = PlayerLoc;
	FVector CamFwd = FVector::ForwardVector;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			PlayerLoc = Pawn->GetActorLocation();
		}
		FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		CamFwd = CamRot.Vector();
	}

	// 피날레 전용: 거리 기반 경로 구간 열기 + 옆 통로 닫기. (시프트 모드의 개폐 예약은 ShiftTick에서 관측 기반으로 한다.)
	if (bFinaleActive)
	{
	bool bAnyChangeThisFrame = false;

	// (1) 경로 구간 열기: 부모 구간이 열렸고 플레이어가 구간 시작에 근접 → 그 구간 벽들을 '열기'(치움).
	for (FRevealSegment& Seg : Segments)
	{
		if (Seg.bOpened)
		{
			continue;
		}
		const bool bParentReady = (Seg.Parent == INDEX_NONE) || Segments[Seg.Parent].bOpened;
		if (!bParentReady || FVector::Dist2D(PlayerLoc, Seg.TriggerWorld) > OpenAheadDistance)
		{
			continue;
		}
		Seg.bOpened = true;
		bAnyChangeThisFrame = true;
		for (const TPair<FIntPoint, FIntPoint>& E : Seg.Edges)
		{
			ScheduleOpenEdge(E.Key, E.Value, Seg.Style);
		}
	}

	// (2) 경로 아닌 옆 통로 닫기: 플레이어가 경로 셀에 근접하면 그 셀의 '경로 아닌' 열린 옆면에 벽을 새로 생성.
	const FIntPoint Dirs4[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	// 솟아오르는 벽이 플레이어 캡슐과 겹치면 밀려나므로, 겹치면 다음 프레임에 재시도(WallWouldHitPlayer, 멤버 함수).
	for (int32 i = 0; i < RouteCells.Num(); ++i)
	{
		if (RouteCellClosed[i])
		{
			continue;
		}
		const FIntPoint C = RouteCells[i];
		if (FVector::Dist2D(PlayerLoc, GetCellCenterWorld(C.X, C.Y)) > CloseAheadDistance)
		{
			continue;
		}

		// 목표 방(시작 공간)은 닫지 않고 열린 채 둔다.
		if (bFinaleHasGoalRoom && C.X >= FinaleGoalRoom.Min.X && C.X < FinaleGoalRoom.Max.X
			&& C.Y >= FinaleGoalRoom.Min.Y && C.Y < FinaleGoalRoom.Max.Y)
		{
			RouteCellClosed[i] = true;
			continue;
		}

		bool bAllDone = true; // 이번 프레임에 미룬 벽이 하나도 없을 때만 셀을 '닫음 완료' 처리.
		for (const FIntPoint& D : Dirs4)
		{
			const FIntPoint N = C + D;
			if (N.X < 0 || N.X >= GridWidth || N.Y < 0 || N.Y >= GridHeight)
			{
				continue; // 격자 경계(이미 외벽).
			}
			if (IsWallClosed(C, N))
			{
				continue; // 이미 벽이 있음.
			}
			const int64 K = EdgeKey(C, N);
			if (K < 0 || RouteEdgeSet.Contains(K))
			{
				continue; // 경로 벽은 닫지 않음.
			}
			if (WallWouldHitPlayer(C, N, PlayerLoc))
			{
				bAllDone = false; // 플레이어와 겹침 → 밀림 방지를 위해 다음 프레임에 재시도.
				continue;
			}
			// 닫기: 슬라이드/회전 없이 바닥에서 수직으로만 솟아올라 슬롯에 맞게 닫힘.
			ScheduleCloseEdge(C, N, EWallAnimStyle::RiseFall);
			bAnyChangeThisFrame = true;
		}
		// 미룬 벽이 있으면 false로 남겨 다음 프레임 재검사(이미 스케줄된 edge는 AnimatedEdges로 중복 차단됨).
		RouteCellClosed[i] = bAllDone;
	}

	// 벽 열림/닫힘에는 흔들림 없음(잡힐 때만 흔들림 — HandlePlayerCaught).
	(void)bAnyChangeThisFrame;
	} // end if (bFinaleActive)

	// (3) 진행 중 개폐 애니 갱신(피날레·시프트 공용).
	const float Dur = FMath::Max(0.05f, WallSlideDuration);
	for (int32 i = ActiveAnims.Num() - 1; i >= 0; --i)
	{
		FWallAnim& An = ActiveAnims[i];

		// 시프트 모드: 관측(빛/시야) 중인 벽은 개폐를 진행하지 않는다(관측=얼림).
		// - 열기: 관측이면 그 자리에서 멈춤(Elapsed 유지).
		// - 닫기: 관측이면 바닥으로 되사라져 '완전히 열린 채' 대기(Elapsed→0). 어둠이 되면 다시 솟음.
		// 어둠일 때만 정상 진행 → 완료 분기는 Alpha>=1일 때만이라 관측 중엔 절대 완료되지 않음.
		bool bObserved = false;
		if (bShiftActive)
		{
			bObserved = IsPointObserved(EdgeMidWorld(An.CellA, An.CellB), CamLoc, CamFwd);
		}
		if (!bObserved)
		{
			An.Elapsed += DeltaSeconds;
		}
		else if (!An.bOpening)
		{
			An.Elapsed = FMath::Max(0.f, An.Elapsed - DeltaSeconds); // 닫기: 관측 중엔 되사라짐.
		}
		// (열기 + 관측: Elapsed 유지 = 멈춤.)

		const float Alpha = FMath::Clamp(An.Elapsed / Dur, 0.f, 1.f);
		const float Open = An.bOpening ? Alpha : (1.f - Alpha); // 0=제자리, 1=사라짐.

		if (Alpha < 1.f)
		{
			WallISM->UpdateInstanceTransform(An.InstanceIndex, AnimXform(An, Open),
				/*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
		}
		else if (An.bOpening)
		{
			// 열림 완료: 데이터에서 벽 제거 + 회전/슬라이드가 끝난 자리에 인스턴스를 그대로 둔다.
			// (순간 사라지거나 다른 데서 올라오지 않음 — 옆 슬롯에 정렬/문처럼 열린 상태로 정지.
			//  사방이 막혀 RiseFall로 처리된 벽은 AnimXform(O=1)=바닥 아래 → 그대로 가라앉아 사라짐.)
			// 안착 슬롯 봉인은 스케줄 시점(ScheduleOpenEdge)에서 이미 예약됨(애니 도중 이중 벽 방지).
			ClearWallBetween(An.CellA, An.CellB);
			WallISM->UpdateInstanceTransform(An.InstanceIndex, AnimXform(An, 1.f), /*bWorldSpace=*/false, true, true);
			ActiveAnims.RemoveAtSwap(i);
		}
		else
		{
			// 닫힘 완료: 데이터에 벽 추가 + 인스턴스를 정확히 제자리로.
			SetWallBetween(An.CellA, An.CellB);
			WallISM->UpdateInstanceTransform(An.InstanceIndex, AnimXform(An, 0.f), /*bWorldSpace=*/false, true, true);
			ActiveAnims.RemoveAtSwap(i);
		}
	}

	// 피날레 동안 Tick 유지(미끼 분기는 플레이어가 다가올 때 열리므로 계속 검사).
	// 종료는 탈출(HandleEscaped)·잡힘(ResetFinaleToPreClear)에서 SetActorTickEnabled(false).
}

void AMazeGenerator::HandleEscaped()
{
	SetActorTickEnabled(false);
	bFinaleActive = false;
	if (AMazeChaser* C = Chaser.Get())
	{
		C->Destroy(); // 탈출 성공 → 추격자 제거.
	}
	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: 탈출 성공 — 피날레 종료."));
}

void AMazeGenerator::HandlePlayerCaught()
{
	UWorld* World = GetWorld();
	if (bHandlingCaught || !World)
	{
		return; // 중복 방지.
	}
	bHandlingCaught = true;

	// 암전(페이드 아웃, 검은 화면 유지) + 강한 카메라 흔들림(붙잡힌 충격).
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->ClientStartCameraShake(UFinaleCameraShake::StaticClass(), 1.6f);
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FMath::Max(0.f, CaughtFadeTime),
				FLinearColor::Black, /*bFadeAudio=*/false, /*bHoldWhenFinished=*/true);
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("잡혔다!"));
	}

	// 암전이 깔린 뒤 리스폰.
	World->GetTimerManager().SetTimer(RespawnTimer, this, &AMazeGenerator::RespawnAtGoal,
		FMath::Max(0.01f, CaughtFadeTime), /*bLoop=*/false);
}

void AMazeGenerator::ResetFinaleToPreClear()
{
	UWorld* World = GetWorld();

	// 피날레 상태 종료.
	SetActorTickEnabled(false);
	bFinaleActive = false;
	bRecordInstanceMap = false;
	Segments.Reset();
	OpenedWallCount = 0;
	ActiveAnims.Reset();
	FreedWallPool.Reset();
	WallInstanceIndex.Reset();
	RouteCells.Reset();
	RouteCellClosed.Reset();
	RouteEdgeSet.Reset();
	AnimatedEdges.Reset();
	bFinaleHasGoalRoom = false;

	// 추격자/출구 제거.
	if (AMazeChaser* C = Chaser.Get())
	{
		C->Destroy();
	}
	Chaser = nullptr;
	if (AMazeExit* E = ExitActor.Get())
	{
		E->Destroy();
	}
	ExitActor = nullptr;

	// 원본 미로 복구(Seed 그대로 → 원래 미로, 방도 열림).
	CarveMaze();
	CarveRooms();
	LastWindowCenter = FIntPoint(MIN_int32, MIN_int32);
	UpdateRenderWindow();
	if (World)
	{
		World->GetTimerManager().SetTimer(WindowTimer, this, &AMazeGenerator::UpdateRenderWindow,
			WindowUpdateInterval, /*bLoop=*/true);

		// 클리어됐던 목표지점을 '열쇠 보유·미클리어' 상태로 되돌린다.
		for (TActorIterator<AGoalPoint> It(World); It; ++It)
		{
			if (AGoalPoint* Goal = *It)
			{
				if (Goal->IsCleared())
				{
					Goal->ResetForRetry();
				}
			}
		}

		// 숨겼던 퍼즐 복구(다시 보이고 풀 수 있게).
		for (TActorIterator<ASlidingTilePuzzle> It(World); It; ++It)
		{
			if (ASlidingTilePuzzle* Puzzle = *It)
			{
				Puzzle->SetActorHiddenInGame(false);
				Puzzle->SetActorEnableCollision(true);
				Puzzle->SetActorTickEnabled(true);
			}
		}
	}
}

void AMazeGenerator::RespawnAtGoal()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();

	// 플레이어를 목표 방으로 이동.
	if (PC)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocation(FinaleStartWorld, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 미로 원상복구 + 추격자/출구 제거 + 목표 미클리어/열쇠 복구(= 피날레 전 상태).
	ResetFinaleToPreClear();

	// 화면 밝히기(페이드 인).
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, FMath::Max(0.01f, CaughtFadeTime),
			FLinearColor::Black, /*bFadeAudio=*/false, /*bHoldWhenFinished=*/false);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, TEXT("다시! 열쇠를 꽂아 탈출을 재시도하라."));
	}

	bHandlingCaught = false;
	UE_LOG(LogTemp, Log, TEXT("AMazeGenerator: 잡힘 → 목표 방 부활(미로 복구, 열쇠 복구, 미클리어)."));
}
