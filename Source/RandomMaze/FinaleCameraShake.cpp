// Fill out your copyright notice in the Description page of Project Settings.


#include "FinaleCameraShake.h"

UFinaleCameraShake::UFinaleCameraShake()
{
	// 짧고 빠른 럼블(한 번 재생). Scale로 강약 조절.
	OscillationDuration = 0.35f;
	OscillationBlendInTime = 0.05f;
	OscillationBlendOutTime = 0.12f;

	// 회전 흔들림(도). 위아래/좌우/롤을 서로 다른 주파수로 흔들어 자연스럽게.
	RotOscillation.Pitch.Amplitude = 1.6f;
	RotOscillation.Pitch.Frequency = 28.f;
	RotOscillation.Yaw.Amplitude = 1.4f;
	RotOscillation.Yaw.Frequency = 24.f;
	RotOscillation.Roll.Amplitude = 2.2f;
	RotOscillation.Roll.Frequency = 22.f;

	// 위치 흔들림(cm).
	LocOscillation.X.Amplitude = 1.5f;
	LocOscillation.X.Frequency = 26.f;
	LocOscillation.Y.Amplitude = 1.5f;
	LocOscillation.Y.Frequency = 23.f;
	LocOscillation.Z.Amplitude = 2.0f;
	LocOscillation.Z.Frequency = 25.f;
}
