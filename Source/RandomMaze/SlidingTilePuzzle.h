// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Templates/SubclassOf.h"
#include "SlidingTilePuzzle.generated.h"

/** 완성 보상 연출 단계. */
UENUM()
enum class ESolvePhase : uint8
{
	None,     // 진행 중(연출 아님)
	Rising,   // 9번째 조각이 바닥에서 솟는 중
	Merging,  // 틈이 닫혀 한 그림으로 합쳐지는 중
	Done      // 연출 끝(열쇠 스폰됨)
};

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UTexture2D;
class APlayerController;
class AMazeGenerator;

/**
 * 바닥에 까는 슬라이딩 타일 퍼즐(15퍼즐 방식, Phase 1 — 동작 테스트용).
 * N×N 격자에 타일을 깔고 한 칸을 비운다. 1인칭 카메라 중앙으로 조준한 타일이 빈칸과
 * 직교 인접하면 발광(오버레이 머티리얼)하고, InteractKey(기본 좌클릭)를 누르면 빈칸으로 슬라이드.
 * 그림/텍스처는 아직 없음. 셔플은 랜덤 유효이동(시드 결정론)이라 항상 풀이 가능.
 */
UCLASS()
class RANDOMMAZE_API ASlidingTilePuzzle : public AActor
{
	GENERATED_BODY()

public:
	ASlidingTilePuzzle();

	virtual void Tick(float DeltaSeconds) override;

	// --- 퍼즐 파라미터 (디테일 패널) ---

	/** 격자 한 변의 타일 수(N). 8타일=3, 15타일=4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "2", ClampMax = "8"))
	int32 GridSize = 3;

	/** 타일 한 변 길이(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "10.0"))
	float TileSize = 250.f;

	/** 타일 사이 간격(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "0.0"))
	float TileGap = 15.f;

	/** 타일 두께(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "2.0"))
	float TileThickness = 25.f;

	/** 셔플 난수 시드. 같은 시드면 같은 시작 배치. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	int32 Seed = 1337;

	/** 시작 셔플 횟수(랜덤 유효 이동). 0이면 정렬된 상태로 시작. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "0"))
	int32 ShuffleMoves = 20;

	/** 조준 라인트레이스 최대 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "1.0"))
	float TraceDistance = 6000.f;

	/** 슬라이드 애니메이션 시간(초). 0이면 즉시 스냅. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (ClampMin = "0.0"))
	float SlideDuration = 0.15f;

	/** 타일을 옮기는 입력 키(폴링). 기본 좌클릭, 디테일에서 변경 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Input")
	FKey InteractKey = EKeys::LeftMouseButton;

	// --- 미로 방 배치 ---

	/** 켜면 미로가 이 퍼즐을 방 하나에 배치한다(AMazeGenerator::DistributeRoomActors가 읽음). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Placement")
	bool bPlaceInMazeRoom = true;

	// --- 완성 보상 연출 ---

	/** 완성 시 중앙에 열쇠를 스폰할지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Reward")
	bool bSpawnKeyOnSolve = true;

	/** 스폰할 열쇠 액터 클래스. 기본 AKeyPickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Reward")
	TSubclassOf<AActor> KeyClass;

	/** 9번째 조각이 바닥에서 솟아오르는 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Reward", meta = (ClampMin = "0.0"))
	float RiseDuration = 0.6f;

	/** 틈이 닫혀 합쳐지는 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Reward", meta = (ClampMin = "0.0"))
	float MergeDuration = 0.5f;

	/** 열쇠가 보드 중앙 위로 뜨는 높이(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Reward")
	float KeyHoverHeight = 200.f;

	// --- 디버그 ---

	/** 디버그: 즉시 풀기 키 사용 여부. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Debug")
	bool bEnableSolveCheat = true;

	/** 디버그: 이 키를 누르면 플레이어와 가장 가까운 퍼즐이 즉시 완성된다(연출 확인용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Debug")
	FKey SolveCheatKey = EKeys::K;

	// --- 비주얼 에셋 ---

	/** 타일 메시. 기본 엔진 큐브(평평하게 스케일). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Visual")
	TObjectPtr<UStaticMesh> TileMesh;

	/** (선택) 타일 베이스 머티리얼. 비우면 메시 기본 머티리얼 사용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Visual")
	TObjectPtr<UMaterialInterface> TileMaterial;

	/** 슬라이딩 퍼즐로 분할 표시할 그림 텍스처. 비우면 그림 없음(TileMaterial 사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Visual")
	TObjectPtr<UTexture2D> PictureTexture;

	/**
	 * 그림 타일용 마스터 머티리얼. 파라미터 필요: 텍스처 "Picture", 스칼라 "UVScale", 벡터 "UVOffset".
	 * 코드가 타일마다 다이내믹 인스턴스로 UV 구역을 지정한다. 비우면 그림 표시 안 함.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Visual")
	TObjectPtr<UMaterialInterface> TilePictureMaterial;

	/**
	 * 조준 시 외곽선을 그릴 커스텀 뎁스 스텐실 값(1~255). 포스트프로세스 외곽선 머티리얼이
	 * 이 값과 일치하는 픽셀의 가장자리에 선을 그린다. 머티리얼의 비교 값과 똑같이 맞춰야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Visual", meta = (ClampMin = "1", ClampMax = "255"))
	int32 OutlineStencilValue = 1;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 루트 씬 컴포넌트(타일들의 부모). */
	UPROPERTY(VisibleAnywhere, Category = "Puzzle")
	TObjectPtr<USceneComponent> Root;

