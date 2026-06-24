// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Math/RandomStream.h"
#include "Templates/SubclassOf.h"
#include "MazeGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class AMazeExit;
class AMazeChaser;

/**
 * 시드 기반 결정론적 랜덤 미로 생성기 (Phase 1).
 * FRandomStream(Seed)로 DFS 백트래킹 미로를 만들고, 벽을 HISM 인스턴스로 배치한다.
 * 같은 Seed면 항상 동일한 미로가 나온다.
 */
UCLASS()
class RANDOMMAZE_API AMazeGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMazeGenerator();

	// --- 미로 파라미터 (디테일 패널 / BP에서 조정) ---

	/** 난수 시드. 같은 시드면 항상 같은 미로가 생성된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 Seed = 1337;

	/** 미로 격자 가로 셀 수. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "16384", UIMax = "2048"))
	int32 GridWidth = 10;

	/** 미로 격자 세로 셀 수. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "16384", UIMax = "2048"))
	int32 GridHeight = 10;

	/** 셀 한 칸의 한 변 길이(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1.0"))
	float CellSize = 400.f;

	/**
	 * 모서리 메움(코너 겹침) 한도(cm). 실제 벽 두께는 메시 단면 비율이 결정한다(균일 수평 스케일).
	 * 직각으로 만나는 벽이 코너에서 겹치도록 길이를 이 값만큼 늘리되, 실제 두께를 넘지 않게 캡되어
	 * 통로로 튀어나오지 않는다. 큰 값을 줘도 자동으로 두께 선까지만 사용된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.0"))
	float WallThickness = 20.f;

	/** 벽 높이(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1.0"))
	float WallHeight = 400.f;

	/** 벽에 사용할 스태틱 메시. 기본은 엔진 큐브, 나중에 Fab 벽 메시로 교체 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	TObjectPtr<UStaticMesh> WallStaticMesh;

	/**
	 * 벽 메시의 "길이" 축이 로컬 +X가 아닐 때 보정하는 추가 Yaw(도).
	 * 코드는 메시가 로컬 X=길이, Y=두께, Z=높이로 제작됐다고 가정한다.
	 * 임포트된 Fab 벽이 다른 방향이면(예: 길이가 Y축) 여기서 90/180/270을 넣어 맞춘다.
	 * 재컴파일 없이 디테일 패널에서 바로 조절 가능.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float WallMeshYawOffset = 0.f;

	/** 플레이어 중심으로 이 반경(셀) 안의 벽만 렌더한다. 대형 미로에서 렌더 메모리를 일정하게 유지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1"))
	int32 RenderRadiusCells = 40;

	/** 플레이어 위치를 점검해 렌더 윈도우를 갱신하는 주기(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.02"))
	float WindowUpdateInterval = 0.2f;

	/** 생성(데이터) 허용 최대 셀 수. 데이터는 1바이트/셀이라 저렴. 초과 시 생성 거부(크래시 방지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Safety", meta = (ClampMin = "1"))
	int32 MaxCells = 100000000;

	/** 에디터 미리보기에서 렌더할 최대 셀 수(원점 주변). 데이터는 전체 생성하되 렌더만 제한. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Safety", meta = (ClampMin = "1"))
	int32 EditorPreviewMaxCells = 250000;

	/** 뚫을 공터(방) 개수. 같은 Seed면 같은 위치. 0이면 방 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Rooms", meta = (ClampMin = "0"))
	int32 RoomCount = 5;

	/** 방 한 변의 최소 셀 수. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Rooms", meta = (ClampMin = "2"))
	int32 RoomMinSize = 3;

	/** 방 한 변의 최대 셀 수. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Rooms", meta = (ClampMin = "2"))
	int32 RoomMaxSize = 6;

	/** 켜면 BeginPlay에 레벨의 퍼즐/목표 액터를 방에 분배 배치한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Rooms")
	bool bDistributeRoomActors = true;

	// --- 피날레(클리어 후 탈출극) ---

	/** 켜면 목표 클리어 시 미로 실시간 재배열 + 탈출 출구 생성을 시작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	bool bEnableFinale = true;

	/** 플레이어가 이 거리(cm) 안으로 다가온 '구간'이 열린다(코너까지 통째로). 클수록 더 멀리 앞서 열림. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "0.0"))
	float OpenAheadDistance = 1000.f;

	/** 벽 하나가 미끄러져 완전히 열리는 데 걸리는 시간(초). 클수록 천천히. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "0.05"))
	float WallSlideDuration = 0.9f;

	/** 슬라이드 개방 시 도는 수직축(yaw) 회전량(도). 0이면 회전 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	float WallOpenSpinDegrees = 120.f;

	/** 문(여닫이) 방식 회전 각도(도). 90이면 인접 슬롯에 플러시로 활짝 열려 통로를 막지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	float WallSwingDegrees = 90.f;

	/** 플레이어가 경로 셀에 이 거리(cm) 안으로 다가오면, 그 셀의 '길이 아닌' 옆 통로에 벽이 새로 생겨 닫힌다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "0.0"))
	float CloseAheadDistance = 700.f;

	/** 탈출 출구로 스폰할 액터 클래스(비우면 AMazeExit 기본). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	TSubclassOf<AMazeExit> ExitClass;

	/** 켜면 피날레에 추격자가 등장한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	bool bEnableChaser = true;

	/** 추격자로 스폰할 액터 클래스(비우면 AMazeChaser 기본). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale")
	TSubclassOf<AMazeChaser> ChaserClass;

	/** 추격자 이동 속도(cm/s). 도보(~600)보다 약간 느리게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "1.0"))
	float ChaserSpeed = 500.f;

	/** 플레이어가 메인 경로를 이만큼(칸) 전진하면 추격자가 등장(뒤에서). 시작점 겹침 즉사 방지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "0"))
	int32 ChaserStartCells = 3;

	/** 추격자가 플레이어를 이 거리(cm) 안으로 좁히면 잡힌 것으로 처리. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "1.0"))
	float ChaserCatchRadius = 150.f;

	/** 잡힘 시 암전(페이드 아웃)·복귀(페이드 인) 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Finale", meta = (ClampMin = "0.0"))
	float CaughtFadeTime = 0.5f;

	/** 미로 데이터를 (재)생성한다(렌더는 윈도우가 담당). 디테일 패널 버튼 또는 BP에서 호출 가능. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Maze")
	void GenerateMaze();

	/** 배치된 모든 벽 인스턴스를 제거한다. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Maze")
	void ClearMaze();

	/** 배치된 방(공터) 개수. 다른 액터가 방에 배치될 때 조회. */
	UFUNCTION(BlueprintCallable, Category = "Maze|Rooms")
	int32 GetRoomCount() const { return Rooms.Num(); }

	/** RoomIndex번째 방 중심의 월드 좌표(바닥면 Z=0 기준). 범위 밖이면 액터 위치 반환. */
	UFUNCTION(BlueprintCallable, Category = "Maze|Rooms")
	FVector GetRoomCenterWorld(int32 RoomIndex) const;

	/** (X,Y) 셀 중심의 월드 좌표(바닥면 Z=0 기준). 출구 등 임의 셀 배치에 사용. */
	UFUNCTION(BlueprintCallable, Category = "Maze")
	FVector GetCellCenterWorld(int32 X, int32 Y) const;

