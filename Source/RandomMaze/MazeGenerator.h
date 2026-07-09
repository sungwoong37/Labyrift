// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Math/RandomStream.h"
#include "Templates/SubclassOf.h"
#include "MazeGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class AMazeExit;
class AMazeChaser;
class ASlidingTilePuzzle;
class USpotLightComponent;
class UCameraComponent;
class APawn;
class UMaterialParameterCollection;
class UMaterialInterface;

/**
 * 시드 기반 결정론적 랜덤 미로 생성기 (Phase 1).
 * FRandomStream(Seed)로 DFS 백트래킹 미로를 만들고, 벽을 ISM 인스턴스로 배치한다.
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

	// --- 외곽 성벽(테두리) — 미로 바깥 둘레를 감싸는 크고 높은 벽. 시각/경계용. 서쪽 시작 입구만 트임. ---

	/** 켜면 미로 바깥 둘레에 크고 높은 외곽 성벽(테두리)을 두른다. 시작 구역(서쪽 입구)만 트여 통행 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Border")
	bool bBorderWall = true;

	/** 외곽 성벽 높이(cm). 내부 벽(WallHeight=기본 400)보다 훨씬 높게 두어 미로를 '요새'처럼 가둔다(엄청 높게). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Border", meta = (ClampMin = "1.0"))
	float BorderWallHeight = 4000.f;

	/** 외곽 성벽 두께(cm). 두껍게 두어 웅장한 테두리 느낌. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Border", meta = (ClampMin = "1.0"))
	float BorderWallThickness = 300.f;

	/** 미로 바깥 가장자리와 성벽 안쪽 면 사이 여백(cm). 0=미로 외벽에 바짝 붙임. 크게 하면 성벽이 더 멀리 물러난다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Border", meta = (ClampMin = "0.0"))
	float BorderWallMargin = 0.f;

	/** 외곽 성벽 머티리얼(선택). 미지정 시 원거리 벽 머티리얼(FarWallMaterial)→메시 기본 순으로 폴백. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Border")
	TObjectPtr<UMaterialInterface> BorderWallMaterial;

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

	/** 플레이어 중심으로 이 반경(셀) 안의 근거리 벽(충돌·풀 머티리얼)만 렌더한다. 대형 미로에서 렌더 메모리를 일정하게 유지.
	 *  일반 모드에선 이 반경 밖을 원거리 HISM(bAutoFarCull로 이 반경 근처까지만)과 안개(bAutoFogFromRenderDistance)가 덮어
	 *  하드 컷이 안개 뒤에 숨는다 → 작게 잡을수록 렌더 비용↓이고 컷도 안 보인다. (시프트 모드는 전체 렌더라 이 값을 안 씀.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1"))
	int32 RenderRadiusCells = 18;

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

	// --- 시작 구역 — 미로 '밖'(서쪽) 마당+전망대에서 시작, 테두리 입구로 진입(미로와 시작 지점 분리) ---

	/** 켜면 미로 서쪽 밖에 시작 구역(마당+전망대+경사로)을 만들고 (0,0) 서쪽 테두리에 입구를 뚫는다.
	 *  미로 데이터 자체는 건드리지 않으므로(테두리 비트 1개 제외) 같은 Seed의 방 배치가 그대로 유지된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start")
	bool bStartZone = true;

	/** 켜면 BeginPlay 직후 플레이어를 시작 구역(전망대 위)으로 순간이동시킨다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start")
	bool bTeleportPlayerToStart = true;

	/** 시작 전망대(단상) 높이(cm). >0이면 단상+경사로가 생겨 벽 너머 미로 전경이 내려다보인다.
	 *  0이면 전망대 없이 평지 시작(마당 중앙 스폰) — 기본값. 밀폐 시작 구역엔 전망대가 없는 게 자연스럽다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start", meta = (ClampMin = "0.0"))
	float StartPlatformHeight = 0.f;

	/** 시작 구역(마당)을 입구 쪽으로 늘리는 추가 길이(cm). 클수록 시작 지점과 미로 사이가 멀어진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start", meta = (ClampMin = "0.0"))
	float StartCorridorLength = 1500.f;

	/** 켜면 시작 구역 '전체'를 천장+좌우 측벽+뒷벽으로 덮어 완전 밀폐한다(위/옆/뒤로 하늘·곡면 노출 차단, 미로 진입 전 시야 격리). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start")
	bool bStartCorridorRoof = true;

	/** 시작 구역 밀폐 천장 높이(cm, 바닥 기준). 벽 높이쯤이 자연스럽다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Start", meta = (ClampMin = "50.0"))
	float StartCorridorHeight = 400.f;

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

	// --- 관측-반응 시프트 모드 ("어둠 속, 안 보면 움직이는 미로") ---

	/** 켜면 플레이어가 보지 않는(빛 밖 어둠) 근처 벽이 주기적으로 재배열된다(앞 열고 뒤 닫기). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift")
	bool bShiftMode = false;

	/** 시프트 1회 사이클 주기(초). 짧을수록 자주 바뀜. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.05"))
	float ShiftInterval = 1.5f;

	/** 한 사이클에 바꿀 최대 벽 수(열기+닫기 합). 작을수록 국소적으로 스멀스멀 바뀜. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "1"))
	int32 ShiftBudgetPerCycle = 3;

	/** 관측(얼림) 판정용 카메라 원뿔 반각(도). 손전등 OuterConeAngle과 대충 맞춘다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float ObserveConeAngle = 50.f;

	/** 관측(얼림) 판정 최대 거리(cm). 이 안 + 원뿔 안의 벽은 얼어붙는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.0"))
	float ObserveRange = 1500.f;

	/** 플레이어 주변 이 반경(cm) 안의 벽은 시야와 무관하게 항상 얼림(코앞 봉쇄/끼임 방지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.0"))
	float FreezeRadius = 250.f;

	/** 경로상 플레이어 앞 이 칸 수까지의 닫힌 경로 간선을 (어둠 속이면) 연다. 클수록 더 멀리 앞서 길이 열림.
	 *  ShiftInnerRadius(근접 무변화 반경) 밖에도 시프트 밴드가 남도록 이 값을 반경보다 넉넉히 크게 둔다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "1"))
	int32 ShiftForwardCells = 16;

	/** 경로상 플레이어 뒤 이 칸 수까지의 경로 셀에서, 비경로 옆 간선을 (어둠 속이면) 닫는다(백트래킹 봉인). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "1"))
	int32 ShiftBackCells = 12;

	/** 플레이어 이 반경(cm) 안의 벽은 시프트 대상에서 제외 → '멀리 있는 벽만' 재배열된다(근처는 항상 고정).
	 *  FreezeRadius(끼임 방지, 작게)와 별개의 '근접 안정 구역'. 크게 잡을수록 더 멀리서만 바뀐다.
	 *  단, 이 반경이 ShiftForwardCells·ShiftBackCells(셀×CellSize)를 넘으면 후보가 없어 시프트가 멈추니 함께 키운다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.0"))
	float ShiftInnerRadius = 1100.f;

	/** 켜면 시프트 척추 경로를 '가장 가까운 미해결 퍼즐'로 동적 재조준한다(풀면 다음 퍼즐 → 다 풀면 목표=키 꽂는 곳).
	 *  끄면 시작 시 정한 목표 셀로만 인도(기존 동작). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift")
	bool bShiftGuideToPuzzles = true;

	/** 켜면 시프트 후보 벽(얼림/열기후보/닫기후보)과 원뿔을 디버그 드로로 표시(개발용, 기본 꺼짐). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift")
	bool bShiftDebugDraw = false;

	/** 켜면 시프트 모드에서 플레이어 카메라에 손전등(스포트라이트)을 코드로 부착(어둠 동행, BP 불필요). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift")
	bool bShiftFlashlight = true;

	/** 손전등 밝기. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.0"))
	float FlashlightIntensity = 5000.f;

	/** 시프트 모드 벽 렌더 거리(cm). 이 거리 너머 벽 인스턴스는 '렌더만' 생략한다(WallISM 거리 컬링) → Draw/GPU 비용↓.
	 *  ★인스턴스/인덱스는 그대로라 개폐 애니·피날레 불변식에 영향 없음(윈도우화가 아님). 0=컬링 없음(전체 렌더).
	 *  ★★반드시 CurveStartDist(월드 커브 시작 거리, 기본 8000)보다 넉넉히 크게 둘 것 — 그보다 작으면 휘어지는 구간이
	 *  통째로 컬링돼 커브가 안 보인다. 등방성(3D 반경) 컬링이라 '휘어 올라간 먼 벽'과 '평평한 먼 벽'을 방향으로 구분하진 못한다
	 *  (같은 반경이면 둘 다 잘림). 크게=커브 잘 보임/무거움, 작게=가벼움/커브 잘림. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Shift", meta = (ClampMin = "0.0"))
	float ShiftRenderDistance = 20000.f;

	// --- 월드 커브(인셉션식 곡면) — 멀리 있는 벽/랜드마크가 위로 휘어 보여 방향감을 준다(시각만, 충돌 무관) ---

	/** 켜면 시프트 시작 시 곡률(MPC Curvature)을 적용한다. 휨은 머티리얼 WPO가 담당(충돌/이동은 평면 유지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	bool bWorldCurve = true;

	/** 곡률 강도(rad/cm). 정점을 카메라 Y축 둘레로 θ = (Y거리 − CurveStartDist) × CurveStrength 만큼 회전.
	 *  클수록 급하게 말려 올라간다. 시프트 사이클마다 재주입 → PIE 중 수정 즉시 반영. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve", meta = (ClampMin = "0.0"))
	float CurveStrength = 0.00004f;

	/** 플레이어 근처 이 Y거리(cm) 안은 완전 평지, 그 밖부터 휘기 시작. MPC 'CurveStartDist'로 주입. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve", meta = (ClampMin = "0.0"))
	float CurveStartDist = 8000.f;

	/** 곡률 스칼라('Curvature'/'CurveStartDist')를 담은 Material Parameter Collection. 기본 /Game/Maze/MPC_Curve 자동 로드. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TObjectPtr<UMaterialParameterCollection> CurveMPC;

	/** 켜면 목표 셀에 발광 비콘을 스폰(어둠 속에서도 멀리 휘어 보이는 목표 마커). 선택. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	bool bSpawnGoalBeacon = false;

	/** 발광 비콘으로 스폰할 액터 클래스(키 큰 emissive 메시 권장). bSpawnGoalBeacon이 켜져 있고 지정됐을 때만. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TSubclassOf<AActor> GoalBeaconClass;

	/** 켜면 시프트 모드에서 각 퍼즐 방에도 발광 비콘 기둥을 세운다(어둠 속 멀리서 커브로 휘어 보이는 퍼즐 위치 표시). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	bool bSpawnPuzzleBeacons = true;

	/** 퍼즐 비콘으로 스폰할 액터 클래스(키 큰 emissive 메시). 미지정 시 GoalBeaconClass로 폴백. 해결된 퍼즐 비콘은 숨겨진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TSubclassOf<AActor> PuzzleBeaconClass;

	// --- 휘는 바닥(시프트 모드) — 레벨 평면 바닥은 정점 4개라 WPO로 안 휘므로, 셀 단위 타일 ISM으로 깔아 같은 커브 머티리얼 적용 ---

	/** 켜면 시프트 모드에서 셀마다 바닥 타일(ISM)을 깔아 벽과 같은 커브 머티리얼로 휘게 한다(멀리 타일일수록 들려 곡면 근사). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	bool bShiftCurvedFloor = true;

	/** 바닥 타일 메시(기본 엔진 Cube → 얇은 슬래브로 스케일). 닫힌 메시라 커브로 멀리서 위로 말려도
	 *  아랫면이 그려진다(Plane은 단면이라 말리는 순간 백페이스 컬링으로 사라짐). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TObjectPtr<UStaticMesh> FloorStaticMesh;

	/** 바닥 타일 머티리얼. 벽과 같은 커브 WPO 머티리얼을 지정하면 휜다. 비우면 메시 기본 머티리얼(평평). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TObjectPtr<UMaterialInterface> FloorMaterial;

	/** 시프트 시작 시 숨길 레벨의 평평한 바닥 액터(Floor_0 등). 휘는 타일과 겹치지 않도록. 충돌은 유지(시각만 숨김 → 평면 위를 걷는다). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Curve")
	TObjectPtr<AActor> FloorActorToHide;

	// --- 거리 안개 — ExponentialHeightFog를 코드로 스폰해 '천장(위쪽 빈 공간)'과 'X방향 먼 거리'를 함께 정리 ---

	/** 켜면 시프트 모드 시작 시 ExponentialHeightFog를 코드로 스폰/조정한다. 천장 부재(하늘)와 수평 먼 벽을 안개로 덮어 시야를 손전등 반경으로 가둔다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	bool bShiftFog = true;

	/** 켜면 일반 모드에서도 같은 거리 안개를 스폰한다. bAutoFogFromRenderDistance와 짝을 이뤄, 줄인 렌더 거리 경계를 안개로 가려 자연스럽게 만든다.
	 *  일반 모드가 안개 없는 밝은 미로여야 한다면 끈다(레벨에 이미 있는 ExponentialHeightFog 파라미터를 덮어쓰지 않게 됨). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	bool bNormalFog = true;

	/** 켜면 일반 모드 안개의 시작거리·밀도를 '실제 벽 렌더 거리'(원거리 컷 또는 RenderRadiusCells)에서 자동 산출한다 →
	 *  렌더 거리를 줄이면 안개가 그만큼 앞당겨 짙어져 컷 경계를 항상 가린다(FogStartDistance·FogDensity는 무시). 끄면 아래 수동값 사용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	bool bAutoFogFromRenderDistance = true;

	/** 안개 밀도(수동값). bAutoFogFromRenderDistance가 꺼졌을 때(및 시프트 모드에서) 사용. 클수록 짙어 가까이서 페이드.
	 *  시프트 모드: 크게 잡을수록 먼 벽이 '실루엣'으로만 읽힌다(먼 배경=균일한 FogColor, 그 앞 검은 벽=어두운 윤곽).
	 *  ★단 너무 크면 커브(CurveStartDist=80m+)까지 완전 불투명이 돼 실루엣이 아니라 아예 사라진다 — 커브가 남는 선까지만. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0"))
	float FogDensity = 0.12f;

	/** 높이 감쇠. 작을수록 위쪽(천장 방향)까지 안개가 고르게 차올라 천장 부재를 가린다(천장 정리엔 0.01 이하 권장). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0"))
	float FogHeightFalloff = 0.005f;

	/** 안개 시작 수평 거리(cm). 이 거리 너머부터 안개가 짙어져 X방향 먼 벽을 정리한다(손전등 사거리 근처 권장). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0"))
	float FogStartDistance = 600.f;

	/** 안개 색(=inscattering, self-lit). 천장/먼 거리가 이 색으로 채워지며 **씬 조명과 무관하게 스스로 빛난다** →
	 *  이 값이 곧 안개의 밝기다(조명 lux를 낮춰도 이건 안 어두워짐). '어둡고 안개낀' 분위기엔 검정에 가깝게(≈0.02).
	 *  단 너무 검으면 안개 자체가 안 보이고(먼 벽이 그냥 어둠에 잠김), 시프트 실루엣 대비도 약해진다 — 취향껏 미세조정. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	FLinearColor FogColor = FLinearColor(0.012f, 0.012f, 0.016f, 1.f);

	/** 안개 최대 불투명도(1=완전히 가림). 천장(무한 거리 하늘)을 확실히 덮으려면 1 권장. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FogMaxOpacity = 1.f;

	/** 켜면 벽 위쪽(천장 방향)에 2차 안개 레이어를 얹어 위를 볼 때 안개가 더 짙다(수평 안개와 독립 조절). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	bool bCeilingFog = true;

	/** 천장(2차) 안개 밀도. 주 안개(FogDensity)보다 크게 잡아 위쪽을 확실히 덮는다.
	 *  ★월드 커브로 '위로 말려 올라간 먼 벽/렌더 컷'을 가리는 핵심 레버 — 크게 할수록 휘어지는 쪽이 짙게 덮인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0"))
	float CeilingFogDensity = 0.3f;

	/** 천장 안개 레이어의 높이 오프셋(cm, 주 안개 기준 높이에서 위로). 벽 높이 위쯤에 두면 벽 너머 위가 덮인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog")
	float CeilingFogHeightOffset = 600.f;

	/** 천장 안개의 상방 감쇠. 작을수록 위로 더 높이 차오른다 → 커브로 높이 말려 올라간 먼 벽까지 덮으려면 작게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Fog", meta = (ClampMin = "0.0"))
	float CeilingFogHeightFalloff = 0.01f;

	// --- 원거리 필드(Far Field) — 렌더 윈도우 밖을 '값싼 벽/바닥'으로 채워 하드 컷 없이 먼 거리까지 보이게 ---

	/** 켜면 일반 모드에서 미로 전체를 덮는 원거리 벽 레이어(HISM, 충돌/그림자 없음)를 깐다.
	 *  근거리 윈도우 벽이 위에 겹쳐 그려지므로 창 이동 시 리빌드가 필요 없다(BeginPlay 1회 빌드). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField")
	bool bFarWalls = true;

	/** 원거리 벽 머티리얼(텍스처 없는 값싼 것 권장, 단 커브 WPO는 근거리와 동일해야 함).
	 *  기본 /Game/Maze/M_MazeWallFar 자동 로드. 없으면 메시 기본 머티리얼로 폴백. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField")
	TObjectPtr<UMaterialInterface> FarWallMaterial;

	/** 원거리 벽 병합 상한(셀). 일직선 연속 벽을 이 길이까지 인스턴스 1개로 합친다(개수 1/3~1/5).
	 *  커브 WPO는 정점만 휘므로 너무 길게 합치면 곡면이 직선 현(chord)으로 각져 보인다 — 4셀 권장. 1=병합 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField", meta = (ClampMin = "1"))
	int32 FarMergeMaxRunCells = 4;

	/** 원거리 벽 축소 비율. 근거리 벽보다 살짝 작게 만들어 '안쪽'에 숨긴다 → 겹쳐도 z-파이팅 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float FarWallShrink = 0.97f;

	/** 원거리 벽 컬링 거리(cm). 0이면 bAutoFarCull에 위임(자동/무제한). >0이면 그 너머 인스턴스는 렌더 제외(수동 지정). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField", meta = (ClampMin = "0.0"))
	float FarCullDistance = 0.f;

	/** 켜면 FarCullDistance가 0일 때 원거리 벽을 RenderRadiusCells 근처(≈1.15배)에서 자동 컷한다 → 근거리 윈도우를 줄이면 원거리도 함께 줄어 안개 뒤에서 끊긴다.
	 *  끄고 FarCullDistance=0이면 예전처럼 미로 전체(무제한)를 렌더한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField")
	bool bAutoFarCull = true;

	/** 켜면 시프트 모드에서 근거리 바닥 창 밖을 굵은 슈퍼타일 바닥(충돌/그림자 없음)으로 채운다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField")
	bool bFarFloor = true;

	/** 원거리 바닥 슈퍼타일 한 변의 셀 수. 크면 인스턴스가 줄지만 커브 WPO 현 오차로 타일 경계가 꺼져 보인다 — 8 이하 권장. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField", meta = (ClampMin = "1"))
	int32 FarFloorTileCells = 8;

	/** 원거리 바닥 머티리얼(값싼 단색 + 커브 WPO). 기본 /Game/Maze/M_MazeFloorFar 자동 로드. 없으면 FloorMaterial로 폴백. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|FarField")
	TObjectPtr<UMaterialInterface> FarFloorMaterial;

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

	/** 바닥 타일 인스턴싱용 ISM(시프트 모드 휘는 바닥). 충돌 없음 — 충돌은 숨긴 레벨 바닥이 담당. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UInstancedStaticMeshComponent> FloorISM;

	/** 원거리 벽 레이어(HISM — 클러스터 컬링/LOD 무료). 애니 없음 → 인스턴스 인덱스 안정성 불필요라 HISM 안전.
	 *  근거리 WallISM은 피날레/시프트가 인덱스에 의존하므로 절대 HISM으로 바꾸지 말 것. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FarWallISM;

	/** 원거리 바닥 슈퍼타일 ISM(시프트 모드). 충돌/그림자 없음. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UInstancedStaticMeshComponent> FarFloorISM;

	/** 외곽 성벽(테두리) 인스턴싱용 ISM — 엔진 큐브 몇 개로 미로 둘레 링 구성. 충돌 O(못 넘어감).
	 *  애니 없음 → 인덱스 안정성 불필요. BuildBorderWall이 1회 채우고 ClearMaze는 건드리지 않는다(창 갱신에 유지). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UInstancedStaticMeshComponent> BorderWallISM;

	/** 시작 구역 마당 바닥 슬래브(미로 서쪽 밖, 레벨 바닥 범위에 의존하지 않는 자기완결 지면). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartGround;

	/** 시작 구역 전망대 단상(런타임에 미로 밖 마당 서쪽 끝에 배치·표시). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartPlatform;

	/** 전망대에서 마당 바닥으로 내려가는 경사로(내려와 +X로 걸으면 미로 입구). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartRamp;

	/** 시작 통로 터널 천장(입구 앞 구간을 덮어 위쪽 하늘/곡면을 가림). bStartCorridorRoof일 때만 표시. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartCeiling;

	/** 시작 통로 터널 좌측벽(옆으로 하늘을 가림). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartCorridorWallLeft;

	/** 시작 통로 터널 우측벽. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartCorridorWallRight;

	/** 시작 구역 뒷벽(서쪽 끝을 막아 뒤로도 하늘이 안 보이게 완전 밀폐). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UStaticMeshComponent> StartCorridorBackWall;

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

	/** 시작 셀(GetStartCell=입구)에서 가장 먼 방의 인덱스(목표지점용). */
	int32 GetGoalRoomIndex() const;

	/** 시작 셀 = 미로 입구 (0,0). 시작 구역이 (0,0) 서쪽 밖이라 입구 셀이 거리 기준점. */
	FIntPoint GetStartCell() const;

	/** 미로 서쪽 밖에 시작 구역(마당 슬래브+전망대 단상+경사로)을 배치·표시한다(런타임, bStartZone일 때). */
	void BuildStartZone();

	/** 미로 바깥 둘레에 큰 외곽 성벽(테두리)을 1회 세운다(엔진 큐브 4~5개 박스 링, 서쪽 입구만 트임).
	 *  BeginPlay에서 GenerateMaze 뒤 1회 — ClearMaze가 지우지 않으므로 창 갱신에도 유지. 런타임 전용. */
	void BuildBorderWall();

	/** 플레이어를 전망대 위(단상 없으면 마당 중앙)로 순간이동 + 반대편 코너를 향해 시선 고정.
	 *  폰 소유가 액터 BeginPlay보다 늦을 수 있어 다음 틱 타이머로 재시도한다. */
	void PlacePlayerAtStart();

	/** 전망대 상판 중심(액터 로컬). BuildStartZone이 채우고 PlacePlayerAtStart가 사용. */
	FVector StartPlatformTopLocal = FVector::ZeroVector;

	/** 시작 마당 중앙(액터 로컬). 단상이 꺼졌을 때의 스폰 지점. */
	FVector StartYardCenterLocal = FVector::ZeroVector;

	/** PlacePlayerAtStart 폰 대기 재시도 횟수(무한 재시도 방지). */
	int32 StartPlaceRetries = 0;

	/** Center 셀 기준 ±Radius 윈도우 안의 벽만 ISM 인스턴스로 배치한다(기존 인스턴스는 교체).
	 *  bRecordInstanceMap이 켜져 있으면 (X,Y,Side)→인스턴스 인덱스를 WallInstanceIndex에 기록한다. */
	void BuildWallsInWindow(FIntPoint Center, int32 Radius);

	/** Center 셀 기준 ±Radius 윈도우 안의 셀마다 바닥 타일을 FloorISM에 깐다(시프트 모드 휘는 바닥). */
	void BuildFloorInWindow(FIntPoint Center, int32 Radius);

	/** 미로 전체의 벽을 원거리 레이어(FarWallISM)로 1회 빌드. 일직선 연속 벽은 FarMergeMaxRunCells까지 병합.
	 *  Cells의 순수 함수(RNG 없음) — 결정론 자동. 근거리 벽보다 FarWallShrink만큼 작아 안쪽에 숨는다. */
	void BuildFarWalls();

	/** 일반 모드 '실제 벽 렌더 거리'(cm) = 원거리 벽이 컬링되는 거리. FarCullDistance>0이면 그 값,
	 *  아니면 bAutoFarCull일 때 RenderRadiusCells 근처(≈1.15배), 둘 다 아니면 0(무제한). 원거리 컷과 자동 안개가 공유. */
	float GetEffectiveFarCullDistance() const;

	/** 미로 전체를 FarFloorTileCells 간격 슈퍼타일 바닥으로 1회 빌드(시프트 모드). 상판 z=-2cm로 근거리 바닥 아래. */
	void BuildFarFloor();

	/** 원거리 레이어(벽+바닥) 표시 토글. 피날레 전체 렌더 중엔 숨긴다(중복/유령 벽 방지). */
	void SetFarLayerVisible(bool bVisible);

	/** 런타임(시프트): 플레이어가 Step 이상 움직였으면 근거리 바닥 창을 다시 깐다(UpdateRenderWindow의 바닥판). */
	void UpdateFloorWindow();

	/** MPC에 곡률 스칼라(Curvature/CurveStartDist)를 주입한다. 시프트 사이클마다 재주입 → PIE 중 튜닝 즉시 반영. */
	void ApplyWorldCurve();

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

	/** 마지막으로 바닥 창을 깐 중심 셀(시프트 모드). 벽 창과 독립 추적. */
	FIntPoint LastFloorCenter = FIntPoint(MIN_int32, MIN_int32);

	/** 바닥 창 갱신 타이머 핸들(시프트 모드). */
	FTimerHandle FloorTimer;

	// --- 관측-반응 시프트 모드 내부 상태/헬퍼 ---

	/** 시프트 사이클 타이머. */
	FTimerHandle ShiftTimer;

	/** 시프트 대상 선택용 난수(결정론 불필요, 매번 다른 재배열). */
	FRandomStream ShiftStream;

	/** 연결성 보장 목표 셀(플레이어→이 셀 경로가 항상 유지되도록 닫기를 가드). */
	FIntPoint ShiftGoalCell = FIntPoint(0, 0);
	bool bShiftGoalValid = false;

	/** 코드로 플레이어 카메라에 부착한 손전등(시프트 모드 어둠 동행). */
	TWeakObjectPtr<USpotLightComponent> Flashlight;

	/** 코드로 스폰한 시프트 안개(천장/수평 먼 거리 정리). */
	TWeakObjectPtr<class AExponentialHeightFog> ShiftFog;

	/** 시프트 경로(플레이어 시작→목표) + 렌더가 준비되어 Tick이 개폐 애니를 구동하는 상태. */
	bool bShiftActive = false;

	// --- 시프트 퍼즐 인도(동적 재타겟팅) 상태 ---

	/** 배치된 퍼즐들의 방 중심 셀(DistributeRoomActors가 채움). ShiftPuzzles와 병렬. */
	TArray<FIntPoint> ShiftPuzzleCells;

	/** 배치된 퍼즐 액터(해결 상태 폴링용). ShiftPuzzleCells와 병렬. */
	TArray<TWeakObjectPtr<ASlidingTilePuzzle>> ShiftPuzzles;

	/** 퍼즐 셀마다 세운 발광 비콘(해결되면 숨김). ShiftPuzzleCells와 병렬. */
	TArray<TWeakObjectPtr<AActor>> ShiftPuzzleBeacons;

	/** 모든 퍼즐 해결 후의 최종 도착점 = 목표 방(키 꽂는 곳) 중심 셀. */
	FIntPoint ShiftFinalGoalCell = FIntPoint(0, 0);

	/** 현재 척추가 향하는 도착점(변화 감지용 — 바뀌면 경로 재빌드). */
	FIntPoint ShiftCurrentTarget = FIntPoint(MIN_int32, MIN_int32);

	/** 현재 플레이어 셀 → Goal까지 단조 staircase 경로(RouteCells/RouteEdgeSet)를 재구성. 진행 중 애니는 보존.
	 *  InitShiftRun(첫 회)과 UpdateShiftTarget(재타겟팅)이 공유. */
	void BuildRouteTo(FIntPoint Goal);

	/** 매 사이클: 가장 가까운 미해결 퍼즐(없으면 목표)로 척추를 재조준하고, 해결된 퍼즐 비콘을 숨긴다. */
	void UpdateShiftTarget(const FVector& PlayerLoc);

	/** 첫 ShiftTick에서 1회: 플레이어 시작→목표 staircase 경로(RouteCells/RouteEdgeSet) 산출 + Tick 활성. */
	void InitShiftRun();

	/** 시프트 1회 사이클: 어둠 속 경로 앞 열기/뒤 닫기를 피날레식 실시간 애니로 예약(안전·연결성 가드 포함). */
	void ShiftTick();

	/** 시프트 모드: 플레이어 카메라에 손전등(스포트라이트)을 코드로 1회 부착(관측 파라미터와 원뿔/사거리 일치). */
	void EnsureShiftFlashlight(APawn* Pawn);

	/** ExponentialHeightFog를 코드로 1회 스폰/조정(천장 부재 + 먼 벽을 안개로 정리). 시프트=손전등 반경 안개,
	 *  일반=bAutoFogFromRenderDistance면 렌더 거리에 맞춰 시작거리·밀도 자동 산출. 모드별 게이트(bShiftFog/bNormalFog). */
	void EnsureFog();

	/** 어떤 경우에도 플레이어→목표 길을 보장: 단절 시 어둠 우선 최소-벽 경로(Dijkstra)를 찾아 즉시 개통. 연 벽 수 반환. */
	int32 RepairConnectivityToGoal(FIntPoint PlayerCell, const FVector& CamLoc, const FVector& CamFwd);

	/** 시프트 디버그 시각화: 관측 원뿔 + 플레이어→목표 실제 경로선(BFS). bShiftDebugDraw일 때만 호출. */
	void DrawShiftDebug(FIntPoint PlayerCell, const FVector& CamLoc, const FVector& CamFwd) const;

	/** 인접 두 셀 A,B 사이 간선의 월드 중점(벽 높이 절반 Z). 관측/디버그/복구에서 공통 사용. */
	FVector EdgeMidWorld(FIntPoint A, FIntPoint B) const;

	/** 닫히며 솟는 벽(C,N)이 플레이어 캡슐과 겹쳐 밀어낼지 — 겹치면 이번 프레임 닫기 보류. 피날레/시프트 공용. */
	bool WallWouldHitPlayer(FIntPoint C, FIntPoint N, const FVector& PlayerLoc) const;

	/** 월드 점이 카메라 원뿔(반각 ObserveConeAngle, 거리 ObserveRange) 안인지 = 관측(얼림). */
	bool IsPointObserved(const FVector& WorldPoint, const FVector& CamLoc, const FVector& CamFwd) const;

	/** From 셀에서 To 셀까지 현재 열린 간선만으로 도달 가능한지(BFS). 닫기 연결성 가드용.
	 *  ExtraClosedEdges가 주어지면 그 간선들(EdgeKey)도 '닫힘(벽)'으로 간주한다 — 아직 Cells에
	 *  반영 안 된 진행 중/이번 틱 예정 닫기들을 누적 반영해 '합쳐서 길 막힘'을 방지. */
	bool IsCellReachable(FIntPoint From, FIntPoint To, const TSet<int64>* ExtraClosedEdges = nullptr) const;
};