	/** 생성한 타일 컴포넌트들. 인덱스 = 타일ID(0..N*N-2). GC 보호용 UPROPERTY. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Tiles;

	/** 슬롯별 상태(길이 N*N). 값 = 타일ID(0..N*N-2) 또는 빈칸(-1). */
	TArray<int32> Slots;

	/** 타일ID → 현재 슬롯 인덱스(역방향 O(1) 조회). */
	TArray<int32> TileSlot;

	/** 트레이스 히트 컴포넌트 → 타일ID. 컴포넌트 수명은 Tiles가 보장. */
	TMap<UPrimitiveComponent*, int32> CompToTile;

	/** 현재 빈칸 슬롯 인덱스. */
	int32 EmptyIndex = 0;

	/** 현재 발광 중인 타일ID(없으면 INDEX_NONE). */
	int32 HighlightedTile = INDEX_NONE;

	// 슬라이드 애니메이션 상태.
	bool bSliding = false;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SlidingComp;
	FVector SlideFrom = FVector::ZeroVector;
	FVector SlideTo = FVector::ZeroVector;
	float SlideElapsed = 0.f;

	// 완성 보상 연출 상태.
	ESolvePhase SolvePhase = ESolvePhase::None;
	float SolvePhaseElapsed = 0.f;
	/** 연출 때 바닥에서 솟는 9번째(빠졌던) 조각. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NinthTile;

	/** N×N 타일 컴포넌트를 생성·배치한다(런타임). */
	void BuildTiles();

	/** 생성한 타일 컴포넌트를 모두 파괴하고 상태를 비운다. */
	void DestroyTiles();

	/** 랜덤 유효 이동으로 셔플(시드 결정론, 항상 풀이 가능). */
	void ShufflePuzzle();

	/** 조준 트레이스로 타일ID와 그 슬롯을 얻는다. 성공 시 true. */
	bool TraceForTile(int32& OutTileId, int32& OutSlot) const;

	/** 슬롯이 빈칸과 직교 인접한가(맨해튼 거리 1). */
	bool IsSlotAdjacentToEmpty(int32 Slot) const;

	/** 타일을 빈칸으로 옮긴다(논리 즉시 갱신 + 비주얼 보간/스냅). */
	void SlideTileToEmpty(int32 TileId, int32 FromSlot);

	/** 셔플/이동 공통: 타일ID를 현재 빈칸으로 이동(논리 모델만 갱신). 타일이 새로 차지한 슬롯을 반환. */
	int32 MoveTileLogical(int32 TileId);

	/** 모든 타일이 제자리(타일ID == 슬롯)인가. */
	bool IsSolved() const;

	/** 완성 보상 연출 시작(9번째 조각 솟기 준비). */
	void StartSolveSequence();

	/** Tick에서 보상 연출 단계 진행(솟기→합쳐짐→열쇠). */
	void AdvanceSolveSequence(float DeltaSeconds);

	/** 보드 중앙 위에 열쇠를 스폰한다. */
	void SpawnKey();

	/** 디버그: 보드를 즉시 정답으로 만들고 완성 연출을 시작한다. */
	void SolveInstant();

	/** 디버그: 이 퍼즐이 플레이어와 가장 가까운 퍼즐인가. */
	bool IsNearestPuzzleToPlayer() const;

	/** 타일ID에 맞는 머티리얼(그림 다이내믹 인스턴스 또는 TileMaterial)을 만든다. 없으면 null. */
	UMaterialInterface* CreateTileMaterial(int32 TileId);

	/** 조준 가능 시 하이라이트 갱신(이전 것 해제). */
	void UpdateHighlight(int32 DesiredTile);

	FORCEINLINE int32 Idx(int32 Col, int32 Row) const { return Row * GridSize + Col; }

	/** 슬롯 인덱스 → 액터 로컬 타일 중심 위치(바닥 평면). 간격 포함 기본 스텝 사용. */
	FVector SlotLocalLocation(int32 Slot) const;

	/** 슬롯 → 로컬 위치를 임의 스텝으로 계산(합쳐짐 연출은 간격 0인 TileSize 스텝 사용). */
	FVector SlotLocalLocationStep(int32 Slot, float Step) const;
};