protected:
	/** 에디터에서 프로퍼티 변경 시/스폰 시 자동으로 미로를 다시 생성한다(렌더는 안전 한도 내 미리보기). */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 런타임에서 플레이어 추적 렌더 윈도우 타이머를 시작한다. */
	virtual void BeginPlay() override;

	/** 피날레 길 열기 애니메이션 구동(피날레 중에만 켜짐). */
	virtual void Tick(float DeltaSeconds) override;

	/** 벽 인스턴싱용 ISM 컴포넌트(루트). 자주 갱신되므로 비계층 ISM 사용(깜빡임 방지). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UInstancedStaticMeshComponent> WallISM;

private:
	/** 셀별 벽 비트마스크 저장 (크기 = GridWidth * GridHeight). */
	TArray<uint8> Cells;

	FORCEINLINE int32 Index(int32 X, int32 Y) const { return Y * GridWidth + X; }

	/** FRandomStream 기반 반복 DFS 백트래킹으로 통로를 뚫는다(벽 데이터 산출). */
	void CarveMaze();

	/** CarveMaze 후처리: 결정론적으로 직사각형 방을 골라 내부 벽을 연다(연결성 보존). */
	void CarveRooms();

	/** 배치된 방들(셀 좌표, Max 배타적). 다음 단계의 퍼즐 배치 등에서 재사용. */
	TArray<FIntRect> Rooms;

	/** 레벨의 퍼즐/목표 액터를 방에 분배한다(퍼즐=서로 다른 방, 목표=가장 먼 방). */
	void DistributeRoomActors();

	/** 시작점(0,0)에서 가장 먼 방의 인덱스(목표지점용). */
	int32 GetGoalRoomIndex() const;

	/** Center 셀 기준 ±Radius 윈도우 안의 벽만 HISM 인스턴스로 배치한다(기존 인스턴스는 교체).
	 *  bRecordInstanceMap이 켜져 있으면 (X,Y,Side)→인스턴스 인덱스를 WallInstanceIndex에 기록한다. */
	void BuildWallsInWindow(FIntPoint Center, int32 Radius);

	/** 런타임: 플레이어 위치를 셀로 변환해 윈도우 중심이 바뀌었으면 다시 렌더한다. */
	void UpdateRenderWindow();

	/** 플레이어 폰 위치를 액터 로컬좌표 → 클램프된 셀(FIntPoint)로 변환. 폰 없으면 액터 위치 기준. */
	FIntPoint GetPlayerCell() const;

	// --- 피날레: 벽을 열고(치우고) 닫으며(생성) 출구까지 길을 만드는 연출 ---

	/** 벽 개폐 애니메이션 방식. 여러 방식을 섞어 다채롭게 길을 만든다. */
	enum class EWallAnimStyle : uint8
	{
		Slide,     // 옆으로 미끄러짐(+약간 회전).
		SwingDoor, // 한쪽 끝을 축으로 문처럼 여닫힘.
		RiseFall,  // 바닥에서 솟거나 바닥으로 가라앉음(닫기 전용 사용).
	};

	/** 진행 중인 한 벽 개폐 애니메이션(열기=치움, 닫기=생성). */
	struct FWallAnim
	{
		int32 InstanceIndex = INDEX_NONE;
		EWallAnimStyle Style = EWallAnimStyle::Slide;
		bool bOpening = true;     // true=열림(있다가 사라짐), false=닫힘(없다가 생김).
		float Elapsed = 0.f;
		FIntPoint CellA = FIntPoint::ZeroValue; // 이 벽이 가르는 두 셀(완료 시 비트 갱신).
		FIntPoint CellB = FIntPoint::ZeroValue;
		// 기하(제자리=닫힘 상태 기준).
		FQuat BaseRot = FQuat::Identity;
		FVector Scale = FVector::OneVector;
		FVector Center = FVector::ZeroVector;       // 제자리일 때 바운드 중심(액터 로컬).
		FVector BoundsOffset = FVector::ZeroVector; // 스케일된 피벗 보정.
		FVector SlideDir = FVector::ForwardVector;  // 슬라이드 방향(벽 길이축).
		FVector PivotOffset = FVector::ZeroVector;  // 문 경첩축 = Center + PivotOffset.
		float SwingSign = 1.f;                      // 문 회전 방향(+1/-1).
		// 열기 완료 시 벽이 안착하는 인접 빈 슬롯(이 슬롯을 봉인 처리·이중 방지). 무효=(-1,-1).
		FIntPoint RestSlotA = FIntPoint(-1, -1);
		FIntPoint RestSlotB = FIntPoint(-1, -1);
	};

	/** 코너 단위로 한 번에 열리는 직선 구간(메인 경로). */
	struct FRevealSegment
	{
		TArray<TPair<FIntPoint, FIntPoint>> Edges;  // 이 구간에서 열 벽들(인접 셀 쌍).
		FVector TriggerWorld = FVector::ZeroVector; // 구간 시작 셀 월드(플레이어가 근접하면 열림).
		int32 Parent = INDEX_NONE;                  // 직전 구간(-1=루트). 부모가 열려야 열림.
		EWallAnimStyle Style = EWallAnimStyle::Slide; // 이 구간 벽들의 개방 방식.
		bool bOpened = false;
	};

	/** (X,Y,Side)를 하나의 정수 키로 인코딩. Side: 0=North,1=East,2=South(경계),3=West(경계). */
	int64 EncodeWallKey(int32 X, int32 Y, int32 Side) const;

	/** 인접 두 셀 A,B 사이 벽의 렌더 인스턴스 인덱스를 찾는다(없으면 false). */
	bool FindWallInstance(FIntPoint A, FIntPoint B, int32& OutIndex) const;

	/** 인접 두 셀 A,B 사이의 벽 비트를 양쪽에서 제거한다. */
	void ClearWallBetween(FIntPoint A, FIntPoint B);

	/** 인접 두 셀 A,B 사이에 벽 비트를 양쪽에서 추가한다(닫기). */
	void SetWallBetween(FIntPoint A, FIntPoint B);

	/** A,B 사이에 벽이 있는지(닫혀 있는지). 인접 아님/범위 밖은 true(막힘) 취급. */
	bool IsWallClosed(FIntPoint A, FIntPoint B) const;

	/** 인접 두 셀 A,B를 잇는 벽의 소유 셀/면(Side: 0=North, 1=East)을 구한다. 인접 아니면 false. */
	bool EdgeOwner(FIntPoint A, FIntPoint B, int32& OutX, int32& OutY, int32& OutSide) const;

	/** 인접 두 셀 A,B 사이 벽의 정규화 키(EncodeWallKey 기반). 인접 아니면 -1. */
	int64 EdgeKey(FIntPoint A, FIntPoint B) const;

	/** 벽 메시 스케일/피벗 보정(렌더와 동일 규칙, 한 곳에서 산출). */
	void GetWallMeshScaling(FVector& OutScale, FVector& OutBoundsOffset) const;

	/** (X,Y,Side) 벽의 기하(회전/스케일/중심/피벗보정). 렌더(EmplaceWall)와 동일 규칙. */
	bool ComputeWallGeom(int32 X, int32 Y, int32 Side, FQuat& OutRot, FVector& OutScale, FVector& OutCenter, FVector& OutBoundsOffset) const;

	/** 개방량 OpenAmount(0=제자리, 1=완전개방/사라짐)에 해당하는 인스턴스 트랜스폼. */
	FTransform AnimXform(const FWallAnim& A, float OpenAmount) const;

	/** 간선(A,B) 벽이 Desired 방식으로 열릴 때, 길을 막지 않게 안착할 '비경로 빈 인접 슬롯'을 찾는다.
	 *  슬라이드/문 모두 불가하면 RiseFall(바닥으로 가라앉아 사라짐)로 폴백.
	 *  Out: 슬라이드 방향/문 피벗/문 회전 부호/안착 슬롯(A,B). 폴백이면 안착 슬롯은 (-1,-1). */
	EWallAnimStyle ResolveAnimStyle(FIntPoint A, FIntPoint B, EWallAnimStyle Desired,
		FVector& OutSlideDir, FVector& OutPivotOffset, float& OutSwingSign,
		FIntPoint& OutRestA, FIntPoint& OutRestB) const;

	/** 경로 간선(A,B)의 벽을 '열기'로 예약(렌더된 인스턴스가 있을 때만). */
	void ScheduleOpenEdge(FIntPoint A, FIntPoint B, EWallAnimStyle Style);

	/** 경로 아닌 간선(A,B)에 벽을 '닫기'로 예약. 치운 벽(FreedWallPool) 재사용 + 바닥 아래에서 시작(팝 없음). */
	void ScheduleCloseEdge(FIntPoint A, FIntPoint B, EWallAnimStyle Style);

	/** 방(R) 내부 벽을 열어 공터로 만든다(피날레 폐쇄 후 목표 방 크기 유지용). */
	void OpenRoomInterior(const FIntRect& R);

	/** 잡힘 시 호출: 미로 원본 복구 + 추격자/출구 제거 + 목표 미클리어/열쇠 복구(피날레 전 상태로). */
	void ResetFinaleToPreClear();

	/** 목표 클리어 시 호출(AGoalPoint::OnGoalCleared 구독). 출구 스폰 + 길 열기 시퀀스 시작. */
	UFUNCTION()
	void HandleGoalCleared();

	/** 출구 도달 시 호출(AMazeExit::OnEscaped 구독). 피날레 종료. */
	UFUNCTION()
	void HandleEscaped();

	/** 추격자에게 잡혔을 때 호출(AMazeChaser::OnPlayerCaught 구독). 암전 → 목표 앞 리스폰. */
	UFUNCTION()
	void HandlePlayerCaught();

	/** 암전된 동안 플레이어를 목표 셀 앞으로 옮기고 추격자를 리셋한 뒤 화면을 밝힌다. */
	void RespawnAtGoal();

	/** 피날레 진행 중 여부(Tick 구동 조건). */
	bool bFinaleActive = false;

	/** 켜면 BuildWallsInWindow가 WallInstanceIndex를 채운다(피날레 렌더 시에만). */
	bool bRecordInstanceMap = false;

	/** (X,Y,Side) 키 → 렌더된 벽 인스턴스 인덱스(피날레 길찾기/애니용). */
	TMap<int64, int32> WallInstanceIndex;

	/** 열 구간들(메인 경로). 부모-자식 순서로 proximity 개방. */
	TArray<FRevealSegment> Segments;

	/** 지금까지 열기 시작한 벽 총 개수(로그/디버그용). */
	int32 OpenedWallCount = 0;

	/** 진행 중인 벽 개폐 애니메이션들. */
	TArray<FWallAnim> ActiveAnims;

	/** 열기 완료로 풀린(바닥 아래 파킹) 벽 인스턴스 — 닫기에서 재배치 재사용. */
	TArray<int32> FreedWallPool;

	/** 메인 경로 셀들(닫기 트리거 검사용). */
	TArray<FIntPoint> RouteCells;

	/** 각 경로 셀의 옆 통로 닫기 처리 여부. */
	TArray<bool> RouteCellClosed;

	/** 메인 경로 간선 키 집합(이 벽들은 절대 닫지 않는다 → 길 보장). */
	TSet<int64> RouteEdgeSet;

	/** 이미 개폐 애니를 건 간선 키(중복 방지). */
	TSet<int64> AnimatedEdges;

	/** 피날레 연출용 난수(개폐 방식 선택). */
	FRandomStream FinaleStream;

	/** 목표 방(시작 공간) — 이 안의 셀은 옆 통로 닫기에서 제외. */
	FIntRect FinaleGoalRoom = FIntRect();
	bool bFinaleHasGoalRoom = false;

	/** 스폰한 출구 액터(약참조). */
	TWeakObjectPtr<AMazeExit> ExitActor;

	/** 스폰한 추격자(약참조). */
	TWeakObjectPtr<AMazeChaser> Chaser;

	/** 잡힘→리스폰 위치(목표 셀 앞, 월드). */
	FVector FinaleStartWorld = FVector::ZeroVector;

	/** 잡힘 처리(페이드~리스폰) 진행 중 — 중복 방지. */
	bool bHandlingCaught = false;

	/** 피날레 시도 횟수(경로 RNG에 섞어 재도전마다 다른 길). */
	int32 FinaleAttempt = 0;

	/** 잡힘 시 암전 후 리스폰 타이머. */
	FTimerHandle RespawnTimer;

	/** 마지막으로 렌더한 윈도우 중심 셀. 변화가 없으면 재빌드 생략. */
	FIntPoint LastWindowCenter = FIntPoint(MIN_int32, MIN_int32);

	/** 렌더 윈도우 갱신 타이머 핸들. */
	FTimerHandle WindowTimer;
};
