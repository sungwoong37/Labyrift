// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "KeyInventory.h"

UKeyInventory* AMazeHUD::GetInventory() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UKeyInventory>();
	}
	return nullptr;
}

void AMazeHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}

	// 현재 열쇠 수 폴링.
	const int32 Keys = GetInventory() ? GetInventory()->GetNumKeys() : 0;

	// 변경 감지 → 펄스 시작(첫 프레임은 연출 없이 초기화만).
	if (!bInitialized)
	{
		CachedKeys = Keys;
		bInitialized = true;
	}
	else if (Keys != CachedKeys)
	{
		CachedKeys = Keys;
		PulseRemaining = PulseDuration;
	}

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	if (PulseRemaining > 0.f)
	{
		PulseRemaining = FMath::Max(0.f, PulseRemaining - Dt);
	}

	// 펄스 보간: 시작 시 1.0 → 0으로 가며 사라짐(사인으로 부드럽게 부풀었다 복귀).
	float PulseAlpha = 0.f;
	if (PulseDuration > 0.f && PulseRemaining > 0.f)
	{
		const float T = PulseRemaining / PulseDuration; // 1→0
		PulseAlpha = FMath::Sin(T * PI);                // 0→1→0 곡선
	}
	const float Scale = TextScale * (1.f + PulseScale * PulseAlpha);

	// 표시 문자열.
	const FString Text = FString::Printf(TEXT("열쇠  %d"), Keys);

	// 텍스트 크기 측정(우상단 정렬용).
	float TextW = 0.f, TextH = 0.f;
	GetTextSize(Text, TextW, TextH, Font, Scale);

	// 배경 박스 패딩.
	const float PadX = 18.f;
	const float PadY = 10.f;
	const float BoxW = TextW + PadX * 2.f;
	const float BoxH = TextH + PadY * 2.f;

	// 우상단 기준 위치(펄스로 커질 때 박스 우측·상단 모서리 고정).
	const float BoxX = Canvas->SizeX - Margin - BoxW;
	const float BoxY = Margin;

	// 반투명 어두운 배경.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), BoxX, BoxY, BoxW, BoxH);

	// 텍스트(개수가 변하면 노란 강조, 평소 흰색).
	const FLinearColor TextColor = (PulseAlpha > 0.f)
		? FLinearColor(1.f, 0.85f, 0.2f, 1.f)
		: FLinearColor::White;

	FCanvasTextItem TextItem(FVector2D(BoxX + PadX, BoxY + PadY), FText::FromString(Text), Font, TextColor);
	TextItem.Scale = FVector2D(Scale, Scale);
	TextItem.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Canvas->DrawItem(TextItem);
}
